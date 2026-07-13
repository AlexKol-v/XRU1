// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Templates/SubclassOf.h"
#include "CSTPlayerController.generated.h"

class UCommonActivatableWidget;
class UPrimaryGameLayout;

/**
 * Контроллер игрока для APlayerCharacter.
 *
 * Не наследует ATopDownCSTPlayerController сознательно: тот несёт
 * click-to-move-логику стратегического варианта, не нужную нашему
 * Enhanced-Input-персонажу. Игровой ввод (WASD, взаимодействие) живёт на
 * пешке APlayerCharacter (Character_ru.md §9.1); этот контроллер отвечает
 * исключительно за UI:
 *  - в BeginPlay создаёт корневой UPrimaryGameLayout через
 *    UGameUIManagerSubsystem;
 *  - в SetupInputComponent биндит клавишу J на открытие журнала квестов.
 */
UCLASS(Abstract)
class XRU1_API ACSTPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ACSTPlayerController();

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;

    /** Класс корневого UI-слоя; создаётся для локального игрока в BeginPlay. */
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UPrimaryGameLayout> RootLayoutClass;

    /** Класс виджета журнала квестов; пушится на слой Menu по клавише J. */
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UCommonActivatableWidget> QuestLogWidgetClass;

private:
    /** Обработчик нажатия J: открывает журнал, а если он открыт — закрывает (toggle). */
    void OnOpenQuestLog();

    /** Текущий открытый журнал; слабая ссылка для toggle по клавише J. */
    TWeakObjectPtr<UCommonActivatableWidget> OpenQuestLog;
};
