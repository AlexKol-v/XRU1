#include "ActionPointsComponent.h"

UActionPointsComponent::UActionPointsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UActionPointsComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentActionPoints = MaxActionPoints;
	BroadcastChanged();
}

bool UActionPointsComponent::CanSpend(int32 Cost) const
{
	return Cost > 0 && CurrentActionPoints >= Cost;
}

bool UActionPointsComponent::TrySpendActionPoint(int32 Cost)
{
	if (!CanSpend(Cost))
	{
		return false;
	}

	CurrentActionPoints -= Cost;
	BroadcastChanged();
	return true;
}

void UActionPointsComponent::ResetForNewTurn()
{
	CurrentActionPoints = MaxActionPoints;
	BroadcastChanged();
}

void UActionPointsComponent::SpendAllRemaining()
{
	if (CurrentActionPoints > 0)
	{
		CurrentActionPoints = 0;
		BroadcastChanged();
	}
}

void UActionPointsComponent::GrantExtraPoints(int32 Amount)
{
	if (Amount > 0)
	{
		// Сознательно допускаем превышение MaxActionPoints (Run & Gun даёт очко сверх нормы).
		CurrentActionPoints += Amount;
		BroadcastChanged();
	}
}

void UActionPointsComponent::BroadcastChanged()
{
	OnActionPointsChanged.Broadcast(CurrentActionPoints, MaxActionPoints);
}
