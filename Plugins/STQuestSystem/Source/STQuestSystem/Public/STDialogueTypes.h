// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "STDialogueTypes.generated.h"

/** Один вариант выбора игрока в диалоге. */
USTRUCT(BlueprintType)
struct FDialogueChoice
{
    GENERATED_BODY()

    /** Текст варианта, показываемый игроку. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    FText Text;

    /** Идентификатор варианта — для привязки/аналитики (необязателен). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    FGameplayTag ChoiceId;
};

/** Действие над квестом, выполняемое из диалога задачей FDialogueTask_QuestAction. */
UENUM(BlueprintType)
enum class EDialogueQuestAction : uint8
{
    /** Сделать квест доступным игроку (с проверкой пререквизитов). */
    MakeAvailable UMETA(DisplayName = "Сделать доступным"),
    /** Запустить квест немедленно. */
    Start         UMETA(DisplayName = "Запустить"),
    /** Завершить квест успехом. */
    Complete      UMETA(DisplayName = "Завершить"),
    /** Провалить квест. */
    Fail          UMETA(DisplayName = "Провалить")
};
