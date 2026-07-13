// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "STGameplayTagLibrary.generated.h"

/**
 * Вспомогательные Blueprint-функции по GameplayTag для системы квестов.
 */
UCLASS()
class STQUESTSYSTEM_API USTGameplayTagLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Листовой сегмент тега как текст — часть после последней точки.
     * Quest.Speaker.Elder → "Elder". Невалидный тег → пустой текст;
     * тег без точек → тег целиком. Удобно для подписи говорящего в UI диалога.
     */
    UFUNCTION(BlueprintPure, Category = "GameplayTags", meta = (DisplayName = "Get Tag Leaf Name"))
    static FText GetTagLeafName(FGameplayTag Tag);
};
