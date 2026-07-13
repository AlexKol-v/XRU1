#include "ShieldIconWidget.h"

#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "TDAttributeSet.h"

void UShieldIconWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (!IconImage && WidgetTree)
    {
        IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Icon"));
        WidgetTree->RootWidget = IconImage;
    }

    if (IconImage)
    {
        IconImage->SetColorAndOpacity(BaseColor);
        if (IconTexture)
        {
            IconImage->SetBrushFromTexture(IconTexture);
        }
    }
}

void UShieldIconWidget::ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor)
{
    Super::ApplyStyle(InSize, InColor);

    if (IconImage)
    {
        IconImage->SetColorAndOpacity(InColor);
    }
}

void UShieldIconWidget::BindDelegates()
{
    UAbilitySystemComponent* C = ASC.Get();
    if (!C) return;

    ShieldHandle = C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetShieldAttribute())
                      .AddUObject(this, &UShieldIconWidget::OnShieldChanged);
}

void UShieldIconWidget::UnbindDelegates()
{
    UAbilitySystemComponent* C = ASC.Get();
    if (!C) return;

    C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetShieldAttribute()).Remove(ShieldHandle);
}

void UShieldIconWidget::RefreshFromASC() { Redraw(); }
void UShieldIconWidget::OnShieldChanged(const FOnAttributeChangeData& /*D*/) { Redraw(); }

void UShieldIconWidget::Redraw()
{
    UAbilitySystemComponent* C = ASC.Get();
    const float ShieldValue = C ? C->GetNumericAttribute(UTDAttributeSet::GetShieldAttribute()) : 0.f;
    SetVisibility(ShieldValue > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}
