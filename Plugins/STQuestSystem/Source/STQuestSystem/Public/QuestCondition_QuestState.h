// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeConditionBase.h"
#include "QuestTypes.h"
#include "QuestCondition_QuestState.generated.h"

/** Данные экземпляра условия «состояние квеста». */
USTRUCT()
struct FQuestCondition_QuestStateInstanceData
{
    GENERATED_BODY()

    /** Идентификатор проверяемого квеста. */
    UPROPERTY(EditAnywhere, Category = "Quest")
    FGameplayTag QuestId;

    /** Требуемое состояние квеста. */
    UPROPERTY(EditAnywhere, Category = "Quest")
    EQuestState RequiredState = EQuestState::Completed;
};

/**
 * Условие State Tree: истинно, когда указанный квест находится в требуемом
 * состоянии. Основа гейтирования целей и переходов по состоянию квестов.
 */
USTRUCT(meta = (DisplayName = "Quest Is In State", Category = "Quest"))
struct STQUESTSYSTEM_API FQuestCondition_QuestState : public FStateTreeConditionCommonBase
{
    GENERATED_BODY()

    using FInstanceDataType = FQuestCondition_QuestStateInstanceData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

    virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
