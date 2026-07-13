// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "QuestProgressStore.h"
#include "QuestTypes.h"
#include "ReplicatedProgressStore.generated.h"

class UQuestInstance;

/**
 * Реплицируемый элемент прогресса одного квеста. Хранит ДАННЫЕ снимка
 * (FQuestProgress), а не UObject-инстанс — по сети передаются данные.
 */
USTRUCT()
struct FQuestProgressFastArrayItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    /** Идентификатор квеста. */
    UPROPERTY()
    FGameplayTag QuestId;

    /** Снимок прогресса квеста (реплицируемые данные). */
    UPROPERTY()
    FQuestProgress Progress;
};

/**
 * FastArray прогресса квестов — реплицируемая коллекция снимков. В реальном
 * мультиплеере объявляется UPROPERTY(Replicated) на APlayerState.
 */
USTRUCT()
struct FQuestProgressFastArray : public FFastArraySerializer
{
    GENERATED_BODY()

    /** Элементы прогресса по квестам. */
    UPROPERTY()
    TArray<FQuestProgressFastArrayItem> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FQuestProgressFastArrayItem, FQuestProgressFastArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FQuestProgressFastArray> : public TStructOpsTypeTraitsBase2<FQuestProgressFastArray>
{
    enum
    {
        WithNetDeltaSerializer = true
    };
};

/**
 * Заготовка MP-реализации шва: вместо UObject-инстансов реплицирует ДАННЫЕ
 * (FQuestProgress) через FastArray. Демонстрирует границу и форму репликации;
 * в Л3 НЕ подключается к подсистеме (полная MP-логика — вне области, §22 YAGNI).
 * Серверный авторитет: сервер исполняет State Tree и кладёт снимки сюда;
 * клиенты читают реплицированные FQuestProgress (UQuestInstance на клиентах нет).
 */
class STQUESTSYSTEM_API FReplicatedProgressStore : public IQuestProgressStore
{
public:
    //~ Begin IQuestProgressStore
    virtual void StoreInstance(FGameplayTag QuestId, UQuestInstance* Instance) override;
    virtual UQuestInstance* GetInstance(FGameplayTag QuestId) const override;
    virtual void RemoveInstance(FGameplayTag QuestId) override;
    virtual void GetAllInstances(TArray<UQuestInstance*>& OutInstances) const override;
    //~ End IQuestProgressStore

    /** Реплицируемые снимки прогресса (на сервере наполняются из инстансов). */
    FQuestProgressFastArray ReplicatedItems;
};
