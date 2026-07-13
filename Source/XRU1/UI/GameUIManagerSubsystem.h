// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SubclassOf.h"
#include "GameUIManagerSubsystem.generated.h"

class APlayerController;
class UPrimaryGameLayout;

/**
 * Менеджер UI: создаёт корневой UPrimaryGameLayout для локального игрока
 * и даёт к нему доступ. Усечённый аналог Lyra-UGameUIManagerSubsystem.
 */
UCLASS()
class XRU1_API UGameUIManagerSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** Создаёт корневой UI-слой для игрока и добавляет его на экран. */
    void CreateLayout(APlayerController* OwningPlayer, TSubclassOf<UPrimaryGameLayout> LayoutClass);

    /** Возвращает корневой UI-слой или nullptr. */
    UFUNCTION(BlueprintPure, Category = "UI")
    UPrimaryGameLayout* GetRootLayout() const { return RootLayout; }

private:
    /** Корневой виджет-слой текущего локального игрока. */
    UPROPERTY(Transient)
    TObjectPtr<UPrimaryGameLayout> RootLayout;
};
