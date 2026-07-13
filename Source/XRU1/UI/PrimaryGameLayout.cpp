// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrimaryGameLayout.h"

#include "Widgets/CommonActivatableWidgetContainer.h"

UCommonActivatableWidgetStack* UPrimaryGameLayout::GetStackForLayer(EUILayer Layer) const
{
    switch (Layer)
    {
    case EUILayer::Game:
        return GameStack;
    case EUILayer::GameMenu:
        return GameMenuStack;
    case EUILayer::Menu:
        return MenuStack;
    case EUILayer::Modal:
        return ModalStack;
    default:
        return nullptr;
    }
}

UCommonActivatableWidget* UPrimaryGameLayout::PushWidgetToLayer(EUILayer Layer, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
    UCommonActivatableWidgetStack* Stack = GetStackForLayer(Layer);
    if (!Stack || !WidgetClass)
    {
        return nullptr;
    }

    return Stack->AddWidget<UCommonActivatableWidget>(WidgetClass);
}

void UPrimaryGameLayout::PopWidgetFromLayer(EUILayer Layer, UCommonActivatableWidget* Widget)
{
    UCommonActivatableWidgetStack* Stack = GetStackForLayer(Layer);
    if (Stack && Widget)
    {
        Stack->RemoveWidget(*Widget);
    }
}
