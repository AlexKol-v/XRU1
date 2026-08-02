// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Templates/SubclassOf.h"
#include "PrimaryGameLayout.generated.h"

class UCommonActivatableWidgetStack;
class UImage;
class UTexture2D;

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

    /**
     * Ставит общий фон экранов — он живёт ПОД всеми стеками и переживает
     * переходы между экранами.
     *
     * Почему не в самом экране: `UCommonActivatableWidgetStack` показывает
     * только верхний виджет (внутри `SCommonAnimatedSwitcher`), нижние не
     * рисуются вовсе. Значит фон, лежащий в экране, обязан исчезать и
     * появляться заново на каждом переходе — со всеми морганиями и повторной
     * анимацией. Один фон на лейаут снимает проблему в корне: меняются только
     * панели поверх него.
     *
     * `Texture == nullptr` скрывает фон (экран заявил, что фон ему не нужен —
     * HUD, экран результата поверх боя).
     *
     * Фон «принадлежит» вызвавшему экрану: снять его может только он сам
     * (`ClearScreenBackdrop`). Иначе порядок активации решал бы исход —
     * CommonUI не гарантирует, что уходящий экран деактивируется раньше, чем
     * активируется новый, и «уборка за собой» стирала бы уже поставленный
     * следующим экраном фон.
     */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void SetScreenBackdrop(const UWidget* Owner, UTexture2D* Texture, FLinearColor Tint);

    /** Снимает фон, только если он принадлежит этому экрану. */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void ClearScreenBackdrop(const UWidget* Owner);

    /**
     * Прячет игровой слой (HUD) на время полноэкранного экрана поверх него.
     *
     * Слои независимы: HUD хаба лежит на `Game`, брифинг — на `Menu`, и без
     * этого карточка точки продолжала висеть под окном брифинга. Учёт по
     * владельцам, а не флагом: экранов поверх HUD может быть несколько
     * (брифинг → настройки), и первый же закрывшийся вернул бы HUD слишком рано.
     */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void SetGameLayerHidden(const UWidget* Owner, bool bHidden);

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

    /**
     * Общий фон под всеми стеками. BindWidgetOptional: лейаут без него остаётся
     * рабочим, экраны просто окажутся без фона.
     */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI")
    TObjectPtr<UImage> Img_ScreenBackdrop;

private:
    /** Возвращает стек, соответствующий слою. */
    UCommonActivatableWidgetStack* GetStackForLayer(EUILayer Layer) const;

    /** Экран, который поставил текущий фон. */
    TWeakObjectPtr<const UWidget> BackdropOwner;

    /** Экраны, требующие спрятать игровой слой; пуст — HUD виден. */
    TArray<TWeakObjectPtr<const UWidget>> GameLayerHiders;

    /** Применяет видимость игрового слоя по текущему списку (чистит мёртвых). */
    void RefreshGameLayerVisibility();
};
