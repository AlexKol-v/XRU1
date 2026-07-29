#include "FogOfWarSubsystem.h"

#include "TacticsCombatStatics.h"
#include "TurnManagerSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

bool UFogOfWarSubsystem::IsActorCurrentlyVisible(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return false;
	}

	for (const AActor* Member : TurnManager->GetPlayerSideUnits())
	{
		if (Member && UTacticsCombatStatics::IsUnitAlive(Member) &&
			FVector::Dist(Member->GetActorLocation(), Actor->GetActorLocation())
				<= UTacticsCombatStatics::SquadVisionRange &&
			UTacticsCombatStatics::HasLineOfSight(Member, Actor))
		{
			return true;
		}
	}

	return false;
}

TArray<AActor*> UFogOfWarSubsystem::GetCurrentlyVisibleEnemies() const
{
	TArray<AActor*> Result;
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return Result;
	}

	for (AActor* Enemy : TurnManager->GetEnemySideUnits())
	{
		if (Enemy && UTacticsCombatStatics::IsUnitAlive(Enemy) && IsActorCurrentlyVisible(Enemy))
		{
			Result.Add(Enemy);
		}
	}
	return Result;
}

int32 UFogOfWarSubsystem::GetCurrentlyVisibleEnemyCount() const
{
	return GetCurrentlyVisibleEnemies().Num();
}
