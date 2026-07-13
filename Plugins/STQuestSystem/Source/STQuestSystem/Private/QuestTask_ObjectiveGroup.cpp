// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestTask_ObjectiveGroup.h"

#include "QuestRunnerActor.h"
#include "QuestTypes.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"

namespace QuestTask_ObjectiveGroup_Internal
{
    /** Сообщает раннеру снимок прогресса всех целей группы. */
    void ReportProgress(FStateTreeExecutionContext& Context, const FQuestTask_ObjectiveGroupInstanceData& Inst)
    {
        AQuestRunnerActor* Runner = AQuestRunnerActor::GetFromContext(Context);
        if (!Runner)
        {
            return;
        }

        for (int32 Index = 0; Index < Inst.Objectives.Num(); ++Index)
        {
            const FQuestObjectiveSpec& Spec = Inst.Objectives[Index];
            const int32 Current = Inst.Counts.IsValidIndex(Index) ? Inst.Counts[Index] : 0;

            FObjectiveProgress Progress;
            Progress.ObjectiveId = Spec.ObjectiveId;
            Progress.Current = Current;
            Progress.Required = Spec.RequiredCount;
            Progress.Description = Spec.Description;
            Progress.State = (Current >= Spec.RequiredCount)
                ? EObjectiveState::Completed
                : EObjectiveState::Active;
            Runner->ReportObjectiveProgress(Progress);
        }
    }
} // namespace QuestTask_ObjectiveGroup_Internal

FQuestTask_ObjectiveGroup::FQuestTask_ObjectiveGroup()
{
    bShouldCallTickOnlyOnEvents = true;
}

EStateTreeRunStatus FQuestTask_ObjectiveGroup::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& Inst = Context.GetInstanceData(*this);
    Inst.Counts.Reset();
    Inst.Counts.SetNumZeroed(Inst.Objectives.Num());

    // При восстановлении подхватываем стартовые счётчики целей из снимка.
    if (AQuestRunnerActor* Runner = AQuestRunnerActor::GetFromContext(Context))
    {
        for (int32 Index = 0; Index < Inst.Objectives.Num(); ++Index)
        {
            Inst.Counts[Index] = Runner->GetRestoredObjectiveCount(Inst.Objectives[Index].ObjectiveId);
        }
    }

    QuestTask_ObjectiveGroup_Internal::ReportProgress(Context, Inst);

    // Группа уже выполнена восстановленными счётчиками — завершаемся сразу
    // (задача событийная, без событий Tick не вызовется).
    int32 CompletedCount = 0;
    for (int32 Index = 0; Index < Inst.Objectives.Num(); ++Index)
    {
        if (Inst.Counts[Index] >= Inst.Objectives[Index].RequiredCount)
        {
            ++CompletedCount;
        }
    }
    const int32 Required = Inst.RequiredObjectives > 0
        ? FMath::Min(Inst.RequiredObjectives, Inst.Objectives.Num())
        : Inst.Objectives.Num();

    return (Inst.Objectives.Num() > 0 && CompletedCount >= Required)
        ? EStateTreeRunStatus::Succeeded
        : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FQuestTask_ObjectiveGroup::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& Inst = Context.GetInstanceData(*this);

    // Выравниваем массив счётчиков по числу целей (на случай правок данных).
    if (Inst.Counts.Num() != Inst.Objectives.Num())
    {
        Inst.Counts.SetNumZeroed(Inst.Objectives.Num());
    }

    // Каждое событие засчитываем во все цели, чей канал ему соответствует.
    Context.ForEachEvent([&Inst](const FStateTreeEvent& Event)
    {
        for (int32 Index = 0; Index < Inst.Objectives.Num(); ++Index)
        {
            if (Event.Tag.MatchesTag(Inst.Objectives[Index].EventChannel))
            {
                ++Inst.Counts[Index];
            }
        }
        return EStateTreeLoopEvents::Next;
    });

    QuestTask_ObjectiveGroup_Internal::ReportProgress(Context, Inst);

    int32 CompletedCount = 0;
    for (int32 Index = 0; Index < Inst.Objectives.Num(); ++Index)
    {
        if (Inst.Counts[Index] >= Inst.Objectives[Index].RequiredCount)
        {
            ++CompletedCount;
        }
    }

    const int32 Required = Inst.RequiredObjectives > 0
        ? FMath::Min(Inst.RequiredObjectives, Inst.Objectives.Num())
        : Inst.Objectives.Num();

    return (Inst.Objectives.Num() > 0 && CompletedCount >= Required)
        ? EStateTreeRunStatus::Succeeded
        : EStateTreeRunStatus::Running;
}
