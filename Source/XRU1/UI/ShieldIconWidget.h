#pragma once

#include "CoreMinimal.h"
#include "UnitAttributeWidget.h"
#include "ShieldIconWidget.generated.h"

class UImage;
struct FOnAttributeChangeData;

/**
 * CommonUI-виджет иконки щита. Видим, когда атрибут Shield > 0; иначе скрыт.
 * Текстура задаётся в BP-наследнике через IconTexture.
 */
UCLASS(BlueprintType)
class XRU1_API UShieldIconWidget : public UUnitAttributeWidget
{
    GENERATED_BODY()

public:
    virtual void ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor) override;

protected:
    virtual void NativeOnInitialized() override;
    virtual void BindDelegates() override;
    virtual void UnbindDelegates() override;
    virtual void RefreshFromASC() override;

    /** Текстура иконки. Если не задана — виджет всё равно работает, но виден как пустой прямоугольник BaseColor. */
    UPROPERTY(EditDefaultsOnly, Category = "Shield Icon")
    TObjectPtr<UTexture2D> IconTexture;

private:
    void OnShieldChanged(const FOnAttributeChangeData& Data);
    void Redraw();

    UPROPERTY(Transient)
    TObjectPtr<UImage> IconImage;

    FDelegateHandle ShieldHandle;
};
