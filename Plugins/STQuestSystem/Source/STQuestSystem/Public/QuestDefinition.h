// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StateTreeReference.h"
#include "QuestTypes.h"
#include "QuestRequirement.h"
#include "QuestReward.h"
#include "QuestDefinition.generated.h"

/**
 * Связи цепочки квестов — какие квесты становятся доступны по завершении
 * или провалу этого квеста. Связи — это данные, а не дерево: один квест
 * может открываться несколькими предыдущими (сходящиеся цепочки).
 */
USTRUCT(BlueprintType)
struct FQuestChainLinks
{
    GENERATED_BODY()

    /** Квесты, открываемые при успешном завершении этого квеста. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    TArray<FGameplayTag> UnlockOnComplete;

    /** Квесты, открываемые при провале этого квеста. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    TArray<FGameplayTag> UnlockOnFail;
};

/**
 * Неизменяемое определение квеста — designer-authored ассет (Definition в
 * расколе Definition/Instance). Логика квеста (цели, ветвления, завершение)
 * задаётся отдельным ассетом State Tree, на который ссылается QuestLogic.
 * Прогресс в этот ассет НИКОГДА не записывается — он один общий на всех.
 */
UCLASS(BlueprintType)
class STQUESTSYSTEM_API UQuestDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Уникальный идентификатор квеста; ключ в реестре подсистемы. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    FGameplayTag QuestId;

    /** Отображаемое имя квеста. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    FText DisplayName;

    /** Описание квеста для журнала. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest", meta = (MultiLine = true))
    FText Description;

    /** Категория квеста. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    EQuestCategory Category = EQuestCategory::Side;

    /** Сложность квеста. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    EQuestDifficulty Difficulty = EQuestDifficulty::Normal;

    /** Ориентировочная длительность прохождения, минут. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest", meta = (ClampMin = "0"))
    float EstimatedMinutes = 5.f;

    /**
     * Логика квеста — ассет State Tree. Цели реализуются задачами
     * FQuestTask_*, требования — условиями FQuestCondition_*. Дерево
     * исполняется компонентом UStateTreeComponent на акторе-раннере.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    FStateTreeReference QuestLogic;

    /** Пререквизиты — все должны быть выполнены, чтобы квест стал доступен. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    TArray<FQuestRequirement> Prerequisites;

    /** Связи цепочки — какие квесты этот квест открывает по завершении/провале. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
    FQuestChainLinks ChainLinks;

    /** Награды за успешное завершение квеста — полиморфные instanced-объекты. */
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Quest")
    TArray<TObjectPtr<UQuestReward>> Rewards;

    /** Идентификатор primary-ассета: тип «Quest», имя — из QuestId. */
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
