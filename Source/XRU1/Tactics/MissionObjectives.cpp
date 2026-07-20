#include "MissionObjectives.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "TacticsCombatStatics.h"
#include "Components/StaticMeshComponent.h"

// --- ABombObjective -----------------------------------------------------------

ABombObjective::ABombObjective()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
}

bool ABombObjective::CanDefuse(const AUnitBase* Unit) const
{
	if (bDisarmed || !Unit || !UTacticsCombatStatics::IsUnitAlive(Unit))
	{
		return false;
	}
	const UActionPointsComponent* ActionPoints = Unit->GetActionPoints();
	if (!ActionPoints || !ActionPoints->CanSpend(1))
	{
		return false;
	}
	// Dist2D (не Dist): интеракция «встал рядом», не должна зависеть от разницы
	// по высоте между пивотом меша заряда (стол/консоль) и полом юнита —
	// тот же принцип, что у AEvacZone::IsUnitInside.
	return FVector::Dist2D(Unit->GetActorLocation(), GetActorLocation()) <= InteractRadius;
}

bool ABombObjective::TryDefuse(AUnitBase* Unit)
{
	if (!CanDefuse(Unit))
	{
		return false;
	}

	Unit->GetActionPoints()->TrySpendActionPoint();
	++DefuseProgress;

	const bool bComplete = DefuseProgress >= RequiredActions;
	OnDefuseProgress.Broadcast(DefuseProgress, RequiredActions);
	OnDefuseStep(Unit, bComplete);

	if (bComplete)
	{
		bDisarmed = true;
		OnDisarmed.Broadcast();
	}
	return true;
}

// --- AEvacZone ------------------------------------------------------------------

AEvacZone::AEvacZone()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AEvacZone::BeginPlay()
{
	Super::BeginPlay();
	if (bActiveFromStart)
	{
		ActivateZone();
	}
}

void AEvacZone::ActivateZone()
{
	if (bActive)
	{
		return;
	}
	bActive = true;
	OnZoneActivated();
}

bool AEvacZone::IsUnitInside(const AUnitBase* Unit) const
{
	return Unit && FVector::Dist2D(Unit->GetActorLocation(), GetActorLocation()) <= ZoneRadius;
}

bool AEvacZone::CanEvacuate(const AUnitBase* Unit) const
{
	if (!bActive || !Unit || !UTacticsCombatStatics::IsUnitAlive(Unit) || !IsUnitInside(Unit))
	{
		return false;
	}
	const UActionPointsComponent* ActionPoints = Unit->GetActionPoints();
	return ActionPoints && ActionPoints->CanSpend(1);
}

bool AEvacZone::TryEvacuate(AUnitBase* Unit)
{
	if (!CanEvacuate(Unit))
	{
		return false;
	}

	Unit->GetActionPoints()->TrySpendActionPoint();
	Unit->Evacuate();
	OnUnitEvacuated.Broadcast(Unit);
	return true;
}
