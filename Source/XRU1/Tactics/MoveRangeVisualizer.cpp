#include "MoveRangeVisualizer.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "TacticsCombatStatics.h"
#include "ProceduralMeshComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

/**
 * Диагностика стоимости построения зоны: в лог уходят размер сетки и время.
 * Под cvar и выключено по умолчанию — цена `CellSize` квадратична, и решение
 * о разрешении надо принимать по замеру, а не на глаз. Включить в консоли:
 * `xru1.MoveRange.LogBuildTime 1`.
 */
static TAutoConsoleVariable<int32> CVarLogMoveRangeBuildTime(
	TEXT("xru1.MoveRange.LogBuildTime"),
	0,
	TEXT("1 — писать в лог время и размер сетки при каждой перестройке зоны хода."),
	ECVF_Default);

AMoveRangeVisualizer::AMoveRangeVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;

	ZoneMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ZoneMesh"));
	SetRootComponent(ZoneMesh);
	ZoneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneMesh->SetCastShadow(false);
}

bool AMoveRangeVisualizer::ShowForUnit(AUnitBase* Unit)
{
	Hide();
	CurrentUnit = Unit;
	if (!Unit || !Unit->GetActionPoints())
	{
		return true; // показывать нечего по правилам — не повод для ретраев
	}

	const int32 ActionPoints = Unit->GetActionPoints()->CurrentActionPoints;
	if (ActionPoints <= 0)
	{
		return true;
	}

	// Бюджеты по ДЛИНЕ ПУТИ: 1 AP = MoveRange, 2 AP = 2×MoveRange (GDD §5.3).
	const double BudgetOne = Unit->MoveRange;
	const double BudgetMax = (ActionPoints >= 2) ? BudgetOne * 2. : BudgetOne;

	const double BuildStartTime = FPlatformTime::Seconds();
	if (!BuildDistanceField(Unit, BudgetOne, BudgetMax))
	{
		return false; // навмеш не готов (дыра под юнитом) — контроллер ретраит
	}
	const double FieldDoneTime = FPlatformTime::Seconds();

	UMaterialInterface* BorderOne = BorderOneMaterial ? BorderOneMaterial.Get()
		: (PathOneMaterial ? PathOneMaterial.Get() : ZoneOneMaterial.Get());
	UMaterialInterface* BorderTwo = BorderTwoMaterial ? BorderTwoMaterial.Get()
		: (PathDashMaterial ? PathDashMaterial.Get() : ZoneTwoMaterial.Get());

	if (ActionPoints >= 2)
	{
		// Синяя зона (после хода останется действие) + жёлтое кольцо «рывка».
		BuildContourSection(0, -1.0e9, BudgetOne, ZoneOneMaterial, 4, BorderOne);
		BuildContourSection(1, BudgetOne, BudgetMax, ZoneTwoMaterial, 5, BorderTwo);
	}
	else
	{
		// Последний AP: любой ход завершает активацию юнита — вся зона жёлтая.
		BuildContourSection(0, -1.0e9, BudgetOne, ZoneTwoMaterial, 4, BorderTwo);
	}

	// Замер ПОЛНОЙ стоимости: контур масштабируется с CellSize так же, как поле
	// (ячеек столько же), и по одному лишь времени волны решение о разрешении
	// принимать нельзя — это половина картины.
	if (CVarLogMoveRangeBuildTime.GetValueOnGameThread() != 0)
	{
		int32 ReachableSamples = 0;
		for (const FZoneSample& Sample : Field)
		{
			ReachableSamples += Sample.bReachable ? 1 : 0;
		}
		const double Now = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Log,
			TEXT("[MoveRange] CellSize=%.0f сетка=%dx%d (%d сэмплов, достижимо %d): волна %.2f мс + контур %.2f мс = %.2f мс"),
			CellSize, FieldSamplesPerSide, FieldSamplesPerSide, Field.Num(), ReachableSamples,
			(FieldDoneTime - BuildStartTime) * 1000., (Now - FieldDoneTime) * 1000.,
			(Now - BuildStartTime) * 1000.);
	}

	SetActorHiddenInGame(false);
	return true;
}

void AMoveRangeVisualizer::Hide()
{
	ZoneMesh->ClearAllMeshSections();
	SetActorHiddenInGame(true);
	CurrentUnit = nullptr;
	bPathPreviewVisible = false;
	Field.Reset();
	FieldSamplesPerSide = 0;
	FieldObstacles.Reset();
}

// --- Общие примитивы поля (одни для отрисовки, запроса и волны) --------------------

double AMoveRangeVisualizer::SampleDistance(int32 SampleIndex) const
{
	const FZoneSample& Sample = Field[SampleIndex];
	return Sample.bReachable ? Sample.PathDist : FieldUnreachableDistance;
}

FVector AMoveRangeVisualizer::SampleLocation(int32 SampleIndex) const
{
	// Достижимый сэмпл живёт там, куда его спроецировал навмеш (по этим точкам
	// волна и считала длины). Недостижимый спроецировать не удалось — остаётся
	// узел сетки: он нужен контуру как «внешний» угол ячейки.
	const FZoneSample& Sample = Field[SampleIndex];
	if (Sample.bReachable)
	{
		return Sample.Position;
	}
	const int32 IX = SampleIndex % FieldSamplesPerSide;
	const int32 IY = SampleIndex / FieldSamplesPerSide;
	return FVector(FieldOrigin.X + IX * CellSize, FieldOrigin.Y + IY * CellSize, 0.);
}

double AMoveRangeVisualizer::FindObstacleEntry(const FVector& From, const FVector& To) const
{
	// Пересечение отрезка с окружностью радиуса FieldClearance: ближайший корень
	// квадратного уравнения |From + t·d − O|² = C² в плоскости XY.
	const FVector2D Start(From.X, From.Y);
	const FVector2D Delta(To.X - From.X, To.Y - From.Y);
	const double A = FVector2D::DotProduct(Delta, Delta);
	if (A <= UE_DOUBLE_SMALL_NUMBER)
	{
		return 1.;
	}

	double Earliest = 1.;
	for (const FVector& Obstacle : FieldObstacles)
	{
		const FVector2D ToStart = Start - FVector2D(Obstacle.X, Obstacle.Y);
		const double B = 2. * FVector2D::DotProduct(ToStart, Delta);
		const double C = FVector2D::DotProduct(ToStart, ToStart) - FMath::Square(FieldClearance);
		if (C <= 0.)
		{
			return 0.; // начало уже внутри диска
		}
		const double Discriminant = B * B - 4. * A * C;
		if (Discriminant < 0.)
		{
			continue; // мимо
		}
		const double T = (-B - FMath::Sqrt(Discriminant)) / (2. * A);
		if (T >= 0. && T < Earliest)
		{
			Earliest = T;
		}
	}
	return Earliest;
}

bool AMoveRangeVisualizer::HasClearLine(const ANavigationData* NavData, const FVector& From, const FVector& To,
	NavNodeRef FromPoly) const
{
	if (!NavData)
	{
		return false;
	}

	// 1) Геометрия уровня: surface-raycast по навмешу — стена или обрыв рвут линию.
	// Вариант с известным полигоном старта дешевле (не проецирует точку заново).
	const FSharedConstNavQueryFilter Filter = NavData->GetDefaultQueryFilter();
	FVector HitLocation;
	const bool bHitGeometry = (FromPoly != INVALID_NAVNODEREF)
		? ARecastNavMesh::NavMeshRaycast(NavData, FromPoly, From, To, HitLocation, Filter, this)
		: ARecastNavMesh::NavMeshRaycast(NavData, From, To, HitLocation, Filter, this);
	if (bHitGeometry)
	{
		return false;
	}

	// 2) Занятость: между линией и чужой занятой клеткой нужен просвет
	// FieldClearance — он ВКЛЮЧАЕТ собственную ширину бегущего. Без неё маршрут
	// прокладывался в щель между двумя бойцами, куда третий физически не лезет.
	// Радиус тот же, что у правила «сюда нельзя встать»: одно конфигурационное
	// пространство на всё, иначе появляются клетки, куда встать можно, а выйти
	// нельзя.
	const double ClearanceSq = FMath::Square(FieldClearance);
	const FVector SegStart(From.X, From.Y, 0.);
	const FVector SegEnd(To.X, To.Y, 0.);
	const FVector Segment = SegEnd - SegStart;
	const double SegmentLenSq = Segment.SizeSquared();

	for (const FVector& Obstacle : FieldObstacles)
	{
		const FVector Flat(Obstacle.X, Obstacle.Y, 0.);
		const double T = (SegmentLenSq > UE_DOUBLE_SMALL_NUMBER)
			? FMath::Clamp(FVector::DotProduct(Flat - SegStart, Segment) / SegmentLenSq, 0., 1.)
			: 0.;
		if (FVector::DistSquared(Flat, SegStart + Segment * T) >= ClearanceSq)
		{
			continue; // просвет есть
		}

		// Единственное послабление: ближайшая к диску точка отрезка — его НАЧАЛО,
		// значит просвет был нарушен уже на старте, а отрезок только удаляется.
		// Это «выйти из тесноты»: боец, оказавшийся вплотную к союзнику, обязан
		// иметь возможность сдвинуться. Войти в теснину или пройти сквозь неё
		// это не разрешает — там минимум приходится на середину отрезка.
		if (T <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}
		return false;
	}
	return true;
}

// --- Поле дистанций ---------------------------------------------------------------

bool AMoveRangeVisualizer::BuildDistanceField(const AUnitBase* Unit, double BudgetOne, double BudgetMax)
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	const ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavData);
	if (!NavMesh)
	{
		return false;
	}

	const FVector Origin = Unit->GetActorLocation();
	FieldStart = Origin;

	// Диски занятости ДРУГИХ юнитов (навмеш статичен и о юнитах не знает —
	// XCOM-подход «занятые клетки»): волна в диск не заходит. Снимок держим в
	// поле: запрос приказа обязан судить по ТЕМ ЖЕ дискам, что и волна.
	UTacticsCombatStatics::GetUnitObstacles(GetWorld(), Unit, FieldObstacles);

	// Просвет с учётом ширины САМОГО бойца — одна величина на «встать» и
	// «пройти» (см. UTacticsCombatStatics::GetUnitClearance). Кладём в поле:
	// им же судит запрос приказа, иначе волна и валидация разойдутся.
	FieldClearance = UTacticsCombatStatics::GetUnitClearance(Unit);
	const double BlockRadiusSq = FMath::Square(FieldClearance);
	auto IsBlockedByUnit = [this, BlockRadiusSq](const FVector& Position)
	{
		for (const FVector& Obstacle : FieldObstacles)
		{
			if (FVector::DistSquared2D(Obstacle, Position) < BlockRadiusSq)
			{
				return true;
			}
		}
		return false;
	};

	// ВОЛНА ПО СЕТКЕ СЭМПЛОВ (не по полигонам: полигонный Dijkstra на огромных
	// полигонах Recast врал в разы) — но не пошаговая, а ANY-ANGLE (Theta*):
	// релаксируя соседа, сначала пробуем протянуть прямую от РОДИТЕЛЯ текущей
	// ячейки. Это даёт два свойства, ради которых всё и переделано:
	//
	//  1) длина в поле = длина настоящей ломаной, а не октильная оценка с
	//     завышением ~8%. Костыль «уточнить спорную полосу у порогов честным
	//     FindPathSync» больше не нужен — а он смешивал в одном поле ДВЕ метрики
	//     (октиль и funnel), из-за чего граница зоны дрожала и расходилась с лентой;
	//  2) цепочка родителей — это уже готовый маршрут. Лента превью рисуется ПО
	//     НЕЙ, а не отдельным navmesh-запросом «для картинки», который шёл своей
	//     дорогой и мог увести линию за пределы нарисованной зоны.
	//
	// Проходимость каждого отрезка проверяет HasClearLine (навмеш-рэйкаст +
	// диски). Отсюда ключевая гарантия: ломаная целиком проходима по навмешу,
	// значит её длина — ВЕРХНЯЯ оценка кратчайшего пути, и боец не может
	// пробежать больше, чем обещала зона.
	//
	// Проекция соседа берётся от высоты текущей волны — поле следует пандусам и
	// не прилипает к чужим этажам.
	const double ExpandLimit = BudgetMax + CellSize * 2.;

	// Регулярная сетка сэмплов с запасом в ячейку по краям (для интерполяции границы).
	const float GridRadius = static_cast<float>(BudgetMax) + CellSize * 2.f;
	FieldSamplesPerSide = FMath::CeilToInt(2.f * GridRadius / CellSize) + 1;
	FieldOrigin = Origin - FVector(GridRadius, GridRadius, 0.f);
	const int32 NumSamples = FieldSamplesPerSide * FieldSamplesPerSide;
	Field.Reset();
	Field.SetNum(NumSamples);

	// Одна условность «недостижимо» на ВСЕХ потребителей поля: чуть дальше
	// максимального порога. Раньше отрисовка и запрос считали её по-разному —
	// отсюда и расхождение «нарисовано больше, чем кликается».
	FieldUnreachableDistance = BudgetMax + CellSize;

	// Рабочие данные волны: позиция сэмпла на навмеше и его полигон.
	TArray<FVector> SamplePositions;
	SamplePositions.SetNumZeroed(NumSamples);
	TArray<NavNodeRef> SamplePolys;
	SamplePolys.Init(INVALID_NAVNODEREF, NumSamples);
	TArray<bool> ProjectionTried;
	ProjectionTried.Init(false, NumSamples);

	const FSharedConstNavQueryFilter Filter = NavMesh->GetDefaultQueryFilter();
	auto IndexOf = [this](int32 IX, int32 IY) { return IY * FieldSamplesPerSide + IX; };

	// Стартовая ячейка — под юнитом.
	const int32 StartIX = FMath::RoundToInt((Origin.X - FieldOrigin.X) / CellSize);
	const int32 StartIY = FMath::RoundToInt((Origin.Y - FieldOrigin.Y) / CellSize);
	FNavLocation StartLocation;
	if (!NavData->ProjectPoint(
			FVector(FieldOrigin.X + StartIX * CellSize, FieldOrigin.Y + StartIY * CellSize, Origin.Z),
			StartLocation, FVector(CellSize, CellSize, 300.f), Filter))
	{
		return false;
	}
	const int32 StartIndex = IndexOf(StartIX, StartIY);
	Field[StartIndex].PathDist = FVector::Dist2D(Origin, StartLocation.Location);
	Field[StartIndex].Position = StartLocation.Location;
	Field[StartIndex].bReachable = true;
	Field[StartIndex].Parent = StartIndex; // старт — сам себе родитель (конец цепочки)
	SamplePositions[StartIndex] = StartLocation.Location;
	SamplePolys[StartIndex] = StartLocation.NodeRef;
	ProjectionTried[StartIndex] = true;

	struct FQueueEntry
	{
		double Dist;
		int32 Index;
		bool operator<(const FQueueEntry& Other) const { return Dist < Other.Dist; }
	};
	TArray<FQueueEntry> Heap;
	Heap.HeapPush({Field[StartIndex].PathDist, StartIndex});

	const int32 NeighborDX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
	const int32 NeighborDY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
	// Вертикальный допуск проекции соседа: перепад пандуса на одну ячейку.
	const FVector ProjectExtent(CellSize * 0.5f, CellSize * 0.5f, 250.f);

	while (Heap.Num() > 0)
	{
		FQueueEntry Top;
		Heap.HeapPop(Top, EAllowShrinking::No);
		if (Top.Dist > ExpandLimit)
		{
			break; // куча упорядочена: дальше только записи за пределом волны
		}
		if (Top.Dist > Field[Top.Index].PathDist + UE_KINDA_SMALL_NUMBER)
		{
			continue; // устаревшая запись кучи (ячейка уже релаксирована короче)
		}

		const int32 CX = Top.Index % FieldSamplesPerSide;
		const int32 CY = Top.Index / FieldSamplesPerSide;
		const FVector CurrentPos = SamplePositions[Top.Index];

		for (int32 Dir = 0; Dir < 8; ++Dir)
		{
			const int32 NX = CX + NeighborDX[Dir];
			const int32 NY = CY + NeighborDY[Dir];
			if (NX < 0 || NY < 0 || NX >= FieldSamplesPerSide || NY >= FieldSamplesPerSide)
			{
				continue;
			}
			const int32 NIdx = IndexOf(NX, NY);

			// Первая волна, дошедшая до ячейки, проецирует её на СВОЙ уровень.
			if (!ProjectionTried[NIdx])
			{
				ProjectionTried[NIdx] = true;
				const FVector Guess(FieldOrigin.X + NX * CellSize, FieldOrigin.Y + NY * CellSize, CurrentPos.Z);
				FNavLocation NavLoc;
				// Проекция не должна «уползать» вбок из своей ячейки (прилипание к соседней геометрии).
				if (NavData->ProjectPoint(Guess, NavLoc, ProjectExtent, Filter) &&
					FVector::DistSquared2D(NavLoc.Location, Guess) <= FMath::Square(CellSize * 0.5f))
				{
					SamplePositions[NIdx] = NavLoc.Location;
					SamplePolys[NIdx] = NavLoc.NodeRef;
				}
			}
			if (SamplePolys[NIdx] == INVALID_NAVNODEREF)
			{
				continue; // вне навмеша
			}

			// Клетка занята другим юнитом — волна не заходит. Флаг здесь чисто
			// как КЭШ: не пускать в клетку уже умеет HasClearLine (отрезок,
			// упирающийся в диск, свободным не считается), но ранний выход
			// экономит рэйкаст на каждую попытку релаксации такой клетки.
			if (!Field[NIdx].bBlockedByUnit && !Field[NIdx].bReachable && IsBlockedByUnit(SamplePositions[NIdx]))
			{
				Field[NIdx].bBlockedByUnit = true;
			}
			if (Field[NIdx].bBlockedByUnit)
			{
				continue;
			}

			// ANY-ANGLE: сначала пробуем протянуть прямую от РОДИТЕЛЯ текущей
			// ячейки — если она свободна, маршрут спрямляется и длина становится
			// настоящей евклидовой, а не суммой шагов по сетке. Родитель уже
			// извлечён из кучи, значит его дистанция окончательна.
			//
			// ПОРЯДОК ПРОВЕРОК ВАЖЕН: сначала дешёвая арифметика (улучшает ли
			// кандидат и влезает ли в волну), и только потом рэйкаст. Соседей
			// пробуют до 8 раз каждого, и почти всегда сосед уже релаксирован
			// короче — рэйкаст для него чистая трата. Проверка «сначала LOS»
			// стоила бы примерно на порядок больше запросов к навмешу.
			const int32 ParentIdx = Field[Top.Index].Parent;
			const double BestKnown = Field[NIdx].PathDist;
			double NewDist = TNumericLimits<double>::Max();
			int32 NewParent = INDEX_NONE;

			auto TryBranch = [&](int32 FromIdx, double FromDist)
			{
				const double Candidate = FromDist + FVector::Dist(SamplePositions[FromIdx], SamplePositions[NIdx]);
				if (Candidate > ExpandLimit || Candidate + UE_KINDA_SMALL_NUMBER >= BestKnown)
				{
					return false;
				}
				if (!HasClearLine(NavData, SamplePositions[FromIdx], SamplePositions[NIdx], SamplePolys[FromIdx]))
				{
					return false;
				}
				NewDist = Candidate;
				NewParent = FromIdx;
				return true;
			};

			// Спрямление от родителя даёт кандидата НЕ ХУЖЕ обычного шага
			// (неравенство треугольника, D(C) = D(P) + |P−C| по построению),
			// поэтому пробуем его первым; обычный шаг — только если спрямить
			// не вышло геометрически.
			if (ParentIdx == INDEX_NONE || ParentIdx == Top.Index ||
				!TryBranch(ParentIdx, Field[ParentIdx].PathDist))
			{
				TryBranch(Top.Index, Top.Dist);
			}

			if (NewParent == INDEX_NONE)
			{
				continue;
			}

			Field[NIdx].PathDist = NewDist;
			Field[NIdx].Position = SamplePositions[NIdx];
			Field[NIdx].bReachable = true;
			Field[NIdx].Parent = NewParent;
			Heap.HeapPush({NewDist, NIdx});
		}
	}
	return true;
}

// --- Запрос поля (валидация приказа и превью) ---------------------------------------

bool AMoveRangeVisualizer::IsFieldBuiltFor(const AUnitBase* Unit) const
{
	return Unit && CurrentUnit.Get() == Unit && FieldSamplesPerSide >= 2;
}

bool AMoveRangeVisualizer::PlanMoveTo(const FVector& Goal, FMoveOrderPlan& OutPlan) const
{
	OutPlan = FMoveOrderPlan();
	OutPlan.Goal = Goal;

	const AUnitBase* Unit = CurrentUnit.Get();
	const UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
	if (!ActionPoints || FieldSamplesPerSide < 2)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	if (!NavData)
	{
		return false;
	}

	// 1) Приводим цель к полю ОДИН раз — и превью, и приказ дальше работают с
	// этой точкой. Раньше превью просто пряталось на занятой точке, а клик её
	// выталкивал и проходил: ховер говорил «нельзя», клик делал.
	FVector Adjusted = Goal;
	if (!UTacticsCombatStatics::AdjustGoalOutOfUnits(FieldObstacles, Unit, Adjusted))
	{
		return false; // плотная толпа — встать негде
	}

	// Проекция на навмеш: клик по крыше/пропасти отсекается здесь, а не даёт
	// «недостижимую» ячейку, которую потом кто-нибудь истолкует как близкую.
	const FSharedConstNavQueryFilter Filter = NavData->GetDefaultQueryFilter();
	FNavLocation GoalOnNav;
	if (!NavData->ProjectPoint(Adjusted, GoalOnNav, FVector(CellSize, CellSize, 300.f), Filter))
	{
		return false;
	}

	// 2) Дистанция до произвольной точки = продолжение той же волны: минимум по
	// ближайшим сэмплам от «дойти до сэмпла + прямая до цели», причём прямая
	// проверяется тем же HasClearLine. Это НЕ интерполяция поля «на глаз»:
	// каждое слагаемое — реально проходимая ломаная, поэтому результат всегда
	// не меньше настоящего кратчайшего пути.
	//
	// Именно здесь жил главный баг: старая билинейная версия при ячейке БЕЗ
	// достижимых углов возвращала CellSize (≈50 см) вместо «недостижимо» —
	// и любая точка за стеной или в углу квадратной сетки (а её угол дальше
	// бюджета в 1.4 раза!) стоила 1 AP. Отсюда «убегаю дальше, чем показано,
	// за один поинт». Теперь недостижимость — это отсутствие кандидатов.
	const int32 CenterIX = FMath::RoundToInt((GoalOnNav.Location.X - FieldOrigin.X) / CellSize);
	const int32 CenterIY = FMath::RoundToInt((GoalOnNav.Location.Y - FieldOrigin.Y) / CellSize);

	double BestDistance = TNumericLimits<double>::Max();
	int32 BestSample = INDEX_NONE;
	for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			const int32 IX = CenterIX + OffsetX;
			const int32 IY = CenterIY + OffsetY;
			if (IX < 0 || IY < 0 || IX >= FieldSamplesPerSide || IY >= FieldSamplesPerSide)
			{
				continue;
			}
			const int32 Index = IY * FieldSamplesPerSide + IX;
			if (!Field[Index].bReachable)
			{
				continue;
			}
			const FVector SamplePos = SampleLocation(Index);
			const double Candidate = Field[Index].PathDist + FVector::Dist(SamplePos, GoalOnNav.Location);
			if (Candidate >= BestDistance || !HasClearLine(NavData, SamplePos, GoalOnNav.Location))
			{
				continue;
			}
			BestDistance = Candidate;
			BestSample = Index;
		}
	}
	if (BestSample == INDEX_NONE)
	{
		return false; // ни одного достижимого сэмпла со свободной линией до цели
	}

	// 3) Стоимость — через ЕДИНСТВЕННОЕ правило порогов (GDD §5.3).
	const int32 Cost = UTacticsCombatStatics::GetMoveCostForDistance(
		Unit, static_cast<float>(BestDistance), ActionPoints->CurrentActionPoints);
	if (Cost == 0)
	{
		return false;
	}

	// 4) Маршрут — цепочка родителей поля, развёрнутая от цели к старту.
	TArray<FVector> Reversed;
	int32 Current = BestSample;
	for (int32 Guard = 0; Current != INDEX_NONE && Guard <= Field.Num(); ++Guard)
	{
		Reversed.Add(SampleLocation(Current));
		const int32 ParentIndex = Field[Current].Parent;
		if (ParentIndex == Current)
		{
			break; // дошли до старта
		}
		Current = ParentIndex;
	}

	OutPlan.PathPoints.Reserve(Reversed.Num() + 2);
	OutPlan.PathPoints.Add(FieldStart);
	for (int32 i = Reversed.Num() - 1; i >= 0; --i)
	{
		// Отбрасываем ТОЛЬКО стартовый сэмпл, и только если он практически
		// совпал с позицией бойца. Остальные вершины — поворотные точки у
		// занятых клеток: выкидывать их «за близостью» нельзя, срезанный угол
		// уводит в союзника. Лишние вершины уберёт спрямление ниже — оно, в
		// отличие от этой проверки, доказывает проходимость рэйкастом.
		const bool bIsStartSample = (i == Reversed.Num() - 1);
		if (bIsStartSample &&
			FVector::DistSquared2D(Reversed[i], OutPlan.PathPoints.Last()) <= FMath::Square(CellSize * 0.5))
		{
			continue;
		}
		OutPlan.PathPoints.Add(Reversed[i]);
	}
	if (FVector::DistSquared2D(GoalOnNav.Location, OutPlan.PathPoints.Last()) > 1.)
	{
		OutPlan.PathPoints.Add(GoalOnNav.Location);
	}

	// Спрямление (string pulling): выкидываем вершину, если её сосед слева виден
	// соседу справа. Волна any-angle уже даёт почти прямые участки, но остаточные
	// изломы от сетки остаются — а по этой ломаной боец РЕАЛЬНО бежит, тормозя на
	// каждой вершине. Спрямление и рисует чище, и убирает лишние остановки.
	// Длину приказа НЕ пересчитываем: платим по метрике поля, спрямление делает
	// фактический путь только короче — ошибка остаётся в пользу игрока.
	for (int32 i = 1; i + 1 < OutPlan.PathPoints.Num(); )
	{
		if (HasClearLine(NavData, OutPlan.PathPoints[i - 1], OutPlan.PathPoints[i + 1]))
		{
			OutPlan.PathPoints.RemoveAt(i);
		}
		else
		{
			++i;
		}
	}

	OutPlan.bReachable = true;
	OutPlan.PathLength = static_cast<float>(BestDistance);
	OutPlan.ActionPointCost = Cost;
	OutPlan.Goal = GoalOnNav.Location;
	return true;
}

int32 AMoveRangeVisualizer::GetMoveCostTo(const FVector& Goal) const
{
	FMoveOrderPlan Plan;
	return PlanMoveTo(Goal, Plan) ? Plan.ActionPointCost : 0;
}

// --- Marching squares ---------------------------------------------------------------

void AMoveRangeVisualizer::BuildContourSection(int32 SectionIndex, double MinDist, double MaxDist,
	UMaterialInterface* Material, int32 BorderSectionIndex, UMaterialInterface* BorderMaterial)
{
	if (FieldSamplesPerSide < 2)
	{
		return;
	}

	// Навдата для точных кромок у стен (surface-raycast до края навмеша).
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	const FSharedConstNavQueryFilter Filter = NavData ? NavData->GetDefaultQueryFilter() : nullptr;

	// «Внутренность» области MinDist < D ≤ MaxDist как знаковая функция:
	// f = min(D − MinDist, MaxDist − D) ≥ 0 внутри; ноль — искомый контур.
	//
	// Дистанцию берём через ОБЩИЙ SampleDistance — ту же, по которой отвечает
	// PlanMoveTo. Прежняя версия дополнительно ЗАЖИМАЛА достижимые сэмплы
	// (min(D, MaxDist+CellSize)): на крутом градиенте у стены или диска это
	// уводило нарисованную границу к дальнему углу ячейки, и зона выглядела
	// заметно больше того, что реально кликалось.
	auto Inside = [this, MinDist, MaxDist](int32 IX, int32 IY) -> double
	{
		const double D = SampleDistance(IY * FieldSamplesPerSide + IX);
		return FMath::Min(D - MinDist, MaxDist - D);
	};
	// Через общий SampleLocation: достижимый угол берётся ТАМ, куда его
	// спроецировал навмеш (по этим точкам волна и считала длины), недостижимый —
	// узлом сетки. Раньше здесь был узел сетки всегда, и контур расходился с
	// метрикой на пол-ячейки там, где проекция уползала.
	auto SamplePos = [this](int32 IX, int32 IY) -> FVector
	{
		return SampleLocation(IY * FieldSamplesPerSide + IX);
	};

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	const FVector MeshOffset = FVector(0, 0, SurfaceOffset) - GetActorLocation();

	// Хорды внешней границы области (мировые координаты) — сырьё для каймы.
	TArray<TPair<FVector, FVector>> BorderChords;

	// Обход ячеек: полигон ячейки = внутренние углы + точки пересечения контура
	// с рёбрами (в порядке обхода) — покрывает все 16 случаев marching squares.
	for (int32 CY = 0; CY + 1 < FieldSamplesPerSide; ++CY)
	{
		for (int32 CX = 0; CX + 1 < FieldSamplesPerSide; ++CX)
		{
			// Углы против часовой: (0,0) → (1,0) → (1,1) → (0,1).
			const int32 CornerX[4] = {CX, CX + 1, CX + 1, CX};
			const int32 CornerY[4] = {CY, CY, CY + 1, CY + 1};

			double F[4];
			FVector V[4];
			bool bReach[4];
			int32 NumInside = 0;
			double MinZ = TNumericLimits<double>::Max();
			double MaxZ = -TNumericLimits<double>::Max();
			for (int32 i = 0; i < 4; ++i)
			{
				F[i] = Inside(CornerX[i], CornerY[i]);
				V[i] = SamplePos(CornerX[i], CornerY[i]);
				bReach[i] = Field[CornerY[i] * FieldSamplesPerSide + CornerX[i]].bReachable;
				NumInside += (F[i] >= 0.);
				if (bReach[i])
				{
					MinZ = FMath::Min(MinZ, static_cast<double>(V[i].Z));
					MaxZ = FMath::Max(MaxZ, static_cast<double>(V[i].Z));
				}
			}
			if (NumInside == 0)
			{
				continue;
			}
			// Ячейка накрывает обрыв (сэмплы на разных уровнях) — заливку не
			// натягиваем через вертикаль, оставляем разрыв на кромке.
			if (MaxZ - MinZ > 160.)
			{
				continue;
			}

			// Вершины полигона ячейки + тип каждой: 0 — угол сетки, 1 — точка
			// внешней границы, 2 — точка внутреннего порога кольца (кайма её
			// пропускает: линию порога уже рисует кайма внутренней зоны).
			TArray<FVector, TInlineAllocator<8>> CellPoly;
			TArray<uint8, TInlineAllocator<8>> CellKind;
			for (int32 i = 0; i < 4; ++i)
			{
				const int32 j = (i + 1) % 4;
				if (F[i] >= 0.)
				{
					CellPoly.Add(V[i]);
					CellKind.Add(0);
				}
				if ((F[i] >= 0.) != (F[j] >= 0.))
				{
					const int32 In = (F[i] >= 0.) ? i : j;   // внутренний угол
					const int32 Out = (In == i) ? j : i;      // внешний угол

					// Доля отрезка In→Out до границы. Интерполировать дистанции
					// можно ТОЛЬКО вдоль свободной прямой: поле 1-липшицево лишь
					// там, где сосед достижим напрямую. Если прямой нет (стена или
					// занятая клетка), сосед взят длинным обходом, скачок дистанции
					// огромен — интерполяция прижала бы границу к внутреннему углу,
					// и зона «съедалась» бы вокруг бойцов и стен, хотя клик туда
					// проходит. В этом случае границу ставим ГЕОМЕТРИЧЕСКИ.
					const FVector From = V[In];
					const FVector To(V[Out].X, V[Out].Y, V[In].Z);
					double T;
					if (NavData && !HasClearLine(NavData, From, To))
					{
						const double FullLen = FVector::Dist2D(From, To);
						FVector HitLocation = To;
						if (ARecastNavMesh::NavMeshRaycast(NavData, From, To, HitLocation, Filter, this) &&
							FullLen > 1.)
						{
							// Мешает геометрия — режем по кромке навмеша.
							T = FVector::Dist2D(From, HitLocation) / FullLen;
						}
						else
						{
							// Навмеш чист — значит мешает диск занятости: режем по
							// его окружности, ровно там, где AdjustGoalOutOfUnits
							// поставит цель клика. Нарисованное и кликаемое совпадают.
							T = FindObstacleEntry(From, To);
						}
						T = FMath::Clamp(T, 0.05, 1.0);
					}
					else
					{
						T = F[In] / (F[In] - F[Out]); // ноль f на ребре (порог бюджета)
					}

					FVector Cross = FMath::Lerp(V[In], V[Out], static_cast<float>(T));
					// Высота от достижимого угла (у недостижимого Z не сэмплирован).
					if (!bReach[Out])
					{
						Cross.Z = V[In].Z;
					}

					// Классификация порога: пересечение внутренней границы кольца
					// (внешний угол ещё ДОСТИЖИМ, но ближе MinDist) — тип 2.
					uint8 Kind = 1;
					if (MinDist > 0. && bReach[Out])
					{
						const double OutDist = SampleDistance(CornerY[Out] * FieldSamplesPerSide + CornerX[Out]);
						if (OutDist < MinDist)
						{
							Kind = 2;
						}
					}
					CellPoly.Add(Cross);
					CellKind.Add(Kind);
				}
			}
			if (CellPoly.Num() < 3)
			{
				continue;
			}

			// Хорды границы: соседние (по обходу полигона) точки-пересечения.
			// Пара с точкой внутреннего порога (тип 2) пропускается.
			for (int32 i = 0; i < CellPoly.Num(); ++i)
			{
				const int32 j = (i + 1) % CellPoly.Num();
				if (CellKind[i] == 1 && CellKind[j] == 1
					&& FVector::DistSquared2D(CellPoly[i], CellPoly[j]) > 1.)
				{
					BorderChords.Emplace(CellPoly[i], CellPoly[j]);
				}
			}

			const int32 Base = Vertices.Num();
			for (const FVector& P : CellPoly)
			{
				Vertices.Add(P + MeshOffset);
			}
			for (int32 i = 1; i + 1 < CellPoly.Num(); ++i)
			{
				Triangles.Add(Base);
				Triangles.Add(Base + i);
				Triangles.Add(Base + i + 1);
			}
		}
	}

	// Кайма по внешней границе (сглаженная линия — XCOM-обводка зоны).
	if (BorderWidth > 0.f)
	{
		BuildZoneBorder(BorderSectionIndex, BorderChords, BorderMaterial);
	}

	if (Triangles.Num() == 0)
	{
		return;
	}

	ZoneMesh->CreateMeshSection(SectionIndex, Vertices, Triangles,
		TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(),
		/*bCreateCollision=*/false);
	if (Material)
	{
		ZoneMesh->SetMaterial(SectionIndex, Material);
	}
}

void AMoveRangeVisualizer::BuildZoneBorder(int32 SectionIndex, const TArray<TPair<FVector, FVector>>& Segments,
	UMaterialInterface* Material)
{
	ZoneMesh->ClearMeshSection(SectionIndex);
	if (Segments.Num() == 0)
	{
		return;
	}

	// --- Сшивка хорд в полилинии: концы совпадают на общих рёбрах ячеек
	// (одинаковая арифметика в соседних ячейках), квантование до сантиметра.
	auto KeyOf = [](const FVector& P) { return FIntPoint(FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y)); };

	TMultiMap<FIntPoint, int32> SegmentsByEnd;
	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		SegmentsByEnd.Add(KeyOf(Segments[i].Key), i);
		SegmentsByEnd.Add(KeyOf(Segments[i].Value), i);
	}

	TArray<bool> Used;
	Used.Init(false, Segments.Num());

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	// Кайма выше заливки, но ниже ленты пути (+4), чтобы путь читался поверх.
	const FVector MeshOffset = FVector(0, 0, SurfaceOffset + 2.f) - GetActorLocation();
	const float HalfWidth = BorderWidth * 0.5f;

	// Лента вдоль полилинии (митра на изломах — как у ленты пути).
	auto EmitPolyline = [&](const TArray<FVector>& Points, bool bClosed)
	{
		if (Points.Num() < 2)
		{
			return;
		}
		const int32 Base = Vertices.Num();
		const int32 Num = Points.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector& Prev = Points[bClosed ? (i + Num - 1) % Num : FMath::Max(i - 1, 0)];
			const FVector& Next = Points[bClosed ? (i + 1) % Num : FMath::Min(i + 1, Num - 1)];
			FVector Dir = Next - Prev;
			Dir.Z = 0.f;
			if (!Dir.Normalize())
			{
				Dir = FVector::ForwardVector;
			}
			const FVector Side(-Dir.Y * HalfWidth, Dir.X * HalfWidth, 0.f);
			Vertices.Add(Points[i] - Side + MeshOffset);
			Vertices.Add(Points[i] + Side + MeshOffset);
		}
		const int32 QuadCount = bClosed ? Num : Num - 1;
		for (int32 i = 0; i < QuadCount; ++i)
		{
			const int32 A = Base + i * 2;
			const int32 B = Base + ((i + 1) % Num) * 2;
			Triangles.Add(A); Triangles.Add(B); Triangles.Add(A + 1);
			Triangles.Add(A + 1); Triangles.Add(B); Triangles.Add(B + 1);
		}
	};

	for (int32 SeedIndex = 0; SeedIndex < Segments.Num(); ++SeedIndex)
	{
		if (Used[SeedIndex])
		{
			continue;
		}
		Used[SeedIndex] = true;

		TArray<FVector> Chain;
		Chain.Add(Segments[SeedIndex].Key);
		Chain.Add(Segments[SeedIndex].Value);

		// Наращивание цепочки: сначала вперёд от хвоста, затем назад от головы.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			bool bExtended = true;
			while (bExtended)
			{
				bExtended = false;
				const FVector Tip = (Pass == 0) ? Chain.Last() : Chain[0];
				TArray<int32> Candidates;
				SegmentsByEnd.MultiFind(KeyOf(Tip), Candidates);
				for (const int32 CandIndex : Candidates)
				{
					if (Used[CandIndex])
					{
						continue;
					}
					const FVector& A = Segments[CandIndex].Key;
					const FVector& B = Segments[CandIndex].Value;
					const bool bFromA = FVector::DistSquared2D(A, Tip) < 4.;
					const bool bFromB = !bFromA && FVector::DistSquared2D(B, Tip) < 4.;
					if (!bFromA && !bFromB)
					{
						continue;
					}
					Used[CandIndex] = true;
					const FVector& NextPoint = bFromA ? B : A;
					if (Pass == 0)
					{
						Chain.Add(NextPoint);
					}
					else
					{
						Chain.Insert(NextPoint, 0);
					}
					bExtended = true;
					break;
				}
			}
		}

		const bool bClosed = Chain.Num() > 3
			&& FVector::DistSquared2D(Chain[0], Chain.Last()) < 4.;
		if (bClosed)
		{
			Chain.Pop(); // замкнутый контур: последняя точка дублирует первую
		}
		if (Chain.Num() < (bClosed ? 3 : 2))
		{
			continue; // мусорные обрывки
		}

		// Сглаживание Chaikin (срез углов): пиксельная «лесенка» сетки уходит,
		// кайма течёт плавной линией. Открытые цепочки сохраняют концы.
		for (int32 Iter = 0; Iter < BorderSmoothIterations && Chain.Num() >= 3; ++Iter)
		{
			TArray<FVector> Smoothed;
			Smoothed.Reserve(Chain.Num() * 2 + 2);
			const int32 Num = Chain.Num();
			if (!bClosed)
			{
				Smoothed.Add(Chain[0]);
			}
			const int32 EdgeCount = bClosed ? Num : Num - 1;
			for (int32 i = 0; i < EdgeCount; ++i)
			{
				const FVector& P = Chain[i];
				const FVector& Q = Chain[(i + 1) % Num];
				Smoothed.Add(P * 0.75f + Q * 0.25f);
				Smoothed.Add(P * 0.25f + Q * 0.75f);
			}
			if (!bClosed)
			{
				Smoothed.Add(Chain.Last());
			}
			Chain = MoveTemp(Smoothed);
		}

		EmitPolyline(Chain, bClosed);
	}

	if (Triangles.Num() == 0)
	{
		return;
	}

	ZoneMesh->CreateMeshSection(SectionIndex, Vertices, Triangles,
		TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(),
		/*bCreateCollision=*/false);
	if (Material)
	{
		ZoneMesh->SetMaterial(SectionIndex, Material);
	}
}

// --- Превью пути ---------------------------------------------------------------

void AMoveRangeVisualizer::UpdatePathPreview(const FVector& GoalLocation)
{
	const AUnitBase* Unit = CurrentUnit.Get();
	const UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;

	// ЕДИНЫЙ план: превью показывает ровно то, что сделает клик, — и линию, и
	// цену. Своего navmesh-запроса «для формы линии» здесь больше нет: он шёл
	// своей дорогой (funnel не знает про диски занятости) и мог увести ленту
	// далеко за нарисованную зону.
	FMoveOrderPlan Plan;
	if (!ActionPoints || !PlanMoveTo(GoalLocation, Plan))
	{
		HidePathPreview();
		return;
	}

	// Цвет: тратит ПОСЛЕДНИЙ AP («рывок») — жёлтый, иначе синий.
	UMaterialInterface* Material = (Plan.ActionPointCost == ActionPoints->CurrentActionPoints)
		? (PathDashMaterial ? PathDashMaterial.Get() : ZoneTwoMaterial.Get())
		: (PathOneMaterial ? PathOneMaterial.Get() : ZoneOneMaterial.Get());

	BuildPathRibbon(Plan.PathPoints, Material);
}

void AMoveRangeVisualizer::HidePathPreview()
{
	if (!bPathPreviewVisible)
	{
		return;
	}
	ZoneMesh->ClearMeshSection(2);
	ZoneMesh->ClearMeshSection(3);
	bPathPreviewVisible = false;
}

void AMoveRangeVisualizer::BuildPathRibbon(const TArray<FVector>& PathPoints, UMaterialInterface* Material)
{
	ZoneMesh->ClearMeshSection(2);
	ZoneMesh->ClearMeshSection(3);
	bPathPreviewVisible = false;
	if (PathPoints.Num() < 2)
	{
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	const FSharedConstNavQueryFilter Filter = NavData ? NavData->GetDefaultQueryFilter() : nullptr;
	auto ProjectToNav = [&](const FVector& Point) -> FVector
	{
		FNavLocation NavLoc;
		if (NavData && NavData->ProjectPoint(Point, NavLoc, FVector(CellSize, CellSize, 300.f), Filter))
		{
			return NavLoc.Location;
		}
		return Point;
	};

	// Точки нав-пути стоят редко (изломы у углов полигонов) — прямые отрезки
	// между ними режут рельеф на пандусах. Подразбиваем сегменты и проецируем
	// промежуточные точки на навмеш: лента ложится по поверхности.
	TArray<FVector> Dense;
	Dense.Add(PathPoints[0]);
	const float SubdivideStep = FMath::Max(25.f, CellSize * 0.75f);
	for (int32 i = 0; i + 1 < PathPoints.Num(); ++i)
	{
		const FVector SegStart = PathPoints[i];
		const FVector SegEnd = PathPoints[i + 1];
		const int32 NumSteps = FMath::Max(1, FMath::CeilToInt(FVector::Dist(SegStart, SegEnd) / SubdivideStep));
		for (int32 Step = 1; Step <= NumSteps; ++Step)
		{
			FVector Point = FMath::Lerp(SegStart, SegEnd, static_cast<float>(Step) / NumSteps);
			if (Step < NumSteps)
			{
				Point = ProjectToNav(Point); // изломы пути уже на навмеше
			}
			Dense.Add(Point);
		}
	}

	// Лента чуть выше заливки зоны, чтобы читалась поверх неё.
	const FVector MeshOffset = FVector(0, 0, SurfaceOffset + 4.f) - GetActorLocation();
	const float HalfWidth = PathWidth * 0.5f;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	Vertices.Reserve(Dense.Num() * 2);

	for (int32 i = 0; i < Dense.Num(); ++i)
	{
		// Направление в точке: усреднение соседних сегментов (митра на изломах).
		const FVector Prev = Dense[FMath::Max(i - 1, 0)];
		const FVector Next = Dense[FMath::Min(i + 1, Dense.Num() - 1)];
		FVector Dir = (Next - Prev);
		Dir.Z = 0.f;
		if (!Dir.Normalize())
		{
			Dir = FVector::ForwardVector;
		}
		const FVector Side(-Dir.Y * HalfWidth, Dir.X * HalfWidth, 0.f);

		Vertices.Add(Dense[i] - Side + MeshOffset);
		Vertices.Add(Dense[i] + Side + MeshOffset);
	}
	for (int32 i = 0; i + 1 < Dense.Num(); ++i)
	{
		const int32 B = i * 2;
		Triangles.Append({B, B + 1, B + 2, B + 1, B + 3, B + 2});
	}

	ZoneMesh->CreateMeshSection(2, Vertices, Triangles,
		TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);

	// Кружок-маркер точки назначения; обод проецируется на навмеш (склоны).
	TArray<FVector> MarkerVerts;
	TArray<int32> MarkerTris;
	const FVector Center = Dense.Last();
	const float MarkerRadius = PathWidth * 1.8f;
	const int32 NumSegments = 24;
	MarkerVerts.Add(Center + MeshOffset);
	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const float Angle = 2.f * PI * i / NumSegments;
		const FVector RimPoint = Center + FVector(FMath::Cos(Angle) * MarkerRadius, FMath::Sin(Angle) * MarkerRadius, 0.f);
		MarkerVerts.Add(ProjectToNav(RimPoint) + MeshOffset);
	}
	for (int32 i = 1; i <= NumSegments; ++i)
	{
		MarkerTris.Append({0, i, i + 1});
	}
	ZoneMesh->CreateMeshSection(3, MarkerVerts, MarkerTris,
		TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);

	if (Material)
	{
		ZoneMesh->SetMaterial(2, Material);
		ZoneMesh->SetMaterial(3, Material);
	}
	SetActorHiddenInGame(false);
	bPathPreviewVisible = true;
}
