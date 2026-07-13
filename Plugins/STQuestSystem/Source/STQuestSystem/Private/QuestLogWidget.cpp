// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestLogWidget.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "QuestDefinition.h"
#include "QuestEntryWidget.h"
#include "QuestSubsystem.h"

UQuestSubsystem* UQuestLogWidget::GetQuestSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
}

void UQuestLogWidget::NativeOnActivated()
{
    Super::NativeOnActivated();

    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->OnQuestStateChanged.AddDynamic(this, &UQuestLogWidget::HandleQuestStateChanged);
        Subsystem->OnObjectiveProgressChanged.AddDynamic(this, &UQuestLogWidget::HandleObjectiveProgress);
    }
    RebuildEntries();
}

void UQuestLogWidget::NativeOnDeactivated()
{
    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->OnQuestStateChanged.RemoveDynamic(this, &UQuestLogWidget::HandleQuestStateChanged);
        Subsystem->OnObjectiveProgressChanged.RemoveDynamic(this, &UQuestLogWidget::HandleObjectiveProgress);
    }

    Super::NativeOnDeactivated();
}

void UQuestLogWidget::SetSortMode(EQuestSortMode Mode)
{
    SortMode = Mode;
    RebuildEntries();
}

void UQuestLogWidget::RequestStartQuest(FGameplayTag QuestId)
{
    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->StartQuestById(QuestId);
    }
}

void UQuestLogWidget::RequestTrackQuest(FGameplayTag QuestId)
{
    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->SetTrackedQuest(QuestId);
    }
}

void UQuestLogWidget::RequestAbandonQuest(FGameplayTag QuestId)
{
    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->AbandonQuest(QuestId);
    }
}

void UQuestLogWidget::HandleQuestStateChanged(FGameplayTag QuestId, EQuestState NewState)
{
    RebuildEntries();
}

void UQuestLogWidget::HandleObjectiveProgress(FGameplayTag QuestId)
{
    RebuildEntries();
}

void UQuestLogWidget::RebuildEntries()
{
    UQuestSubsystem* Subsystem = GetQuestSubsystem();
    if (!Subsystem || !EntryWidgetClass)
    {
        return;
    }

    // Собираем известные квесты и сортируем выбранным способом.
    TArray<UQuestDefinition*> Quests = Subsystem->GetAllKnownQuests();
    Quests.Sort([Subsystem, this](const UQuestDefinition& A, const UQuestDefinition& B)
    {
        switch (SortMode)
        {
        case EQuestSortMode::ByDifficulty:
            return A.Difficulty < B.Difficulty;
        case EQuestSortMode::ByDuration:
            return A.EstimatedMinutes < B.EstimatedMinutes;
        case EQuestSortMode::ByState:
        default:
            return Subsystem->GetQuestState(A.QuestId) < Subsystem->GetQuestState(B.QuestId);
        }
    });

    // Пересоздаём виджеты-строки.
    EntryWidgets.Reset();
    for (UQuestDefinition* Quest : Quests)
    {
        if (!Quest)
        {
            continue;
        }

        UQuestEntryWidget* Entry = CreateWidget<UQuestEntryWidget>(this, EntryWidgetClass);
        if (Entry)
        {
            Entry->SetOwningLog(this);
            Entry->SetQuest(Quest->QuestId);
            EntryWidgets.Add(Entry);
        }
    }

    // Передаём строки в разметку для размещения.
    TArray<UQuestEntryWidget*> Entries;
    Entries.Reserve(EntryWidgets.Num());
    for (const TObjectPtr<UQuestEntryWidget>& Entry : EntryWidgets)
    {
        Entries.Add(Entry);
    }
    OnEntriesRebuilt(Entries);
}
