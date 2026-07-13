// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "QuestTask_DynamicObjectiveGroup.generated.h"

/** Данные экземпляра служебной задачи динамических целей. */
USTRUCT()
struct FQuestTask_DynamicObjectiveGroupInstanceData
{
    GENERATED_BODY()

    /** Сколько динамических целей достаточно выполнить (0 — нужны все). */
    UPROPERTY(EditAnywhere, Category = "Quest", meta = (ClampMin = "0"))
    int32 RequiredObjectives = 0;
};

/**
 * Служебная задача-группа State Tree: наблюдает динамические цели инстанса
 * квеста (UQuestInstance::DynamicObjectives), добавленные в рантайме через
 * UQuestSubsystem. Завершается успехом, когда выполнено не менее
 * RequiredObjectives целей (0 — нужны все динамические цели).
 */
USTRUCT(meta = (DisplayName = "Quest Dynamic Objective Group", Category = "Quest"))
struct STQUESTSYSTEM_API FQuestTask_DynamicObjectiveGroup : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    FQuestTask_DynamicObjectiveGroup();

    using FInstanceDataType = FQuestTask_DynamicObjectiveGroupInstanceData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
