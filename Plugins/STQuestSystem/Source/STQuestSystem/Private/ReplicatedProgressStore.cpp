// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReplicatedProgressStore.h"

#include "QuestInstance.h"

void FReplicatedProgressStore::StoreInstance(FGameplayTag QuestId, UQuestInstance* Instance)
{
    if (!Instance)
    {
        return;
    }

    // Сервер: переносим снимок прогресса инстанса в реплицируемый FastArray.
    FQuestProgressFastArrayItem* Item = ReplicatedItems.Items.FindByPredicate(
        [QuestId](const FQuestProgressFastArrayItem& Candidate)
        {
            return Candidate.QuestId == QuestId;
        });
    if (!Item)
    {
        Item = &ReplicatedItems.Items.AddDefaulted_GetRef();
        Item->QuestId = QuestId;
    }
    Item->Progress = Instance->Progress;
    ReplicatedItems.MarkItemDirty(*Item);
}

UQuestInstance* FReplicatedProgressStore::GetInstance(FGameplayTag QuestId) const
{
    // Клиенты не держат UQuestInstance — на руках только реплицированные данные.
    // Серверная сторона работала бы с авторитетными инстансами отдельно.
    return nullptr;
}

void FReplicatedProgressStore::RemoveInstance(FGameplayTag QuestId)
{
    const int32 Removed = ReplicatedItems.Items.RemoveAll(
        [QuestId](const FQuestProgressFastArrayItem& Candidate)
        {
            return Candidate.QuestId == QuestId;
        });
    if (Removed > 0)
    {
        ReplicatedItems.MarkArrayDirty();
    }
}

void FReplicatedProgressStore::GetAllInstances(TArray<UQuestInstance*>& OutInstances) const
{
    // Заготовка: реплицируются данные, а не UObject-инстансы.
    OutInstances.Reset();
}
