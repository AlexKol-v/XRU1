// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameUIManagerSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "PrimaryGameLayout.h"

void UGameUIManagerSubsystem::CreateLayout(APlayerController* OwningPlayer, TSubclassOf<UPrimaryGameLayout> LayoutClass)
{
    if (!OwningPlayer || !LayoutClass)
    {
        return;
    }

    // Слой создаётся один раз на локального игрока. Подсистема живёт на
    // GameInstance и переживает смену уровня, а виджет умирает вместе со своим
    // PlayerController'ом — для нового контроллера лейаут пересоздаём.
    if (RootLayout && RootLayout->GetOwningPlayer() == OwningPlayer)
    {
        return;
    }

    RootLayout = CreateWidget<UPrimaryGameLayout>(OwningPlayer, LayoutClass);
    if (RootLayout)
    {
        RootLayout->AddToViewport();
    }
}
