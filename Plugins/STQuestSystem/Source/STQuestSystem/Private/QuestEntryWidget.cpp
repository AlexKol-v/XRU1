// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestEntryWidget.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "QuestSubsystem.h"

void UQuestEntryWidget::SetQuest(FGameplayTag InQuestId)
{
    QuestId = InQuestId;

    const UWorld* World = GetWorld();
    const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    if (const UQuestSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr)
    {
        OnQuestDataRefreshed(Subsystem->GetQuestDisplayData(QuestId));
    }
}
