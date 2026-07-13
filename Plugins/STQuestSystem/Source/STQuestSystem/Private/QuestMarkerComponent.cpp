// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestMarkerComponent.h"

#include "GameFramework/Actor.h"

TArray<TWeakObjectPtr<UQuestMarkerComponent>> UQuestMarkerComponent::AllMarkers;

UQuestMarkerComponent::UQuestMarkerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UQuestMarkerComponent::BeginPlay()
{
    Super::BeginPlay();
    AllMarkers.Add(this);
}

void UQuestMarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    AllMarkers.Remove(this);
    Super::EndPlay(EndPlayReason);
}

FVector UQuestMarkerComponent::GetMarkerLocation() const
{
    const AActor* Owner = GetOwner();
    return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}
