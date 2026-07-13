// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameUIManagerSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "PrimaryGameLayout.h"

void UGameUIManagerSubsystem::CreateLayout(APlayerController* OwningPlayer, TSubclassOf<UPrimaryGameLayout> LayoutClass)
{
    // Слой создаётся один раз на локального игрока.
    if (RootLayout || !OwningPlayer || !LayoutClass)
    {
        return;
    }

    RootLayout = CreateWidget<UPrimaryGameLayout>(OwningPlayer, LayoutClass);
    if (RootLayout)
    {
        RootLayout->AddToViewport();
    }
}
