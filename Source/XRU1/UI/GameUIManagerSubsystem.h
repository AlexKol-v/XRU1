// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SubclassOf.h"
#include "GameUIManagerSubsystem.generated.h"

class APlayerController;
class UPrimaryGameLayout;
class UWorld;

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

    /**
     * Мир, для которого лейаут был создан.
     *
     * Хранится отдельно, потому что спросить это у самого виджета нельзя:
     * его Outer — GameInstance (виджет переживает travel), а `GetWorld()`
     * через Outer вернёт уже НОВЫЙ мир. Владелец тоже не показатель:
     * `GetOwningPlayer()` идёт через ULocalPlayer, который travel переживает и
     * указывает на свежий PlayerController. Без явной отметки мира лейаут из
     * прошлого уровня считался «своим», а он к тому моменту уже снят с экрана
     * (`UWorld::CleanupWorld` чистит виджеты viewport) — так хаб и оставался
     * без HUD.
     */
    TWeakObjectPtr<UWorld> LayoutWorld;
};
