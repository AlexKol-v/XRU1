#include "AttributeBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"

#define LOCTEXT_NAMESPACE "AttributeBarWidget"

UAttributeBarWidget::UAttributeBarWidget()
{
    PercentFormat = LOCTEXT("PercentFormat_Default", "{Percent}%");
    PercentFont   = FCoreStyle::GetDefaultFontStyle("Bold", 10);
    PercentColor  = FSlateColor(FLinearColor::White);
}

void UAttributeBarWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    // Если BP-наследник уже задал корневой виджет, не пересобираем дерево —
    // студент мог сделать кастомную раскладку в Designer'е.
    if (Root || !WidgetTree)
    {
        return;
    }

    Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
    WidgetTree->RootWidget = Root;

    Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar"));
    // Стиль должен встать ДО первого RebuildWidget — иначе SProgressBar
    // уже кэширует прежний и без protected SynchronizeProperties() его
    // безболезненно не обновить.
    //
    // USlateWidgetStyleAsset — движковый wrapper-uasset; внутри CustomStyle
    // лежит UProgressBarWidgetStyle. GetStyle<FProgressBarStyle>() вернёт
    // nullptr, если CustomStyle пустой или содержит контейнер другого типа
    // (например, кнопочный) — в этом случае молча оставляем дефолт.
    if (BarStyleAsset)
    {
        if (const FProgressBarStyle* Style = BarStyleAsset->GetStyle<FProgressBarStyle>())
        {
            Bar->SetWidgetStyle(*Style);
        }
    }
    if (UOverlaySlot* BarSlot = Root->AddChildToOverlay(Bar))
    {
        BarSlot->SetHorizontalAlignment(HAlign_Fill);
        BarSlot->SetVerticalAlignment(VAlign_Fill);
    }

    // Решётка секций лежит над заливкой, но под текстом процента.
    if (bSegmented)
    {
        SegmentTicks = WidgetTree->ConstructWidget<UHorizontalBox>(
            UHorizontalBox::StaticClass(), TEXT("SegmentTicks"));
        SegmentTicks->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (UOverlaySlot* TicksSlot = Root->AddChildToOverlay(SegmentTicks))
        {
            TicksSlot->SetHorizontalAlignment(HAlign_Fill);
            TicksSlot->SetVerticalAlignment(VAlign_Fill);
        }
    }

    PercentText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Percent"));
    PercentText->SetFont(PercentFont);
    PercentText->SetColorAndOpacity(PercentColor);
    PercentText->SetVisibility(bShowPercent ? ESlateVisibility::HitTestInvisible
                                            : ESlateVisibility::Collapsed);
    if (UOverlaySlot* TextSlot = Root->AddChildToOverlay(PercentText))
    {
        TextSlot->SetHorizontalAlignment(HAlign_Center);
        TextSlot->SetVerticalAlignment(VAlign_Center);
    }
}

void UAttributeBarWidget::ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor)
{
    Super::ApplyStyle(InSize, InColor);

    if (Bar)
    {
        Bar->SetFillColorAndOpacity(InColor);
    }
}

void UAttributeBarWidget::UpdateBarVisual(float Value, float MaxValue, const FLinearColor& Tint)
{
    if (!Bar)
    {
        return;
    }
    const float Percent = (MaxValue > KINDA_SMALL_NUMBER)
        ? FMath::Clamp(Value / MaxValue, 0.f, 1.f)
        : 0.f;
    Bar->SetPercent(Percent);
    Bar->SetFillColorAndOpacity(Tint);

    if (bSegmented)
    {
        RebuildSegments(MaxValue);
    }

    if (bShowPercent && PercentText)
    {
        FFormatNamedArguments Args;
        Args.Add(TEXT("Percent"), FMath::RoundToInt(Percent * 100.f));
        Args.Add(TEXT("Value"),   FMath::FloorToInt(Value));
        Args.Add(TEXT("Max"),     FMath::FloorToInt(MaxValue));
        PercentText->SetText(FText::Format(PercentFormat, Args));
    }
}

void UAttributeBarWidget::RebuildSegments(float MaxValue)
{
    if (!SegmentTicks || !WidgetTree || UnitsPerSegment <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    // Количество секций от MaxHealth: 100 HP при 10 HP/секцию = 10 ячеек.
    const int32 SegmentCount = FMath::Clamp(FMath::RoundToInt(MaxValue / UnitsPerSegment), 1, 50);
    if (SegmentCount == BuiltSegmentCount)
    {
        return;
    }
    BuiltSegmentCount = SegmentCount;
    SegmentTicks->ClearChildren();

    // Одна секция — решётка не нужна вовсе.
    if (SegmentCount < 2)
    {
        return;
    }

    for (int32 Index = 0; Index < SegmentCount; ++Index)
    {
        USpacer* Cell = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
        if (UHorizontalBoxSlot* CellSlot = SegmentTicks->AddChildToHorizontalBox(Cell))
        {
            CellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }

        if (Index < SegmentCount - 1)
        {
            USizeBox* TickFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
            TickFrame->SetWidthOverride(SegmentSeparatorThickness);
            UBorder* Tick = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
            Tick->SetBrushColor(SegmentSeparatorColor);
            Tick->SetPadding(FMargin(0.f));
            TickFrame->AddChild(Tick);
            if (UHorizontalBoxSlot* TickSlot = SegmentTicks->AddChildToHorizontalBox(TickFrame))
            {
                TickSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                TickSlot->SetVerticalAlignment(VAlign_Fill);
            }
        }
    }
}

#undef LOCTEXT_NAMESPACE
