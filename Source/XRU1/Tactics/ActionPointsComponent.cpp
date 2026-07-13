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

void UActionPointsComponent::BroadcastChanged()
{
	OnActionPointsChanged.Broadcast(CurrentActionPoints, MaxActionPoints);
}
