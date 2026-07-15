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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedUnitChanged, AUnitBase*, NewSelected);

/**
 * Контроллер игрока в тактическом бою (GDD §11): выбор юнита (ЛКМ/1–4/Tab),
 * приказ перемещения (ПКМ, бюджет по длине пути навмеша), атака (ЛКМ по врагу
 * → GameplayEvent Event.Attack), хоткеи действий (Y/X/Q/F/Backspace/Enter),
 * камера (WASD/QE/колесо) и пауза (Esc). Ввод — Enhanced Input (IMC задаётся
 * в BP-наследнике). В фазу врага приказы заблокированы.
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

	/** Хоткей/кнопка Overwatch (Y). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestOverwatch();

	/** Хоткей/кнопка глухой обороны (X). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Control")
	void RequestHunkerDown();

	/** Хоткей/кнопка способности класса (Q). Для медика включает режим выбора цели. */
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

	/** В режиме ли выбора цели способности (медик ждёт клика по союзнику). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Control")
	bool IsTargetingAbility() const { return bAwaitingAbilityTarget; }

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Control")
	FOnSelectedUnitChanged OnSelectedUnitChanged;

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

	/** Сейчас фаза игрока и бой идёт (приказы разрешены). */
	bool IsPlayerPhase() const;

	/** Обновить зону хода под выбранного юнита (или спрятать). */
	void RefreshMoveRange();

	/** Превью пути к точке под курсором (лента; троттлинг по сдвигу курсора). */
	void UpdatePathPreviewUnderCursor();

	/** Выполняет ли выбранный юнит приказ перемещения прямо сейчас. */
	bool IsSelectedUnitMoving() const;

	/** Колбэк смены фазы: блокировка/разблокировка, сброс выбора. */
	UFUNCTION()
	void HandleTurnStarted(ETurnPhase Phase);

	/** Колбэк трат AP выбранного юнита — перестроить зону хода. */
	UFUNCTION()
	void HandleSelectedUnitAPChanged(int32 NewCurrent, int32 Max);

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

	// --- Состояние ---------------------------------------------------------------

	UPROPERTY(Transient)
	TObjectPtr<AUnitBase> SelectedUnit;

	UPROPERTY(Transient)
	TObjectPtr<AMoveRangeVisualizer> MoveRangeVisualizer;

	/** Ждём клик по цели способности (Event.Heal медика). */
	bool bAwaitingAbilityTarget = false;

	/** Последняя точка превью пути (троттлинг перзапроса FindPath). */
	FVector LastPathPreviewGoal = FVector(TNumericLimits<float>::Max());

	/** Двигался ли выбранный юнит в прошлый тик (ловим остановку → перестроить зону). */
	bool bSelectedUnitWasMoving = false;
};
