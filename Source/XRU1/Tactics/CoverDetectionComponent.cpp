#include "CoverDetectionComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

UCoverDetectionComponent::UCoverDetectionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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

ECoverType UCoverDetectionComponent::TraceCoverInDirection(const FVector& Direction) const
{
	const AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World)
	{
		return ECoverType::None;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverTrace), false, Owner);
	const FVector Base = Owner->GetActorLocation();
	const FVector Dir = Direction.GetSafeNormal2D();

	auto WallAt = [&](float HeightOffset) -> bool
	{
		const FVector Start = Base + FVector(0.f, 0.f, HeightOffset);
		const FVector End = Start + Dir * CoverTraceDistance;
		FHitResult Hit;
		return World->LineTraceSingleByChannel(Hit, Start, End, CoverTraceChannel, Params);
	};

	// Есть стена на высоте полного укрытия -> Full; иначе если есть на высоте half -> Half.
	if (WallAt(FullCoverHeight))
	{
		return ECoverType::Full;
	}
	if (WallAt(HalfCoverHeight))
	{
		return ECoverType::Half;
	}
	return ECoverType::None;
}
