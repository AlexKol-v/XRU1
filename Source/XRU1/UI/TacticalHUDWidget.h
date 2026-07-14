#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "TacticsTypes.h"
#include "TacticalHUDWidget.generated.h"

class AUnitBase;
class ATacticalPlayerController;
class UTurnManagerSubsystem;

/**
 * C++ база боевого HUD (GDD §9). Живёт на слое Game. Сама подписывается на
 * TurnManager и тактический контроллер; визуал (портреты, панель действий,
 * счётчики) — в WBP-наследнике через BP-хуки On*.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UTacticalHUDWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Тактический контроллер игрока (для кнопок панели действий). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	ATacticalPlayerController* GetTacticalController() const;

	/** Менеджер ходов (фаза, номер хода, таймер бомбы). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	UTurnManagerSubsystem* GetTurnManager() const;

	/** Отряд для портретов 1–4. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	TArray<AUnitBase*> GetSquad() const;

	/** Шанс попадания выбранного юнита по цели (для подсказки у курсора), -1 = нельзя. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	float GetHitChanceOnTarget(AActor* Target) const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- BP-хуки ---------------------------------------------------------------

	/** Смена фазы хода (обновить «ВАШ ХОД / ХОД ПРОТИВНИКА», номер, таймер). */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnPhaseChanged(ETurnPhase Phase, int32 TurnNumber, int32 TurnsRemaining);

	/** Смена выбранного юнита (рамка портрета, панель действий). */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnSelectedUnitChanged(AUnitBase* Selected);

	/** Бой окончен (спрятать панель действий). */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnCombatFinished(bool bPlayerWon);

private:
	UFUNCTION()
	void HandleTurnStarted(ETurnPhase Phase);

	UFUNCTION()
	void HandleCombatEnded(bool bPlayerWon);

	UFUNCTION()
	void HandleSelectedUnitChanged(AUnitBase* Selected);
};
