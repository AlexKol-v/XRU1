#include "APPipsWidget.h"

#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"

#include "ActionPointsComponent.h"

void UAPPipsWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (!Row && WidgetTree)
    {
        Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PipsRow"));
        WidgetTree->RootWidget = Row;
    }
}

void UAPPipsWidget::ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor)
{
    Super::ApplyStyle(InSize, InColor);
    Redraw();
}

void UAPPipsWidget::BindDelegates()
{
    AActor* Avatar = ResolveAvatarActor();
    if (!Avatar) return;

    ActionPoints = Avatar->FindComponentByClass<UActionPointsComponent>();
    if (ActionPoints.IsValid())
    {
        ActionPoints->OnActionPointsChanged.AddDynamic(this, &UAPPipsWidget::OnActionPointsChanged);
    }
}

void UAPPipsWidget::UnbindDelegates()
{
    if (UActionPointsComponent* AP = ActionPoints.Get())
    {
        AP->OnActionPointsChanged.RemoveDynamic(this, &UAPPipsWidget::OnActionPointsChanged);
    }
    ActionPoints = nullptr;
}

void UAPPipsWidget::RefreshFromASC() { Redraw(); }

void UAPPipsWidget::OnActionPointsChanged(int32 /*NewCurrent*/, int32 /*Max*/) { Redraw(); }

void UAPPipsWidget::Redraw()
{
    const UActionPointsComponent* AP = ActionPoints.Get();
    if (!AP || !Row)
    {
        return;
    }

    // «Рывок и удар» может выдать AP сверх максимума — рисуем все фактические.
    const int32 PipCount = FMath::Max(AP->MaxActionPoints, AP->CurrentActionPoints);
    if (Pips.Num() != PipCount)
    {
        RebuildPips(PipCount);
    }

    for (int32 i = 0; i < Pips.Num(); ++i)
    {
        Pips[i]->SetColorAndOpacity(i < AP->CurrentActionPoints ? BaseColor : SpentColor);
    }
}

void UAPPipsWidget::RebuildPips(int32 Count)
{
    if (!Row || !WidgetTree)
    {
        return;
    }

    Row->ClearChildren();
    Pips.Reset();

    for (int32 i = 0; i < Count; ++i)
    {
        UImage* Pip = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
        UHorizontalBoxSlot* PipSlot = Row->AddChildToHorizontalBox(Pip);
        PipSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        PipSlot->SetPadding(FMargin(i > 0 ? PipSpacing : 0.f, 0.f, 0.f, 0.f));
        Pips.Add(Pip);
    }
}
