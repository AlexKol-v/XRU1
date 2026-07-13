#include "UnitAttributeWidget.h"

#include "AbilitySystemComponent.h"

void UUnitAttributeWidget::InitFromASC(UAbilitySystemComponent* InASC)
{
    UnbindDelegates();
    ASC = InASC;
    if (!ASC.IsValid())
    {
        return;
    }
    BindDelegates();
    RefreshFromASC();
}

void UUnitAttributeWidget::ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor)
{
    SlotSize  = InSize;
    BaseColor = InColor;
}

void UUnitAttributeWidget::NativeDestruct()
{
    UnbindDelegates();
    Super::NativeDestruct();
}
