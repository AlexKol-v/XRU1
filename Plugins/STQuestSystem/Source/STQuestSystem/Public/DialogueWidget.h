// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "GameplayTagContainer.h"
#include "STDialogueTypes.h"
#include "DialogueWidget.generated.h"

class UDialogueSubsystem;

/**
 * Виджет диалога — активируемый виджет CommonUI. Подписан на делегаты
 * UDialogueSubsystem: показывает реплики и варианты выбора (разметка реализует
 * DisplayLine/DisplayChoices), а выбор игрока транслирует обратно в подсистему.
 */
UCLASS()
class STQUESTSYSTEM_API UDialogueWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()

public:
    /** Передаёт выбор игрока в активный диалог. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void SelectChoice(int32 ChoiceIndex);

    /** Продолжает реплику без вариантов (эквивалент выбора с индексом 0). */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void ContinueDialogue();

protected:
    virtual void NativeOnActivated() override;
    virtual void NativeOnDeactivated() override;

    /** Реализуется в разметке: показать реплику NPC. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
    void DisplayLine(const FText& Line, FGameplayTag SpeakerId);

    /** Реализуется в разметке: показать варианты выбора. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue")
    void DisplayChoices(const TArray<FDialogueChoice>& Choices);

private:
    /** Подсистема диалогов текущего GameInstance или nullptr. */
    UDialogueSubsystem* GetDialogueSubsystem() const;

    UFUNCTION()
    void HandleLine(const FText& Line, FGameplayTag SpeakerId);

    UFUNCTION()
    void HandleChoices(const TArray<FDialogueChoice>& Choices);

    UFUNCTION()
    void HandleDialogueEnded();
};
