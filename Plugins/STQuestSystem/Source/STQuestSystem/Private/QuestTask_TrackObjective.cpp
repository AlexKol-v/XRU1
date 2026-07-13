// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestTask_TrackObjective.h"

#include "QuestRunnerActor.h"
#include "QuestTypes.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"

namespace QuestTask_TrackObjective_Internal
{
    /** Сообщает раннеру снимок прогресса этой цели. */
    void ReportProgress(FStateTreeExecutionContext& Context, const FQuestTask_TrackObjectiveInstanceData& Inst)
    {
        AQuestRunnerActor* Runner = AQuestRunnerActor::GetFromContext(Context);
        if (!Runner)
        {
            return;
        }

        FObjectiveProgress Progress;
        Progress.ObjectiveId = Inst.ObjectiveId;
        Progress.Current = Inst.CurrentCount;
        Progress.Required = Inst.RequiredCount;
        Progress.Description = Inst.Description;
        Progress.State = (Inst.CurrentCount >= Inst.RequiredCount)
            ? EObjectiveState::Completed
            : EObjectiveState::Active;
        Runner->ReportObjectiveProgress(Progress);
    }
} // namespace QuestTask_TrackObjective_Internal

FQuestTask_TrackObjective::FQuestTask_TrackObjective()
{
    // Задача обрабатывается только при поступлении событий State Tree —
    // никакого поллинга в каждом кадре.
    bShouldCallTickOnlyOnEvents = true;
}

EStateTreeRunStatus FQuestTask_TrackObjective::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& Inst = Context.GetInstanceData(*this);

    // При восстановлении берём стартовый счётчик из снимка (через раннер), иначе 0.
    AQuestRunnerActor* Runner = AQuestRunnerActor::GetFromContext(Context);
    Inst.CurrentCount = Runner ? Runner->GetRestoredObjectiveCount(Inst.ObjectiveId) : 0;

    QuestTask_TrackObjective_Internal::ReportProgress(Context, Inst);

    // Цель уже выполнена восстановленным счётчиком — завершаемся сразу
    // (задача событийная, без событий Tick не вызовется).
    return Inst.CurrentCount >= Inst.RequiredCount
        ? EStateTreeRunStatus::Succeeded
        : EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FQuestTask_TrackObjective::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& Inst = Context.GetInstanceData(*this);

    // Считаем каждое событие, чей тег совпадает с каналом цели или вложен в него.
    Context.ForEachEvent([&Inst](const FStateTreeEvent& Event)
    {
        if (Event.Tag.MatchesTag(Inst.EventChannel))
        {
            ++Inst.CurrentCount;
        }
        return EStateTreeLoopEvents::Next;
    });

    QuestTask_TrackObjective_Internal::ReportProgress(Context, Inst);

    return Inst.CurrentCount >= Inst.RequiredCount
        ? EStateTreeRunStatus::Succeeded
        : EStateTreeRunStatus::Running;
}
