#include "MissionObjectives.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "TacticalQuestEvents.h"
#include "TacticsAudioSubsystem.h"
#include "TacticsCombatStatics.h"
#include "TurnManagerSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"

namespace MissionObjectives_Internal
{
	/** Микшер живёт в GameInstance: у целей миссии своего аудио-состояния нет. */
	static UTacticsAudioSubsystem* GetAudio(const AActor* Actor)
	{
		const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UTacticsAudioSubsystem>() : nullptr;
	}
}

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
	// Звук — после списания AP и роста прогресса: отклонённое нажатие F не
	// должно звучать как работа сапёра.
	if (UTacticsAudioSubsystem* Audio = MissionObjectives_Internal::GetAudio(this))
	{
		Audio->PlayBombDefuse(GetActorLocation(), bComplete);
	}
	UTacticalQuestEvents::BroadcastQuestEventEx(this,
		bComplete
			? TacticalQuestTags::Event_Tactical_Objective_Defuse_Completed
			: TacticalQuestTags::Event_Tactical_Objective_Defuse_Progressed,
		Unit, this);

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

	// Постоянная рамка зоны: игрок видит ОБЛАСТЬ эвакуации (как в XCOM), а не
	// только дым-маркер. Материал задаёт BP/инстанс; без него рамка скрыта.
	FrameDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("FrameDecal"));
	FrameDecal->SetupAttachment(Root);
	FrameDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	FrameDecal->SetFadeScreenSize(0.f);
	FrameDecal->SetVisibility(false);
}

void AEvacZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!FrameDecal)
	{
		return;
	}

	UMaterialInterface* Material = FrameMaterial.LoadSynchronous();
	FrameDecal->SetDecalMaterial(Material);
	FrameDecal->SetVisibility(Material != nullptr);

	// Декаль проецируется вниз; X — глубина, Y/Z — половины прямоугольника
	// (ровно как рамка ATacticalQuestZone). Круговой legacy-режим рисует
	// квадрат по радиусу.
	const bool bBox = ZoneExtent.X > 0.f && ZoneExtent.Y > 0.f;
	const FVector2D Extent = bBox ? ZoneExtent : FVector2D(ZoneRadius, ZoneRadius);
	FrameDecal->DecalSize = FVector(300.f, Extent.Y, Extent.X);
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
	if (UTacticsAudioSubsystem* Audio = MissionObjectives_Internal::GetAudio(this))
	{
		Audio->PlayEvacZoneActivated(GetActorLocation());
	}
}

bool AEvacZone::IsUnitInside(const AUnitBase* Unit) const
{
	if (!Unit)
	{
		return false;
	}
	// Прямоугольный режим (v2.5): вход считается по локальному боксу — ровно
	// той области, которую показывает рамка. Круговой ZoneRadius — legacy.
	if (ZoneExtent.X > 0.f && ZoneExtent.Y > 0.f)
	{
		const FVector Local =
			GetActorTransform().InverseTransformPosition(Unit->GetActorLocation());
		return FMath::Abs(Local.X) <= ZoneExtent.X && FMath::Abs(Local.Y) <= ZoneExtent.Y;
	}
	return FVector::Dist2D(Unit->GetActorLocation(), GetActorLocation()) <= ZoneRadius;
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
	UTacticalQuestEvents::BroadcastQuestEventEx(
		this, TacticalQuestTags::Event_Tactical_Objective_Evac_Unit, Unit, this);
	if (UTacticsAudioSubsystem* Audio = MissionObjectives_Internal::GetAudio(this))
	{
		Audio->PlayEvacUnit(GetActorLocation());
	}
	OnUnitEvacuated.Broadcast(Unit);
	return true;
}

int32 AEvacZone::TryEvacuateAllInside()
{
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager =
		World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return 0;
	}

	// Снимок списка до эвакуаций: Evacuate меняет состояние юнитов по ходу.
	const TArray<AActor*> SideUnits = TurnManager->GetPlayerSideUnits();
	int32 Evacuated = 0;
	for (AActor* Actor : SideUnits)
	{
		if (AUnitBase* Unit = Cast<AUnitBase>(Actor))
		{
			if (TryEvacuate(Unit))
			{
				++Evacuated;
			}
		}
	}
	return Evacuated;
}
