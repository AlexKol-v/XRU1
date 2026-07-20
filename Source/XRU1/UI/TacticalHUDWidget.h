#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "TacticsTypes.h"
#include "CoverTypes.h"
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

	/**
	 * Укрытие цели ПРОТИВ выбранного стрелка (None — открыт или фланкирован).
	 * Для иконки щита в панели цели, как в XCOM 2. None и при пустом выборе.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	ECoverType GetTargetCoverAgainstSelected(AActor* Target) const;

	/** Живые враги на поле (счётчик в HUD). Обновлять по OnUnitsStateChanged. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	int32 GetAliveEnemyCount() const;

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

	/** Смена юнита под курсором (панель цели: HP + «Попадание: N%»; nullptr — спрятать). */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnHoveredUnitChanged(AUnitBase* Hovered);

	/**
	 * Любой юнит боя сменил состояние (смерть/ранение/подъём/эвакуация).
	 * Обновить портреты и счётчик врагов. Вызывается и один раз при старте.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnUnitsStateChanged();

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

	UFUNCTION()
	void HandleHoveredUnitChanged(AUnitBase* Hovered);

	UFUNCTION()
	void HandleUnitStateChanged();

	/**
	 * Подписка на OnUnitStateChanged всех юнитов боя. Идемпотентна (уже
	 * подписанные пропускаются) и зовётся на каждой смене фазы: HUD мог быть
	 * создан до StartCombat, а юниты — добавлены в бой позже (подкрепления).
	 */
	void SubscribeToUnitStates();

	/** Юниты обеих сторон, на чей OnUnitStateChanged мы подписаны (для отписки). */
	TArray<TWeakObjectPtr<AUnitBase>> StateSubscribedUnits;
};
