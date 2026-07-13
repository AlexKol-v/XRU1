// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "QuestTypes.h"
#include "QuestSaveGame.generated.h"

/**
 * Сохранение прогресса квестов. Сериализуются ТОЛЬКО снимки FQuestProgress —
 * определения (UQuestDefinition) и State Tree не сохраняются: при загрузке
 * определения берутся из реестра, а State Tree пересоздаётся.
 */
UCLASS()
class STQUESTSYSTEM_API UQuestSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    /** Снимки прогресса всех известных системе квестов. */
    UPROPERTY()
    TArray<FQuestProgress> QuestSnapshots;

    /** Отслеживаемый игроком квест на момент сохранения. */
    UPROPERTY()
    FGameplayTag TrackedQuestId;

    /** Имя слота (метаданные сохранения). */
    UPROPERTY()
    FString SlotName;

    /** Индекс пользователя (метаданные сохранения). */
    UPROPERTY()
    int32 UserIndex = 0;
};
