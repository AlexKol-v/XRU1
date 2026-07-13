// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"
#include "STDialogueTypes.h"
#include "DialogueTask_QuestAction.generated.h"

/** Данные экземпляра задачи-действия над квестом. */
USTRUCT()
struct FDialogueTask_QuestActionInstanceData
{
    GENERATED_BODY()

    /** Идентификатор квеста, над которым выполняется действие. */
    UPROPERTY(EditAnywhere, Category = "Dialogue")
    FGameplayTag QuestId;

    /** Что сделать с квестом. */
    UPROPERTY(EditAnywhere, Category = "Dialogue")
    EDialogueQuestAction Action = EDialogueQuestAction::Start;
};

/**
 * Задача State Tree диалога: выполняет действие над квестом через реальный API
 * UQuestSubsystem (сделать доступным / запустить / завершить / провалить).
 * Завершается мгновенно в EnterState — без ожидания событий.
 */
USTRUCT(meta = (DisplayName = "Dialogue Quest Action", Category = "Dialogue"))
struct STQUESTSYSTEM_API FDialogueTask_QuestAction : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FDialogueTask_QuestActionInstanceData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
