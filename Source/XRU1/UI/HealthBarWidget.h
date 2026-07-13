#pragma once

#include "CoreMinimal.h"
#include "AttributeBarWidget.h"
#include "HealthBarWidget.generated.h"

struct FOnAttributeChangeData;

/**
 * CommonUI-виджет шкалы здоровья. Слушает три атрибута через ASC:
 *  - Health     → процент шкалы;
 *  - MaxHealth  → знаменатель процента;
 *  - Shield     → если > 0, шкала перекрашивается в ShieldActiveTint (жёлтый).
 *
 * Базовый цвет (красный) приходит из DataAsset'а через ApplyStyle().
 */
UCLASS(BlueprintType)
class XRU1_API UHealthBarWidget : public UAttributeBarWidget
{
    GENERATED_BODY()

public:
    UHealthBarWidget();

protected:
    virtual void BindDelegates() override;
    virtual void UnbindDelegates() override;
    virtual void RefreshFromASC() override;

    /** Цвет шкалы здоровья, когда щит активен (Shield > 0). */
    UPROPERTY(EditDefaultsOnly, Category = "Health Bar")
    FLinearColor ShieldActiveTint = FLinearColor(1.0f, 0.85f, 0.f, 1.f); // жёлтый

private:
    void OnHealthChanged(const FOnAttributeChangeData& Data);
    void OnMaxHealthChanged(const FOnAttributeChangeData& Data);
    void OnShieldChanged(const FOnAttributeChangeData& Data);

    /** Полностью переотрисовывает шкалу, опрашивая текущие значения трёх атрибутов. */
    void RedrawFromAttributes();

    FDelegateHandle HealthHandle;
    FDelegateHandle MaxHealthHandle;
    FDelegateHandle ShieldHandle;
};
