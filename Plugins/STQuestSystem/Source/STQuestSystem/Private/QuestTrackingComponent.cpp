// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestTrackingComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "QuestDefinition.h"
#include "QuestInstance.h"
#include "QuestMarkerComponent.h"
#include "QuestSubsystem.h"
#include "QuestTypes.h"
#include "QuestWaypoint.h"

UQuestTrackingComponent::UQuestTrackingComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

TArray<FVector> UQuestTrackingComponent::GetActiveMarkerLocations() const
{
    TArray<FVector> Result;

    const UWorld* World = GetWorld();
    const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    const UQuestSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
    if (!Subsystem)
    {
        return Result;
    }

    // Собираем идентификаторы активных целей всех активных квестов.
    TSet<FGameplayTag> ActiveObjectiveIds;
    for (const UQuestDefinition* Definition : Subsystem->GetQuestsInState(EQuestState::Active))
    {
        const UQuestInstance* Instance = Definition ? Subsystem->GetQuestInstance(Definition->QuestId) : nullptr;
        if (!Instance)
        {
            continue;
        }
        for (const FObjectiveProgress& Objective : Instance->Progress.Objectives)
        {
            if (Objective.State == EObjectiveState::Active && Objective.ObjectiveId.IsValid())
            {
                ActiveObjectiveIds.Add(Objective.ObjectiveId);
            }
        }
    }

    // Точки на карте с совпадающим тегом.
    for (const TWeakObjectPtr<AQuestWaypoint>& Waypoint : AQuestWaypoint::GetAllWaypoints())
    {
        if (Waypoint.IsValid() && ActiveObjectiveIds.Contains(Waypoint->WaypointTag))
        {
            Result.Add(Waypoint->GetActorLocation());
        }
    }

    // Маркеры на акторах с совпадающим тегом.
    for (const TWeakObjectPtr<UQuestMarkerComponent>& Marker : UQuestMarkerComponent::GetAllMarkers())
    {
        if (Marker.IsValid() && ActiveObjectiveIds.Contains(Marker->MarkerTag))
        {
            Result.Add(Marker->GetMarkerLocation());
        }
    }

    return Result;
}
