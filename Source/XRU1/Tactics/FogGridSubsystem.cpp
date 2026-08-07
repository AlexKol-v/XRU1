#include "FogGridSubsystem.h"

#include "FogOfWarConfigDataAsset.h"
#include "FogOfWarSubsystem.h"
#include "TacticsCombatStatics.h"
#include "CoverTuningDataAsset.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "XRU1Log.h"

#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h" // разбор «кто именно закрыл клетку»
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"           // TActorIterator
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "ProfilingDebugging/CpuProfilerTrace.h" // замер запекания и растеризации в Insights
#include "RHI.h"                   // FUpdateTextureRegion2D

/**
 * `xru1.Fog.Grid 0` — выключить ВИЗУАЛЬНЫЙ слой, не трогая правила. Отдельный
 * выключатель от `xru1.Fog.Disable` намеренно: тот показывает скрытых врагов
 * (правила), этот снимает затемнение местности (картинка). Смешивать их значило бы
 * потерять возможность отличить «баг тумана» от «баг сетки».
 */
static TAutoConsoleVariable<int32> CVarFogGrid(
	TEXT("xru1.Fog.Grid"), 1,
	TEXT("Туман войны: визуальный слой затемнения местности (0 — выключить)."),
	ECVF_Default);

/**
 * `xru1.Fog.ScriptedReveals 0` — запретить сценарные раскрытия местности.
 *
 * Нужен для диагностики: раскрытия берутся и снимаются по ходу обучения (беат
 * взял на 12 с, такт снял по завершении), и на экране это неотличимо от сбоя
 * самой сетки. Выключив их, остаются только постоянные источники — живые бойцы.
 */
static TAutoConsoleVariable<int32> CVarFogScriptedReveals(
	TEXT("xru1.Fog.ScriptedReveals"), 1,
	TEXT("Туман войны: сценарные раскрытия местности (0 — только зрение бойцов)."),
	ECVF_Default);

/**
 * `xru1.Fog.Blockers 0` — растеризовать видимость БЕЗ окклюзии: чистые круги от
 * бойцов, лучи не останавливаются на стенах.
 *
 * Это разделяющий тест: если при выключенных блокерах картинка становится
 * ровным кругом, значит клинья и ступеньки родила маска блокеров или DDA, а не
 * привязка к миру и не формула материала. Разбирать три подозреваемых сразу —
 * самый долгий способ не найти ни одного.
 */
static TAutoConsoleVariable<int32> CVarFogBlockers(
	TEXT("xru1.Fog.Blockers"), 1,
	TEXT("Туман войны: учитывать стены при растеризации (0 — чистые круги обзора)."),
	ECVF_Default);

/**
 * `xru1.Fog.ExplainBlockers 1` — при следующем запекании перечислить, КАКИЕ
 * акторы сделали клетки непрозрачными (топ по количеству клеток).
 *
 * Одно число «блокеров N» ни о чём не говорит: доля в 10-15 % карты — это может
 * быть и честная застройка, и заборы с мусором, попавшие в пробу. Ответ даёт
 * только список виновников по именам акторов.
 */
static TAutoConsoleVariable<int32> CVarFogExplainBlockers(
	TEXT("xru1.Fog.ExplainBlockers"), 0,
	TEXT("Туман войны: при запекании перечислить акторов, формирующих блокеры."),
	ECVF_Default);

/**
 * `xru1.Fog.GridDump` — распечатать сетку в журнал как текстовую карту.
 *
 * Диагностика картинки глазами по скриншоту не работает: тень рельефа, полоса
 * затемнения и «спица» между лучами выглядят одинаково. Дамп отвечает точно —
 * где блокеры, где видно сейчас и что разведано.
 */
static FAutoConsoleCommandWithWorld GFogGridDumpCommand(
	TEXT("xru1.Fog.GridDump"),
	TEXT("Туман войны: распечатать сетку затемнения в журнал (# блокер, * видно, . разведано)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UFogGridSubsystem* Grid = World ? World->GetSubsystem<UFogGridSubsystem>() : nullptr)
		{
			Grid->DumpGridToLog();
		}
	}));

namespace FogGrid
{
	/** Имена параметров материала — один список на подсистему и на генератор материала. */
	static const FName ParamTexture(TEXT("FogTexture"));
	static const FName ParamOrigin(TEXT("FogGridOrigin"));
	static const FName ParamSize(TEXT("FogGridSize"));
	static const FName ParamCellUV(TEXT("FogCellUV"));
	static const FName ParamUnknownColor(TEXT("FogUnknownColor"));
	static const FName ParamUnknownBrightness(TEXT("FogUnknownBrightness"));
	static const FName ParamExploredBrightness(TEXT("FogExploredBrightness"));
	static const FName ParamDesaturation(TEXT("FogDesaturation"));
	static const FName ParamOutsideVisible(TEXT("FogOutsideVisible"));
	static const FName ParamEdgeLow(TEXT("FogEdgeLow"));
	static const FName ParamEdgeHigh(TEXT("FogEdgeHigh"));
	static const FName ParamEnabled(TEXT("FogEnabled"));

	/** Дальше этого сдвига источник считается сдвинувшимся (см). */
	static constexpr float SourceMoveEpsilon = 5.f;
}

UFogGridSubsystem* UFogGridSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UFogGridSubsystem>() : nullptr;
}

bool UFogGridSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Только игровые миры: в редакторском затемнять местность нельзя — дизайнер
	// перестанет видеть карту во вьюпорте (то же правило, что у слоя правил).
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UFogGridSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFogGridSubsystem, STATGROUP_Tickables);
}

bool UFogGridSubsystem::IsGridEnabled()
{
	return CVarFogGrid.GetValueOnGameThread() != 0;
}

void UFogGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Слой правил обязан существовать раньше: сетка на него ПОДПИСЫВАЕТСЯ, а не
	// наоборот. Порядок «сетка спрашивает правила» — единственно допустимый.
	Collection.InitializeDependency<UFogOfWarSubsystem>();
	Super::Initialize(Collection);

	if (UFogOfWarSubsystem* Fog = GetWorld() ? GetWorld()->GetSubsystem<UFogOfWarSubsystem>() : nullptr)
	{
		VisibilityRecomputedHandle = Fog->OnVisibilityRecomputed.AddUObject(
			this, &UFogGridSubsystem::HandleVisibilityRecomputed);
	}
}

void UFogGridSubsystem::Deinitialize()
{
	if (VisibilityRecomputedHandle.IsValid())
	{
		if (UFogOfWarSubsystem* Fog = GetWorld() ? GetWorld()->GetSubsystem<UFogOfWarSubsystem>() : nullptr)
		{
			Fog->OnVisibilityRecomputed.Remove(VisibilityRecomputedHandle);
		}
		VisibilityRecomputedHandle.Reset();
	}
	Super::Deinitialize();
}

void UFogGridSubsystem::HandleVisibilityRecomputed()
{
	// Только помечаем: пересчётов «движение» идут сотни за бой, и растеризовать
	// синхронно внутри чужого пересчёта значило бы платить за оба слоя разом.
	bVisibilityDirty = true;
}

// --- Сессия ---------------------------------------------------------------------

void UFogGridSubsystem::ResetForScenario(FName ScenarioId, int32 RunId, bool bStartFullyExplored)
{
	ActiveScenarioId = ScenarioId;
	ActiveRunId = RunId;
	// Запоминаем профиль: сетку можно построить и позже — например, если слой был
	// выключен `xru1.Fog.Grid 0` на старте боя и включён посреди него.
	bScenarioStartsExplored = bStartFullyExplored;

	// Сценарные раскрытия прошлого запуска снимаются ВСЕГДА: оборванный StateTree
	// может не дойти до своего `ExitState`, и повисшее раскрытие оставило бы
	// сектор разведанным весь следующий прогон.
	ScriptedReveals.Reset();
	bHasTimedReveals = false;
	LastSourcePositions.Reset();

	if (!IsGridEnabled())
	{
		bGridValid = false;
		PushMaterialParameters();
		UE_LOG(LogXRU1Fog, Log, TEXT("[FogGrid] Слой выключен (xru1.Fog.Grid 0) — сетка не строится"));
		return;
	}

	BuildGrid();
	if (!bGridValid)
	{
		return;
	}

	Explored.Init(bScenarioStartsExplored, GridWidth * GridHeight);
	// Профиль сценария мог объявить карту разведанной — сглаженные маски обязаны
	// это отразить до первой же заливки текстуры.
	RebuildSmoothedMasks();

	// Первая растеризация — СРАЗУ и без сглаживания: управление игроку отдаётся
	// после старта сценария, и к этому моменту картинка обязана быть готовой.
	// Иначе первый кадр боя проявляется на глазах.
	RasterizeVisibility();
	AdvanceDisplayAndUpload(0.f, /*bInstant=*/true);
	PushMaterialParameters();

	UE_LOG(LogXRU1Fog, Log,
		TEXT("[FogGrid] Reset: сценарий %s, run %d — сетка %dx%d по %.0f см, старт %s"),
		*ScenarioId.ToString(), RunId, GridWidth, GridHeight, CellSize,
		bStartFullyExplored ? TEXT("разведанным") : TEXT("с чёрной картой"));
}

// --- Построение и запекание -----------------------------------------------------

float UFogGridSubsystem::GetVisionHeightAboveFloor() const
{
	const UWorld* World = GetWorld();

	// Половину капсулы берём с ЖИВОГО юнита, а не константой: BP-наследник вправе
	// переопределить капсулу, и тогда запечённая высота глаз разошлась бы с той,
	// из которой считает боевой LOS.
	float HalfHeight = 88.f; // дефолт ACharacter — фолбэк, если юнитов ещё нет
	if (World)
	{
		for (TActorIterator<AUnitBase> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (const UCapsuleComponent* Capsule = It->GetCapsuleComponent())
			{
				HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
				break;
			}
		}
	}

	// Смещение глаз — из общего тюнинга укрытий: у сетки и у линии огня одна
	// высота взгляда, иначе граница затемнения поедет относительно правил.
	return HalfHeight + UTacticsCombatStatics::GetCoverTuning(World)->EyeHeightOffset;
}

void UFogGridSubsystem::BuildGrid()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UFogGridSubsystem::BuildGrid);

	bGridValid = false;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFogOfWarConfigDataAsset* Config = UFogOfWarConfigDataAsset::Get(World);

	// ⚠️ Границы собираются по ВСЕМ загруженным объёмам навигации, а не по одному.
	// На `Main_Map_Showreel` они лежат в scenario sublevel (`SL_Showreel_Tutorial`
	// / `SL_Showreel_Mission01`), а не в persistent, и у каждого сценария их
	// несколько. Поэтому объединение, а не «найти первый».
	FBox Bounds(ForceInit);
	int32 VolumeCount = 0;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		Bounds += It->GetComponentsBoundingBox(/*bNonColliding=*/true);
		++VolumeCount;
	}

	if (!Bounds.IsValid)
	{
		// Не ошибка кода, а незаполненная карта: без навмеша бой всё равно не
		// работает. Слой молча выключается, правила продолжают действовать.
		UE_LOG(LogXRU1Fog, Warning,
			TEXT("[FogGrid] На карте нет ни одного ANavMeshBoundsVolume — затемнение местности выключено"));
		return;
	}

	Bounds = Bounds.ExpandBy(FVector(Config->BoundsPadding, Config->BoundsPadding, 0.f));

	CellSize = FMath::Max(25.f, Config->CellSize);
	const FVector Extent = Bounds.GetSize();
	GridWidth = FMath::CeilToInt(Extent.X / CellSize);
	GridHeight = FMath::CeilToInt(Extent.Y / CellSize);

	// Предел стороны сетки: при превышении УКРУПНЯЕМ КЛЕТКУ, а не растим память.
	// Иначе большая карта молча превратила бы старт боя в секунды запекания.
	const int32 MaxSide = FMath::Max(GridWidth, GridHeight);
	if (MaxSide > Config->MaxGridResolution)
	{
		const float Scale = static_cast<float>(MaxSide) / static_cast<float>(Config->MaxGridResolution);
		CellSize *= Scale;
		GridWidth = FMath::CeilToInt(Extent.X / CellSize);
		GridHeight = FMath::CeilToInt(Extent.Y / CellSize);
		UE_LOG(LogXRU1Fog, Log,
			TEXT("[FogGrid] Клетка укрупнена до %.0f см: %.0fx%.0f м не влезали в предел %d клеток"),
			CellSize, Extent.X / 100.f, Extent.Y / 100.f, Config->MaxGridResolution);
	}

	GridWidth = FMath::Clamp(GridWidth, 1, 4096);
	GridHeight = FMath::Clamp(GridHeight, 1, 4096);
	GridOrigin = FVector2D(Bounds.Min.X, Bounds.Min.Y);

	const int32 NumCells = GridWidth * GridHeight;
	Blockers.Init(false, NumCells);
	Visible.Init(false, NumCells);
	Explored.Init(false, NumCells);
	DisplayVisible.Init(0, NumCells);
	DisplayExplored.Init(0, NumCells);
	// Сглаженные маски инициализируются ЗДЕСЬ, а не только в растеризации: та
	// вправе выйти рано (состав источников не изменился), и заливка текстуры
	// прочитала бы пустые массивы.
	SmoothedVisible.Init(0, NumCells);
	SmoothedExplored.Init(0, NumCells);

	// --- Запекание блокеров -----------------------------------------------------
	// Один трейс вниз (найти пол) + одна проба на высоте глаз (стоит ли там
	// непрозрачная геометрия) на клетку. Object-типы — те же, что у боевой линии
	// огня: единое определение «геометрии, которая останавливает взгляд».

	const double BakeStart = FPlatformTime::Seconds();

	// ⚠️ Высота пробы — порог ПОЛНОГО укрытия, а не только высота глаз. Низкое
	// укрытие (мешки, ящик по пояс) обзор рвать не должно: в XCOM из-за него
	// стреляют поверх, и линия видимости не прерывается — блокирует лишь высокое.
	// Порог берём тот же, по которому боевые правила отличают Full от Half
	// (`UCoverTuningDataAsset::FullCoverHeight`), иначе картинка и укрытия
	// разъедутся в понимании того, что такое «стена».
	const UCoverTuningDataAsset* Tuning = UTacticsCombatStatics::GetCoverTuning(World);
	BakedVisionHeight = FMath::Max(GetVisionHeightAboveFloor(), Tuning->FullCoverHeight);

	const FCollisionObjectQueryParams& ObjectQuery = UTacticsCombatStatics::GetShotGeometryObjects();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FogGridBake), /*bTraceComplex=*/false);
	const float ProbeRadius = FMath::Max(1.f, Config->BlockerProbeRadius);
	const FCollisionShape Probe = FCollisionShape::MakeSphere(ProbeRadius);

	const float TraceTop = Bounds.Max.Z + 500.f;
	const float TraceBottom = Bounds.Min.Z - 500.f;
	int32 BlockedCells = 0;
	int32 FloorlessCells = 0;

	const bool bExplainBlockers = CVarFogExplainBlockers.GetValueOnGameThread() != 0;
	TMap<FName, int32> BlockerActors;

	// ⚠️ ЗАПЕКАНИЕ НЕ СПРАШИВАЕТ НАВИГАЦИЮ. Промежуточная редакция брала пол
	// клетки из навмеша (`ProjectPointToNavigation`), и это была ошибка
	// архитектуры: в проекте включены Navigation Invokers, поэтому навмеш
	// существует только ВОКРУГ БОЙЦОВ и достраивается тайлами. Симптомы были
	// ровно такие: квадрат чистой местности вокруг юнита, дырявое покрытие вдали
	// и РАЗНОЕ число клеток от запуска к запуску (12039 / 9463 / 9238 на одной и
	// той же карте). «Нет навмеша» не значит ни «не видно», ни «стена» — это
	// вопрос проходимости для агента, а не геометрии.
	//
	// Пол ищем трейсом СНИЗУ ВВЕРХ: первая поверхность снизу — земля, а не навес.
	// Трейс сверху находил крышу ангара и объявлял клетку непрозрачной, из-за чего
	// вдоль сооружений шли полосы затемнения.
	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const FVector Center(
				GridOrigin.X + (X + 0.5f) * CellSize,
				GridOrigin.Y + (Y + 0.5f) * CellSize,
				0.f);

			FHitResult FloorHit;
			const bool bHasFloor = World->LineTraceSingleByObjectType(FloorHit,
				FVector(Center.X, Center.Y, TraceBottom),
				FVector(Center.X, Center.Y, TraceTop),
				ObjectQuery, QueryParams);

			// Пола нет (дыра в геометрии, край арены) — клетка остаётся ПРОЗРАЧНОЙ.
			// Считать её стеной нельзя: тогда любая незакрытая геометрией область
			// превращалась бы в глухой блокер и рвала лучи там, где смотреть не
			// мешает ничто.
			if (!bHasFloor)
			{
				++FloorlessCells;
				continue;
			}

			const FVector EyePoint(Center.X, Center.Y, FloorHit.ImpactPoint.Z + BakedVisionHeight);

			if (bExplainBlockers)
			{
				// Разбор: кто именно закрыл клетку. Дороже обычной пробы, поэтому
				// только под cvar — зато отвечает на вопрос «12 % карты в стенах:
				// это застройка или в пробу лезет мусор».
				TArray<FOverlapResult> Overlaps;
				if (World->OverlapMultiByObjectType(Overlaps, EyePoint, FQuat::Identity, ObjectQuery, Probe, QueryParams))
				{
					Blockers[CellIndex(X, Y)] = true;
					++BlockedCells;
					for (const FOverlapResult& Overlap : Overlaps)
					{
						if (const AActor* Blocker = Overlap.GetActor())
						{
							// Имя актора, а не класс: половина карты — это
							// `StaticMeshActor`, и по классу не отличить бетонную
							// стену от мешка с песком, который перекрывать обзор
							// не должен вовсе.
							++BlockerActors.FindOrAdd(FName(*Blocker->GetActorNameOrLabel()));
						}
					}
				}
			}
			else if (World->OverlapAnyTestByObjectType(EyePoint, FQuat::Identity, ObjectQuery, Probe, QueryParams))
			{
				Blockers[CellIndex(X, Y)] = true;
				++BlockedCells;
			}
		}
	}

	const double BakeMs = (FPlatformTime::Seconds() - BakeStart) * 1000.0;

	EnsureTexture();
	bGridValid = true;

	// Контрольная сумма маски блокеров печатается НЕ для красоты: запекание обязано
	// быть детерминированным, и одинаковая сумма в двух подряд запусках — это
	// единственный дешёвый способ убедиться, что оно ни от чего не «плавает».
	uint32 BlockerHash = 0;
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		BlockerHash = BlockerHash * 31u + (Blockers[Index] ? 1u : 0u);
	}

	UE_LOG(LogXRU1Fog, Log,
		TEXT("[FogGrid] Запечено %d клеток (%dx%d по %.0f см, объёмов навигации=%d): ")
		TEXT("блокеров=%d (%.1f%%), без пола=%d, высота взгляда=%.0f см, проба r=%.0f см, hash=0x%08X, %.1f мс"),
		NumCells, GridWidth, GridHeight, CellSize, VolumeCount,
		BlockedCells, NumCells > 0 ? 100.f * BlockedCells / NumCells : 0.f,
		FloorlessCells, BakedVisionHeight, ProbeRadius, BlockerHash, BakeMs);

	if (bExplainBlockers && BlockerActors.Num() > 0)
	{
		BlockerActors.ValueSort([](int32 A, int32 B) { return A > B; });
		int32 Printed = 0;
		for (const TPair<FName, int32>& Entry : BlockerActors)
		{
			UE_LOG(LogXRU1Fog, Display, TEXT("[FogGrid]   блокер %-40s клеток=%d"),
				*Entry.Key.ToString(), Entry.Value);
			if (++Printed >= 15)
			{
				break;
			}
		}
	}
}

// --- Растеризация ---------------------------------------------------------------

void UFogGridSubsystem::CastVisionRays(const FVector& Origin, float Range)
{
	// Позиция источника в дробных клетках.
	const float Cx = (Origin.X - GridOrigin.X) / CellSize;
	const float Cy = (Origin.Y - GridOrigin.Y) / CellSize;
	const int32 StartX = FMath::FloorToInt(Cx);
	const int32 StartY = FMath::FloorToInt(Cy);
	if (StartX < 0 || StartY < 0 || StartX >= GridWidth || StartY >= GridHeight)
	{
		// Источник вне сетки (боец за пределами навмеша, сценарная точка вдалеке):
		// раскрывать нечего, лучи стартовать неоткуда.
		return;
	}

	Visible[CellIndex(StartX, StartY)] = true;

	const UFogOfWarConfigDataAsset* Config = UFogOfWarConfigDataAsset::Get(GetWorld());
	const float RangeCells = Range / CellSize;
	if (RangeCells <= 0.f)
	{
		return;
	}

	// Число лучей — от длины дуги на границе обзора: реже — и у края появляются
	// «спицы» непросвеченных клеток между лучами.
	const float TwoPi = 2.f * UE_PI;
	const int32 NumRays = FMath::Clamp(
		FMath::CeilToInt(TwoPi * RangeCells * Config->RaysPerEdgeCell), 16, 4096);
	// Выключенная окклюзия — разделяющий тест: остаётся чистый круг обзора.
	const bool bUseBlockers = CVarFogBlockers.GetValueOnGameThread() != 0;

	for (int32 RayIndex = 0; RayIndex < NumRays; ++RayIndex)
	{
		const float Angle = (TwoPi * RayIndex) / NumRays;
		const float Dx = FMath::Cos(Angle);
		const float Dy = FMath::Sin(Angle);

		// Amanatides-Woo: идём по клеткам, а не по точкам вдоль луча — ни одна
		// клетка на пути не пропускается и ни одна не обрабатывается дважды.
		int32 X = StartX;
		int32 Y = StartY;
		const int32 StepX = Dx > 0.f ? 1 : -1;
		const int32 StepY = Dy > 0.f ? 1 : -1;

		const float InvDx = FMath::Abs(Dx) > UE_KINDA_SMALL_NUMBER ? 1.f / FMath::Abs(Dx) : TNumericLimits<float>::Max();
		const float InvDy = FMath::Abs(Dy) > UE_KINDA_SMALL_NUMBER ? 1.f / FMath::Abs(Dy) : TNumericLimits<float>::Max();

		// Расстояние вдоль луча (в клетках) до первой границы по каждой оси.
		float NextX = InvDx == TNumericLimits<float>::Max()
			? TNumericLimits<float>::Max()
			: (Dx > 0.f ? (StartX + 1 - Cx) : (Cx - StartX)) * InvDx;
		float NextY = InvDy == TNumericLimits<float>::Max()
			? TNumericLimits<float>::Max()
			: (Dy > 0.f ? (StartY + 1 - Cy) : (Cy - StartY)) * InvDy;

		// Стартовая клетка уже помечена; блокер под самим источником не должен
		// гасить его собственный обзор (боец стоит вплотную к стене).
		while (true)
		{
			const float Travelled = FMath::Min(NextX, NextY);
			if (Travelled > RangeCells)
			{
				break;
			}

			if (NextX < NextY)
			{
				X += StepX;
				NextX += InvDx;
			}
			else
			{
				Y += StepY;
				NextY += InvDy;
			}

			if (X < 0 || Y < 0 || X >= GridWidth || Y >= GridHeight)
			{
				break;
			}

			const int32 Index = CellIndex(X, Y);
			Visible[Index] = true;

			// Стена ВИДНА (её помечаем), но дальше взгляд не идёт — это и есть
			// граница затемнения на углу.
			if (bUseBlockers && Blockers[Index])
			{
				break;
			}
		}
	}
}

void UFogGridSubsystem::RasterizeVisibility()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UFogGridSubsystem::RasterizeVisibility);

	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager || !bGridValid)
	{
		return;
	}

	// Источники: живые бойцы отряда + сценарные раскрытия. Ровно тот же состав
	// наблюдателей, что у слоя правил, плюс не-юнитные источники XCOM
	// (`XComGameState_SquadViewer` / `CreateFOWViewer`).
	struct FSource
	{
		FVector Location;
		float Range;
	};
	TArray<FSource, TInlineAllocator<8>> Sources;

	const UFogOfWarConfigDataAsset* Config = UFogOfWarConfigDataAsset::Get(World);
	for (AActor* Unit : TurnManager->GetPlayerSideUnits())
	{
		if (!Unit || UTacticsCombatStatics::IsUnitEvacuated(Unit))
		{
			continue;
		}

		if (UTacticsCombatStatics::IsUnitAlive(Unit))
		{
			Sources.Add({ Unit->GetActorLocation(), UTacticsCombatStatics::SquadVisionRange });
		}
		else if (UTacticsCombatStatics::IsUnitDowned(Unit) && Config->DownedVisionRange > 0.f)
		{
			// Тяжело раненый даёт КАРТИНКЕ маленький круг вокруг себя (у XCOM это
			// `BLEEDOUT_SIGHT_RADIUS = 3` м). Иначе свой же боец лежит в темноте, и
			// приказ «дойти и поднять» отдаётся вслепую. Правил это не касается:
			// зрения отряду Downed не даёт, тут только видимость местности.
			Sources.Add({ Unit->GetActorLocation(), Config->DownedVisionRange });
		}
	}
	if (CVarFogScriptedReveals.GetValueOnGameThread() != 0)
	{
		for (const FScriptedReveal& Reveal : ScriptedReveals)
		{
			const AActor* Anchor = Reveal.Anchor.Get();
			Sources.Add({ Anchor ? Anchor->GetActorLocation() : Reveal.Location, Reveal.Radius });
		}
	}

	// Ничего не сдвинулось и состав тот же — работы нет. Пересчёт видимости
	// приходит и от событий, которые сетки не касаются вовсе (смерть врага,
	// граница хода), а полная растеризация стоит десятки тысяч чтений массива.
	bool bSourcesChanged = Sources.Num() != LastSourcePositions.Num();
	if (!bSourcesChanged)
	{
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			if (!Sources[Index].Location.Equals(LastSourcePositions[Index], FogGrid::SourceMoveEpsilon))
			{
				bSourcesChanged = true;
				break;
			}
		}
	}
	if (!bSourcesChanged)
	{
		return;
	}

	LastSourcePositions.Reset(Sources.Num());
	for (const FSource& Source : Sources)
	{
		LastSourcePositions.Add(Source.Location);
	}

	const double Start = FPlatformTime::Seconds();

	Visible.Init(false, GridWidth * GridHeight);
	for (const FSource& Source : Sources)
	{
		CastVisionRays(Source.Location, Source.Range);
	}

	// `Explored` только растёт — до конца сессии сценария (сброс живёт в
	// `ResetForScenario`, и только там).
	Explored.CombineWithBitwiseOR(Visible, EBitwiseOperatorFlags::MaintainSize);

	// Сглаживание считается ЗДЕСЬ, а не в кадровом проходе: маска меняется
	// десять раз в секунду, а тик идёт шестьдесят, и окно 3×3 по двум слоям на
	// каждую клетку каждый кадр — это работа впустую в шесть раз чаще нужного.
	RebuildSmoothedMasks();

	bDisplayDirty = true;

	if (UFogOfWarSubsystem::IsExplainEnabled())
	{
		UE_LOG(LogXRU1Fog, Display, TEXT("[FogGrid] растеризация: источников=%d, %.2f мс"),
			Sources.Num(), (FPlatformTime::Seconds() - Start) * 1000.0);
	}
}

// --- Сценарное раскрытие --------------------------------------------------------

int32 UFogGridSubsystem::AddScriptedReveal(const AActor* Anchor, const FVector& Location,
	float Radius, float Duration)
{
	if (!bGridValid)
	{
		return 0;
	}

	const UWorld* World = GetWorld();
	const UFogOfWarConfigDataAsset* Config = UFogOfWarConfigDataAsset::Get(World);

	FScriptedReveal& Reveal = ScriptedReveals.AddDefaulted_GetRef();
	Reveal.Anchor = Anchor;
	Reveal.Location = Anchor ? Anchor->GetActorLocation() : Location;
	Reveal.Radius = Radius > 0.f ? Radius : Config->ScriptedRevealRadius;
	Reveal.ExpiryTime = (Duration > 0.f && World) ? World->GetTimeSeconds() + Duration : -1.0;
	Reveal.Handle = NextRevealHandle++;

	bHasTimedReveals = bHasTimedReveals || Reveal.ExpiryTime >= 0.0;
	// Состав источников изменился — растеризовать заново, даже если никто не двигался.
	LastSourcePositions.Reset();
	bVisibilityDirty = true;

	// ⚠️ Под разбором, а не всегда. Раскрытия берутся не только режиссурой: КАЖДЫЙ
	// кадр выстрела берёт своё, то есть в бою это десятки пар «взято/снято».
	// Проект уже платил за такую щедрость: подавляющее большинство строк тумана
	// были бесполезны и топили значимое.
	if (UFogOfWarSubsystem::IsExplainEnabled())
	{
		UE_LOG(LogXRU1Fog, Display, TEXT("[FogGrid] сценарное раскрытие #%d: %s, радиус %.0f см%s"),
			Reveal.Handle,
			Anchor ? *GetNameSafe(Anchor) : TEXT("точка"),
			Reveal.Radius,
			Reveal.ExpiryTime >= 0.0 ? *FString::Printf(TEXT(", на %.1f с"), Duration) : TEXT(""));
	}

	return Reveal.Handle;
}

void UFogGridSubsystem::RemoveScriptedReveal(int32 Handle)
{
	if (Handle == 0)
	{
		return;
	}

	const int32 Removed = ScriptedReveals.RemoveAll([Handle](const FScriptedReveal& Reveal)
	{
		return Reveal.Handle == Handle;
	});
	if (Removed > 0)
	{
		LastSourcePositions.Reset();
		bVisibilityDirty = true;
		if (UFogOfWarSubsystem::IsExplainEnabled())
		{
			UE_LOG(LogXRU1Fog, Display, TEXT("[FogGrid] сценарное раскрытие #%d снято"), Handle);
		}
	}
}

void UFogGridSubsystem::ExpireScriptedReveals()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	bool bAnyTimed = false;
	const int32 Removed = ScriptedReveals.RemoveAll([Now, &bAnyTimed](const FScriptedReveal& Reveal)
	{
		if (Reveal.ExpiryTime >= 0.0 && Now >= Reveal.ExpiryTime)
		{
			return true;
		}
		bAnyTimed = bAnyTimed || Reveal.ExpiryTime >= 0.0;
		return false;
	});

	bHasTimedReveals = bAnyTimed;
	if (Removed > 0)
	{
		LastSourcePositions.Reset();
		bVisibilityDirty = true;
	}
}

// --- Текстура и материал --------------------------------------------------------

void UFogGridSubsystem::RebuildSmoothedMasks()
{
	const int32 NumCells = GridWidth * GridHeight;
	if (NumCells <= 0)
	{
		return;
	}

	const int32 Radius = FMath::Clamp(
		UFogOfWarConfigDataAsset::Get(GetWorld())->MaskSmoothingRadius, 0, 3);

	SmoothedVisible.SetNumUninitialized(NumCells);
	SmoothedExplored.SetNumUninitialized(NumCells);
	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const int32 Index = CellIndex(X, Y);
			SmoothedVisible[Index] = SampleSmoothed(Visible, X, Y, Radius);
			SmoothedExplored[Index] = SampleSmoothed(Explored, X, Y, Radius);
		}
	}
}

uint8 UFogGridSubsystem::SampleSmoothed(const TBitArray<>& Mask, int32 X, int32 Y, int32 Radius) const
{
	if (Radius <= 0)
	{
		return Mask[CellIndex(X, Y)] ? 255 : 0;
	}

	// Доля видимых соседей в окне. Клетки за краем сетки в счёт не идут вовсе —
	// иначе у границы карты появлялась бы ложная кайма из «половины» соседей.
	int32 Set = 0;
	int32 Total = 0;
	const int32 MinX = FMath::Max(0, X - Radius);
	const int32 MaxX = FMath::Min(GridWidth - 1, X + Radius);
	const int32 MinY = FMath::Max(0, Y - Radius);
	const int32 MaxY = FMath::Min(GridHeight - 1, Y + Radius);
	for (int32 SampleY = MinY; SampleY <= MaxY; ++SampleY)
	{
		for (int32 SampleX = MinX; SampleX <= MaxX; ++SampleX)
		{
			Set += Mask[CellIndex(SampleX, SampleY)] ? 1 : 0;
			++Total;
		}
	}
	return Total > 0 ? static_cast<uint8>((Set * 255) / Total) : 0;
}

void UFogGridSubsystem::EnsureTexture()
{
	const int32 NumCells = GridWidth * GridHeight;
	if (NumCells <= 0)
	{
		return;
	}

	// ⚠️ Заливать текстуру придётся ЦЕЛИКОМ, а не только изменившимся куском.
	// `CreateTransient` отдаёт НЕинициализированную память, а обычное обновление
	// шлёт лишь прямоугольник изменений — и тексели, чьё значение с самого начала
	// совпало с целевым (а таких большинство: «не видно» = 0), не попадали в
	// заливку никогда. На экране это выглядело как случайные пятна тумана,
	// РАЗНЫЕ при каждом запуске (мусор из памяти), при верной картинке рядом с
	// бойцом, где значения действительно менялись.
	bTextureNeedsFullUpload = true;

	if (FogTexture && FogTexture->GetSizeX() == GridWidth && FogTexture->GetSizeY() == GridHeight)
	{
		TextureData.Init(0, NumCells * 4);
		return;
	}

	// Имя не задаём: транзиентный пакет один на процесс, и второй запуск сценария
	// столкнулся бы с уже занятым именем прошлой текстуры.
	FogTexture = UTexture2D::CreateTransient(GridWidth, GridHeight, PF_B8G8R8A8);
	if (!FogTexture)
	{
		UE_LOG(LogXRU1Fog, Warning, TEXT("[FogGrid] Не удалось создать текстуру %dx%d"), GridWidth, GridHeight);
		bGridValid = false;
		return;
	}

	// sRGB выключен: в каналах не цвет, а два признака (видно сейчас / видели
	// когда-то), и гамма-коррекция исказила бы их значения.
	FogTexture->SRGB = false;
	// Билинейная фильтрация сама сглаживает границу между клетками — отдельного
	// размытия не нужно.
	FogTexture->Filter = TextureFilter::TF_Bilinear;
	// Clamp: за краем сетки продолжается тексель края, а не повтор карты.
	FogTexture->AddressX = TextureAddress::TA_Clamp;
	FogTexture->AddressY = TextureAddress::TA_Clamp;
	// Без стриминга: иначе `UpdateTextureRegions` молча пропускает заливку
	// (движок пишет в лог «without calling TemporarilyDisableStreaming»).
	FogTexture->NeverStream = true;
	FogTexture->UpdateResource();

	TextureData.Init(0, NumCells * 4);
}

void UFogGridSubsystem::AdvanceDisplayAndUpload(float DeltaSeconds, bool bInstant)
{
	if (!FogTexture || !bGridValid)
	{
		return;
	}

	const UFogOfWarConfigDataAsset* Config = UFogOfWarConfigDataAsset::Get(GetWorld());
	const float FadeSpeed = Config->EdgeFadeSpeed;
	const int32 SmoothingRadius = FMath::Clamp(Config->MaskSmoothingRadius, 0, 3);

	// Шаг сглаживания в единицах 0..255 за кадр. Мгновенный режим (старт сценария,
	// нулевая скорость в конфиге) переводит значения сразу.
	const int32 Step = (bInstant || FadeSpeed <= 0.f)
		? 255
		: FMath::Clamp(FMath::CeilToInt(FadeSpeed * DeltaSeconds * 255.f), 1, 255);

	// Первая заливка после создания текстуры идёт целиком (см. `bTextureNeedsFullUpload`).
	int32 MinX = GridWidth;
	int32 MinY = GridHeight;
	int32 MaxX = -1;
	int32 MaxY = -1;
	if (bTextureNeedsFullUpload)
	{
		MinX = 0;
		MinY = 0;
		MaxX = GridWidth - 1;
		MaxY = GridHeight - 1;
	}

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const int32 Index = CellIndex(X, Y);
			const uint8 TargetVisible = SmoothedVisible[Index];
			const uint8 TargetExplored = SmoothedExplored[Index];

			uint8& CurrentVisible = DisplayVisible[Index];
			uint8& CurrentExplored = DisplayExplored[Index];
			bool bChanged = false;

			if (CurrentVisible != TargetVisible)
			{
				CurrentVisible = TargetVisible > CurrentVisible
					? static_cast<uint8>(FMath::Min<int32>(TargetVisible, CurrentVisible + Step))
					: static_cast<uint8>(FMath::Max<int32>(TargetVisible, CurrentVisible - Step));
				bChanged = true;
			}
			if (CurrentExplored != TargetExplored)
			{
				CurrentExplored = TargetExplored > CurrentExplored
					? static_cast<uint8>(FMath::Min<int32>(TargetExplored, CurrentExplored + Step))
					: static_cast<uint8>(FMath::Max<int32>(TargetExplored, CurrentExplored - Step));
				bChanged = true;
			}

			if (!bChanged)
			{
				continue;
			}

			// BGRA: R — текущая видимость, G — `Explored`. Порядок байтов в памяти
			// формата PF_B8G8R8A8 именно такой, поэтому индексы не «перепутаны».
			uint8* Pixel = TextureData.GetData() + Index * 4;
			Pixel[0] = 0;
			Pixel[1] = CurrentExplored;
			Pixel[2] = CurrentVisible;
			Pixel[3] = 255;

			MinX = FMath::Min(MinX, X);
			MinY = FMath::Min(MinY, Y);
			MaxX = FMath::Max(MaxX, X);
			MaxY = FMath::Max(MaxY, Y);
		}
	}

	if (MaxX < 0)
	{
		// Всё сошлось — до следующей растеризации работы нет.
		bDisplayDirty = false;
		return;
	}

	// Заливаем только изменившийся прямоугольник (аналог `CurrentUpdateBox` в
	// XCOM): при движении одного бойца это узкая полоса, а не вся текстура.
	const int32 RegionWidth = MaxX - MinX + 1;
	const int32 RegionHeight = MaxY - MinY + 1;

	// Копия региона живёт до выполнения команды рендер-потоком, поэтому владение
	// передаётся в cleanup-функцию — иначе буфер освободится под работающим RHI.
	uint8* RegionData = new uint8[RegionWidth * RegionHeight * 4];
	for (int32 Row = 0; Row < RegionHeight; ++Row)
	{
		FMemory::Memcpy(
			RegionData + Row * RegionWidth * 4,
			TextureData.GetData() + ((MinY + Row) * GridWidth + MinX) * 4,
			RegionWidth * 4);
	}

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(MinX, MinY, 0, 0, RegionWidth, RegionHeight);
	FogTexture->UpdateTextureRegions(0, 1, Region, RegionWidth * 4, 4, RegionData,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			delete[] Data;
			delete Regions;
		});

	bTextureNeedsFullUpload = false;
	bDisplayDirty = true;
}

void UFogGridSubsystem::PushMaterialParameters()
{
	const UFogOfWarConfigDataAsset* Config = UFogOfWarConfigDataAsset::Get(GetWorld());
	const bool bActive = bGridValid && IsGridEnabled() && FogTexture != nullptr;

	FogMaterials.RemoveAllSwap([](const TWeakObjectPtr<UMaterialInstanceDynamic>& Entry)
	{
		return !Entry.IsValid();
	});

	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Entry : FogMaterials)
	{
		UMaterialInstanceDynamic* Material = Entry.Get();
		if (!Material)
		{
			continue;
		}

		// Выключенный слой не «прозрачный туман», а ОТСУТСТВИЕ затемнения: материал
		// умножает сцену на единицу. Так `xru1.Fog.Grid 0` и незапечённая сетка
		// выглядят одинаково — никакой картинки, только правила.
		Material->SetScalarParameterValue(FogGrid::ParamEnabled, bActive ? 1.f : 0.f);
		if (!bActive)
		{
			continue;
		}

		Material->SetTextureParameterValue(FogGrid::ParamTexture, FogTexture);
		Material->SetVectorParameterValue(FogGrid::ParamOrigin,
			FLinearColor(GridOrigin.X, GridOrigin.Y, 0.f, 0.f));
		Material->SetVectorParameterValue(FogGrid::ParamSize,
			FLinearColor(GridWidth * CellSize, GridHeight * CellSize, 0.f, 0.f));
		// Размер клетки в UV — шаг сглаживающих сэмплов материала. Считается здесь,
		// потому что материал не знает разрешения сетки, а оно меняется вместе с
		// границами сценария.
		Material->SetVectorParameterValue(FogGrid::ParamCellUV,
			FLinearColor(1.f / FMath::Max(1, GridWidth), 1.f / FMath::Max(1, GridHeight), 0.f, 0.f));
		Material->SetVectorParameterValue(FogGrid::ParamUnknownColor, Config->UnknownColor);
		Material->SetScalarParameterValue(FogGrid::ParamUnknownBrightness, Config->UnknownBrightness);
		Material->SetScalarParameterValue(FogGrid::ParamExploredBrightness, Config->ExploredBrightness);
		Material->SetScalarParameterValue(FogGrid::ParamDesaturation, Config->ExploredDesaturation);
		Material->SetScalarParameterValue(FogGrid::ParamOutsideVisible,
			Config->bShowTerrainOutsideBounds ? 1.f : 0.f);

		// Мягкость кромки — одно дизайнерское число, в шейдер уходит парой
		// порогов вокруг середины: чем шире вилка, тем растушёваннее граница.
		const float Softness = FMath::Clamp(Config->EdgeSoftness, 0.f, 1.f);
		Material->SetScalarParameterValue(FogGrid::ParamEdgeLow, 0.5f - 0.49f * Softness);
		Material->SetScalarParameterValue(FogGrid::ParamEdgeHigh, 0.5f + 0.49f * Softness);
	}
}

void UFogGridSubsystem::RegisterFogMaterial(UMaterialInstanceDynamic* Material)
{
	if (!Material)
	{
		return;
	}
	FogMaterials.AddUnique(Material);
	// Новый материал обязан получить параметры сразу: камера может появиться уже
	// после старта сценария (respawn пешки), и ждать следующего пересчёта нельзя —
	// один кадр без параметров это кадр с нулевой яркостью сцены.
	PushMaterialParameters();
}

// --- Диагностика ----------------------------------------------------------------

void UFogGridSubsystem::DumpGridToLog() const
{
	if (!bGridValid)
	{
		UE_LOG(LogXRU1Fog, Display, TEXT("[FogGrid] сетка не построена (слой выключен или нет объёмов навигации)"));
		return;
	}

	int32 VisibleCells = 0;
	int32 ExploredCells = 0;
	int32 BlockedCells = 0;
	for (int32 Index = 0; Index < GridWidth * GridHeight; ++Index)
	{
		VisibleCells += Visible[Index] ? 1 : 0;
		ExploredCells += Explored[Index] ? 1 : 0;
		BlockedCells += Blockers[Index] ? 1 : 0;
	}

	UE_LOG(LogXRU1Fog, Display,
		TEXT("[FogGrid] дамп %dx%d по %.0f см, origin=(%.0f, %.0f): видно=%d, разведано=%d, блокеров=%d, ")
		TEXT("сценарных раскрытий=%d"),
		GridWidth, GridHeight, CellSize, GridOrigin.X, GridOrigin.Y,
		VisibleCells, ExploredCells, BlockedCells, ScriptedReveals.Num());

	// Печатаем СТРОКАМИ снизу вверх: так карта в журнале лежит как на экране —
	// мировая ось Y растёт вверх, а строки лога идут сверху вниз.
	FString Row;
	Row.Reserve(GridWidth + 8);
	for (int32 Y = GridHeight - 1; Y >= 0; --Y)
	{
		Row.Reset();
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const int32 Index = CellIndex(X, Y);
			// Порядок значим: блокер важнее всего (по нему видно форму геометрии),
			// затем текущая видимость, затем память.
			Row.AppendChar(Blockers[Index] ? TEXT('#')
				: Visible[Index] ? TEXT('*')
				: Explored[Index] ? TEXT('.')
				: TEXT(' '));
		}
		UE_LOG(LogXRU1Fog, Display, TEXT("[FogGrid] %3d|%s|"), Y, *Row);
	}
}

// --- Тик ------------------------------------------------------------------------

void UFogGridSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Переключение выключателя — событие: без этого `xru1.Fog.Grid 0` снимал бы
	// затемнение только со следующим пересчётом, и выключатель выглядел бы сломанным.
	const bool bEnabledNow = IsGridEnabled();
	if (bEnabledNow != bGridEnabledLastTick)
	{
		bGridEnabledLastTick = bEnabledNow;
		if (bEnabledNow && !bGridValid && !ActiveScenarioId.IsNone())
		{
			// Слой включили посреди боя (сетка на старте не строилась). Строим
			// сейчас — иначе выключатель был бы односторонним: погасить можно, а
			// вернуть только новым запуском сценария.
			ResetForScenario(ActiveScenarioId, ActiveRunId, bScenarioStartsExplored);
		}
		else
		{
			PushMaterialParameters();
		}
	}

	if (!bEnabledNow || !bGridValid)
	{
		return;
	}

	// Отладочные выключатели меняют СОСТАВ и ФОРМУ видимости, поэтому их
	// переключение — тоже событие: иначе картинка осталась бы прежней до
	// ближайшего шага бойца, и выключатель выглядел бы неработающим.
	const bool bBlockersNow = CVarFogBlockers.GetValueOnGameThread() != 0;
	const bool bScriptedNow = CVarFogScriptedReveals.GetValueOnGameThread() != 0;
	if (bBlockersNow != bBlockersLastTick || bScriptedNow != bScriptedRevealsLastTick)
	{
		bBlockersLastTick = bBlockersNow;
		bScriptedRevealsLastTick = bScriptedNow;
		LastSourcePositions.Reset(); // заставить растеризацию пройти заново
		bVisibilityDirty = true;
		UE_LOG(LogXRU1Fog, Display, TEXT("[FogGrid] отладка: стены=%d, сценарные раскрытия=%d"),
			bBlockersNow ? 1 : 0, bScriptedNow ? 1 : 0);
	}

	if (bHasTimedReveals)
	{
		ExpireScriptedReveals();
	}

	if (bVisibilityDirty)
	{
		bVisibilityDirty = false;
		RasterizeVisibility();
	}

	if (bDisplayDirty)
	{
		AdvanceDisplayAndUpload(DeltaTime, /*bInstant=*/false);
	}

	// Ничего не изменилось — работы ноль: тик стоит двух проверок флагов.
}
