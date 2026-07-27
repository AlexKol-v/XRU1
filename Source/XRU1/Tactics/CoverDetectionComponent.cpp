#include "CoverDetectionComponent.h"
#include "CoverTuningDataAsset.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
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
	GatherCoverSides(Owner->GetWorld(), FloorBase, GetTuning(), Owner, CoverSides);

	ECoverType Best = ECoverType::None;
	FVector BestDirection = FVector::ZeroVector;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FCoverSide& Side : CoverSides)
	{
		// Лучшая сторона: сначала по типу (Full > Half), при равенстве — ближняя.
		if (Side.Type > Best || (Side.Type == Best && Side.Distance < BestDistance))
		{
			Best = Side.Type;
			BestDirection = Side.Direction;
			BestDistance = Side.Distance;
		}
	}

	BestCoverDirection = BestDirection;
	if (Best != BestCoverAround)
	{
		BestCoverAround = Best;
		ApplyCoverEffect(BestCoverAround);
		OnCoverStateChanged.Broadcast(BestCoverAround);
	}
	return BestCoverAround;
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
	for (const float SideSign : {1.f, -1.f})
	{
		for (float Offset = Step; Offset <= Tuning->PeekEdgeMaxDistance; Offset += Step)
		{
			const FVector Probe = FloorBase + Side * (SideSign * Offset);
			if (TraceCoverAtLocation(World, Probe, ToWall, Tuning->CoverTraceDistance,
				Tuning->HalfCoverHeight, Tuning->FullCoverHeight, Owner) != ECoverType::None)
			{
				continue; // ещё за укрытием — шагаем дальше к краю
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
		const ECoverType FromHere = TraceLength > 0.f
			? TraceCoverAtLocation(Owner->GetWorld(), FloorBase, ToShooter,
				TraceLength, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
				Owner, Tuning->LosSphereRadius, &Hit)
			: ECoverType::None;

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
		UE_LOG(LogTemp, Log,
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
	return TraceCoverAtLocation(Owner->GetWorld(), Base, ToThreat,
		TraceLength, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
		Owner, Tuning->LosSphereRadius);
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
