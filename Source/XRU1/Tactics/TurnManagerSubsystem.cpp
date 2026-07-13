#include "TurnManagerSubsystem.h"
#include "ActionPointsComponent.h"
#include "GameFramework/Actor.h"

void UTurnManagerSubsystem::StartCombat(const TArray<AActor*>& PlayerUnits, const TArray<AActor*>& EnemyUnits)
{
	PlayerSide.Reset();
	for (AActor* A : PlayerUnits)
	{
		if (A) { PlayerSide.Add(A); }
	}

	EnemySide.Reset();
	for (AActor* A : EnemyUnits)
	{
		if (A) { EnemySide.Add(A); }
	}

	bInCombat = true;
	TurnNumber = 1;
	BeginPhase(ETurnPhase::Player);
}

void UTurnManagerSubsystem::EndTurn()
{
	if (!bInCombat)
	{
		return;
	}

	switch (CurrentPhase)
	{
	case ETurnPhase::Player:
		BeginPhase(ETurnPhase::Enemy);
		break;
	case ETurnPhase::Enemy:
		++TurnNumber;
		BeginPhase(ETurnPhase::Player);
		break;
	default:
		break;
	}
}

void UTurnManagerSubsystem::EndCombat(bool bPlayerWon)
{
	if (!bInCombat)
	{
		return;
	}

	bInCombat = false;
	CurrentPhase = ETurnPhase::None;
	OnCombatEnded.Broadcast(bPlayerWon);
}

void UTurnManagerSubsystem::BeginPhase(ETurnPhase Phase)
{
	CurrentPhase = Phase;
	ResetActionPointsForSide(Phase == ETurnPhase::Player ? PlayerSide : EnemySide);
	OnTurnStarted.Broadcast(Phase);
}

void UTurnManagerSubsystem::ResetActionPointsForSide(const TArray<TObjectPtr<AActor>>& Units)
{
	for (const TObjectPtr<AActor>& Unit : Units)
	{
		if (!Unit) { continue; }
		if (UActionPointsComponent* AP = Unit->FindComponentByClass<UActionPointsComponent>())
		{
			AP->ResetForNewTurn();
		}
	}
}

const TArray<AActor*>& UTurnManagerSubsystem::GetActiveSideUnits() const
{
	ActiveSideCache.Reset();
	const TArray<TObjectPtr<AActor>>& Side = (CurrentPhase == ETurnPhase::Enemy) ? EnemySide : PlayerSide;
	for (const TObjectPtr<AActor>& A : Side)
	{
		ActiveSideCache.Add(A.Get());
	}
	return ActiveSideCache;
}
