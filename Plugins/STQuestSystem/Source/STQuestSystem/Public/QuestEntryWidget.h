// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "QuestTypes.h"
#include "QuestEntryWidget.generated.h"

class UQuestLogWidget;

/**
 * Виджет-строка одного квеста в журнале. Принимает идентификатор квеста,
 * запрашивает у подсистемы сводку FQuestDisplayData и отдаёт её разметке
 * через событие OnQuestDataRefreshed. Хранит обратную ссылку на журнал-
 * владелец (OwningLog), чтобы кнопки строки адресно звали Request* методы
 * лога без поиска родителя через дерево виджетов.
 */
UCLASS()
class STQUESTSYSTEM_API UQuestEntryWidget : public UCommonUserWidget
{
    GENERATED_BODY()

public:
    /** Привязывает строку к квесту и обновляет отображение. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void SetQuest(FGameplayTag InQuestId);

    /** Идентификатор отображаемого квеста. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    FGameplayTag GetQuestId() const { return QuestId; }

    /** Устанавливает журнал-владелец строки (зовётся из UQuestLogWidget). */
    void SetOwningLog(UQuestLogWidget* InOwningLog) { OwningLog = InOwningLog; }

    /** Возвращает журнал-владелец строки или nullptr. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    UQuestLogWidget* GetOwningLog() const { return OwningLog; }

protected:
    /** Реализуется в разметке: обновляет визуал строки по данным квеста. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Quest")
    void OnQuestDataRefreshed(const FQuestDisplayData& DisplayData);

private:
    /** Идентификатор привязанного квеста. */
    FGameplayTag QuestId;

    /** Журнал-владелец строки; проставляется UQuestLogWidget::RebuildEntries. */
    UPROPERTY(Transient)
    TObjectPtr<UQuestLogWidget> OwningLog;
};
