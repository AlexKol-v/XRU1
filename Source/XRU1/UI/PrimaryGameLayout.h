// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Templates/SubclassOf.h"
#include "PrimaryGameLayout.generated.h"

class UCommonActivatableWidgetStack;

/** Слой UI, на который проталкиваются активируемые виджеты. */
UENUM(BlueprintType)
enum class EUILayer : uint8
{
    Game     UMETA(DisplayName = "Игровой слой"),
    GameMenu UMETA(DisplayName = "Игровое меню"),
    Menu     UMETA(DisplayName = "Меню"),
    Modal    UMETA(DisplayName = "Модальный слой")
};

/**
 * Корневой виджет UI: держит четыре слоя-стека активируемых виджетов.
 * Виджеты проталкиваются на слой по EUILayer. Усечённый аналог
 * Lyra-PrimaryGameLayout на примитивах движкового плагина CommonUI.
 */
UCLASS(Abstract)
class XRU1_API UPrimaryGameLayout : public UCommonActivatableWidget
{
    GENERATED_BODY()

public:
    /** Проталкивает виджет на слой; возвращает созданный виджет или nullptr. */
    UFUNCTION(BlueprintCallable, Category = "UI")
    UCommonActivatableWidget* PushWidgetToLayer(EUILayer Layer, TSubclassOf<UCommonActivatableWidget> WidgetClass);

    /** Убирает виджет с указанного слоя. */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void PopWidgetFromLayer(EUILayer Layer, UCommonActivatableWidget* Widget);

protected:
    /** Стек игрового HUD-слоя; привязка по имени виджета из разметки. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI")
    TObjectPtr<UCommonActivatableWidgetStack> GameStack;

    /** Стек слоя игрового меню. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI")
    TObjectPtr<UCommonActivatableWidgetStack> GameMenuStack;

    /** Стек слоя меню. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI")
    TObjectPtr<UCommonActivatableWidgetStack> MenuStack;

    /** Стек модального слоя. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "UI")
    TObjectPtr<UCommonActivatableWidgetStack> ModalStack;

private:
    /** Возвращает стек, соответствующий слою. */
    UCommonActivatableWidgetStack* GetStackForLayer(EUILayer Layer) const;
};
