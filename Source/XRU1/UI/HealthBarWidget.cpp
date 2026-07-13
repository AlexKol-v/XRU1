#include "HealthBarWidget.h"

#include "AbilitySystemComponent.h"
#include "TDAttributeSet.h"

UHealthBarWidget::UHealthBarWidget()
{
    BaseColor = FLinearColor::Red;
}

void UHealthBarWidget::BindDelegates()
{
    UAbilitySystemComponent* C = ASC.Get();
    if (!C) return;

    HealthHandle = C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetHealthAttribute())
                       .AddUObject(this, &UHealthBarWidget::OnHealthChanged);
    MaxHealthHandle = C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetMaxHealthAttribute())
                       .AddUObject(this, &UHealthBarWidget::OnMaxHealthChanged);
    ShieldHandle = C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetShieldAttribute())
                       .AddUObject(this, &UHealthBarWidget::OnShieldChanged);
}

void UHealthBarWidget::UnbindDelegates()
{
    UAbilitySystemComponent* C = ASC.Get();
    if (!C) return;

    C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetHealthAttribute()).Remove(HealthHandle);
    C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthHandle);
    C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetShieldAttribute()).Remove(ShieldHandle);
}

void UHealthBarWidget::RefreshFromASC()
{
    RedrawFromAttributes();
}

void UHealthBarWidget::OnHealthChanged(const FOnAttributeChangeData& /*Data*/)    { RedrawFromAttributes(); }
void UHealthBarWidget::OnMaxHealthChanged(const FOnAttributeChangeData& /*Data*/) { RedrawFromAttributes(); }
void UHealthBarWidget::OnShieldChanged(const FOnAttributeChangeData& /*Data*/)    { RedrawFromAttributes(); }

void UHealthBarWidget::RedrawFromAttributes()
{
    UAbilitySystemComponent* C = ASC.Get();
    if (!C) return;

    const float Health    = C->GetNumericAttribute(UTDAttributeSet::GetHealthAttribute());
    const float MaxHealth = C->GetNumericAttribute(UTDAttributeSet::GetMaxHealthAttribute());
    const float Shield    = C->GetNumericAttribute(UTDAttributeSet::GetShieldAttribute());

    const FLinearColor Tint = (Shield > 0.f) ? ShieldActiveTint : BaseColor;
    UpdateBarVisual(Health, MaxHealth, Tint);
}
