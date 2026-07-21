#include "CoverDetectionComponent.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

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
	switch (GetCoverAgainst(Threat))
	{
	case ECoverType::Half: return HalfCoverDefenseBonus;
	case ECoverType::Full: return FullCoverDefenseBonus;
	default:               return 0.f;
	}
}

ECoverType UCoverDetectionComponent::TraceCoverInDirection(const FVector& Direction) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return ECoverType::None;
	}
	// Общее ядро: юнит на месте — Base и есть его ActorLocation.
	return TraceCoverAtLocation(Owner->GetWorld(), Owner->GetActorLocation(), Direction,
		CoverTraceDistance, HalfCoverHeight, FullCoverHeight, CoverTraceChannel, Owner);
}

ECoverType UCoverDetectionComponent::EvaluateCoverAtLocation(const FVector& Base, const FVector& ThreatLocation) const
{
	const AActor* Owner = GetOwner();
	const FVector ToThreat = (ThreatLocation - Base).GetSafeNormal2D();
	if (!Owner || ToThreat.IsNearlyZero())
	{
		return ECoverType::None;
	}
	return TraceCoverAtLocation(Owner->GetWorld(), Base, ToThreat,
		CoverTraceDistance, HalfCoverHeight, FullCoverHeight, CoverTraceChannel, Owner);
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
