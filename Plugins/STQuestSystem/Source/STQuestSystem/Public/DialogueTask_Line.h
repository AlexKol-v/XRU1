// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"
#include "DialogueTask_Line.generated.h"

/** Данные экземпляра задачи-реплики. */
USTRUCT()
struct FDialogueTask_LineInstanceData
{
    GENERATED_BODY()

    /** Текст реплики NPC, публикуемый в UI. */
    UPROPERTY(EditAnywhere, Category = "Dialogue")
    FText LineText;

    /**
     * Необязательный override говорящего. Пусто — реплику озвучивает NPC,
     * запустивший диалог (берётся с раннера, ассет переиспользуем). Задаётся
     * явно лишь для нестандартного голоса в этой реплике (игрок, третий персонаж).
     */
    UPROPERTY(EditAnywhere, Category = "Dialogue")
    FGameplayTag SpeakerId;
};

/**
 * Задача-реплика State Tree диалога: публикует текст в UI через подсистему
 * и держится в Running, пока игрок не продолжит диалог
 * (событие Dialogue.Event.Choice).
 */
USTRUCT(meta = (DisplayName = "Dialogue Line", Category = "Dialogue"))
struct STQUESTSYSTEM_API FDialogueTask_Line : public FStateTreeTaskCommonBase
{
    GENERATED_BODY()

    FDialogueTask_Line();

    using FInstanceDataType = FDialogueTask_LineInstanceData;
    virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
