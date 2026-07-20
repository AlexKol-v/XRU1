#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "TacticsTypes.h"
#include "CoverTypes.h"
#include "TacticalHUDWidget.generated.h"

class AUnitBase;
class ATacticalPlayerController;
class UTurnManagerSubsystem;
class UTacticalHUDStyleData;
class UActionPointsComponent;
class UButton;
class UImage;
class UTextBlock;
class UProgressBar;
class UHorizontalBox;
class UWidget;

/**
 * C++ база боевого HUD (GDD §9). Живёт на слое Game. Сама подписывается на
 * TurnManager и тактический контроллер; визуал (портреты, панель действий,
 * счётчики) — в WBP-наследнике через BP-хуки On*.
 *
 * Часть логики перенесена из WBP в C++ (аудит 2026-07-20):
 * - RefreshActionButtons — серость кнопок (BP-версия ловила Accessed None:
 *   AND в Blueprint не short-circuit, Pure-цепочки от невалидного S считались);
 * - UpdateTargetPanel — панель цели у курсора (имя/HP/шанс/щит укрытия);
 * - UpdateSquadCardVisibility — видна карточка только выбранного (XCOM-стиль);
 * - ApplyStyle — размеры/иконки из UTacticalHUDStyleData (DataAsset).
 * Виджеты приходят через BindWidgetOptional: имена должны совпадать с Designer,
 * отсутствующий виджет просто пропускается (HUD не падает на неполной разметке).
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

	/**
	 * Пересчитать доступность кнопок действий по выбранному юниту (AP, Downed,
	 * лимит способности, вид интеракции F). Зовётся из C++ по всем событиям
	 * (фаза/выбор/AP/статусы); из BP дергать не нужно.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void RefreshActionButtons();

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Виджеты из Designer (BindWidgetOptional: имена = имена в WBP).
	// BlueprintReadOnly обязателен: BP-граф читает часть из них (Get SquadPanel
	// в Construct, Get ActionsPanel/EndTurnBtn в OnPhaseChanged и т.д.).

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> AttackBtn;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> OverwatchBtn;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> HunkerBtn;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> AbilityBtn;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> InteractBtn;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> SkipBtn;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> EndTurnBtn;

	/** Картинка внутри InteractBtn (иконка меняется: обезвредить/эвакуация). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UImage> InteractIcon;

	/** Панель действий (низ-центр); контейнер портретов (низ-лево). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UHorizontalBox> ActionsPanel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UHorizontalBox> SquadPanel;

	// Панель цели у курсора (центр-право).
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UWidget> TargetPanel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TargetNameText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UImage> TargetCoverIcon;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> TargetHPBar;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HitChanceText;

	// --- Настройки (Class Defaults WBP-наследника) ------------------------------

	/** Единый стиль HUD: размеры и текстуры иконок (DA_TacticalHUDStyle). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD|Style")
	TObjectPtr<UTacticalHUDStyleData> Style;

	/**
	 * XCOM-стиль карточек отряда: видна только карточка выбранного бойца;
	 * без выбора показываются все (иначе мышью некого выбрать).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style")
	bool bShowOnlySelectedCard = true;

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

	/** Выбранный боец добежал: пересчитать серость кнопок и панель цели. */
	UFUNCTION()
	void HandleAvailableActionsChanged();

	/** Лимит ходов сняли/сменили посреди боя — перерисовать индикатор фазы с таймером. */
	UFUNCTION()
	void HandleTurnLimitChanged();

	/** Трата AP любым бойцом отряда — пересчитать серость кнопок. */
	UFUNCTION()
	void HandleSquadAPChanged(int32 NewCurrent, int32 Max);

	/** Кнопка «Огонь» (подписана из C++ — в BP её OnClicked НЕ добавлять). */
	UFUNCTION()
	void HandleAttackClicked();

	/**
	 * Подписка на OnUnitStateChanged всех юнитов боя (+ OnActionPointsChanged
	 * отряда). Идемпотентна (уже подписанные пропускаются) и зовётся на каждой
	 * смене фазы: HUD мог быть создан до StartCombat, а юниты — добавлены позже.
	 */
	void SubscribeToUnitStates();

	/** Применить Style (размеры/иконки) к виджетам. Работает и в превью Designer. */
	void ApplyStyle();

	/** Панель цели у курсора: показать по врагу при выбранном бойце, иначе спрятать. */
	void UpdateTargetPanel(AUnitBase* Hovered);

	/** XCOM-стиль: скрыть карточки всех, кроме выбранного (см. bShowOnlySelectedCard). */
	void UpdateSquadCardVisibility(AUnitBase* Selected);

	/** Юниты обеих сторон, на чей OnUnitStateChanged мы подписаны (для отписки). */
	TArray<TWeakObjectPtr<AUnitBase>> StateSubscribedUnits;

	/** AP-компоненты отряда, на чей OnActionPointsChanged мы подписаны (для отписки). */
	TArray<TWeakObjectPtr<UActionPointsComponent>> APSubscribedComponents;

	/** Предупреждение «в портрете нет переменной Unit» уже выдано (не спамим в лог). */
	bool bPortraitUnitLookupWarned = false;
};
