// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrimaryGameLayout.h"

#include "Components/Image.h"
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

void UPrimaryGameLayout::ClearScreenBackdrop(const UWidget* Owner)
{
    // Чужой фон не трогаем: экран, ушедший под другой, не должен гасить чужую
    // картинку только потому, что деактивировался позже.
    if (BackdropOwner.Get() != Owner)
    {
        return;
    }
    BackdropOwner = nullptr;
    if (Img_ScreenBackdrop)
    {
        Img_ScreenBackdrop->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UPrimaryGameLayout::SetScreenBackdrop(const UWidget* Owner, UTexture2D* Texture, FLinearColor Tint)
{
    BackdropOwner = Owner;

    if (!Img_ScreenBackdrop)
    {
        return;
    }

    if (!Texture)
    {
        // Скрываем, а не оставляем прозрачным: пустая кисть UImage — белый
        // квадрат на весь экран, и «фон не нужен» превратилось бы в засветку.
        Img_ScreenBackdrop->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }

    Img_ScreenBackdrop->SetBrushFromTexture(Texture);
    Img_ScreenBackdrop->SetColorAndOpacity(Tint);
    // HitTestInvisible: фон не должен перехватывать клики у панелей поверх него.
    Img_ScreenBackdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UPrimaryGameLayout::SetGameLayerHidden(const UWidget* Owner, bool bHidden)
{
    if (bHidden)
    {
        GameLayerHiders.AddUnique(Owner);
    }
    else
    {
        GameLayerHiders.Remove(Owner);
    }
    RefreshGameLayerVisibility();
}

void UPrimaryGameLayout::RefreshGameLayerVisibility()
{
    if (!GameStack)
    {
        return;
    }
    // Мёртвые владельцы вычищаются здесь, а не по событию: экран может уйти
    // вместе со своим миром, не успев отпустить слой.
    GameLayerHiders.RemoveAll([](const TWeakObjectPtr<const UWidget>& Weak) { return !Weak.IsValid(); });

    const bool bHide = GameLayerHiders.Num() > 0;
    GameStack->SetVisibility(bHide ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}
