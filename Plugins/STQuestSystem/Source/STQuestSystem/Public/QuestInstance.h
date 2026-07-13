// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "QuestTypes.h"
#include "QuestRequirement.h"
#include "QuestInstance.generated.h"

class UQuestDefinition;
class AQuestRunnerActor;
class UQuestObjective;
class UQuestReward;

/**
 * Рантайм одного квеста — «инстанс» в расколе Definition/Instance.
 * Лёгкий мутабельный объект: ссылается на неизменяемое определение,
 * хранит снимок прогресса и актор-раннер, исполняющий State Tree квеста.
 */
UCLASS(BlueprintType)
class STQUESTSYSTEM_API UQuestInstance : public UObject
{
    GENERATED_BODY()

public:
    /** Неизменяемое определение, на основе которого создан инстанс. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    TObjectPtr<UQuestDefinition> Definition;

    /** Снимок прогресса квеста — наблюдаемый и (в Л3) сохраняемый. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    FQuestProgress Progress;

    /** Актор, исполняющий State Tree этого квеста. */
    UPROPERTY()
    TObjectPtr<AQuestRunnerActor> Runner;

    /** Владелец квеста (персонаж/контроллер). Индиректность — для переноса в Л2. */
    UPROPERTY()
    TWeakObjectPtr<AActor> Owner;

    /** Динамические цели, добавленные в рантайме через UQuestSubsystem. */
    UPROPERTY()
    TArray<TObjectPtr<UQuestObjective>> DynamicObjectives;

    /** Динамические награды, добавленные в рантайме через UQuestSubsystem. */
    UPROPERTY()
    TArray<TObjectPtr<UQuestReward>> DynamicRewards;

    /** Динамические требования, добавленные в рантайме через UQuestSubsystem. */
    UPROPERTY()
    TArray<FQuestRequirement> DynamicRequirements;

    /** Идентификатор квеста. */
    FGameplayTag GetQuestId() const { return Progress.QuestId; }

    /** Текущее состояние квеста. */
    EQuestState GetState() const { return Progress.State; }
};
