#pragma once

#include "CoreMinimal.h"
#include "AttributeBarWidget.h"
#include "MoveSpeedBarWidget.generated.h"

struct FOnAttributeChangeData;

/**
 * CommonUI-виджет шкалы фактической скорости персонажа (спидометр).
 *
 * Источник числителя — `CharacterMovementComponent::Velocity.Size2D()` аватара
 * ASC (читается из NativeTick каждый кадр). Знаменатель — атрибут
 * `MaxMoveSpeed` (потолок без модификаций). Такой выбор знаменателя нужен,
 * чтобы slow-дебафф через GE (`MoveSpeed` ← меньше) визуально оставлял зазор
 * до 100% даже при максимальном беге.
 *
 * Почему не атрибут `MoveSpeed`? Этот атрибут отражает «cap» (используется в
 * UTDAttributeSet::PostGameplayEffectExecute для проброса в
 * MaxWalkSpeed), а не реальную мгновенную скорость. Реальная скорость —
 * рантайм-показатель, для него не нужен GAS-атрибут.
 */
UCLASS(BlueprintType)
class XRU1_API UMoveSpeedBarWidget : public UAttributeBarWidget
{
    GENERATED_BODY()

public:
    UMoveSpeedBarWidget();

protected:
    virtual void BindDelegates() override;
    virtual void UnbindDelegates() override;
    virtual void RefreshFromASC() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void OnMaxMoveSpeedChanged(const FOnAttributeChangeData& Data);

    /** Считывает скорость с CMC и MaxMoveSpeed с ASC, обновляет полосу. */
    void Redraw();

    FDelegateHandle MaxMoveSpeedHandle;
};
