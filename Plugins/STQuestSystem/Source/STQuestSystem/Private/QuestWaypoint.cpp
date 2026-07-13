// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestWaypoint.h"

#include "Components/SphereComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "QuestGameplayTags.h"
#include "QuestTypes.h"

TArray<TWeakObjectPtr<AQuestWaypoint>> AQuestWaypoint::AllWaypoints;

AQuestWaypoint::AQuestWaypoint()
{
    PrimaryActorTick.bCanEverTick = false;

    TriggerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerSphere"));
    TriggerSphere->InitSphereRadius(200.f);
    TriggerSphere->SetCollisionProfileName(TEXT("Trigger"));
    SetRootComponent(TriggerSphere);
}

void AQuestWaypoint::BeginPlay()
{
    Super::BeginPlay();

    AllWaypoints.Add(this);
    TriggerSphere->OnComponentBeginOverlap.AddDynamic(this, &AQuestWaypoint::OnTriggerBeginOverlap);
}

void AQuestWaypoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    AllWaypoints.Remove(this);
    Super::EndPlay(EndPlayReason);
}

void AQuestWaypoint::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!WaypointTag.IsValid())
    {
        return;
    }

    // Реагируем только на пешку, управляемую игроком.
    const APawn* Pawn = Cast<APawn>(OtherActor);
    if (!Pawn || !Pawn->IsPlayerControlled())
    {
        return;
    }

    if (!UGameplayMessageSubsystem::HasInstance(this))
    {
        return;
    }

    // Броадкастим достижение точки: канал и уточняющий тег события — WaypointTag.
    FQuestEventData EventData;
    EventData.EventTag = WaypointTag;
    EventData.Source = this;
    UGameplayMessageSubsystem::Get(this).BroadcastMessage(WaypointTag, EventData);
}
