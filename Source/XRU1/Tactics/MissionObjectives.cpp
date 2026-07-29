#include "MissionObjectives.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "TacticalQuestEvents.h"
#include "TacticsCombatStatics.h"
#include "TurnManagerSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

namespace
{
	bool IsRegisteredPlayerUnit(const AUnitBase* Unit)
	{
		const UWorld* World = Unit ? Unit->GetWorld() : nullptr;
		const UTurnManagerSubsystem* TurnManager = World
			? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
		if (!TurnManager || TurnManager->GetCurrentPhase() != ETurnPhase::Player)
		{
			return false;
		}

		for (const AActor* PlayerUnit : TurnManager->GetPlayerSideUnits())
		{
			if (PlayerUnit == Unit)
			{
				return true;
			}
		}
		return false;
	}
}

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
	if (bDisarmed || !Unit || !UTacticsCombatStatics::IsUnitAlive(Unit) ||
		!IsRegisteredPlayerUnit(Unit))
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

	if (!Unit->GetActionPoints()->TrySpendActionPoint())
	{
		return false;
	}
	++DefuseProgress;

	const bool bComplete = DefuseProgress >= RequiredActions;
	bDisarmed = bComplete;
	OnDefuseProgress.Broadcast(DefuseProgress, RequiredActions);
	OnDefuseStep(Unit, bComplete);
	UTacticalQuestEvents::BroadcastQuestEvent(this,
		bComplete
			? TacticalQuestTags::Event_Tactical_Objective_Defuse_Completed
			: TacticalQuestTags::Event_Tactical_Objective_Defuse_Progressed,
		Unit);

	if (bComplete)
	{
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
	if (!bActive || !Unit || !UTacticsCombatStatics::IsUnitAlive(Unit) ||
		!IsRegisteredPlayerUnit(Unit) || !IsUnitInside(Unit))
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

	if (!Unit->GetActionPoints()->TrySpendActionPoint())
	{
		return false;
	}
	Unit->Evacuate();
	if (!Unit->IsEvacuated())
	{
		return false;
	}
	UTacticalQuestEvents::BroadcastQuestEvent(
		this, TacticalQuestTags::Event_Tactical_Objective_Evac_Unit, Unit);
	OnUnitEvacuated.Broadcast(Unit);
	return true;
}
