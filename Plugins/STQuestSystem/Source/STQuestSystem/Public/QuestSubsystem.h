// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "QuestTypes.h"
#include "QuestRequirement.h"
#include "QuestSubsystem.generated.h"

class UQuestDefinition;
class UQuestInstance;
class UQuestObjective;
class UQuestReward;

/** Срабатывает при смене состояния любого квеста. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestStateChanged, FGameplayTag, QuestId, EQuestState, NewState);

/** Срабатывает при изменении прогресса целей квеста. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnObjectiveProgressChanged, FGameplayTag, QuestId);

/** Срабатывает при событии квеста, которое стоит показать игроку тостом. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestNotification, FQuestNotification, Notification);

/** Срабатывает при смене отслеживаемого игроком квеста. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTrackedQuestChanged, FGameplayTag, QuestId);

/**
 * Ядро системы квестов. Переживает смену уровней (UGameInstanceSubsystem).
 * Ведёт реестр всех определений квестов и реестр их инстансов, управляет
 * жизненным циклом, разрешает цепочки квестов и проверяет пререквизиты.
 */
UCLASS()
class STQUESTSYSTEM_API UQuestSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    //~ Begin USubsystem
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    //~ End USubsystem

    /** Активирует квест по его определению. Возвращает созданный/существующий инстанс. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    UQuestInstance* StartQuest(UQuestDefinition* Definition);

    /** Активирует квест по его идентификатору через реестр определений. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    UQuestInstance* StartQuestById(FGameplayTag QuestId);

    /** Принудительно завершает квест успехом. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void CompleteQuest(FGameplayTag QuestId);

    /** Принудительно проваливает квест. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void FailQuest(FGameplayTag QuestId);

    /** Делает квест доступным (Available), если выполнены его пререквизиты. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void MakeQuestAvailable(FGameplayTag QuestId, AActor* Owner);

    /** Возвращает состояние квеста (Inactive, если квест системе неизвестен). */
    UFUNCTION(BlueprintPure, Category = "Quest")
    EQuestState GetQuestState(FGameplayTag QuestId) const;

    /** Проверяет, находится ли квест в заданном состоянии. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    bool IsQuestInState(FGameplayTag QuestId, EQuestState State) const;

    /** Возвращает инстанс квеста или nullptr. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    UQuestInstance* GetQuestInstance(FGameplayTag QuestId) const;

    /** Возвращает определение квеста из реестра или nullptr. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    UQuestDefinition* GetQuestDefinition(FGameplayTag QuestId) const;

    /** Возвращает все определения квестов, известные системе. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    TArray<UQuestDefinition*> GetAllKnownQuests() const;

    /** Возвращает все инстансы квестов, закешированные системой (любое состояние). */
    UFUNCTION(BlueprintPure, Category = "Quest")
    TArray<UQuestInstance*> GetAllInstances() const;

    /** Возвращает инстансы квестов, принадлежащие указанному владельцу. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    TArray<UQuestInstance*> GetQuestsForOwner(AActor* Owner) const;

    /** Возвращает определения квестов, находящихся в заданном состоянии. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    TArray<UQuestDefinition*> GetQuestsInState(EQuestState State) const;

    /** Истинно, если цель с данным ObjectiveId активна в каком-либо активном квесте. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    bool IsObjectiveActive(FGameplayTag ObjectiveId) const;

    /** Проверяет, выполнены ли все требования с учётом владельца квеста. */
    bool AreRequirementsMet(const TArray<FQuestRequirement>& Requirements, AActor* Owner) const;

    /** Добавляет динамическую цель в активный квест. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void AddObjective(FGameplayTag QuestId, UQuestObjective* Objective);

    /** Убирает динамическую цель из активного квеста по её идентификатору. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void RemoveObjective(FGameplayTag QuestId, FGameplayTag ObjectiveId);

    /** Добавляет динамическую награду в активный квест. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void AddReward(FGameplayTag QuestId, UQuestReward* Reward);

    /** Убирает динамическую награду из активного квеста. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void RemoveReward(FGameplayTag QuestId, UQuestReward* Reward);

    /** Добавляет динамическое требование в активный квест. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void AddRequirement(FGameplayTag QuestId, const FQuestRequirement& Requirement);

    /** Убирает динамическое требование из активного квеста по индексу. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void RemoveRequirement(FGameplayTag QuestId, int32 RequirementIndex);

    /** Переносит квест от одного владельца к другому. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void TransferQuest(FGameplayTag QuestId, AActor* From, AActor* To);

    /** Возвращает отслеживаемый игроком квест. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    FGameplayTag GetTrackedQuest() const { return TrackedQuestId; }

    /** Назначает отслеживаемый квест и оповещает слушателей. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void SetTrackedQuest(FGameplayTag QuestId);

    /** Отказывается от активного квеста — возвращает его в доступные. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void AbandonQuest(FGameplayTag QuestId);

    /** Собирает сводку данных квеста для отображения в UI. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    FQuestDisplayData GetQuestDisplayData(FGameplayTag QuestId) const;

    /** Вызывается раннером при изменении прогресса целей квеста. */
    void NotifyObjectiveProgress(FGameplayTag QuestId);

    /** Вызывается раннером, когда State Tree квеста завершился сам. */
    void NotifyQuestFinished(UQuestInstance* Instance, bool bSuccess);

    /** Собирает снимки прогресса всех известных квестов (для сохранения). */
    void CollectProgress(TArray<FQuestProgress>& OutSnapshots) const;

    /** Сохраняет прогресс квестов в слот. Возвращает успех записи. */
    UFUNCTION(BlueprintCallable, Category = "Quest|Save")
    bool SaveToSlot(const FString& SlotName, int32 UserIndex);

    /** Загружает прогресс из слота и восстанавливает квесты. Возвращает успех. */
    UFUNCTION(BlueprintCallable, Category = "Quest|Save")
    bool LoadFromSlot(const FString& SlotName, int32 UserIndex);

    /** Восстанавливает квесты из снимков: пересоздаёт инстансы и State Tree. */
    void RestoreProgress(const TArray<FQuestProgress>& Snapshots);

    /** Оповещает о смене состояния любого квеста. */
    UPROPERTY(BlueprintAssignable, Category = "Quest")
    FOnQuestStateChanged OnQuestStateChanged;

    /** Оповещает об изменении прогресса целей квеста. */
    UPROPERTY(BlueprintAssignable, Category = "Quest")
    FOnObjectiveProgressChanged OnObjectiveProgressChanged;

    /** Оповещает о событии квеста, которое стоит показать тостом. */
    UPROPERTY(BlueprintAssignable, Category = "Quest")
    FOnQuestNotification OnQuestNotification;

    /** Оповещает о смене отслеживаемого квеста. */
    UPROPERTY(BlueprintAssignable, Category = "Quest")
    FOnTrackedQuestChanged OnTrackedQuestChanged;

private:
    /** Меняет состояние квеста и рассылает OnQuestStateChanged. */
    void SetQuestState(UQuestInstance* Instance, EQuestState NewState);

    /** Сканирует Asset Manager и заполняет реестр определений квестов. */
    void BuildQuestRegistry();

    /** Возвращает инстанс квеста, создавая его при необходимости. */
    UQuestInstance* GetOrCreateInstance(UQuestDefinition* Definition);

    /** Открывает квесты, связанные цепочкой с завершённым/проваленным квестом. */
    void ResolveChainLinks(UQuestInstance* Instance, bool bSuccess);

    /** Выдаёт награды за успешно завершённый квест владельцу квеста. */
    void GrantRewards(UQuestInstance* Instance);

    /** Рассылает уведомление о ключевом переходе состояния квеста. */
    void EmitStateNotification(FGameplayTag QuestId, EQuestState NewState);

    /** Реестр всех определений квестов: тег квеста → определение. */
    UPROPERTY(Transient)
    TMap<FGameplayTag, TObjectPtr<UQuestDefinition>> QuestRegistry;

    /** Реестр инстансов квестов: тег квеста → инстанс. */
    UPROPERTY(Transient)
    TMap<FGameplayTag, TObjectPtr<UQuestInstance>> ActiveQuests;

    /** Идентификатор отслеживаемого игроком квеста. */
    UPROPERTY(Transient)
    FGameplayTag TrackedQuestId;
};
