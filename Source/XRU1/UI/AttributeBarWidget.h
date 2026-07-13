#pragma once

#include "CoreMinimal.h"
#include "UnitAttributeWidget.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "AttributeBarWidget.generated.h"

class UOverlay;
class UProgressBar;
class UTextBlock;
class USlateWidgetStyleAsset;

/**
 * Промежуточный класс «полоса-бар». Конструирует дерево виджетов
 *   UOverlay (Root)
 *     ├── UProgressBar (Bar)         — Fill/Fill, рисует прогресс
 *     └── UTextBlock   (PercentText) — Center/Center, опциональный оверлей процента
 * прямо в C++ (без необходимости в WBP-наследнике).
 *
 * Стиль полосы (фон + заливка, «плоский» вид и т.п.) приходит из Slate Widget
 * Style Asset через BarStyleAsset. Базовый Tint (FillColorAndOpacity) приходит
 * из FUnitHUDWidgetSlot::Color через ApplyStyle() — он умножается поверх FillImage.
 *
 * Наследники (UHealthBarWidget, UMoveSpeedBarWidget) переопределяют
 * BindDelegates / RefreshFromASC — здесь только рисующая часть.
 */
UCLASS(BlueprintType)
class XRU1_API UAttributeBarWidget : public UUnitAttributeWidget
{
    GENERATED_BODY()

public:
    UAttributeBarWidget();

    virtual void ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor) override;

protected:
    virtual void NativeOnInitialized() override;

    /** Обновляет полосу: percent = clamp(Value/Max), цвет = Tint,
     *  текст PercentText (если bShowPercent) — FText::Format по PercentFormat. */
    void UpdateBarVisual(float Value, float MaxValue, const FLinearColor& Tint);

    /** Slate-стиль полосы (BackgroundImage / FillImage). Ассет создаётся
     *  как USlateWidgetStyleAsset (фабрика «Slate Widget Style» в Content
     *  Browser), CustomStyle внутри — наш UProgressBarWidgetStyle. Если
     *  nullptr или не того типа — Bar остаётся с дефолтным стилем. */
    UPROPERTY(EditAnywhere, Category = "Style")
    TObjectPtr<USlateWidgetStyleAsset> BarStyleAsset;

    /** Показывать ли текст процента поверх бара. */
    UPROPERTY(EditAnywhere, Category = "Style|Percent")
    bool bShowPercent = false;

    /** Формат текста процента. Поддерживаемые именованные аргументы
     *  для FText::Format: {Percent} (0..100, int), {Value}, {Max}.
     *  По умолчанию — "{Percent}%". */
    UPROPERTY(EditAnywhere, Category = "Style|Percent",
              meta = (EditCondition = "bShowPercent"))
    FText PercentFormat;

    UPROPERTY(EditAnywhere, Category = "Style|Percent",
              meta = (EditCondition = "bShowPercent"))
    FSlateFontInfo PercentFont;

    UPROPERTY(EditAnywhere, Category = "Style|Percent",
              meta = (EditCondition = "bShowPercent"))
    FSlateColor PercentColor;

    UPROPERTY(Transient)
    TObjectPtr<UOverlay> Root;

    UPROPERTY(Transient)
    TObjectPtr<UProgressBar> Bar;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> PercentText;
};
