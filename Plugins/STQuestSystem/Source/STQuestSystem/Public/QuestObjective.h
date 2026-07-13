// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "QuestTypes.h"
#include "QuestObjective.generated.h"

/**
 * Динамическая цель квеста — полиморфный объект, создаваемый в C++ или
 * Blueprints и добавляемый в инстанс квеста в рантайме. Считает квест-события
 * на своём канале; наблюдается задачей FQuestTask_DynamicObjectiveGroup.
 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced)
class STQUESTSYSTEM_API UQuestObjective : public UObject
{
    GENERATED_BODY()

public:
    /** Идентификатор цели — уникален в пределах квеста. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    FGameplayTag ObjectiveId;

    /** Канал квест-событий, засчитываемых в эту цель. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    FGameplayTag EventChannel;

    /** Требуемое число событий для завершения цели. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest", meta = (ClampMin = "1"))
    int32 RequiredCount = 1;

    /** Описание цели для журнала и трекера. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    FText Description;

    /** Текущее число засчитанных событий. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    int32 CurrentCount = 0;

    /** Текущее состояние цели. */
    UPROPERTY(BlueprintReadOnly, Category = "Quest")
    EObjectiveState State = EObjectiveState::Active;

    /** Засчитывает событие, если его тег соответствует каналу цели. */
    void HandleEventTag(FGameplayTag EventTag);

    /** Истинно, когда цель выполнена. */
    bool IsSatisfied() const { return CurrentCount >= RequiredCount; }

    /** Возвращает снимок прогресса цели для FQuestProgress. */
    FObjectiveProgress MakeProgress() const;
};
