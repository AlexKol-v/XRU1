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
	 * Приказ идти ПО ГОТОВОЙ ЛОМАНОЙ (маршрут из плана поля), а не «в точку».
	 *
	 * Почему не MoveToLocation в конечную цель: навмеш о юнитах не знает и
	 * прокладывает к цели ПРЯМУЮ — сквозь стоящих бойцов. Detour Crowd потом
	 * пытается их объехать локально, но в тесноте просто упирается: боец стоит,
	 * а очко действия уже списано. При этом игрок видел совсем другую линию —
	 * ту, что посчитало поле в обход занятых клеток.
	 *
	 * Поэтому исполняем ровно план: ведём бойца от вершины к вершине. Каждый
	 * отрезок ломаной проверен полем (свободен по навмешу и не задевает занятых
	 * клеток), поэтому навмеш внутри отрезка даёт ту же прямую — бежит именно
	 * так, как нарисовано. Следующий отрезок запрашивается из OnMoveCompleted:
	 * движок это разрешает явно (статус сбрасывается в Idle ДО оповещения
	 * наблюдателей — «they can request another move»).
	 *
	 * RoutePoints[0] — позиция бойца; нужно ≥ 2 точек.
	 */
	EPathFollowingRequestResult::Type MoveAlongRoute(const TArray<FVector>& RoutePoints, float AcceptanceRadius = 50.f);

	/**
	 * Юнит в движении — ЕДИНСТВЕННЫЙ предикат «переходного состояния» на весь
	 * проект (зона хода, занятость, автопереход выбора). Включает паузы между
	 * отрезками маршрута, когда path following уже Idle, но боец ещё в пути.
	 *
	 * Раньше занятость судила по velocity: после финиша боец ещё ~0.3 с гасил
	 * скорость торможением, всё это время считался «бегущим» и НЕ ставил диск —
	 * поэтому зона следующего бойца показывала его клетку свободной.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|AI")
	bool IsMoving() const;

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

	/**
	 * Радиус приёмки ПРОМЕЖУТОЧНОЙ вершины маршрута (см). Вершина — поворотная
	 * точка ломаной, останавливаться в ней не нужно.
	 *
	 * ВЕРХНЯЯ ГРАНИЦА НЕ ПРОИЗВОЛЬНА. Path following считает точку достигнутой в
	 * пределах этого радиуса и сразу правит на следующую — то есть СРЕЗАЕТ угол
	 * на эту величину. А поворотные вершины стоят ровно у занятых клеток: поле
	 * держит просвет `GetUnitClearance` (≈94 = 60 + радиус капсулы), физически
	 * бойцу нужно ≈68 (две капсулы), то есть запаса всего ≈26 см. Радиус больше
	 * запаса — и срезанный угол заводит бойца в союзника, ровно в тот баг,
	 * ради которого маршрут и ведётся по ломаной.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat", meta = (ClampMin = "5", ClampMax = "25"))
	float RouteCornerAcceptance = 25.f;

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

	// --- Веса боевых решений (утилити-скоринг позиций, XCOM-подход) -----------
	//
	// Firaxis в XCOM гоняет дерево ОДИН раз на активацию юнита и выбирает
	// позицию по скорингу (укрытие/фланг/дистанция) — не реалтайм-стейты.
	// Здесь тот же принцип: FSM тревоги (Patrol/Investigate/Combat) остаётся
	// источником правды «что юнит знает», а ВНУТРИ Combat позиция выбирается
	// взвешенной оценкой. Новое поведение = новый вес/слагаемое, не новый флаг.

	/** Множитель ценности укрытия в точке (защита 20/40 × вес). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float CoverDefenseWeight = 1.f;

	/** Бонус точке, из которой цель ПРОСТРЕЛИВАЕТСЯ (манёвр не теряет выстрел). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float LineOfFireBonus = 25.f;

	/** Штраф точке без линии огня (при отступлении не применяется). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float LoseLineOfFirePenalty = 45.f;

	/** Цена манёвра за см пути (короткие перебежки лучше марафонов). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float TravelCostPerCm = 0.015f;

	/** Штраф за отход дальше комфортной дистанции боя (за см сверх 0.75×AttackRange). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float OverextendPenaltyPerCm = 0.02f;

	/** Порог значимости: манёвр только если он лучше текущей позиции на столько. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float RelocateBias = 10.f;

	/** Доля HP, ниже которой открытый юнит ОТСТУПАЕТ в укрытие, а не лезет вперёд. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0", ClampMax = "1"))
	float RetreatHealthFraction = 0.35f;

	/** При отступлении: награда за каждый см УДАЛЕНИЯ от угрозы. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float RetreatRewardPerCm = 0.01f;

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
	 * Поиск лучшей боевой позиции против угрозы в бюджете пути PathBudget (см).
	 * Кольцевой сэмплинг вокруг юнита; каждая точка оценивается ТЕМИ ЖЕ
	 * правилами, что действуют при стрельбе: укрытие — математикой
	 * CoverDetectionComponent юнита, линия огня — общим LOS
	 * (HasLineOfSightFromLocation), достижимость — навигацией с занятостью.
	 * Оценка — взвешенная (веса Tactics|AI|Weights): защита укрытия, линия
	 * огня, цена пути, дистанция до цели. bRetreat инвертирует дистанцию
	 * (награда за удаление, потеря LOS не штрафуется) — режим «отойти и
	 * спрятаться» при низком HP.
	 * bAdvance — режим «наступать от укрытия к укрытию»: потеря LOS НЕ штрафуется
	 * (иначе бот отвергает промежуточное укрытие без выстрела и бежит напролом),
	 * дистанция по-прежнему тянет ближе к цели. Так враг сближается, оставаясь в
	 * укрытии (XCOM), а не открытой пробежкой.
	 * false — ничего значимо лучше текущей позиции (порог RelocateBias):
	 * стоим где стоим, а не мечемся ради +0 к укрытию.
	 */
	bool FindCoverPoint(AUnitBase* Unit, const AActor* Threat, float PathBudget, bool bRetreat,
		FVector& OutPoint, bool bAdvance = false);

	/** Старт манёвра к точке: помечает ход и запоминает точку для продолжения. */
	bool StartManeuverTo(AUnitBase* Unit, const FVector& Point, const TCHAR* Reason);

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

	/**
	 * Манёвр в укрытие в этом ходу уже ВЫБРАН. Один выбор на ход (XCOM:
	 * переместился — стреляй): без флага открытый юнит, не нашедший идеального
	 * укрытия, мог бы потратить оба AP на метания и не выстрелить вовсе.
	 */
	bool bCoverMoveDoneThisTurn = false;

	/**
	 * Манёвр длиннее 1 AP (отступление/рывок к дальнему укрытию) продолжается
	 * на следующем шаге хода: MoveWithBudget за раз проходит максимум MoveRange,
	 * вторую ногу к ТОЙ ЖЕ точке делает следующий AdvanceTurnStep. Это
	 * продолжение выбора, а не новый выбор (bCoverMoveDoneThisTurn уже стоит).
	 */
	bool bManeuverInProgress = false;
	FVector PendingManeuverPoint = FVector::ZeroVector;

	FTimerHandle TurnStepTimerHandle;

	// --- Движение по ломаной маршрута (см. MoveAlongRoute) --------------------

	/** Вершины текущего маршрута; [0] — точка старта. */
	TArray<FVector> RouteLegs;

	/** Индекс СЛЕДУЮЩЕЙ вершины, к которой идём. */
	int32 RouteLegIndex = 0;

	/** Радиус приёмки финальной вершины (промежуточные проходим свободнее). */
	float RouteAcceptanceRadius = 50.f;

	/** Идём по ломаной: ход не считается завершённым между отрезками. */
	bool bFollowingRoute = false;

	/**
	 * Внутри запроса очередного отрезка. MoveToLocation умеет завершиться
	 * синхронно (AlreadyAtGoal) и позвать OnMoveCompleted прямо из себя — без
	 * этого флага вложенный вызов перескочил бы на следующие вершины, и боец
	 * ушёл бы к цели по прямой, то есть ровно так, как мы и чиним.
	 */
	bool bRequestingRouteLeg = false;

	/** Запрашивает движение к следующей вершине. false — вершин больше нет. */
	bool RequestNextRouteLeg();

	/** Сбрасывает состояние маршрута (финиш, срыв, новый приказ). */
	void StopRoute();
};
