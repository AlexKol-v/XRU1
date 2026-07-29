// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestTask_WaitOutcome.h"

#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"

FQuestTask_WaitOutcome::FQuestTask_WaitOutcome()
{
    bShouldCallTickOnlyOnEvents = true;
}

EStateTreeRunStatus FQuestTask_WaitOutcome::EnterState(
    FStateTreeExecutionContext& Context,
    const FStateTreeTransitionResult& Transition) const
{
    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FQuestTask_WaitOutcome::Tick(
    FStateTreeExecutionContext& Context,
    const float DeltaTime) const
{
    const FInstanceDataType& Inst = Context.GetInstanceData(*this);
    bool bSucceeded = false;
    bool bFailed = false;

    Context.ForEachEvent([&Inst, &bSucceeded, &bFailed](const FStateTreeEvent& Event)
    {
        const auto Matches = [&Inst, &Event](const FGameplayTag Channel)
        {
            return Channel.IsValid() && (Inst.bRequireExactChannel
                ? Event.Tag == Channel
                : Event.Tag.MatchesTag(Channel));
        };

        bFailed |= Matches(Inst.FailureChannel);
        bSucceeded |= Matches(Inst.SuccessChannel);
        return EStateTreeLoopEvents::Next;
    });

    // При ошибочной двойной публикации failure безопасно доминирует над success.
    if (bFailed)
    {
        return EStateTreeRunStatus::Failed;
    }
    return bSucceeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}
