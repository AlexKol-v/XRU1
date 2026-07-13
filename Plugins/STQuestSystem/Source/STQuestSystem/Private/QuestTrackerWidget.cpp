// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestTrackerWidget.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "QuestSubsystem.h"

UQuestSubsystem* UQuestTrackerWidget::GetQuestSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
}

void UQuestTrackerWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->OnTrackedQuestChanged.AddDynamic(this, &UQuestTrackerWidget::HandleTrackedQuestChanged);
        Subsystem->OnObjectiveProgressChanged.AddDynamic(this, &UQuestTrackerWidget::HandleObjectiveProgress);
    }
    Refresh();
}

void UQuestTrackerWidget::NativeDestruct()
{
    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->OnTrackedQuestChanged.RemoveDynamic(this, &UQuestTrackerWidget::HandleTrackedQuestChanged);
        Subsystem->OnObjectiveProgressChanged.RemoveDynamic(this, &UQuestTrackerWidget::HandleObjectiveProgress);
    }

    Super::NativeDestruct();
}

void UQuestTrackerWidget::HandleTrackedQuestChanged(FGameplayTag QuestId)
{
    Refresh();
}

void UQuestTrackerWidget::HandleObjectiveProgress(FGameplayTag QuestId)
{
    const UQuestSubsystem* Subsystem = GetQuestSubsystem();
    if (Subsystem && Subsystem->GetTrackedQuest() == QuestId)
    {
        Refresh();
    }
}

void UQuestTrackerWidget::Refresh()
{
    const UQuestSubsystem* Subsystem = GetQuestSubsystem();
    if (!Subsystem)
    {
        return;
    }

    OnTrackedQuestRefreshed(Subsystem->GetQuestDisplayData(Subsystem->GetTrackedQuest()));
}
