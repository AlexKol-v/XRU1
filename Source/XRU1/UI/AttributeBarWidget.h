#pragma once

#include "CoreMinimal.h"
#include "UnitAttributeWidget.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "AttributeBarWidget.generated.h"

class UHorizontalBox;
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

    /**
     * Секционный режим: поверх заливки рисуется решётка из разделителей, и
     * полоса читается как набор ячеек по UnitsPerSegment HP (как в XCOM:
     * Chimera Squad) — количество оставшихся секций видно с одного взгляда.
     * Сам ProgressBar и вся логика процентов/цветов не меняются.
     */
    UPROPERTY(EditAnywhere, Category = "Style|Segments")
    bool bSegmented = false;

    /** Сколько единиц атрибута приходится на одну секцию. */
    UPROPERTY(EditAnywhere, Category = "Style|Segments",
              meta = (EditCondition = "bSegmented", ClampMin = "1"))
    float UnitsPerSegment = 10.f;

    /** Толщина разделителя между секциями, px. */
    UPROPERTY(EditAnywhere, Category = "Style|Segments",
              meta = (EditCondition = "bSegmented", ClampMin = "1"))
    float SegmentSeparatorThickness = 2.f;

    /** Цвет разделителей (тёмный «вырез» в заливке). */
    UPROPERTY(EditAnywhere, Category = "Style|Segments", meta = (EditCondition = "bSegmented"))
    FLinearColor SegmentSeparatorColor = FLinearColor(0.f, 0.f, 0.f, 0.85f);

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

    /** Решётка разделителей секций (между Bar и PercentText в Overlay). */
    UPROPERTY(Transient)
    TObjectPtr<UHorizontalBox> SegmentTicks;

private:
    /** Перестраивает решётку при смене количества секций (MaxValue изменился). */
    void RebuildSegments(float MaxValue);

    /** Сколько секций построено сейчас (-1 — ещё не строилась). */
    int32 BuiltSegmentCount = -1;
};
