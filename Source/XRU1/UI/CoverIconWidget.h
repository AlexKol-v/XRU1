#pragma once

#include "CoreMinimal.h"
#include "UnitAttributeWidget.h"
#include "CoverTypes.h"
#include "CoverIconWidget.generated.h"

class UImage;
class UCoverDetectionComponent;
class UTacticalHUDStyleData;

/**
 * Иконка укрытия для HUD'а над головой юнита: полущит при half cover,
 * полный щит при full cover, скрыта на открытой позиции.
 *
 * Источник данных — UCoverDetectionComponent юнита (через аватара ASC),
 * подписка на OnCoverStateChanged (стреляет после каждого перемещения).
 * Текстуры и размер сначала берутся из общей UITheme GameInstance; локальные
 * поля BP остаются fallback для автономного превью/старых ассетов.
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

    /**
     * Пока игрок целится, показывать укрытие ПРОТИВ ВЫБРАННОГО СТРЕЛКА, а не
     * «по кругу»: синий щит — укрытие работает против него, жёлтый — цель
     * флангирована. Вне прицеливания иконка возвращается к локальному
     * BestCoverAround — это статус самого юнита, он не зависит ни от кого.
     *
     * Выключить = всегда показывать локальное укрытие.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Cover Icon")
    bool bShowFlankedWhileTargeting = true;

    /**
     * Стрелок меняется редко (выбор юнита, вход/выход из прицеливания), но
     * узнать об этом можно только опросом — делегата «сменился режим наведения»
     * у контроллера нет. Поэтому тик, но с проверкой изменения: пересчёт (а он
     * делает трейс укрытия) идёт ТОЛЬКО когда стрелок реально сменился.
     */
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UFUNCTION()
    void OnCoverStateChanged(ECoverType NewBestCover);

    void Redraw();
    const UTacticalHUDStyleData* GetUITheme() const;

    /** Стрелок, против которого считать щит; nullptr — показывать локальное укрытие. */
    AActor* ResolveActiveShooter() const;

    UPROPERTY(Transient)
    TObjectPtr<UImage> IconImage;

    /** Детектор укрытий юнита-владельца (weak: юнит может умереть раньше виджета). */
    TWeakObjectPtr<UCoverDetectionComponent> CoverDetection;

    /** Стрелок на момент последней перерисовки — чтобы не считать трейс каждый кадр. */
    TWeakObjectPtr<AActor> LastShooter;
};
