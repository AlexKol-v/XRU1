// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestCondition_QuestState.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "QuestSubsystem.h"
#include "StateTreeExecutionContext.h"

bool FQuestCondition_QuestState::TestCondition(FStateTreeExecutionContext& Context) const
{
    const FInstanceDataType& Inst = Context.GetInstanceData(*this);

    const UWorld* World = Context.GetWorld();
    const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    if (const UQuestSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr)
    {
        return Subsystem->IsQuestInState(Inst.QuestId, Inst.RequiredState);
    }

    return false;
}
