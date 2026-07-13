// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"
#include "QuestTask_ObjectiveGroup.generated.h"

/** Спецификация одной цели внутри группы. */
USTRUCT()
struct FQuestObjectiveSpec
{
    GENERATED_BODY()

    /** Идентификатор цели — для снимка прогресса и привязки точек на карте. */
    UPROPERTY(EditAnywhere, Category = "Quest")
    FGameplayTag ObjectiveId;

    /** Канал квест-событий, засчитываемых в эту цель. */
    UPROPERTY(EditAnywhere, Category = "Quest")
    FGameplayTag EventChannel;

    /** Требуемое число событий для выполнения этой цели. */
    UPROPERTY(EditAnywhere, Category = "Quest", meta = (ClampMin = "1"))
    int32 RequiredCount = 1;

    /** Описание цели. */
    UPROPERTY(EditAnywhere, Category = "Quest")
    FText Description;
};

/** Данные экземпляра задачи-группы целей. */
USTRUCT()
struct FQuestTask_ObjectiveGroupInstanceData
{
    GENERATED_BODY()

    /** Список целей группы. */
    UPROPERTY(EditAnywhere, Category = "Quest")
    TArray<FQuestObjectiveSpec> Objectives;

    /** Сколько целей из списка достаточно выполнить (0 — нужны все). */
    UPROPERTY(EditAnywhere, Category = "Quest", meta = (ClampMin = "0"))
    int32 RequiredObjectives = 0;

    /** Текущие счётчики по каждой цели; выравнивается по Objectives. */
    UPROPERTY()
    TArray<int32> Counts;
};

/**
 * Задача-группа целей State Tree: реализует правило завершения «N из M».
 * Считает события по каждой цели; завершается успехом, когда выполнено
 * не менее RequiredObjectives целей (0 — нужны все).
 */
USTRUCT(meta = (DisplayName = "Quest Objective Group", Category = "Quest"))
struct STQUESTSYSTEM_API FQuestTask_ObjectiveGroup : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    FQuestTask_ObjectiveGroup();

    using FInstanceDataType = FQuestTask_ObjectiveGroupInstanceData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
