// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "QuestTypes.h"
#include "QuestLogWidget.generated.h"

class UQuestEntryWidget;
class UQuestSubsystem;

/**
 * Журнал квестов — активируемый виджет CommonUI. Строит список строк-квестов,
 * сортирует их и отдаёт разметке. Подписан на делегаты подсистемы и
 * перестраивается при изменениях. Команды строк (взять/отслеживать/отказаться)
 * транслируются в UQuestSubsystem.
 */
UCLASS()
class STQUESTSYSTEM_API UQuestLogWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()

public:
    /** Класс виджета-строки для одного квеста. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    TSubclassOf<UQuestEntryWidget> EntryWidgetClass;

    /** Задаёт критерий сортировки и перестраивает журнал. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void SetSortMode(EQuestSortMode Mode);

    /** Команда строки: взять доступный квест. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void RequestStartQuest(FGameplayTag QuestId);

    /** Команда строки: отслеживать квест. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void RequestTrackQuest(FGameplayTag QuestId);

    /** Команда строки: отказаться от квеста. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void RequestAbandonQuest(FGameplayTag QuestId);

protected:
    virtual void NativeOnActivated() override;
    virtual void NativeOnDeactivated() override;

    /** Реализуется в разметке: размещает строки журнала. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Quest")
    void OnEntriesRebuilt(const TArray<UQuestEntryWidget*>& Entries);

private:
    /** Возвращает подсистему квестов или nullptr. */
    UQuestSubsystem* GetQuestSubsystem() const;

    /** Пересобирает строки журнала с учётом сортировки. */
    void RebuildEntries();

    UFUNCTION()
    void HandleQuestStateChanged(FGameplayTag QuestId, EQuestState NewState);

    UFUNCTION()
    void HandleObjectiveProgress(FGameplayTag QuestId);

    /** Текущие виджеты-строки журнала. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UQuestEntryWidget>> EntryWidgets;

    /** Текущий критерий сортировки. */
    EQuestSortMode SortMode = EQuestSortMode::ByState;
};
