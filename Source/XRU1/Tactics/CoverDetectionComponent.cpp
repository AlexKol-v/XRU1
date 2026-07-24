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

ECoverType UCoverDetectionComponent::EvaluateSurroundings()
{
	static const FVector Dirs[] = {
		FVector::ForwardVector, FVector::BackwardVector,
		FVector::RightVector,   FVector::LeftVector
	};

	ECoverType Best = ECoverType::None;
	for (const FVector& Dir : Dirs)
	{
		const ECoverType Found = TraceCoverInDirection(Dir);
		// Full > Half > None
		if (Found > Best)
		{
			Best = Found;
		}
	}

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

	// Укрытие эффективно, если стена находится СО СТОРОНЫ врага.
	const FVector ToThreat = (Threat->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D();
	if (ToThreat.IsNearlyZero())
	{
		return ECoverType::None;
	}
	return TraceCoverInDirection(ToThreat);
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
	const UCoverTuningDataAsset* Tuning = GetTuning();
	return TraceCoverAtLocation(Owner->GetWorld(), Base, ToThreat,
		Tuning->CoverTraceDistance, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
		Tuning->CoverTraceChannel, Owner);
}

ECoverType UCoverDetectionComponent::TraceCoverAtLocation(const UWorld* World, const FVector& Base,
	const FVector& Direction, float TraceDistance, float HalfHeight, float FullHeight,
	ECollisionChannel Channel, const AActor* Ignored)
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
