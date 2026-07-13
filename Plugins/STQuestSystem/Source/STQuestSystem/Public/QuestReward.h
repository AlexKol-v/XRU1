// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "QuestReward.generated.h"

class AActor;
class UGameplayEffect;

/**
 * Базовый класс награды за квест. Полиморфный instanced-объект: каждое
 * определение квеста хранит собственные экземпляры наград. Выдача награды —
 * через GrantTo; реализация переопределяется в C++ или в Blueprints.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class STQUESTSYSTEM_API UQuestReward : public UObject
{
    GENERATED_BODY()

public:
    /** Выдаёт награду получателю — владельцу завершённого квеста. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Quest")
    void GrantTo(AActor* Recipient);
    virtual void GrantTo_Implementation(AActor* Recipient);
};

/** Награда: спаунит актор-предмет рядом с получателем. */
UCLASS(meta = (DisplayName = "Награда: предмет"))
class STQUESTSYSTEM_API UQuestReward_Item : public UQuestReward
{
    GENERATED_BODY()

public:
    /** Класс актора-предмета, выдаваемого получателю. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    TSubclassOf<AActor> ItemActorClass;

    /** Количество выдаваемых экземпляров. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest", meta = (ClampMin = "1"))
    int32 Quantity = 1;

    virtual void GrantTo_Implementation(AActor* Recipient) override;
};

/** Награда: применяет GameplayEffect к ASC получателя. */
UCLASS(meta = (DisplayName = "Награда: GameplayEffect"))
class STQUESTSYSTEM_API UQuestReward_GameplayEffect : public UQuestReward
{
    GENERATED_BODY()

public:
    /** Эффект, применяемый к ASC получателя. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    TSubclassOf<UGameplayEffect> EffectClass;

    /** Уровень применяемого эффекта. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    float EffectLevel = 1.f;

    virtual void GrantTo_Implementation(AActor* Recipient) override;
};

/** Награда: делает другой квест доступным. */
UCLASS(meta = (DisplayName = "Награда: открыть квест"))
class STQUESTSYSTEM_API UQuestReward_UnlockQuest : public UQuestReward
{
    GENERATED_BODY()

public:
    /** Квест, открываемый этой наградой. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    FGameplayTag QuestToUnlock;

    virtual void GrantTo_Implementation(AActor* Recipient) override;
};

/** Награда: выдаёт получателю набор GameplayTag. */
UCLASS(meta = (DisplayName = "Награда: GameplayTag"))
class STQUESTSYSTEM_API UQuestReward_GameplayTag : public UQuestReward
{
    GENERATED_BODY()

public:
    /** Теги, добавляемые получателю как loose-теги. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    FGameplayTagContainer TagsToGrant;

    virtual void GrantTo_Implementation(AActor* Recipient) override;
};

/**
 * Награда-расширение: логика выдачи задаётся в Blueprint-наследнике
 * переопределением события GrantTo. Точка расширения под проектные награды.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Награда: пользовательская"))
class STQUESTSYSTEM_API UQuestReward_Custom : public UQuestReward
{
    GENERATED_BODY()
};
