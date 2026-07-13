#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateWidgetStyleContainerBase.h"
#include "Styling/SlateTypes.h"
#include "ProgressBarWidgetStyle.generated.h"

/**
 * Slate Widget Style Asset для UProgressBar. Тонкая UCLASS-обёртка над
 * FProgressBarStyle (BackgroundImage / FillImage / MarqueeImage) — нужна,
 * чтобы фабрика «Slate Widget Style» в Content Browser показала этот тип
 * в пикере. Создание в редакторе:
 *   Right-click → User Interface → Slate Widget Style → ProgressBarWidgetStyle.
 *
 * Контракт USlateWidgetStyleContainerBase — переопределить GetStyle(),
 * который возвращает указатель на лежащий внутри FSlateWidgetStyle.
 *
 * Применяется через UAttributeBarWidget::BarStyleAsset: значение из контейнера
 * копируется в UProgressBar::WidgetStyle до первого RebuildWidget,
 * после чего SProgressBar читает его при построении.
 */
UCLASS(hidecategories = Object, MinimalAPI, BlueprintType)
class UProgressBarWidgetStyle : public USlateWidgetStyleContainerBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Appearance",
              meta = (ShowOnlyInnerProperties))
    FProgressBarStyle ProgressBarStyle;

    virtual const FSlateWidgetStyle* const GetStyle() const override
    {
        return &ProgressBarStyle;
    }
};
