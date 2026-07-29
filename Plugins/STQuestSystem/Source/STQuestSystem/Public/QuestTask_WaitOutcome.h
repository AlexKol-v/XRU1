// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"
#include "QuestTask_WaitOutcome.generated.h"

/** Данные задачи, которая завершает весь StateTree корректным result status. */
USTRUCT()
struct FQuestTask_WaitOutcomeInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Quest")
    FGameplayTag SuccessChannel;

    UPROPERTY(EditAnywhere, Category = "Quest")
    FGameplayTag FailureChannel;

    /** Exact leaf по умолчанию: terminal-исход не должен ловить соседний тег. */
    UPROPERTY(EditAnywhere, Category = "Quest")
    bool bRequireExactChannel = true;
};

/**
 * Ждёт один из двух terminal events и возвращает соответствующий StateTree
 * status. Обычная Objective для failure непригодна: она всегда возвращает
 * Succeeded и тем самым ошибочно завершает проигранный quest как Completed.
 */
USTRUCT(meta = (DisplayName = "Quest Wait Outcome", Category = "Quest"))
struct STQUESTSYSTEM_API FQuestTask_WaitOutcome : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    FQuestTask_WaitOutcome();

    using FInstanceDataType = FQuestTask_WaitOutcomeInstanceData;
    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }

    virtual EStateTreeRunStatus EnterState(
        FStateTreeExecutionContext& Context,
        const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(
        FStateTreeExecutionContext& Context,
        float DeltaTime) const override;
};
