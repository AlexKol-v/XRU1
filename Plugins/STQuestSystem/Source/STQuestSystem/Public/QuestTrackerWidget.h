// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "QuestTypes.h"
#include "QuestTrackerWidget.generated.h"

class UQuestSubsystem;

/**
 * HUD-трекер отслеживаемого квеста. Подписан на смену отслеживаемого квеста
 * и прогресс его целей; передаёт сводку данных разметке для отрисовки.
 */
UCLASS()
class STQUESTSYSTEM_API UQuestTrackerWidget : public UCommonUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeDestruct() override;

    /** Реализуется в разметке: обновляет трекер по данным отслеживаемого квеста. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Quest")
    void OnTrackedQuestRefreshed(const FQuestDisplayData& DisplayData);

private:
    /** Возвращает подсистему квестов или nullptr. */
    UQuestSubsystem* GetQuestSubsystem() const;

    /** Перечитывает данные отслеживаемого квеста и обновляет разметку. */
    void Refresh();

    UFUNCTION()
    void HandleTrackedQuestChanged(FGameplayTag QuestId);

    UFUNCTION()
    void HandleObjectiveProgress(FGameplayTag QuestId);
};
