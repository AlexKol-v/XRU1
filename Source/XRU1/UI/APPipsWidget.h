#pragma once

#include "CoreMinimal.h"
#include "UnitAttributeWidget.h"
#include "APPipsWidget.generated.h"

class UHorizontalBox;
class UImage;
class UActionPointsComponent;

/**
 * Пипсы Action Points для HUD'а над головой юнита. Ряд квадратов-пипсов:
 * оставшиеся AP — базовым цветом слота (из DataAsset'а), потраченные — SpentColor.
 *
 * В отличие от баров атрибутов, источник данных — не ASC-атрибут, а
 * UActionPointsComponent юнита: компонент достаётся через аватара ASC
 * (ASC->GetAvatarActor), подписка — на его OnActionPointsChanged.
 * При GrantExtraPoints («Рывок и удар») пипсов может стать больше максимума —
 * ряд пересобирается под фактическое число.
 */
UCLASS(BlueprintType)
class XRU1_API UAPPipsWidget : public UUnitAttributeWidget
{
    GENERATED_BODY()

public:
    virtual void ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor) override;

protected:
    virtual void NativeOnInitialized() override;
    virtual void BindDelegates() override;
    virtual void UnbindDelegates() override;
    virtual void RefreshFromASC() override;

    /** Цвет потраченного пипса (оставшиеся красятся базовым цветом слота). */
    UPROPERTY(EditDefaultsOnly, Category = "AP Pips")
    FLinearColor SpentColor = FLinearColor(0.12f, 0.12f, 0.12f, 0.8f);

    /** Зазор между пипсами, px. */
    UPROPERTY(EditDefaultsOnly, Category = "AP Pips", meta = (ClampMin = "0"))
    float PipSpacing = 3.f;

private:
    UFUNCTION()
    void OnActionPointsChanged(int32 NewCurrent, int32 Max);

    /** Перекрашивает (и при смене числа — пересобирает) пипсы по текущим AP. */
    void Redraw();

    /** Пересоздаёт ряд из Count пипсов. */
    void RebuildPips(int32 Count);

    UPROPERTY(Transient)
    TObjectPtr<UHorizontalBox> Row;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UImage>> Pips;

    /** Компонент AP юнита-владельца (weak: юнит может умереть раньше виджета). */
    TWeakObjectPtr<UActionPointsComponent> ActionPoints;
};
