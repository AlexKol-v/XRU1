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

AActor* UUnitAttributeWidget::ResolveAvatarActor() const
{
    UAbilitySystemComponent* C = ASC.Get();
    if (!C)
    {
        return nullptr;
    }
    AActor* Avatar = C->GetAvatarActor();
    return Avatar ? Avatar : C->GetOwnerActor();
}

void UUnitAttributeWidget::NativeDestruct()
{
    UnbindDelegates();
    Super::NativeDestruct();
}
