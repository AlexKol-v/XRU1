#include "TacticalQuestZone.h"

#include "Components/BoxComponent.h"
#include "TacticalQuestEvents.h"
#include "TacticsCombatStatics.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"

ATacticalQuestZone::ATacticalQuestZone()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->InitBoxExtent(FVector(200.f, 200.f, 100.f));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	SetRootComponent(TriggerBox);
}

void ATacticalQuestZone::BeginPlay()
{
	Super::BeginPlay();
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ATacticalQuestZone::HandleBeginOverlap);
}

void ATacticalQuestZone::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	AUnitBase* Unit = Cast<AUnitBase>(OtherActor);
	const TWeakObjectPtr<AActor> WeakUnit(Unit);
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!Unit || !TurnManager || !UTacticsCombatStatics::IsUnitAlive(Unit) ||
		!TurnManager->GetPlayerSideUnits().Contains(Unit) ||
		(bOneShotPerUnit && TriggeredUnits.Contains(WeakUnit)))
	{
		return;
	}

	if (!UTacticalQuestEvents::BroadcastQuestEvent(this, EventChannel, Unit, 1))
	{
		return;
	}

	TriggeredUnits.Add(WeakUnit);
	OnTacticalUnitEntered(Unit);

	if (bDisableAfterFirstUnit)
	{
		TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
