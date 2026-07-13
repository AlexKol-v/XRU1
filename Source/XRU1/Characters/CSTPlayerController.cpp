// Copyright Epic Games, Inc. All Rights Reserved.

#include "CSTPlayerController.h"

#include "CommonActivatableWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameUIManagerSubsystem.h"
#include "InputCoreTypes.h"
#include "PrimaryGameLayout.h"

ACSTPlayerController::ACSTPlayerController()
{
    bShowMouseCursor = true;
    DefaultMouseCursor = EMouseCursor::Default;
}

void ACSTPlayerController::BeginPlay()
{
    Super::BeginPlay();

    // UI-слой нужен только локальному игроку и только если задан класс.
    if (!IsLocalPlayerController() || !RootLayoutClass)
    {
        return;
    }

    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    if (UGameUIManagerSubsystem* UIManager = GameInstance ? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr)
    {
        UIManager->CreateLayout(this, RootLayoutClass);
    }
}

void ACSTPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    // Raw BindKey мимо EnhancedInputLocalPlayerSubsystem: пешка
    // APlayerCharacter::PawnClientRestart вызывает ClearAllMappings(), и
    // любой добавленный сюда IMC был бы снесён позже по жизненному циклу.
    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::J, IE_Pressed, this, &ACSTPlayerController::OnOpenQuestLog);
    }
}

void ACSTPlayerController::OnOpenQuestLog()
{
    // Журнал уже открыт — закрываем (toggle одной клавишей). В PIE клавиша
    // Escape завершает игру, поэтому закрытие журнала тоже вешаем на J.
    if (OpenQuestLog.IsValid() && OpenQuestLog->IsActivated())
    {
        OpenQuestLog->DeactivateWidget();
        OpenQuestLog = nullptr;
        return;
    }

    if (!QuestLogWidgetClass)
    {
        return;
    }

    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    UGameUIManagerSubsystem* UIManager = GameInstance ? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
    UPrimaryGameLayout* RootLayout = UIManager ? UIManager->GetRootLayout() : nullptr;
    if (!RootLayout)
    {
        return;
    }

    OpenQuestLog = RootLayout->PushWidgetToLayer(EUILayer::Menu, QuestLogWidgetClass);
}
