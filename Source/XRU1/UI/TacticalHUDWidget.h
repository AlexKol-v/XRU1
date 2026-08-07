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
class UCoverDetectionComponent;
class UButton;
class UBorder;
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
 * Логика, которая в шаблоне жила в WBP, вынесена в C++:
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

	/** Живые враги на поле (счётчик в HUD). Обновлять по OnUnitsStateChanged. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	int32 GetAliveEnemyCount() const;

	/** Видимые сейчас враги. Для боевого HUD с туманом использовать вместо полного AliveEnemyCount. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	int32 GetVisibleEnemyCount() const;

	/**
	 * Единая UI-тема: в runtime сначала UITheme из UTacticsGameInstance,
	 * локальный Style WBP — только fallback для Designer/ошибочной конфигурации.
	 * Даёт BP доступ к портретам, статусам и layout-блокам.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD|Style")
	UTacticalHUDStyleData* GetUITheme() const;

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

	/**
	 * Иконка рядом со счётчиком живых врагов. Brush и размер всегда задаёт
	 * общая UITheme; Designer-картинка служит только preview/fallback.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UImage> EnemyCountIcon;

	/**
	 * Border вокруг строки EnemyCountIcon + EnemyCountText. Texture, tint и
	 * padding всегда приходят из общей UITheme.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> EnemyCounterBackground;

	/**
	 * Баннер режима прицеливания (P0): «ВЫБЕРИТЕ ЦЕЛЬ · Tab — след. · ЛКМ/Огонь —
	 * подтвердить · ПКМ/Esc — отмена». Опциональный виджет в Designer; C++ лишь
	 * показывает/прячет его по режиму. Нет виджета — работает и без него.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UWidget> TargetingBanner;

	// Панель цели у курсора (центр-право).
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UWidget> TargetPanel;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TargetNameText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UImage> TargetCoverIcon;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> TargetHPBar;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HitChanceText;

	// --- Настройки (Class Defaults WBP-наследника) ------------------------------

	/**
	 * Необязательный локальный fallback общей темы. Оставьте тот же
	 * DA_TacticalHUDStyle для превью Designer либо очистите — корректно
	 * настроенный runtime всегда возьмёт UITheme из BP_TacticsGameInstance.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD|Style")
	TObjectPtr<UTacticalHUDStyleData> Style;

	/**
	 * XCOM-стиль карточек отряда: видна только карточка выбранного бойца;
	 * без выбора показываются все (иначе мышью некого выбрать).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style")
	bool bShowOnlySelectedCard = true;

	/**
	 * Класс карточки отряда для карточек, которые создаёт C++ (пересборка
	 * состава и карточка действующего врага). Пусто — класс берётся у первой
	 * карточки, построенной BP; но пока бой не начался, панель пуста и брать
	 * образец неоткуда, поэтому надёжнее указать WBP_UnitPortrait явно.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style")
	TSubclassOf<UUserWidget> PortraitCardClass;

	/**
	 * Как часто в фазу врага переоценивается видимость действующего врага для
	 * его карточки, сек. Враг активируется за укрытием и становится виден уже
	 * в движении — одной проверки в момент активации мало.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style", meta = (ClampMin = "0.05"))
	float EnemyCardVisibilityCheckInterval = 0.25f;

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
	 * Вход/выход в режим прицеливания (кнопка «Огонь»). Для BP-подачи: сменить
	 * курсор, подсветить цель иначе, анимировать баннер. C++ уже показал/спрятал
	 * TargetingBanner — это хук для доп. оформления.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnTargetingModeChanged(bool bTargeting);

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

	/** Состав боя изменился (staged-бойцы вошли/вышли) — пересобрать панель. */
	UFUNCTION()
	void HandleCombatUnitsChanged();

	/** Локальное укрытие юнита изменилось — обновить cover badge карточки. */
	UFUNCTION()
	void HandleUnitCoverStateChanged(ECoverType NewCover);

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
	 * TurnManager активировал врага в его фазу: показать карточку действующего
	 * врага на панели отряда (низ-лево), без возможности клика. Скрытые туманом
	 * враги карточку не получают (visibility gate docs/03_ARCHITECTURE.md §11).
	 */
	UFUNCTION()
	void HandleEnemyUnitActivated(AActor* Unit);

	/**
	 * После клика по ЛЮБОЙ кнопке HUD фокус клавиатуры возвращается игре.
	 * Без этого UMG-кнопка оставляет фокус себе, и пробел «нажимает» её снова —
	 * баг «пробел выполняет последнее нажатое действие» (завершить ход, глухую
	 * оборону…): это Slate повторял клик по сфокусированной кнопке, а не ввод игры.
	 */
	UFUNCTION()
	void HandleAnyButtonClickedResetFocus();

	/**
	 * Вешает HandleAnyButtonClickedResetFocus на все UButton виджета РЕКУРСИВНО
	 * (включая вложенные UserWidget'ы — портреты отряда). AddUniqueDynamic —
	 * повторные вызовы безопасны; зовётся на конструкции и после пересборки
	 * портретов (состав отряда мог перестроить кнопки).
	 */
	void EnsureButtonsDontStealFocus();

	/**
	 * Подписка на OnUnitStateChanged всех юнитов боя (+ OnActionPointsChanged
	 * отряда). Идемпотентна (уже подписанные пропускаются) и зовётся на каждой
	 * смене фазы: HUD мог быть создан до StartCombat, а юниты — добавлены позже.
	 */
	void SubscribeToUnitStates();

	/** Применить Style (размеры/иконки) к виджетам. Работает и в превью Designer. */
	void ApplyStyle();

	/** Применить layout вложенных карточек после их создания/пересборки BP. */
	void ApplyPortraitCardLayout();

	/** Обновить glyph классовой способности при смене выбранного бойца. */
	void RefreshAbilityIcon(AUnitBase* Selected);

	/** Панель цели у курсора: показать по врагу при выбранном бойце, иначе спрятать. */
	void UpdateTargetPanel(AUnitBase* Hovered);

	/** Баннер прицеливания: показать/спрятать по режиму «Огонь» + BP-хук. */
	void UpdateTargetingBanner();

	/** XCOM-стиль: скрыть карточки всех, кроме выбранного (см. bShowOnlySelectedCard). */
	void UpdateSquadCardVisibility(AUnitBase* Selected);

	/**
	 * Пересобрать карточки отряда под ТЕКУЩИЙ состав. BP строит панель один раз
	 * на Construct; staged-бойцы туториала входят в бой позже, и их карточки
	 * иначе не появились бы вовсе. Класс карточки берётся у уже созданной BP.
	 */
	void RebuildSquadPanel();

	/**
	 * Класс карточки отряда: явный PortraitCardClass, иначе кэш, иначе образец
	 * у уже построенной BP панели. Нужен и пересборке состава, и карточке врага.
	 */
	UClass* ResolvePortraitCardClass();

	/** Показывает карточку активного врага (создаётся из ResolvePortraitCardClass). */
	void ShowActiveEnemyCard(AUnitBase* EnemyUnit);

	/**
	 * Переоценка видимости действующего врага: показывает карточку, когда он
	 * вышел из тумана, и убирает её, когда снова скрылся. Зовётся по таймеру,
	 * пока идёт активация врага.
	 */
	UFUNCTION()
	void RefreshActiveEnemyCardVisibility();

	/** Убирает карточку врага и возвращает обычную видимость карточек отряда. */
	void ClearActiveEnemyCard();

	/** Снимает карточку врага с панели без сброса ActiveEnemyUnit (для ребилда). */
	void RemoveActiveEnemyCardFromPanel();

	/** Класс карточки отряда, увиденный у панели BP (образец для пересборки). */
	UPROPERTY(Transient)
	TObjectPtr<UClass> CachedPortraitClass;

	/** Живая карточка действующего врага в его фазу (nullptr вне фазы врага). */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ActiveEnemyCard;

	/** Враг, чью карточку показываем (для восстановления после ребилда панели). */
	TWeakObjectPtr<AUnitBase> ActiveEnemyUnit;

	/** Периодическая переоценка видимости действующего врага (фаза Enemy). */
	FTimerHandle EnemyCardVisibilityTimer;

	/** Юниты обеих сторон, на чей OnUnitStateChanged мы подписаны (для отписки). */
	TArray<TWeakObjectPtr<AUnitBase>> StateSubscribedUnits;

	/** Cover-компоненты, на чью смену локального укрытия подписана карточка. */
	TArray<TWeakObjectPtr<UCoverDetectionComponent>> CoverSubscribedComponents;

	/** AP-компоненты отряда, на чей OnActionPointsChanged мы подписаны (для отписки). */
	TArray<TWeakObjectPtr<UActionPointsComponent>> APSubscribedComponents;

	/** Предупреждение «в портрете нет переменной Unit» уже выдано (не спамим в лог). */
	bool bPortraitUnitLookupWarned = false;
};
