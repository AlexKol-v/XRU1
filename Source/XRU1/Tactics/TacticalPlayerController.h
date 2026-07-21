#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TacticsTypes.h"
#include "TacticalPlayerController.generated.h"

class AUnitBase;
class ATacticalCameraPawn;
class AMoveRangeVisualizer;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UMenuScreenBase;
class UPrimaryGameLayout;
class ABombObjective;
class AEvacZone;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedUnitChanged, AUnitBase*, NewSelected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHoveredUnitChanged, AUnitBase*, NewHovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAvailableActionsChanged);

/**
 * Контроллер игрока в тактическом бою (GDD §11): выбор юнита (ЛКМ/1–4/Tab),
 * приказ перемещения (ПКМ, бюджет по длине пути навмеша), атака (ЛКМ по врагу
 * или кнопка «Огонь» → GameplayEvent Event.Attack), хоткеи действий
 * (Y/X/R/F/Backspace/Enter), камера (WASD/QE/колесо) и пауза (Esc). Ввод —
 * Enhanced Input (IMC задаётся в BP-наследнике). В фазу врага приказы
 * заблокированы.
 *
 * Также владеет визуализатором зоны хода (AMoveRangeVisualizer) и поднимает
 * корневой UI-layout через UGameUIManagerSubsystem (как ACSTPlayerController).
 */
UCLASS(Abstract)
class XRU1_API ATacticalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATacticalPlayerController();

	/** Текущий выбранный юнит (nullptr — никто). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	AUnitBase* GetSelectedUnit() const { return SelectedUnit; }

	/** Выбирает юнита (или снимает выбор при nullptr). Зовётся и из HUD (клик по портрету). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectUnit(AUnitBase* Unit);

	/** Выбор юнита отряда по слоту 1–4 (порядок — по списку стороны игрока). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectUnitBySlot(int32 SlotIndex);

	/** Следующий юнит отряда с оставшимися AP (Tab). Нет таких — выбор не меняется. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectNextUnit();

	/**
	 * Автовыбор бойца (XCOM: активный боец есть всегда): первый живой с AP,
	 * начиная со следующего за текущим (или с начала отряда, если выбора нет);
	 * нет никого с AP — любой живой; отряд выбит — выбор снимается.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectNextAvailableUnit();

	/** Юниты отряда (сторона игрока, живые) — для портретов HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	TArray<AUnitBase*> GetSquad() const;

	/** Кнопка/хоткей «Завершить ход». */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestEndTurn();

	/**
	 * Кнопка «Огонь» (GDD §6: атака доступна и кликом, и кнопкой): включает
	 * режим прицеливания — следующий ЛКМ по врагу стреляет. Прямой ЛКМ по
	 * врагу работает и без кнопки.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestAttack();

	/** Хоткей/кнопка Overwatch (Y). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestOverwatch();

	/** Хоткей/кнопка глухой обороны (X). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestHunkerDown();

	/** Хоткей/кнопка способности класса (R). Для медика включает режим выбора цели. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestClassAbility();

	/** Хоткей/кнопка «Пропустить ход» юнита (Backspace). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestSkipUnitTurn();

	/** Контекстное действие F: обезвредить бомбу рядом / эвакуация в зоне. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestInteract();

	/** Пауза (Esc): пуш экрана паузы на слой Menu. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestPause();

	/**
	 * Сообщить UI, что набор доступных действий мог измениться извне (открылась
	 * зона эвакуации, скрипт туториала сдвинул мир). Зовут GameMode и level BP —
	 * HUD пересчитает серость кнопок без ожидания следующего события боя.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void NotifyAvailableActionsChanged() { OnAvailableActionsChanged.Broadcast(); }

	/**
	 * Юнит закончил перемещение (зовёт AUnitAIController из OnMoveCompleted).
	 * Если это НЕ выбранный юнит — его диск занятости встал на новое место,
	 * зона хода выбранного пересчитывается немедленно.
	 */
	void NotifyUnitMoveFinished(AUnitBase* Unit);

	/**
	 * Сейчас фаза игрока и бой идёт — ЕДИНЫЙ признак «игрок может действовать».
	 * По нему гейтятся и приказы, и активность кнопок/панели прицеливания HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsPlayerPhase() const;

	/** В режиме ли выбора цели способности (медик ждёт клика по союзнику). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsTargetingAbility() const { return bAwaitingAbilityTarget; }

	/** В режиме ли прицеливания атаки (нажата кнопка «Огонь», ждём клика по врагу). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsTargetingAttack() const { return bAwaitingAttackTarget; }

	/** Юнит под курсором (с обводкой; nullptr — пусто). Для панели цели HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	AUnitBase* GetHoveredUnit() const { return HoveredUnit.Get(); }

	// --- Прицеливание по-XCOM'овски: список целей, Tab, кадр камеры -----------

	/**
	 * Враги, по которым выбранный боец МОЖЕТ выстрелить прямо сейчас
	 * (`UGA_Attack::CanTargetActor` — дальность + LOS/Squadsight), отсортированы
	 * по дальности. Ровно этот список листает Tab и показывает HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	TArray<AUnitBase*> GetAttackTargets() const;

	/**
	 * Взятая на прицел цель (в режиме «Огонь»). HUD показывает шанс именно по
	 * ней, а не по случайному наведению мышью, — как в XCOM, где панель цели
	 * привязана к ВЫБРАННОЙ цели, а не к курсору.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	AUnitBase* GetCurrentAttackTarget() const { return CurrentAttackTarget.Get(); }

	/**
	 * Переключить цель (Tab / Q-E): Direction > 0 — следующая, < 0 — предыдущая.
	 * Камера берёт новую цель в кадр. Вне режима прицеливания ничего не делает.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void CycleAttackTarget(int32 Direction = 1);

	/** Выйти из режима прицеливания (ПКМ/Esc/клик мимо) и вернуть камеру бойцу. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void CancelTargeting();

	/** Выстрелить по взятой на прицел цели (подтверждение: ЛКМ по ней или Enter). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void ConfirmAttack();

	/**
	 * Показать выстрел камерой (кадр «из-за плеча» на время выстрела). Зовут и
	 * атака игрока, и AI врага — иначе игрок не видит, в кого стреляет враг.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void NotifyShotFired(AActor* Shooter, AActor* Target);

	/**
	 * Какая контекстная интеракция (F) доступна выбранному юниту прямо сейчас:
	 * бомба рядом → эвакуация в зоне → ничего. Для текста/серости кнопки HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	EInteractionKind GetAvailableInteraction() const;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Control")
	FOnSelectedUnitChanged OnSelectedUnitChanged;

	/** Смена юнита под курсором (показ/скрытие панели цели с шансом попадания). */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|Control")
	FOnHoveredUnitChanged OnHoveredUnitChanged;

	/**
	 * Набор доступных действий выбранного бойца изменился НЕ из-за смены выбора
	 * или трат AP, а из-за его новой позиции (добежал до бомбы/зоны эвакуации,
	 * сменился шанс попадания по цели под курсором). HUD пересчитывает по нему
	 * серость кнопок и панель цели: иначе кнопка F оставалась бы серой у самой
	 * бомбы — AP списываются в момент приказа, а доехал юнит куда позже.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|Control")
	FOnAvailableActionsChanged OnAvailableActionsChanged;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	// --- Обработчики Enhanced Input -------------------------------------------

	void HandleSelectPressed();                       // ЛКМ
	void HandleCommandPressed();                      // ПКМ
	void HandleCameraPan(const FInputActionValue& Value);
	void HandleCameraRotate(const FInputActionValue& Value);
	void HandleCameraZoom(const FInputActionValue& Value);

	// --- Логика приказов -------------------------------------------------------

	/** Приказ перемещения в точку (валидация бюджета пути, списание AP). */
	void TryMoveSelectedUnit(const FVector& Goal);

	/** Приказ атаки цели (шлёт Event.Attack; GA сама валидирует и платит). */
	void TryAttackTarget(AActor* Target);

	/** Клик в режиме таргетинга способности (медик выбирает союзника). */
	void HandleAbilityTargetClick(AActor* ClickedActor);

	/**
	 * ЕДИНСТВЕННОЕ место с приоритетом интеракций (бомба рядом → эвакуация в зоне):
	 * возвращает вид и найденный объект. Зовут GetAvailableInteraction (для HUD)
	 * и RequestInteract (исполнение) — порядок не может разъехаться.
	 */
	EInteractionKind FindAvailableInteraction(ABombObjective*& OutBomb, AEvacZone*& OutZone) const;

	/** Идёт бой и сейчас фаза врага (наш выбор/ховер своих скрываем). */
	bool IsEnemyPhaseNow() const;

	/** Обновить видимость кольца выбранного юнита по текущей фазе. */
	void RefreshSelectionHighlight();

	/**
	 * Обновить зону хода под выбранного юнита (или спрятать). Синхронно и сразу:
	 * навмеш статичен, занятость юнитов — дисками на уровне запросов
	 * (UTacticsCombatStatics::GetUnitObstacles), гонок с перестройкой тайлов нет.
	 */
	void RefreshMoveRange();

	/** Превью пути к точке под курсором (лента; троттлинг по сдвигу курсора). */
	void UpdatePathPreviewUnderCursor();

	/** Обводка юнита под курсором (Custom Depth; трейс по Pawn-каналу каждый тик). */
	void UpdateHoverHighlight();

	/** Панорама камеры мышью у края экрана (XCOM edge scrolling). */
	void UpdateEdgeScroll();

	/** Выполняет ли выбранный юнит приказ перемещения прямо сейчас. */
	bool IsSelectedUnitMoving() const;

	/** Колбэк смены фазы: блокировка/разблокировка, сброс выбора. */
	UFUNCTION()
	void HandleTurnStarted(ETurnPhase Phase);

	/** Колбэк начала хода вражеского юнита: наводим на него камеру (как в XCOM). */
	UFUNCTION()
	void HandleEnemyUnitActivated(AActor* Unit);

	/** Навести камеру на центр живого отряда (без выбора юнита). */
	void FocusCameraOnSquad(bool bInstant = false);

	/** Колбэк трат AP выбранного юнита — перестроить зону хода. */
	UFUNCTION()
	void HandleSelectedUnitAPChanged(int32 NewCurrent, int32 Max);

	/** Колбэк смены состояния выбранного юнита: погиб/эвакуирован — снять выбор. */
	UFUNCTION()
	void HandleSelectedUnitStateChanged();

	// --- Настройки (BP-наследник) ----------------------------------------------

	/** Контекст ввода тактического боя (IMC_Tactical). */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> TacticalInputContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> SelectAction;      // ЛКМ
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> CommandAction;     // ПКМ
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> EndTurnAction;     // Enter
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> OverwatchAction;   // Y
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> HunkerAction;      // X
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> ClassAbilityAction;// R
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> InteractAction;    // F
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> SkipTurnAction;    // Backspace
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> NextUnitAction;    // Tab
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> PauseAction;       // Esc
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> CameraPanAction;   // WASD (Axis2D)
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> CameraRotateAction;// Q/E (Axis1D)
	UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> CameraZoomAction;  // колесо

	/** Слоты выбора юнитов (клавиши 1–4): элемент i выбирает юнита слота i. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TArray<TObjectPtr<UInputAction>> SelectSlotActions;

	/** Класс корневого UI-слоя (создаётся в BeginPlay через UIManager). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UPrimaryGameLayout> RootLayoutClass;

	/** Экран паузы (пушится на слой Menu по Esc). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UMenuScreenBase> PauseMenuClass;

	/** Класс визуализатора зоны хода (BP с материалами зон). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control")
	TSubclassOf<AMoveRangeVisualizer> MoveRangeVisualizerClass;

	/** Панорама мышью у края экрана (XCOM). Выключается в BP при желании. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control")
	bool bEdgeScrollEnabled = true;

	/**
	 * XCOM-автовыбор бойцов: активный боец есть всегда. Включает: автовыбор в
	 * начале фазы игрока, переход к следующему бойцу после смерти/эвакуации
	 * выбранного и после исчерпания его AP (действие завершило активацию).
	 * Выключается в BP при желании — тогда выбор полностью ручной.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control")
	bool bAutoSelectUnits = true;

	/**
	 * Автоматически завершать ход игрока, когда AP не осталось НИ У КОГО из
	 * отряда (XCOM-темп: не заставляем жать «Завершить ход» вручную). Выключить
	 * в BP, если нужен явный контроль конца хода.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control")
	bool bAutoEndTurnWhenExhausted = true;

	/** Ширина зоны edge scroll от края вьюпорта, px. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Control", meta = (ClampMin = "2", ClampMax = "100"))
	float EdgeScrollMarginPx = 16.f;

	// --- Состояние ---------------------------------------------------------------

	UPROPERTY(Transient)
	TObjectPtr<AUnitBase> SelectedUnit;

	UPROPERTY(Transient)
	TObjectPtr<AMoveRangeVisualizer> MoveRangeVisualizer;

	/** Юнит под курсором (с обводкой). Weak: юнит может умереть/исчезнуть между тиками. */
	TWeakObjectPtr<AUnitBase> HoveredUnit;

	/** Ждём клик по цели способности (Event.Heal медика). */
	bool bAwaitingAbilityTarget = false;

	/** Ждём клик по цели атаки (нажата кнопка «Огонь»). */
	bool bAwaitingAttackTarget = false;

	/** Взятая на прицел цель в режиме «Огонь» (Tab листает, HUD показывает шанс по ней). */
	TWeakObjectPtr<AUnitBase> CurrentAttackTarget;

	/**
	 * Общий вход в режим прицеливания: собирает цели, берёт первую (или ближайшую
	 * к курсору) на прицел, наводит камеру. false — целей нет (в режим не входим,
	 * кнопка «Огонь» гаснет). Зовут кнопка «Огонь» и хоткей.
	 */
	bool BeginAttackTargeting();

	/** Взять КОНКРЕТНУЮ цель на прицел: запомнить, подсветить, навести кадр камеры. */
	void SetAttackTarget(AUnitBase* Target);

	/** Снять прицел и его подсветку (не трогая режим/камеру — это делают вызывающие). */
	void ClearAttackTarget();

	/**
	 * Если AP не осталось ни у кого из отряда и никто не бежит — завершить ход
	 * (авто-переход к врагу). Зовётся из точек, где AP только что кончились.
	 * Ничего не делает, пока хоть один боец может действовать или ещё в пути.
	 */
	void TryAutoEndTurn();

	/** Играет ли камера кадр выстрела прямо сейчас (автопереходы ждут его конца). */
	bool IsCameraFramingShot() const;

	/**
	 * Автопереход (следующий боец / конец хода) отложен до конца кадра выстрела:
	 * AP падают в ноль В МОМЕНТ выстрела, и мгновенный SelectNextUnit увёл бы
	 * камеру с кинематографичного кадра в тот же тик. Разрешается в PlayerTick.
	 */
	bool bPendingAutoAdvance = false;

	/** Последняя точка превью пути (троттлинг перзапроса FindPath). */
	FVector LastPathPreviewGoal = FVector(TNumericLimits<float>::Max());

	/** Двигался ли выбранный юнит в прошлый тик (ловим остановку → перестроить зону). */
	bool bSelectedUnitWasMoving = false;


	/** Стартовый фокус камеры на отряд уже выполнен (не повторять каждый ход). */
	bool bInitialSquadFocusDone = false;
};
