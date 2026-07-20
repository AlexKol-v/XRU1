#pragma once

#include "CoreMinimal.h"
#include "UnitAttributeWidget.h"
#include "CoverTypes.h"
#include "CoverIconWidget.generated.h"

class UImage;
class UCoverDetectionComponent;

/**
 * Иконка укрытия для HUD'а над головой юнита: полущит при half cover,
 * полный щит при full cover, скрыта на открытой позиции.
 *
 * Источник данных — UCoverDetectionComponent юнита (через аватара ASC),
 * подписка на OnCoverStateChanged (стреляет после каждого перемещения).
 * Текстуры задаются в BP-наследнике; без них рисуется квадрат BaseColor.
 */
UCLASS(BlueprintType)
class XRU1_API UCoverIconWidget : public UUnitAttributeWidget
{
    GENERATED_BODY()

public:
    virtual void ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor) override;

protected:
    virtual void NativeOnInitialized() override;
    virtual void BindDelegates() override;
    virtual void UnbindDelegates() override;
    virtual void RefreshFromASC() override;

    /** Иконка половинчатого укрытия (полущит). */
    UPROPERTY(EditDefaultsOnly, Category = "Cover Icon")
    TObjectPtr<UTexture2D> HalfCoverTexture;

    /** Иконка полного укрытия (полный щит). */
    UPROPERTY(EditDefaultsOnly, Category = "Cover Icon")
    TObjectPtr<UTexture2D> FullCoverTexture;

private:
    UFUNCTION()
    void OnCoverStateChanged(ECoverType NewBestCover);

    void Redraw();

    UPROPERTY(Transient)
    TObjectPtr<UImage> IconImage;

    /** Детектор укрытий юнита-владельца (weak: юнит может умереть раньше виджета). */
    TWeakObjectPtr<UCoverDetectionComponent> CoverDetection;
};
