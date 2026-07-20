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

	/** Следующий юнит отряда с оставшимися AP (Tab). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void SelectNextUnit();

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
	 * Юнит закончил перемещение (зовёт AUnitAIController из OnMoveCompleted).
	 * Если это НЕ выбранный юнит — его диск занятости встал на новое место,
	 * зона хода выбранного пересчитывается немедленно.
	 */
	void NotifyUnitMoveFinished(AUnitBase* Unit);

	/** В режиме ли выбора цели способности (медик ждёт клика по союзнику). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsTargetingAbility() const { return bAwaitingAbilityTarget; }

	/** В режиме ли прицеливания атаки (нажата кнопка «Огонь», ждём клика по врагу). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsTargetingAttack() const { return bAwaitingAttackTarget; }

	/** Юнит под курсором (с обводкой; nullptr — пусто). Для панели цели HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	AUnitBase* GetHoveredUnit() const { return HoveredUnit.Get(); }

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

	/** Сейчас фаза игрока и бой идёт (приказы разрешены). */
	bool IsPlayerPhase() const;

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

	/** Последняя точка превью пути (троттлинг перзапроса FindPath). */
	FVector LastPathPreviewGoal = FVector(TNumericLimits<float>::Max());

	/** Двигался ли выбранный юнит в прошлый тик (ловим остановку → перестроить зону). */
	bool bSelectedUnitWasMoving = false;


	/** Стартовый фокус камеры на отряд уже выполнен (не повторять каждый ход). */
	bool bInitialSquadFocusDone = false;
};
