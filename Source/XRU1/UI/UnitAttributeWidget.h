#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "UnitAttributeWidget.generated.h"

class UAbilitySystemComponent;

/**
 * Базовый CommonUI-виджет одного атрибута юнита. Знает только две вещи:
 *  1) как «подцепиться» к UAbilitySystemComponent владельца (одна точка входа);
 *  2) как применить заданные дизайнером Size/Color из DataAsset'а.
 *
 * Все конкретные виджеты (Health-бар, MoveSpeed-бар, Shield-иконка) наследуют
 * этот класс и переопределяют:
 *  - BindDelegates / UnbindDelegates — что слушать в ASC;
 *  - ApplyStyle — как именно применить размер и базовый цвет к собственной разметке.
 *
 * Базируемся на UCommonUserWidget вместо UUserWidget: получаем входной маршрутизатор
 * CommonUI «бесплатно», и широко применяемые CommonUI-стили будут работать в подвиджетах.
 */
UCLASS(Abstract, BlueprintType)
class XRU1_API UUnitAttributeWidget : public UCommonUserWidget
{
    GENERATED_BODY()

public:
    /** Привязывает виджет к ASC и инициирует первое обновление. Безопасно вызывать повторно. */
    UFUNCTION(BlueprintCallable, Category = "UI|Unit HUD")
    void InitFromASC(UAbilitySystemComponent* InASC);

    /** Применяет заданные дизайнером Size/Color из DataAsset'а. Базовая реализация запоминает их. */
    virtual void ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor);

protected:
    virtual void NativeDestruct() override;

    /** Подписывается на изменения нужных атрибутов. Переопределяется наследниками. */
    virtual void BindDelegates() {}

    /** Снимает подписки. Переопределяется наследниками. */
    virtual void UnbindDelegates() {}

    /** Первичная синхронизация виджета с текущими значениями атрибутов. */
    virtual void RefreshFromASC() {}

    UPROPERTY(Transient)
    TWeakObjectPtr<UAbilitySystemComponent> ASC;

    /** Применённый размер слота (для логирования и наследников). */
    UPROPERTY(Transient)
    FVector2D SlotSize = FVector2D(120.f, 16.f);

    /** Применённый базовый цвет слота. */
    UPROPERTY(Transient)
    FLinearColor BaseColor = FLinearColor::White;
};
