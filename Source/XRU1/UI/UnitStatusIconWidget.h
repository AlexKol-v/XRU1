#pragma once

#include "CoreMinimal.h"
#include "UnitAttributeWidget.h"
#include "UnitStatusIconWidget.generated.h"

class AUnitBase;
class UBorder;
class UImage;
class UTacticalHUDStyleData;

/**
 * Главный тактический статус в HUD над головой юнита.
 *
 * Использует тот же резолвер общей UITheme, что и карточка выбранного бойца:
 * Downed → Evacuated → Taunt → HunkerDown → Overwatch → Moving.
 * Это намеренно отдельный badge рядом с укрытием: cover и gameplay-status
 * сообщают разные вещи и не должны вытеснять друг друга.
 */
UCLASS(BlueprintType)
class XRU1_API UUnitStatusIconWidget : public UUnitAttributeWidget
{
	GENERATED_BODY()

public:
	virtual void ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor) override;

protected:
	virtual void NativeOnInitialized() override;
	virtual void BindDelegates() override;
	virtual void UnbindDelegates() override;
	virtual void RefreshFromASC() override;

private:
	UFUNCTION()
	void OnUnitStateChanged();

	void Redraw();
	const UTacticalHUDStyleData* GetUITheme() const;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> BadgeBackground;

	UPROPERTY(Transient)
	TObjectPtr<UImage> IconImage;

	TWeakObjectPtr<AUnitBase> Unit;
	bool bWarnedAboutUndersizedSlot = false;
};
