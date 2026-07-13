// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "QuestTypes.h"
#include "QuestRequirement.generated.h"

/** Тип требования (пререквизита) квеста. */
UENUM(BlueprintType)
enum class EQuestRequirementType : uint8
{
    /** Указанный квест должен находиться в требуемом состоянии. */
    QuestState  UMETA(DisplayName = "Состояние квеста"),
    /** Владелец квеста должен иметь указанный тег. */
    OwnerHasTag UMETA(DisplayName = "Тег у владельца")
};

/**
 * Требование (пререквизит) квеста — гейтирует переход квеста в Available.
 * Проверяется подсистемой на уровне доступности квеста, в отличие от
 * условий State Tree, действующих внутри логики квеста.
 */
USTRUCT(BlueprintType)
struct FQuestRequirement
{
    GENERATED_BODY()

    /** Что именно проверяется. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    EQuestRequirementType Type = EQuestRequirementType::QuestState;

    /** Проверяемый квест — для типа QuestState. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest",
        meta = (EditCondition = "Type == EQuestRequirementType::QuestState", EditConditionHides))
    FGameplayTag RequiredQuestId;

    /** Требуемое состояние проверяемого квеста — для типа QuestState. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest",
        meta = (EditCondition = "Type == EQuestRequirementType::QuestState", EditConditionHides))
    EQuestState RequiredQuestState = EQuestState::Completed;

    /** Требуемый тег у владельца квеста — для типа OwnerHasTag. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest",
        meta = (EditCondition = "Type == EQuestRequirementType::OwnerHasTag", EditConditionHides))
    FGameplayTag RequiredTag;
};
