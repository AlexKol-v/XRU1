#include "CoverDetectionComponent.h"
#include "XRU1Log.h"
#include "CoverTuningDataAsset.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

/**
 * Диагностика укрытия против стрелка: при `xru1.Cover.Debug 1` каждый запрос
 * `GetCoverAgainst` пишет в лог, ЧТО именно засчиталось стеной (актор, дистанция,
 * длина луча) и рисует сам луч. Без этого класс багов «щит не тот» проверялся
 * только глазами по скриншоту: видно РЕЗУЛЬТАТ (синий щит), но не ПРИЧИНУ.
 * Ровно так же, как `xru1.AI.LogCombat` сделал наблюдаемым утилити-слой.
 */
static TAutoConsoleVariable<int32> CVarCoverDebug(
	TEXT("xru1.Cover.Debug"),
	0,
	TEXT("1 — логировать и рисовать луч укрытия цель→стрелок и найденную стену."),
	ECVF_Default);

namespace
{
	/**
	 * Допуск «точка лежит на плоскости стены» (см). Один на все проверки
	 * идентичности стен: и удержание active-стены, и гейт выстрела по своим
	 * плоскостям. Чуть шире стыков модульных секций (§2 передачи).
	 */
	constexpr float CoverPlaneTolerance = 16.f;

	/** Лежит ли точка на плоскости одной из перечисленных стен. */
	bool PointMatchesPlanes(const TArray<UCoverDetectionComponent::FCoverSidePlane>& Planes,
		const FVector& Point)
	{
		for (const UCoverDetectionComponent::FCoverSidePlane& Plane : Planes)
		{
			if (FMath::Abs(FVector::DotProduct(Plane.Normal, Point) - Plane.PlaneDistance)
				<= CoverPlaneTolerance)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Половина капсулы владельца (см), фолбэк — дефолт ACharacter (88). Нужна,
	 * чтобы из ActorLocation (центр капсулы) получить точку ПОЛА: высоты укрытия
	 * отсчитываются от пола (§II.3, Ф2).
	 */
	float OwnerCapsuleHalfHeight(const AActor* Owner)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Owner))
		{
			if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				return Capsule->GetScaledCapsuleHalfHeight();
			}
		}
		return 88.f;
	}

	/**
	 * Кандидат активной стены. `FCoverSide` остаётся старым публичным DTO, а
	 * runtime identity/плоскость нужны только latch-логике компонента.
	 */
	struct FCoverCandidate
	{
		const FCoverSide* Side = nullptr;
		FVector WallNormal = FVector::ZeroVector;
		float PlaneDistance = 0.f;
		int64 WallId = 0;
		TWeakObjectPtr<UPrimitiveComponent> HitComponent;
	};

	/**
	 * Runtime-id поверхности: identity компонента + квантованные normal/plane.
	 * Компонент отличает соседние modular meshes, а plane — две грани одного
	 * ящика. Квантование убирает микрошум hit result между evaluate.
	 */
	int64 MakeCoverWallId(const UObject* ObjectIdentity, const FVector& WallNormal,
		float PlaneDistance)
	{
		const uint32 ObjectHash = ObjectIdentity ? PointerHash(ObjectIdentity) : 0u;
		const int32 NormalX = FMath::RoundToInt(WallNormal.X * 50.f); // шаг 0.02
		const int32 NormalY = FMath::RoundToInt(WallNormal.Y * 50.f);
		const int32 PlaneBucket = FMath::RoundToInt(PlaneDistance / 5.f); // 5 см

		uint32 SurfaceHash = HashCombine(GetTypeHash(NormalX), GetTypeHash(NormalY));
		SurfaceHash = HashCombine(SurfaceHash, GetTypeHash(PlaneBucket));
		uint64 Packed = (static_cast<uint64>(ObjectHash) << 32) | SurfaceHash;
		Packed &= 0x7fffffffffffffffULL; // Blueprint-friendly положительный int64
		return static_cast<int64>(Packed != 0 ? Packed : 1ULL); // 0 зарезервирован для «нет стены»
	}

	FCoverCandidate MakeCoverCandidate(const FCoverSide& Side, const UWorld* World,
		const FVector& FloorBase, const UCoverTuningDataAsset* Tuning, const AActor* Owner)
	{
		FCoverCandidate Candidate;
		Candidate.Side = &Side;
		Candidate.WallNormal = (-Side.Direction).GetSafeNormal2D();

		// GatherCoverSides намеренно остаётся совместимым и не раздувает FCoverSide.
		// Один контрольный луч по уже найденной normal даёт component/plane для id.
		FHitResult Hit;
		const ECoverType TracedType = UCoverDetectionComponent::TraceCoverAtLocation(
			World, FloorBase, Side.Direction, Tuning->CoverTraceDistance,
			Tuning->HalfCoverHeight, Tuning->FullCoverHeight, Owner, 0.f, &Hit);
		const FVector HitNormal = Hit.ImpactNormal.GetSafeNormal2D();
		const bool bHitSameSurface = TracedType == Side.Type && Hit.IsValidBlockingHit()
			&& !HitNormal.IsNearlyZero()
			&& FVector::DotProduct(HitNormal, Candidate.WallNormal) > 0.94f;

		const FVector SurfacePoint = bHitSameSurface
			? Hit.ImpactPoint
			: FloorBase + Side.Direction * Side.Distance;
		Candidate.PlaneDistance = FVector::DotProduct(Candidate.WallNormal, SurfacePoint);

		const UObject* Identity = nullptr;
		if (bHitSameSurface)
		{
			Candidate.HitComponent = Hit.GetComponent();
			Identity = Candidate.HitComponent.IsValid()
				? static_cast<const UObject*>(Candidate.HitComponent.Get())
				: static_cast<const UObject*>(Hit.GetActor());
		}
		Candidate.WallId = MakeCoverWallId(Identity, Candidate.WallNormal, Candidate.PlaneDistance);
		return Candidate;
	}

	bool IsBetterCoverCandidate(const FCoverCandidate& Candidate, const FCoverCandidate* CurrentBest)
	{
		if (!CurrentBest)
		{
			return true;
		}
		if (Candidate.Side->Type != CurrentBest->Side->Type)
		{
			return Candidate.Side->Type > CurrentBest->Side->Type;
		}
		return Candidate.Side->Distance < CurrentBest->Side->Distance;
	}

	bool MatchesActiveWall(const FCoverCandidate& Candidate, int64 ActiveWallId,
		const UPrimitiveComponent* ActiveComponent, const FVector& ActiveNormal,
		float ActivePlaneDistance)
	{
		if (ActiveWallId != 0 && Candidate.WallId == ActiveWallId)
		{
			return true;
		}

		// Fallback для соседних треугольников/малого шума plane bucket: identity
		// та же, normal отличается не больше ~15°, плоскость — не больше 8 см.
		// Component обязателен: совпавшие normal/plane у соседней стены не должны
		// незаметно переключить active cover на другом углу.
		const bool bIdentityCompatible = ActiveComponent && Candidate.HitComponent.IsValid()
			&& Candidate.HitComponent.Get() == ActiveComponent;
		return bIdentityCompatible
			&& FVector::DotProduct(Candidate.WallNormal, ActiveNormal) > 0.9659f
			&& FMath::Abs(Candidate.PlaneDistance - ActivePlaneDistance) <= 8.f;
	}
}

UCoverDetectionComponent::UCoverDetectionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Дефолты из GE-классов (сами теги захардкожены в их конструкторах —
	// тот же паттерн, что у State.HunkeredDown/State.Taunting); при желании
	// переопределяются в BP другим GE-классом.
	HalfCoverEffect = UGE_CoverHalf::StaticClass();
	FullCoverEffect = UGE_CoverFull::StaticClass();
}

void UCoverDetectionComponent::BeginPlay()
{
	Super::BeginPlay();

	// Стартовая оценка: юнит мог заспавниться уже у стены.
	EvaluateSurroundings();
}

void UCoverDetectionComponent::GatherCoverSides(const UWorld* World, const FVector& Base,
	const UCoverTuningDataAsset* Tuning, const AActor* Ignored, TArray<FCoverSide>& OutSides)
{
	OutSides.Reset();
	if (!World || !Tuning)
	{
		return;
	}

	const int32 NumDirections = FMath::Max(4, Tuning->SurroundingDirections);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverSides), false, Ignored);

	// Один луч на направление, на высоте Half и на высоте Full. Нас интересует
	// НОРМАЛЬ стены: по ней стороны склеиваются, поэтому плоская стена рядом
	// даёт одну сторону, а не три пересекающихся луча.
	//
	// Геометрия — общая с LOS (object-query без юнитов). До этого трейс шёл по
	// каналу и ловил капсулы: союзник в 120 см становился «стеной», от него
	// брался BestCoverAround=Full, и юнит в чистом поле считался укрытым —
	// с жёлтым щитом вместо «нет щита» и с доступной глухой обороной.
	auto TraceAt = [&](const FVector& Dir, float Height, FHitResult& OutHit)
	{
		const FVector Start = Base + FVector(0.f, 0.f, Height);
		return World->LineTraceSingleByObjectType(OutHit, Start, Start + Dir * Tuning->CoverTraceDistance,
			UTacticsCombatStatics::GetShotGeometryObjects(), Params);
	};

	for (int32 Index = 0; Index < NumDirections; ++Index)
	{
		const float Angle = 2.f * PI * Index / NumDirections;
		const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);

		FHitResult Hit;
		ECoverType Type = ECoverType::None;
		if (TraceAt(Dir, Tuning->FullCoverHeight, Hit))
		{
			Type = ECoverType::Full;
		}
		else if (TraceAt(Dir, Tuning->HalfCoverHeight, Hit))
		{
			Type = ECoverType::Half;
		}
		if (Type == ECoverType::None)
		{
			continue;
		}

		// Направление стороны — ПРОТИВОПОЛОЖНО нормали стены (нормаль смотрит на
		// нас, сторона — от нас к стене). Если нормаль вырождена (угол/ребро),
		// падаем на само направление луча.
		FVector SideDir = -Hit.ImpactNormal;
		SideDir.Z = 0.f;
		if (!SideDir.Normalize())
		{
			SideDir = Dir;
		}

		// Склейка: та же стена, пойманная соседним лучом, — не новая сторона.
		bool bMerged = false;
		for (FCoverSide& Existing : OutSides)
		{
			if (FVector::DotProduct(Existing.Direction, SideDir) > 0.94f) // ~20°
			{
				if (Type > Existing.Type)
				{
					Existing.Type = Type;
				}
				Existing.Distance = FMath::Min(Existing.Distance, Hit.Distance);
				bMerged = true;
				break;
			}
		}
		if (!bMerged)
		{
			FCoverSide Side;
			Side.Direction = SideDir;
			Side.Type = Type;
			Side.Distance = Hit.Distance;
			OutSides.Add(Side);
		}
	}

	// Юнит прячется за БЛИЖНЕЙ стеной, а не за всем в радиусе трейса. Без этого
	// отсева на плотной застройке набиралось 3–4 стороны из разных ящиков, их
	// дуги перекрывали весь круг, и фланг становился невозможен в принципе.
	if (OutSides.Num() > 1 && Tuning->CoverSideDistanceSlack >= 0.f)
	{
		float MinDistance = TNumericLimits<float>::Max();
		for (const FCoverSide& Side : OutSides)
		{
			MinDistance = FMath::Min(MinDistance, Side.Distance);
		}
		const float Limit = MinDistance + Tuning->CoverSideDistanceSlack;
		OutSides.RemoveAll([Limit](const FCoverSide& Side) { return Side.Distance > Limit; });
	}
}

ECoverType UCoverDetectionComponent::EvaluateSurroundings()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return BestCoverAround;
	}

	// Собираем СТОРОНЫ (стены с нормалями), а не просто «лучший тип»: без
	// направления невозможно ни отличить фланг от укрытия, ни прижать бойца к
	// стене в анимации (дыра D2, фаза S1).
	const FVector FloorBase = Owner->GetActorLocation() - FVector(0.f, 0.f, OwnerCapsuleHalfHeight(Owner));
	const UCoverTuningDataAsset* Tuning = GetTuning();
	GatherCoverSides(Owner->GetWorld(), FloorBase, Tuning, Owner, CoverSides);

	TArray<FCoverCandidate, TInlineAllocator<4>> Candidates;
	Candidates.Reserve(CoverSides.Num());
	for (const FCoverSide& Side : CoverSides)
	{
		Candidates.Add(MakeCoverCandidate(Side, Owner->GetWorld(), FloorBase, Tuning, Owner));
	}

	// ЕДИНЫЙ ИСТОЧНИК ПРАВДЫ: плоскости всех сторон записываются здесь и только
	// здесь. Поза/иконка/GE берут тип из этого же evaluate, а выстрел
	// (GetCoverAgainst) даёт бонус лишь блокеру на одной из этих плоскостей.
	CoverSidePlanes.Reset();
	for (const FCoverCandidate& Candidate : Candidates)
	{
		CoverSidePlanes.Add({ Candidate.WallNormal, Candidate.PlaneDistance });
	}

	const FCoverCandidate* BestCandidate = nullptr;
	for (const FCoverCandidate& Candidate : Candidates)
	{
		// Старый приоритет сохранён: сначала тип (Full > Half), затем расстояние.
		if (IsBetterCoverCandidate(Candidate, BestCandidate))
		{
			BestCandidate = &Candidate;
		}
	}

	// Пока root остаётся у той же home anchor, текущая стена выигрывает у другой
	// стены РАВНОГО типа независимо от сантиметрового шума distance на углу.
	// Более высокий тип по-прежнему переключает стену немедленно.
	const bool bMovedOutsideAnchor = ActiveCoverWallId != 0
		&& ActiveCoverReselectDistance > 0.f
		&& FVector::DistSquared(Owner->GetActorLocation(), ActiveCoverAnchor)
			> FMath::Square(ActiveCoverReselectDistance);
	const bool bMayKeepActiveWall = ActiveCoverWallId != 0 && BestCandidate
		&& !bActiveCoverReselectionRequested && !bMovedOutsideAnchor;

	const FCoverCandidate* SelectedCandidate = BestCandidate;
	bool bKeptActiveGeometry = false;
	if (bMayKeepActiveWall)
	{
		for (const FCoverCandidate& Candidate : Candidates)
		{
			if (Candidate.Side->Type == BestCandidate->Side->Type
				&& MatchesActiveWall(Candidate, ActiveCoverWallId, ActiveCoverComponent.Get(),
					ActiveCoverNormal, ActiveCoverPlaneDistance))
			{
				SelectedCandidate = &Candidate;
				bKeptActiveGeometry = true;
				break;
			}
		}
	}
	bActiveCoverReselectionRequested = false;

	const ECoverType NewBestCover = SelectedCandidate
		? SelectedCandidate->Side->Type
		: ECoverType::None;
	const bool bCoverTypeChanged = NewBestCover != BestCoverAround;
	if (bCoverTypeChanged)
	{
		BestCoverAround = NewBestCover;
		ApplyCoverEffect(BestCoverAround);
	}

	if (!bKeptActiveGeometry)
	{
		if (SelectedCandidate)
		{
			SetActiveCoverGeometry(Owner->GetActorLocation(), SelectedCandidate->WallNormal,
				SelectedCandidate->WallId, SelectedCandidate->HitComponent.Get(),
				SelectedCandidate->PlaneDistance);
		}
		else
		{
			SetActiveCoverGeometry(FVector::ZeroVector, FVector::ZeroVector, 0, nullptr, 0.f);
		}
	}

	// КРАЙ И СТОРОНА ВЫГЛЯДЫВАНИЯ — здесь и только здесь (§6): активная геометрия
	// уже зафиксирована выше, значит пробы края гарантированно сверяются с той
	// стеной, которую мы только что выбрали. Пересчёт безусловный: даже при
	// удержанной стене позиция юнита могла сдвинуться (подшаг к стене).
	PeekEdgeDirection = FindPeekEdgeSide(PeekEdgeDistance);

	// Сторона стены в ПРОЕКТНОЙ стойке: боец стоит вдоль стены лицом к краю
	// (Forward = PeekEdgeDirection), право = Cross(Up, Forward). Знак говорит,
	// с какой стороны от бойца стена, — чистая геометрия, фактический поворот
	// актора не участвует. В правильной стойке совпадает с прежним
	// sign(CoverDirectionLocal.Y); в любой другой — определён, а не ноль.
	PeekSideSign = PeekEdgeDirection.IsNearlyZero()
		? 0.f
		: FMath::Sign(FVector::DotProduct(BestCoverDirection,
			FVector::CrossProduct(FVector::UpVector, PeekEdgeDirection)));

	// СВОДКА КАЖДОГО EVALUATE — полный след решения одной строкой: где стоим,
	// что нашли, удержали или перевыбрали стену, чем кончился поиск края.
	// Evaluate дёргается только в settle-точках (BeginPlay, финиш подшага,
	// финиш перемещения), так что спама нет, а последовательность строк — это
	// готовая трасса вызовов для разбора «кто и когда пересчитал укрытие».
	if (CVarCoverDebug.GetValueOnAnyThread() > 0)
	{
		const FVector Loc = Owner->GetActorLocation();
		UE_LOG(LogXRU1Combat, Log,
			TEXT("[Cover] %s: evaluate @(%.0f, %.0f) — cover=%d, сторон %d, стена %s (active=%s), край %s (%.0f см, сторона %+.0f)"),
			*GetNameSafe(Owner), Loc.X, Loc.Y,
			static_cast<int32>(BestCoverAround), CoverSides.Num(),
			SelectedCandidate ? (bKeptActiveGeometry ? TEXT("удержана") : TEXT("перевыбрана")) : TEXT("НЕТ"),
			*GetNameSafe(ActiveCoverComponent.IsValid() ? ActiveCoverComponent->GetOwner() : nullptr),
			HasPeekEdge() ? TEXT("есть") : TEXT("НЕТ"),
			PeekEdgeDistance, PeekSideSign);
	}

	// Оба события видят уже согласованные тип, GE и geometry. Старый delegate
	// сохраняет прежнюю семантику и не стреляет при Full→Full.
	if (bCoverTypeChanged)
	{
		OnCoverStateChanged.Broadcast(BestCoverAround);
	}
	return BestCoverAround;
}

void UCoverDetectionComponent::RequestActiveCoverReselection()
{
	bActiveCoverReselectionRequested = true;
}

bool UCoverDetectionComponent::MatchesActiveCoverHit(const FHitResult& Hit) const
{
	if (ActiveCoverWallId == 0 || ActiveCoverNormal.IsNearlyZero()
		|| !Hit.IsValidBlockingHit())
	{
		return false;
	}

	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	const FVector HitNormal = Hit.ImpactNormal.GetSafeNormal2D();
	// ⚠️ Валидность ActiveCoverComponent НЕ требуется. Он бывает пуст при живой
	// стене: контрольный луч в MakeCoverCandidate не подтвердил поверхность, и
	// кандидат зафиксирован без компонента. Требование IsValid() здесь проваливало
	// КАЖДУЮ пробу края → «край» на первом же шаге при непрерывной стене
	// (доказано логом: «стена есть, но НЕ active, active=None»). Идентичность
	// стены и так решает плоскость — см. блок ниже.
	if (!HitComponent || HitNormal.IsNearlyZero())
	{
		return false;
	}

	const float HitPlaneDistance = FVector::DotProduct(ActiveCoverNormal, Hit.ImpactPoint);
	const int64 HitWallId = MakeCoverWallId(HitComponent, HitNormal, HitPlaneDistance);
	if (HitWallId == ActiveCoverWallId)
	{
		return true;
	}

	// ⚠️ КОМПОНЕНТ НАМЕРЕННО НЕ СРАВНИВАЕТСЯ.
	//
	// Уровни собираются из модульных секций: одна визуально сплошная стена — это
	// десяток отдельных StaticMeshActor. Требование «тот же компонент» рвало её
	// на куски: шаг вдоль стены попадал в соседнюю секцию, совпадение не
	// проходило, и система объявляла КРАЙ укрытия через 15–45 см. Отсюда боец,
	// доворачивающийся «к краю» посреди стены, и полностью мёртвое выглядывание.
	//
	// Одна и та же стена — это одна ПЛОСКОСТЬ: совпали нормаль и её удаление от
	// начала координат. Допуск по плоскости чуть шире прежнего, чтобы пережить
	// стыки модульных секций и небольшие зазоры между ними.
	return FVector::DotProduct(HitNormal, ActiveCoverNormal) > 0.9659f
		&& FMath::Abs(HitPlaneDistance - ActiveCoverPlaneDistance) <= CoverPlaneTolerance;
}

void UCoverDetectionComponent::SetActiveCoverGeometry(const FVector& NewAnchor,
	const FVector& NewNormal, int64 NewWallId, UPrimitiveComponent* NewComponent,
	float NewPlaneDistance)
{
	const FVector StableNormal = NewWallId != 0
		? NewNormal.GetSafeNormal2D()
		: FVector::ZeroVector;
	const FVector NewDirection = -StableNormal;
	const bool bGeometryChanged = ActiveCoverWallId != NewWallId
		|| !ActiveCoverAnchor.Equals(NewAnchor, 0.5f)
		|| !ActiveCoverNormal.Equals(StableNormal, 0.001f);

	ActiveCoverAnchor = NewAnchor;
	ActiveCoverNormal = StableNormal;
	ActiveCoverWallId = NewWallId;
	BestCoverDirection = NewDirection;
	ActiveCoverComponent = NewComponent;
	ActiveCoverPlaneDistance = NewWallId != 0 ? NewPlaneDistance : 0.f;

	if (bGeometryChanged)
	{
		ActiveCoverRevision = ActiveCoverRevision == MAX_int32
			? 1
			: ActiveCoverRevision + 1;
		OnActiveCoverChanged.Broadcast(ActiveCoverRevision);
	}
}

FVector UCoverDetectionComponent::FindPeekEdgeSide(float& OutEdgeDistance) const
{
	OutEdgeDistance = 0.f;

	const AActor* Owner = GetOwner();
	const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World || BestCoverAround == ECoverType::None)
	{
		return FVector::ZeroVector; // прятаться не за чем — выглядывать неоткуда
	}

	FVector ToWall = BestCoverDirection;
	ToWall.Z = 0.f;
	if (!ToWall.Normalize())
	{
		return FVector::ZeroVector;
	}

	// Ось ВДОЛЬ стены. Знак роли не играет: обе стороны проверяются одинаково,
	// а результат — мировой вектор, который вызывающий переводит в свои оси.
	FVector Side = FVector::CrossProduct(ToWall, FVector::UpVector);
	if (!Side.Normalize())
	{
		return FVector::ZeroVector;
	}

	const UCoverTuningDataAsset* Tuning = GetTuning();
	const FVector FloorBase = Owner->GetActorLocation() - FVector(0.f, 0.f, OwnerCapsuleHalfHeight(Owner));
	const float Step = FMath::Max(1.f, Tuning->PeekEdgeStep);

	// Шагаем вдоль стены в обе стороны, пока трейс В СТЕНУ её находит. Первый
	// шаг, на котором стены уже нет, — и есть край. Побеждает БЛИЖНИЙ край: у
	// пиллара, где стена кончается с обеих сторон, выглядывать логично в ту,
	// до которой ближе. При точной ничьей выигрывает +Side — детерминированно,
	// иначе сторона дёргалась бы между пересчётами.
	FVector BestSide = FVector::ZeroVector;
	float BestEdgeDistance = TNumericLimits<float>::Max();
	// В кого упёрлась ПОСЛЕДНЯЯ совпавшая проба на каждой стороне. Нужно ветке
	// «край не найден»: по актору видно, продолжается ли та же стена или пробы
	// склеили ЧУЖУЮ коллинеарную стену по плоскости (§2) — снаружи эти случаи
	// неотличимы, а лечатся противоположно.
	const AActor* FarthestSameWall[2] = { nullptr, nullptr };
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const float SideSign = SideIndex == 0 ? 1.f : -1.f;
		for (float Offset = Step; Offset <= Tuning->PeekEdgeMaxDistance; Offset += Step)
		{
			const FVector Probe = FloorBase + Side * (SideSign * Offset);
			FHitResult ProbeHit;
			const ECoverType ProbeCover = TraceCoverAtLocation(
				World, Probe, ToWall, Tuning->CoverTraceDistance,
				Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
				Owner, 0.f, &ProbeHit);
			if (ProbeCover != ECoverType::None && MatchesActiveCoverHit(ProbeHit))
			{
				FarthestSameWall[SideIndex] = ProbeHit.GetActor();
				continue; // ещё вдоль той же active wall — шагаем дальше к краю
			}

			// Почему шаг прервался: реального края нет / стена та же, но не
			// опознана как active. Второе — ложный край, из-за него боец
			// доворачивается «в никуда» и не выглядывает.
			if (CVarCoverDebug.GetValueOnAnyThread() > 0)
			{
				UE_LOG(LogXRU1Combat, Log,
					TEXT("[Cover] %s: край на %.0f см — стена %s, попадание в %s, active=%s"),
					*GetNameSafe(Owner), Offset,
					ProbeCover == ECoverType::None ? TEXT("КОНЧИЛАСЬ") : TEXT("есть, но НЕ active"),
					*GetNameSafe(ProbeHit.GetComponent() ? ProbeHit.GetComponent()->GetOwner() : nullptr),
					*GetNameSafe(ActiveCoverComponent.IsValid() ? ActiveCoverComponent->GetOwner() : nullptr));
			}

			if (Offset < BestEdgeDistance)
			{
				BestEdgeDistance = Offset;
				BestSide = Side * SideSign;
			}
			break; // край на этой стороне найден, дальше только открытое место
		}
	}

	if (BestSide.IsNearlyZero())
	{
		// Раньше эта ветка молчала, и «юнит у угла, а края нет» было не отличить
		// от честной глухой стены. Акторы последних совпавших проб дают ответ:
		// тот же актор — стена реально длинная; другой — угол «зашит» склейкой
		// коллинеарной соседки по плоскости.
		if (CVarCoverDebug.GetValueOnAnyThread() > 0)
		{
			UE_LOG(LogXRU1Combat, Log,
				TEXT("[Cover] %s: край НЕ найден — плоскость непрерывна ≥%.0f см: +сторона до %s, −сторона до %s (active=%s)"),
				*GetNameSafe(Owner), Tuning->PeekEdgeMaxDistance,
				*GetNameSafe(FarthestSameWall[0]), *GetNameSafe(FarthestSameWall[1]),
				*GetNameSafe(ActiveCoverComponent.IsValid() ? ActiveCoverComponent->GetOwner() : nullptr));
		}
		return FVector::ZeroVector; // глухая стена шире PeekEdgeMaxDistance
	}
	OutEdgeDistance = BestEdgeDistance;
	return BestSide;
}

ECoverType UCoverDetectionComponent::GetCoverAgainst(const AActor* Threat) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Threat)
	{
		return ECoverType::None;
	}

	// ЛОГИКА УКРЫТИЯ = ФИЗИКА ВЫСТРЕЛА, а не геометрия поз.
	//
	// Единственный честный вопрос: «останавливает ли стена ЭТОТ выстрел». Значит
	// луч надо пускать от цели в сторону ТОЙ ТОЧКИ, откуда пуля реально прилетит,
	// на высоте, которую укрытие обязано прикрывать.
	//
	// Почему не угол/дуга (предыдущая редакция). Дуга — это аппроксимация «с
	// какой стороны меня прикрывает стена», и у неё нет верного значения: 90°
	// засчитывает перпендикулярную стену (боец стоит СБОКУ от ящика, а игра
	// говорит «прикрыт»), 70° ломается на другом угле. В XCOM это работает
	// только потому, что юнит там ПРИТЯНУТ к тайлу укрытия и вжат в конкретную
	// стену. Мы юнитов не притягиваем и не собираемся — значит и опираться на
	// позу нельзя.
	//
	// ⚠️ Стрелок стреляет ИЗ ВЫГЛЯДЫВАНИЯ (Ф4/Ф5), а не из своего центра. Именно
	// поэтому берём его огневую позицию: боец, выглянувший из-за угла, обходит
	// укрытие цели — в XCOM это ровно «peek flanking», и цель обязана стать
	// флангированной. Считать от центра стрелка значило бы врать игроку: он
	// видит выстрел из-за угла, а щит остаётся синим.
	const UCoverTuningDataAsset* Tuning = GetTuning();
	const FVector FloorBase = Owner->GetActorLocation() - FVector(0.f, 0.f, OwnerCapsuleHalfHeight(Owner));

	// ⚠️ УКРЫТИЕ = МИНИМУМ ПО ВСЕМ ОГНЕВЫМ ПОЗИЦИЯМ СТРЕЛКА (правка 2026-07-25).
	//
	// Стрелок сам выбирает, откуда стрелять, и выберет позицию, которая обходит
	// укрытие цели. Раньше здесь бралась ПЕРВАЯ позиция с линией огня
	// (`GetFiringStance`, порядок центр → step-out → края) — и получалась
	// неизбежная жалоба «стою у угла, точка выглядывания заведомо во фланге, а
	// щит синий»: центр давал линию огня поверх низкой стены, перебор на нём
	// останавливался, и фланг считался от центра.
	//
	// Теперь перебираются ВСЕ позиции, откуда есть линия огня, и берётся
	// НАИМЕНЬШЕЕ укрытие. Правило читается одной фразой и совпадает с тем, что
	// видит игрок: «если из какой-то своей огневой точки я обхожу твою стену —
	// ты флангирован». Оно же совпадает с новой механикой выстрела: юнит
	// физически выбегает в точку пика и стреляет ИМЕННО ОТТУДА.
	TArray<FVector, TInlineAllocator<4>> FiringPositions;
	UTacticsCombatStatics::GetViableFiringPositions(Threat, Owner, FiringPositions);
	if (FiringPositions.Num() == 0)
	{
		// Стрелять неоткуда — вопрос об укрытии не имеет смысла; берём центр,
		// чтобы HUD не мигал «нет укрытия» при временной потере линии.
		FVector Fallback = Threat->GetActorLocation();
		Fallback.Z += Tuning->EyeHeightOffset;
		FiringPositions.Add(Fallback);
	}

	// ЕДИНЫЙ ИСТОЧНИК ПРАВДЫ: если EvaluateSurroundings не нашёл юниту стен —
	// у него нет ни позы укрытия, ни иконки, ни GE, и выстрел ОБЯЗАН считать его
	// открытым. Раньше физика ниже решала вопрос заново и находила «укрытие» в
	// случайной геометрии (склон рампы, перепад пола) — юнит в полный рост без
	// иконки получал бонус защиты.
	if (BestCoverAround == ECoverType::None || CoverSidePlanes.Num() == 0)
	{
		return ECoverType::None;
	}

	ECoverType Result = ECoverType::Full; // худшее для стрелка; ищем минимум
	FHitResult CoverHit;
	FVector BestEye = FiringPositions[0];
	for (const FVector& FiringEye : FiringPositions)
	{
		const FVector ToShooter = (FiringEye - FloorBase).GetSafeNormal2D();
		if (ToShooter.IsNearlyZero())
		{
			continue;
		}

		// Стена засчитывается, только если она МЕЖДУ целью и стрелком: длина
		// луча обрезается дистанцией до огневой позиции.
		const float TraceLength = GetCoverTraceLength(Tuning, FloorBase, FiringEye);
		FHitResult Hit;
		// Толстый свип: волосяной луч на скользящем угле проскакивал вдоль грани
		// и терял укрытие. Толщина — та же, что у луча линии огня.
		ECoverType FromHere = TraceLength > 0.f
			? TraceCoverAtLocation(Owner->GetWorld(), FloorBase, ToShooter,
				TraceLength, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
				Owner, Tuning->LosSphereRadius, &Hit)
			: ECoverType::None;

		// Гейт единого источника: блокер обязан лежать на плоскости СВОЕЙ стены.
		// Чужая геометрия между целью и стрелком — вопрос линии огня, а не
		// бонуса защиты: защищает только укрытие, у которого юнит СТОИТ.
		if (FromHere != ECoverType::None
			&& !PointMatchesPlanes(CoverSidePlanes, Hit.ImpactPoint))
		{
			if (CVarCoverDebug.GetValueOnAnyThread() > 0)
			{
				UE_LOG(LogXRU1Combat, Log,
					TEXT("[Cover] %s vs %s: блокер %s НЕ моя стена (плоскость не совпала) — фланг"),
					*GetNameSafe(Owner), *GetNameSafe(Threat),
					*GetNameSafe(Hit.GetActor()));
			}
			FromHere = ECoverType::None;
		}

		if (FromHere < Result)
		{
			Result = FromHere;
			CoverHit = Hit;
			BestEye = FiringEye;
		}
		if (Result == ECoverType::None)
		{
			break; // лучше уже не будет — стрелок нашёл, откуда обойти
		}
	}
#if ENABLE_DRAW_DEBUG
	if (CVarCoverDebug.GetValueOnAnyThread() > 0)
	{
		// Рисуем и логируем ПОБЕДИВШУЮ огневую позицию — ту, по которой принято
		// решение. Число позиций тоже важно: `pos=1` означает, что стрелять
		// можно только из центра, и никакого пика на самом деле нет.
		static const TCHAR* CoverNames[] = { TEXT("None"), TEXT("Half"), TEXT("Full") };
		const float BestTraceLength = GetCoverTraceLength(Tuning, FloorBase, BestEye);
		UE_LOG(LogXRU1Combat, Log,
			TEXT("[Cover] %s vs %s: pos=%d distToShot=%.0f traceLen=%.0f cover=%s blocker=%s"),
			*GetNameSafe(Owner), *GetNameSafe(Threat), FiringPositions.Num(),
			FVector::Dist2D(FloorBase, BestEye), BestTraceLength,
			CoverNames[static_cast<uint8>(Result)],
			Result != ECoverType::None ? *GetNameSafe(CoverHit.GetActor()) : TEXT("-"));

		UWorld* DbgWorld = Owner->GetWorld();
		const float DbgHeight = (Result == ECoverType::Full) ? Tuning->FullCoverHeight : Tuning->HalfCoverHeight;
		const FVector RayStart = FloorBase + FVector(0.f, 0.f, DbgHeight);
		const FVector BestDir = (BestEye - FloorBase).GetSafeNormal2D();
		DrawDebugLine(DbgWorld, RayStart, RayStart + BestDir * FMath::Max(BestTraceLength, 0.f),
			Result != ECoverType::None ? FColor::Green : FColor::Red, false, 0.35f, 0, 3.f);
		if (Result != ECoverType::None && CoverHit.GetActor())
		{
			DrawDebugSphere(DbgWorld, CoverHit.ImpactPoint, 16.f, 8, FColor::Green, false, 0.35f);
		}
	}
#endif

	return Result;
}

float UCoverDetectionComponent::GetCoverTraceLength(const UCoverTuningDataAsset* Tuning,
	const FVector& Base, const FVector& ThreatPoint)
{
	if (!Tuning)
	{
		return 0.f;
	}
	// Толщина луча вычитается, чтобы сфера не «лизнула» стену, у которой стоит
	// сам стрелок: та стена прикрывает ЕГО, а не цель.
	const float ToThreat = FVector::Dist2D(Base, ThreatPoint) - Tuning->LosSphereRadius;
	return FMath::Min(Tuning->CoverTraceDistance, ToThreat);
}

float UCoverDetectionComponent::GetDefenseBonusAgainst(const AActor* Threat) const
{
	const UCoverTuningDataAsset* Tuning = GetTuning();
	switch (GetCoverAgainst(Threat))
	{
	case ECoverType::Half: return Tuning->HalfCoverDefenseBonus;
	case ECoverType::Full: return Tuning->FullCoverDefenseBonus;
	default:               return 0.f;
	}
}

const UCoverTuningDataAsset* UCoverDetectionComponent::GetTuning() const
{
	// Пер-юнит → глобальный → CDO. GetCoverTuning сам подстрахует пустой мир.
	if (TuningOverride)
	{
		return TuningOverride;
	}
	return UTacticsCombatStatics::GetCoverTuning(GetWorld());
}

ECoverType UCoverDetectionComponent::TraceCoverInDirection(const FVector& Direction) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return ECoverType::None;
	}
	// Base — точка ПОЛА (ActorLocation − половина капсулы). Высоты Half/Full
	// отсчитываются от пола, как задумано (§II.3): раньше Base был центром
	// капсулы, и низкое укрытие (ящик 60 см) не детектилось вообще.
	const UCoverTuningDataAsset* Tuning = GetTuning();
	const FVector FloorBase = Owner->GetActorLocation() - FVector(0.f, 0.f, OwnerCapsuleHalfHeight(Owner));
	return TraceCoverAtLocation(Owner->GetWorld(), FloorBase, Direction,
		Tuning->CoverTraceDistance, Tuning->HalfCoverHeight, Tuning->FullCoverHeight, Owner);
}

ECoverType UCoverDetectionComponent::EvaluateCoverAtLocation(const FVector& Base, const FVector& ThreatLocation) const
{
	const AActor* Owner = GetOwner();
	const FVector ToThreat = (ThreatLocation - Base).GetSafeNormal2D();
	if (!Owner || ToThreat.IsNearlyZero())
	{
		return ECoverType::None;
	}
	// ТА ЖЕ физика, что у стоящего юнита (GetCoverAgainst): толстый луч на
	// высотах half/full. План и факт обязаны считаться одинаково, иначе AI
	// бежит в «укрытие», которого по прибытии не окажется.
	//
	// ⚠️ Осознанное упрощение: здесь луч идёт к ЦЕНТРУ угрозы, а не к её
	// огневой позиции. Планирование перебирает десятки точек × несколько угроз,
	// и гонять полный расчёт выглядывания на каждую пару слишком дорого.
	// Погрешность ограничена выносом peek (≈1 м) и заметна только вплотную.
	const UCoverTuningDataAsset* Tuning = GetTuning();

	// Тот же кламп «стена должна быть МЕЖДУ», что и в GetCoverAgainst: план и
	// факт обязаны считаться одинаково, иначе AI выберет точку, которая по
	// прибытии окажется без укрытия (инвариант «план == факт»).
	const float TraceLength = GetCoverTraceLength(Tuning, Base, ThreatLocation);
	if (TraceLength <= 0.f)
	{
		return ECoverType::None;
	}

	// ЕДИНЫЙ ИСТОЧНИК ПРАВДЫ и для ПЛАНА: в точке собираются те же стороны, что
	// соберёт EvaluateSurroundings по прибытии, и бонус даёт только их плоскость.
	// Без этого план обещал бы «укрытие» от склона рампы, которого по прибытии
	// не окажется ни в позе, ни в иконке, ни в защите. Цена — лучи
	// GatherCoverSides на точку; планирование это уже переживает у стоящих
	// юнитов, а здесь точек на порядок меньше, чем у поля хода.
	TArray<FCoverSide> SidesAtPoint;
	GatherCoverSides(Owner->GetWorld(), Base, Tuning, Owner, SidesAtPoint);
	if (SidesAtPoint.Num() == 0)
	{
		return ECoverType::None; // стен рядом нет — и стоя здесь юнит был бы открыт
	}
	TArray<FCoverSidePlane> PlanesAtPoint;
	PlanesAtPoint.Reserve(SidesAtPoint.Num());
	for (const FCoverSide& Side : SidesAtPoint)
	{
		const FCoverCandidate Candidate = MakeCoverCandidate(Side, Owner->GetWorld(), Base, Tuning, Owner);
		PlanesAtPoint.Add({ Candidate.WallNormal, Candidate.PlaneDistance });
	}

	FHitResult PlanHit;
	const ECoverType PlanResult = TraceCoverAtLocation(Owner->GetWorld(), Base, ToThreat,
		TraceLength, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
		Owner, Tuning->LosSphereRadius, &PlanHit);
	if (PlanResult != ECoverType::None && !PointMatchesPlanes(PlanesAtPoint, PlanHit.ImpactPoint))
	{
		return ECoverType::None; // блокер не из сторон этой точки — там юнит был бы открыт
	}
	return PlanResult;
}

ECoverType UCoverDetectionComponent::TraceCoverAtLocation(const UWorld* World, const FVector& Base,
	const FVector& Direction, float TraceDistance, float HalfHeight, float FullHeight,
	const AActor* Ignored, float SphereRadius, FHitResult* OutHit)
{
	if (!World || TraceDistance <= 0.f)
	{
		return ECoverType::None;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverTrace), false, Ignored);
	const FCollisionObjectQueryParams& ObjectParams = UTacticsCombatStatics::GetShotGeometryObjects();
	const FVector Dir = Direction.GetSafeNormal2D();

	auto WallAt = [&](float HeightOffset) -> bool
	{
		const FVector Start = Base + FVector(0.f, 0.f, HeightOffset);
		const FVector End = Start + Dir * TraceDistance;
		FHitResult LocalHit;
		FHitResult& Hit = OutHit ? *OutHit : LocalHit;
		if (SphereRadius > 0.f)
		{
			// Толстый свип: волосяной луч на скользящем угле проскакивал вдоль
			// грани стены и «терял» укрытие. Толщина — та же, что у луча LOS.
			return World->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, ObjectParams,
				FCollisionShape::MakeSphere(SphereRadius), Params);
		}
		return World->LineTraceSingleByObjectType(Hit, Start, End, ObjectParams, Params);
	};

	// Есть стена на высоте полного укрытия -> Full; иначе если есть на высоте half -> Half.
	if (WallAt(FullHeight))
	{
		return ECoverType::Full;
	}
	if (WallAt(HalfHeight))
	{
		return ECoverType::Half;
	}
	return ECoverType::None;
}

void UCoverDetectionComponent::ApplyCoverEffect(ECoverType CoverType)
{
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!ASC)
	{
		return;
	}

	// Снимаем предыдущий GE укрытия (если был).
	if (ActiveCoverEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(ActiveCoverEffectHandle);
		ActiveCoverEffectHandle.Invalidate();
	}

	TSubclassOf<UGameplayEffect> EffectClass;
	switch (CoverType)
	{
	case ECoverType::Half: EffectClass = HalfCoverEffect; break;
	case ECoverType::Full: EffectClass = FullCoverEffect; break;
	default: break; // None — юнит открыт, эффекта нет.
	}

	if (EffectClass)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(this);
		ActiveCoverEffectHandle = ASC->ApplyGameplayEffectToSelf(
			EffectClass->GetDefaultObject<UGameplayEffect>(), 1.f, Context);
	}
}
