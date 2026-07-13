// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "QuestMarkerComponent.generated.h"

/**
 * Помечает актор-владелец как цель квеста (предмет, NPC, объект). Цель квеста
 * с совпадающим ObjectiveId подсвечивает этот актор маркером — через
 * трекинг-компонент игрока.
 */
UCLASS(ClassGroup = "Quest", meta = (BlueprintSpawnableComponent))
class STQUESTSYSTEM_API UQuestMarkerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UQuestMarkerComponent();

    /** Тег маркера — сопоставляется с ObjectiveId активной цели квеста. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    FGameplayTag MarkerTag;

    /** Возвращает мировую позицию маркера. */
    FVector GetMarkerLocation() const;

    /** Возвращает все зарегистрированные маркеры в мире. */
    static const TArray<TWeakObjectPtr<UQuestMarkerComponent>>& GetAllMarkers() { return AllMarkers; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** Глобальный реестр маркеров — для трекинг-компонента игрока. */
    static TArray<TWeakObjectPtr<UQuestMarkerComponent>> AllMarkers;
};
