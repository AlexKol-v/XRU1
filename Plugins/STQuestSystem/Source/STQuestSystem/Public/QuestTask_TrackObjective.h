// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"
#include "QuestTask_TrackObjective.generated.h"

/** Данные экземпляра задачи-цели «счётчик событий». */
USTRUCT()
struct FQuestTask_TrackObjectiveInstanceData
{
    GENERATED_BODY()

    /** Идентификатор цели — для снимка прогресса и привязки точек на карте. */
    UPROPERTY(EditAnywhere, Category = "Quest")
    FGameplayTag ObjectiveId;

    /** Канал квест-событий, засчитываемых в эту цель (например, Quest.Event.Kill). */
    UPROPERTY(EditAnywhere, Category = "Quest")
    FGameplayTag EventChannel;

    /** Требуемое число событий для завершения цели. */
    UPROPERTY(EditAnywhere, Category = "Quest", meta = (ClampMin = "1"))
    int32 RequiredCount = 1;

    /** Описание цели для журнала и трекера (используется UI в Лекции 2). */
    UPROPERTY(EditAnywhere, Category = "Quest")
    FText Description;

    /** Текущее число засчитанных событий. */
    UPROPERTY()
    int32 CurrentCount = 0;
};

/**
 * Задача-цель State Tree: считает квест-события на канале EventChannel,
 * пока счётчик не достигнет RequiredCount, затем завершается успехом.
 * Универсальна — тип цели (убийство, сбор, достижение точки, пользовательская)
 * задаётся выбором канала события.
 */
USTRUCT(meta = (DisplayName = "Quest Objective", Category = "Quest"))
struct STQUESTSYSTEM_API FQuestTask_TrackObjective : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    FQuestTask_TrackObjective();

    using FInstanceDataType = FQuestTask_TrackObjectiveInstanceData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
