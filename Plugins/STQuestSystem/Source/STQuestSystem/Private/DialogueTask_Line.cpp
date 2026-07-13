// Copyright Epic Games, Inc. All Rights Reserved.

#include "DialogueTask_Line.h"

#include "DialogueGameplayTags.h"
#include "DialogueRunnerActor.h"
#include "DialogueSubsystem.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"

FDialogueTask_Line::FDialogueTask_Line()
{
    // Реплика реагирует только на событие продолжения — без поллинга каждый кадр.
    bShouldCallTickOnlyOnEvents = true;
}

EStateTreeRunStatus FDialogueTask_Line::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    const FInstanceDataType& Inst = Context.GetInstanceData(*this);

    if (ADialogueRunnerActor* Runner = ADialogueRunnerActor::GetFromContext(Context))
    {
        if (UDialogueSubsystem* Subsystem = Runner->GetDialogueSubsystem())
        {
            // Говорящий резолвится динамически: явный SpeakerId из ассета —
            // для нестандартного голоса (игрок, третий персонаж); пустой —
            // приписывается NPC, запустившему диалог (ассет переиспользуем).
            const FGameplayTag Speaker = Inst.SpeakerId.IsValid()
                ? Inst.SpeakerId
                : Runner->GetNPCSpeakerId();
            Subsystem->BroadcastLine(Inst.LineText, Speaker);
        }
    }

    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FDialogueTask_Line::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    EStateTreeRunStatus Result = EStateTreeRunStatus::Running;

    // Любое событие продвижения завершает реплику и передаёт ход дереву.
    Context.ForEachEvent([&Result](const FStateTreeEvent& Event)
    {
        if (Event.Tag.MatchesTag(DialogueGameplayTags::Dialogue_Event_Choice))
        {
            Result = EStateTreeRunStatus::Succeeded;
            return EStateTreeLoopEvents::Break;
        }
        return EStateTreeLoopEvents::Next;
    });

    return Result;
}
