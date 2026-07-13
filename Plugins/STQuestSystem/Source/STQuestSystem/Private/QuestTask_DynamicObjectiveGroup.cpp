// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestTask_DynamicObjectiveGroup.h"

#include "QuestInstance.h"
#include "QuestObjective.h"
#include "QuestRunnerActor.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"

FQuestTask_DynamicObjectiveGroup::FQuestTask_DynamicObjectiveGroup()
{
    bShouldCallTickOnlyOnEvents = true;
}

EStateTreeRunStatus FQuestTask_DynamicObjectiveGroup::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FQuestTask_DynamicObjectiveGroup::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& Inst = Context.GetInstanceData(*this);

    AQuestRunnerActor* Runner = AQuestRunnerActor::GetFromContext(Context);
    UQuestInstance* QuestInstance = Runner ? Runner->GetOwningInstance() : nullptr;
    if (!QuestInstance)
    {
        return EStateTreeRunStatus::Running;
    }

    // Маршрутизируем каждое событие во все динамические цели.
    Context.ForEachEvent([QuestInstance](const FStateTreeEvent& Event)
    {
        for (UQuestObjective* Objective : QuestInstance->DynamicObjectives)
        {
            if (Objective)
            {
                Objective->HandleEventTag(Event.Tag);
            }
        }
        return EStateTreeLoopEvents::Next;
    });

    // Обновляем снимок прогресса и считаем выполненные цели.
    int32 CompletedCount = 0;
    for (UQuestObjective* Objective : QuestInstance->DynamicObjectives)
    {
        if (Objective)
        {
            Runner->ReportObjectiveProgress(Objective->MakeProgress());
            if (Objective->IsSatisfied())
            {
                ++CompletedCount;
            }
        }
    }

    const int32 Total = QuestInstance->DynamicObjectives.Num();
    const int32 Required = Inst.RequiredObjectives > 0 ? FMath::Min(Inst.RequiredObjectives, Total) : Total;

    return (Total > 0 && CompletedCount >= Required)
        ? EStateTreeRunStatus::Succeeded
        : EStateTreeRunStatus::Running;
}
