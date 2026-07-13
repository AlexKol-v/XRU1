// Copyright Epic Games, Inc. All Rights Reserved.

#include "DialogueTask_Choices.h"

#include "DialogueGameplayTags.h"
#include "DialogueRunnerActor.h"
#include "DialogueSubsystem.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"

FDialogueTask_Choices::FDialogueTask_Choices()
{
    // Выборы реагируют только на событие выбора — без поллинга каждый кадр.
    bShouldCallTickOnlyOnEvents = true;
}

EStateTreeRunStatus FDialogueTask_Choices::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& Inst = Context.GetInstanceData(*this);
    Inst.SelectedIndex = INDEX_NONE;

    if (ADialogueRunnerActor* Runner = ADialogueRunnerActor::GetFromContext(Context))
    {
        if (UDialogueSubsystem* Subsystem = Runner->GetDialogueSubsystem())
        {
            Subsystem->BroadcastChoices(Inst.Choices);
        }
    }

    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FDialogueTask_Choices::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& Inst = Context.GetInstanceData(*this);
    ADialogueRunnerActor* Runner = ADialogueRunnerActor::GetFromContext(Context);
    EStateTreeRunStatus Result = EStateTreeRunStatus::Running;

    Context.ForEachEvent([&Inst, Runner, &Result](const FStateTreeEvent& Event)
    {
        if (Event.Tag.MatchesTag(DialogueGameplayTags::Dialogue_Event_Choice))
        {
            Inst.SelectedIndex = Runner ? Runner->GetPendingChoiceIndex() : INDEX_NONE;
            Result = EStateTreeRunStatus::Succeeded;
            return EStateTreeLoopEvents::Break;
        }
        return EStateTreeLoopEvents::Next;
    });

    return Result;
}
