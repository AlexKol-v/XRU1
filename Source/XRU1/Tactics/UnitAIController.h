#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Engine/TimerHandle.h"
#include "UnitAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UGameplayEffect;
class AUnitBase;

/** Уровень тревоги AI-юнита (упрощённая модель XCOM: green/yellow/red alert). */
UENUM(BlueprintType)
enum class EUnitAlertState : uint8
{
	/** Green: противник не обнаружен — патрулирует свой маршрут / стоит на посту. */
	Patrol      UMETA(DisplayName = "Patrol (green)"),
	/** Yellow: слышал шум боя или потерял цель из виду — идёт разведать точку. */
	Investigate UMETA(DisplayName = "Investigate (yellow)"),
	/** Red: видел противника — сближается и атакует. */
	Combat      UMETA(DisplayName = "Combat (red)")
};

/**
 * AI-контроллер тактического юнита. Несёт UAIPerceptionComponent с чувством
 * зрения — общий источник «линии видимости» и для вражеского AI, и для
 * реакций Overwatch юнитов игрока.
 *
 * Ход юнита исполняется по запросу TurnManager'а (ExecuteUnitTurn) конечным
 * автоматом тревоги (EUnitAlertState, GDD §8):
 *  - Patrol: движение по PatrolPoints юнита (или стоит на посту);
 *  - Investigate: движение к последней известной точке (шум выстрелов /
 *    потерянная цель); дойдя и никого не найдя — возврат в Patrol;
 *  - Combat: видимая цель в дальности — выстрел, иначе сближение с целью.
 * Все перемещения ограничены бюджетом пути юнита (MoveRange за 1 AP).
 * Боевые статы (aim/урон/дальность) читаются с пешки AUnitBase.
 */
UCLASS()
class XRU1_API AUnitAIController : public AAIController
{
	GENERATED_BODY()

public:
	/**
	 * ObjectInitializer-конструктор: подменяет PathFollowingComponent на
	 * UCrowdFollowingComponent (Detour Crowd) — бегущие юниты локально
	 * объезжают стоящих, не застревая в их капсулах (навмеш статичен и о
	 * юнитах не знает; см. занятость в UTacticsCombatStatics).
	 */
	explicit AUnitAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UAIPerceptionComponent* GetUnitPerception() const { return Perception; }

	/**
	 * Выполняет ход юнита (тратит его AP), по завершении зовёт OnFinished.
	 * Вызывается TurnManager'ом в фазу стороны юнита.
	 */
	void ExecuteUnitTurn(FSimpleDelegate OnFinished);

	/** Шум боя рядом (выстрел): green→yellow, обновить точку интереса. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI")
	void NotifyNoiseHeard(const FVector& NoiseLocation);

	UFUNCTION(BlueprintPure, Category = "Tactics|AI")
	EUnitAlertState GetAlertState() const { return AlertState; }

	// --- Perception ---------------------------------------------------------

	/** Радиус зрения (см). Дизайнерский параметр, влияет на дальность обнаружения. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception")
	float SightRadius = 1400.f;

	/** Радиус, на котором цель теряется после обнаружения. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception")
	float LoseSightRadius = 1600.f;

	/** Половина угла конуса зрения (град). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception")
	float PeripheralVisionHalfAngle = 60.f;

	// --- Параметры хода -------------------------------------------------------

	/**
	 * GE урона для ФОЛБЭК-выстрела (когда юниту не назначен AttackAbilityClass).
	 * В штатном пути урон определяет сама GA_Attack.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat")
	TSubclassOf<UGameplayEffect> DamageEffect;

	/** Насколько близко подходить к точке интереса при разведке (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat", meta = (ClampMin = "0"))
	float InvestigateAcceptanceRadius = 150.f;

	/** Пауза между последовательными действиями юнита в его ход (читабельность). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat", meta = (ClampMin = "0"))
	float ActionInterval = 0.4f;

	/**
	 * Радиус действия провокации танка (GDD §7): враг ближе этого расстояния к
	 * провоцирующей цели обязан бить именно её. Дальше — выбирает цель обычно
	 * (иначе «крик» танка перетягивал бы врагов через всю карту).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat", meta = (ClampMin = "0"))
	float TauntPriorityRadius = 1200.f;

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

	/** Колбэк перцепции: увидел/потерял враждебного актора → смена тревоги. */
	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, struct FAIStimulus Stimulus);

	/** Один шаг хода: атака / движение по состоянию / завершение — пока есть AP. */
	void AdvanceTurnStep();

	/** Действия шага в состоянии Combat. true — шаг обработан (ждём таймер/движение). */
	bool StepCombat(AUnitBase* Unit);

	/**
	 * Выстрел AI по цели. Штатный путь — событие Event.Attack: те же правила,
	 * стоимость AP (включая XCOM-сжигание остатка) и BP-хуки (VFX/анимация),
	 * что и у выстрела игрока. Фолбэк прямым ResolveShot — только для юнитов,
	 * которым в BP не назначен AttackAbilityClass.
	 * false — выстрел не состоялся (способность отказала): ход юнита завершается,
	 * чтобы шаг не зациклился на неоплаченном действии.
	 */
	bool TryFireAtTarget(AUnitBase* Unit, AActor* Target);

	/** Действия шага в состоянии Investigate. */
	bool StepInvestigate(AUnitBase* Unit);

	/** Действия шага в состоянии Patrol. */
	bool StepPatrol(AUnitBase* Unit);

	/** Движение к точке с обрезкой по бюджету пути юнита (1 AP). true — движение началось. */
	bool MoveWithBudget(AUnitBase* Unit, const FVector& Goal, float AcceptanceRadius);

	/** Завершает ход юнита и уведомляет TurnManager. */
	void FinishUnitTurn();

	/** Планирует следующий шаг хода через ActionInterval. */
	void ScheduleNextStep();

	/** Ближайший живой враждебный актор, видимый перцепцией; провоцирующий (Taunt) — приоритетнее. */
	AActor* FindVisibleTarget() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Perception")
	TObjectPtr<UAIPerceptionComponent> Perception;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	/** Текущий уровень тревоги. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|AI")
	EUnitAlertState AlertState = EUnitAlertState::Patrol;

	/** Последняя известная точка противника/шума (Investigate-цель). */
	FVector LastKnownThreatLocation = FVector::ZeroVector;
	bool bHasThreatLocation = false;

	/** Индекс следующей патрульной точки юнита. */
	int32 PatrolIndex = 0;

	/** Колбэк TurnManager'а на завершение хода этого юнита. */
	FSimpleDelegate TurnFinishedDelegate;

	/** Идёт ли сейчас перемещение, начатое в рамках хода. */
	bool bTurnMoveInProgress = false;

	FTimerHandle TurnStepTimerHandle;
};
