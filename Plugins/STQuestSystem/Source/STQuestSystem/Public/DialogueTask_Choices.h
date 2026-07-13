// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "STDialogueTypes.h"
#include "DialogueTask_Choices.generated.h"

/** Данные экземпляра задачи-выборов. */
USTRUCT()
struct FDialogueTask_ChoicesInstanceData
{
    GENERATED_BODY()

    /** Варианты выбора, показываемые игроку. */
    UPROPERTY(EditAnywhere, Category = "Dialogue")
    TArray<FDialogueChoice> Choices;

    /** Индекс выбранного варианта; выход для переходов дерева (рантайм). */
    UPROPERTY(VisibleAnywhere, Category = "Dialogue")
    int32 SelectedIndex = INDEX_NONE;
};

/**
 * Задача-выборы State Tree диалога: публикует варианты в UI и держится,
 * пока игрок не сделает выбор (событие Dialogue.Event.Choice). Индекс выбора
 * сохраняется в SelectedIndex для ветвления переходов.
 */
USTRUCT(meta = (DisplayName = "Dialogue Choices", Category = "Dialogue"))
struct STQUESTSYSTEM_API FDialogueTask_Choices : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    FDialogueTask_Choices();

    using FInstanceDataType = FDialogueTask_ChoicesInstanceData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
