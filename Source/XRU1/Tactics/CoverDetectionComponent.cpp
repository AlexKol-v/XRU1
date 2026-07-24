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
	auto TraceAt = [&](const FVector& Dir, float Height, FHitResult& OutHit)
	{
		const FVector Start = Base + FVector(0.f, 0.f, Height);
		return World->LineTraceSingleByChannel(OutHit, Start, Start + Dir * Tuning->CoverTraceDistance,
			Tuning->CoverTraceChannel, Params);
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

	FVector FiringEye = Threat->GetActorLocation();
	UTacticsCombatStatics::GetFiringStance(Threat, Owner, FiringEye);

	const FVector ToShooter = (FiringEye - FloorBase).GetSafeNormal2D();
	if (ToShooter.IsNearlyZero())
	{
		return ECoverType::None;
	}

	// Толстый свип: волосяной луч на скользящем угле проскакивал вдоль грани и
	// терял укрытие. Толщина — та же, что у луча линии огня.
	return TraceCoverAtLocation(Owner->GetWorld(), FloorBase, ToShooter,
		Tuning->CoverTraceDistance, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
		Tuning->CoverTraceChannel, Owner, Tuning->LosSphereRadius);
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
		Tuning->CoverTraceDistance, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
		Tuning->CoverTraceChannel, Owner);
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
	return TraceCoverAtLocation(Owner->GetWorld(), Base, ToThreat,
		Tuning->CoverTraceDistance, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
		Tuning->CoverTraceChannel, Owner, Tuning->LosSphereRadius);
}

ECoverType UCoverDetectionComponent::TraceCoverAtLocation(const UWorld* World, const FVector& Base,
	const FVector& Direction, float TraceDistance, float HalfHeight, float FullHeight,
	ECollisionChannel Channel, const AActor* Ignored, float SphereRadius)
{
	if (!World)
	{
		return ECoverType::None;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverTrace), false, Ignored);
	const FVector Dir = Direction.GetSafeNormal2D();

	auto WallAt = [&](float HeightOffset) -> bool
	{
		const FVector Start = Base + FVector(0.f, 0.f, HeightOffset);
		const FVector End = Start + Dir * TraceDistance;
		FHitResult Hit;
		if (SphereRadius > 0.f)
		{
			// Толстый свип: волосяной луч на скользящем угле проскакивал вдоль
			// грани стены и «терял» укрытие. Толщина — та же, что у луча LOS.
			return World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Channel,
				FCollisionShape::MakeSphere(SphereRadius), Params);
		}
		return World->LineTraceSingleByChannel(Hit, Start, End, Channel, Params);
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
