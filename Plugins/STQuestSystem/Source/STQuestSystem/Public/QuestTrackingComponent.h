// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "QuestTrackingComponent.generated.h"

/**
 * Компонент пешки игрока: собирает мировые позиции маркеров активных целей
 * всех активных квестов. Используется HUD для отрисовки маркеров и компаса.
 */
UCLASS(ClassGroup = "Quest", meta = (BlueprintSpawnableComponent))
class STQUESTSYSTEM_API UQuestTrackingComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UQuestTrackingComponent();

    /** Возвращает мировые позиции маркеров активных целей активных квестов. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    TArray<FVector> GetActiveMarkerLocations() const;
};
