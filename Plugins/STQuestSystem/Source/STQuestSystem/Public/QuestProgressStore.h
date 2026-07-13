// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/GCObject.h"

class UQuestInstance;

/**
 * Шов хранилища инстансов квестов — точка замены SP↔MP (см. §18 архитектуры).
 * Подсистема в Л1/Л2 держит инстансы прямо в UPROPERTY-карте ActiveQuests;
 * этот интерфейс формализует границу, за которой в мультиплеере встаёт
 * реплицируемое хранилище (FReplicatedProgressStore). В Л3 интерфейс
 * поставляется как шов и НЕ подменяет рабочую подсистему — это шаг будущей
 * MP-миграции.
 */
class STQUESTSYSTEM_API IQuestProgressStore
{
public:
    virtual ~IQuestProgressStore() = default;

    /** Кладёт/обновляет инстанс по идентификатору квеста. */
    virtual void StoreInstance(FGameplayTag QuestId, UQuestInstance* Instance) = 0;

    /** Возвращает инстанс по идентификатору квеста или nullptr. */
    virtual UQuestInstance* GetInstance(FGameplayTag QuestId) const = 0;

    /** Удаляет инстанс по идентификатору квеста. */
    virtual void RemoveInstance(FGameplayTag QuestId) = 0;

    /** Складывает все хранимые инстансы в OutInstances. */
    virtual void GetAllInstances(TArray<UQuestInstance*>& OutInstances) const = 0;
};

/**
 * Одиночная (SP) реализация шва: инстансы в локальной карте. Наследует
 * FGCObject, чтобы держать UObject-инстансы живыми вне UPROPERTY-контекста —
 * именно так корректно хранить UObject в не-UObject классе.
 */
class STQUESTSYSTEM_API FLocalProgressStore : public IQuestProgressStore, public FGCObject
{
public:
    //~ Begin IQuestProgressStore
    virtual void StoreInstance(FGameplayTag QuestId, UQuestInstance* Instance) override;
    virtual UQuestInstance* GetInstance(FGameplayTag QuestId) const override;
    virtual void RemoveInstance(FGameplayTag QuestId) override;
    virtual void GetAllInstances(TArray<UQuestInstance*>& OutInstances) const override;
    //~ End IQuestProgressStore

    //~ Begin FGCObject
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("FLocalProgressStore"); }
    //~ End FGCObject

private:
    /** Инстансы квестов: тег квеста → инстанс. */
    TMap<FGameplayTag, TObjectPtr<UQuestInstance>> Instances;
};
