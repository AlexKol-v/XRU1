// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestGiverComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "QuestDefinition.h"
#include "QuestInstance.h"
#include "QuestSubsystem.h"
#include "QuestTypes.h"

namespace QuestGiverComponent_Internal
{
    /** Возвращает подсистему квестов мира этого компонента. */
    UQuestSubsystem* GetQuestSubsystem(const UActorComponent* Component)
    {
        const UWorld* World = Component ? Component->GetWorld() : nullptr;
        const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
        return GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
    }
} // namespace QuestGiverComponent_Internal

UQuestGiverComponent::UQuestGiverComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

TArray<UQuestDefinition*> UQuestGiverComponent::GetAvailableQuests(AActor* Player) const
{
    TArray<UQuestDefinition*> Result;

    const UQuestSubsystem* Subsystem = QuestGiverComponent_Internal::GetQuestSubsystem(this);
    if (!Subsystem)
    {
        return Result;
    }

    for (const FQuestGiverEntry& Entry : IssuableQuests)
    {
        // Предлагаем только ещё не взятый квест (Inactive) с выполненными условиями.
        // Состояние — источник правды: диалог/код, выдавая квест, уводит его из
        // Inactive, и он перестаёт попадать в список — без ручной настройки Conditions.
        if (Entry.Quest
            && Subsystem->GetQuestState(Entry.Quest->QuestId) == EQuestState::Inactive
            && Subsystem->AreRequirementsMet(Entry.Conditions, Player))
        {
            Result.Add(Entry.Quest);
        }
    }
    return Result;
}

bool UQuestGiverComponent::HasQuestToOffer(AActor* Player) const
{
    return GetAvailableQuests(Player).Num() > 0;
}

void UQuestGiverComponent::GrantQuest(FGameplayTag QuestId, AActor* Player)
{
    UQuestSubsystem* Subsystem = QuestGiverComponent_Internal::GetQuestSubsystem(this);
    if (!Subsystem)
    {
        return;
    }

    // Квест должен числиться среди выдаваемых, и его условия — быть выполнены.
    const FQuestGiverEntry* Entry = IssuableQuests.FindByPredicate(
        [QuestId](const FQuestGiverEntry& Item)
        {
            return Item.Quest && Item.Quest->QuestId == QuestId;
        });
    if (!Entry || !Subsystem->AreRequirementsMet(Entry->Conditions, Player))
    {
        return;
    }

    // Делаем квест доступным игроку; запускаем, только если он действительно стал доступен.
    Subsystem->MakeQuestAvailable(QuestId, Player);
    if (Subsystem->GetQuestState(QuestId) == EQuestState::Available)
    {
        if (UQuestInstance* Instance = Subsystem->StartQuestById(QuestId))
        {
            Instance->Owner = Player;
        }
    }
}
