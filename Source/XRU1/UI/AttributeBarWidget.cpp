#include "AttributeBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
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

    if (bShowPercent && PercentText)
    {
        FFormatNamedArguments Args;
        Args.Add(TEXT("Percent"), FMath::RoundToInt(Percent * 100.f));
        Args.Add(TEXT("Value"),   FMath::FloorToInt(Value));
        Args.Add(TEXT("Max"),     FMath::FloorToInt(MaxValue));
        PercentText->SetText(FText::Format(PercentFormat, Args));
    }
}

#undef LOCTEXT_NAMESPACE
