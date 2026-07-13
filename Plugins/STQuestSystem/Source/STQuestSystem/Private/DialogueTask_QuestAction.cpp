// Copyright Epic Games, Inc. All Rights Reserved.

#include "DialogueTask_QuestAction.h"

#include "DialogueRunnerActor.h"
#include "Engine/GameInstance.h"
#include "QuestSubsystem.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FDialogueTask_QuestAction::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    const FInstanceDataType& Inst = Context.GetInstanceData(*this);

    ADialogueRunnerActor* Runner = ADialogueRunnerActor::GetFromContext(Context);
    const UGameInstance* GameInstance = Runner ? Runner->GetGameInstance() : nullptr;
    UQuestSubsystem* Quests = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;

    // Действие — необязательная связка; невалидная настройка не должна рвать диалог.
    if (Quests && Inst.QuestId.IsValid())
    {
        AActor* Player = Runner ? Runner->GetPlayerActor() : nullptr;
        switch (Inst.Action)
        {
        case EDialogueQuestAction::MakeAvailable:
            Quests->MakeQuestAvailable(Inst.QuestId, Player);
            break;
        case EDialogueQuestAction::Start:
            Quests->StartQuestById(Inst.QuestId);
            break;
        case EDialogueQuestAction::Complete:
            Quests->CompleteQuest(Inst.QuestId);
            break;
        case EDialogueQuestAction::Fail:
            Quests->FailQuest(Inst.QuestId);
            break;
        default:
            break;
        }
    }

    return EStateTreeRunStatus::Succeeded;
}
