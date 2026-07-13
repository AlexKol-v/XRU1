// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestProgressStore.h"

#include "QuestInstance.h"

void FLocalProgressStore::StoreInstance(FGameplayTag QuestId, UQuestInstance* Instance)
{
    Instances.Add(QuestId, Instance);
}

UQuestInstance* FLocalProgressStore::GetInstance(FGameplayTag QuestId) const
{
    const TObjectPtr<UQuestInstance>* Found = Instances.Find(QuestId);
    return Found ? *Found : nullptr;
}

void FLocalProgressStore::RemoveInstance(FGameplayTag QuestId)
{
    Instances.Remove(QuestId);
}

void FLocalProgressStore::GetAllInstances(TArray<UQuestInstance*>& OutInstances) const
{
    OutInstances.Reset();
    OutInstances.Reserve(Instances.Num());
    for (const TPair<FGameplayTag, TObjectPtr<UQuestInstance>>& Pair : Instances)
    {
        if (Pair.Value)
        {
            OutInstances.Add(Pair.Value);
        }
    }
}

void FLocalProgressStore::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (TPair<FGameplayTag, TObjectPtr<UQuestInstance>>& Pair : Instances)
    {
        Collector.AddReferencedObject(Pair.Value);
    }
}
