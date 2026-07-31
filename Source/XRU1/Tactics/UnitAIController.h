#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Engine/TimerHandle.h"
#include "AIBehaviorProfileDataAsset.h"
#include "AIDecisionTypes.h"
#include "UnitAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAIActionEvaluator;
class UAIBehaviorProfileDataAsset;
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

	/**
	 * Единый профиль настройки. Если назначен, в BeginPlay он имеет приоритет над локальными
	 * значениями BP-контроллера, включая необязательную замену набора utility-оценщиков.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|AI|Tuning")
	TObjectPtr<UAIBehaviorProfileDataAsset> BehaviorProfile;

	/**
	 * Назначает профиль уже созданному контроллеру и немедленно применяет его.
	 * Зовёт GameMode на старте боя, выбирая профиль по уровню сложности: стиль
	 * врага обязан отличаться на Easy/Medium/Hard, а не только его меткость.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Tuning")
	void SetBehaviorProfile(UAIBehaviorProfileDataAsset* NewProfile);

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

	/**
	 * Под этого бойца вскрыт: переводит его в бой независимо от того, видит ли
	 * он противника сам. Зовёт UTacticalAIDirectorSubsystem.
	 */
	void NotifyPodActivated();

	// --- Perception ---------------------------------------------------------

	/**
	 * Радиус зрения (см).
	 *
	 * ⚠️ Значение НЕ произвольное. `AUnitBase::AttackRange` = 3000, а обзор отряда
	 * игрока (`SquadVisionRange`) = 2500. Прежние 1400 означали, что игрок
	 * спокойно расстреливает врага с дистанции, на которой тот физически не может
	 * его увидеть, — и враг честно стоял на посту, ничего не зная. Радиус
	 * приведён к дистанциям, на которых реально идёт бой.
	 *
	 * Групповая активация и память контактов (UTacticalAIDirectorSubsystem)
	 * страхуют остальные случаи, но обзор не должен быть заведомо меньше
	 * дальности, с которой по бойцу ведут огонь.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception")
	float SightRadius = 2500.f;

	/** Радиус, на котором цель теряется после обнаружения. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception")
	float LoseSightRadius = 2800.f;

	/**
	 * Половина угла конуса зрения (град). **180 = круговой обзор**, и это
	 * дефолт по двум причинам.
	 *
	 * 1) В XCOM юниты видят на 360° в пределах радиуса зрения — «подкрасться со
	 *    спины» там нет как механики.
	 * 2) У нас предикат выстрела (`HasLineOfSight`) поворот юнита не учитывает
	 *    вообще. При конусе 120° перцепция была СТРОЖЕ предиката: враг физически
	 *    мог выстрелить в бойца за спиной, но не видел его и потому не выбирал
	 *    целью. Отсюда наблюдаемое «стрелял по укрытому, игнорируя открытого» —
	 *    открытый просто не попадал в список видимых.
	 *
	 * Уменьшать имеет смысл, только если специально вводится механика захода со
	 * спины — тогда её придётся ввести и в LOS, иначе правила снова разъедутся.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception",
		meta = (ClampMin = "1", ClampMax = "180"))
	float PeripheralVisionHalfAngle = 180.f;

	// --- Параметры хода -------------------------------------------------------

	/**
	 * Сила расталкивания агентов Detour Crowd (`SetCrowdSeparationWeight`).
	 * Больше — активнее обходят стоящих; слишком много даёт «магнитное»
	 * отталкивание и рыскание. Разумный диапазон 1–4.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Nav", meta = (ClampMin = "0", ClampMax = "10"))
	float CrowdSeparationWeight = 2.f;

	/** Насколько близко подходить к точке интереса при разведке (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat", meta = (ClampMin = "0"))
	float InvestigateAcceptanceRadius = 150.f;

	/**
	 * Шанс встать в НАБЛЮДЕНИЕ, дойдя до последней известной точки врага и никого
	 * там не найдя (0..1). 0 — выключить, вернётся прежнее «сразу в патруль».
	 *
	 * ⚠️ Это сознательное УЛУЧШЕНИЕ относительно XCOM 2, а не копия. Самая частая
	 * претензия игроков к тамошнему AI: враги почти никогда не встают в овервотч,
	 * если сейчас не видят отряд, — то есть боец, слышавший стрельбу, вместо
	 * удержания направления продолжает ходить по маршруту. У нас состояние
	 * `Investigate` ровно и означает «знаю точку, не вижу цель», поэтому данные
	 * для правильного поведения уже есть.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat", meta = (ClampMin = "0", ClampMax = "1"))
	float InvestigateOverwatchChance = 0.5f;

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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat", meta = (ClampMin = "0.01"))
	float ActionInterval = 0.4f;

	/**
	 * Радиус действия провокации танка (GDD §7): враг ближе этого расстояния к
	 * провоцирующей цели обязан бить именно её. Дальше — выбирает цель обычно
	 * (иначе «крик» танка перетягивал бы врагов через всю карту).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Combat", meta = (ClampMin = "0"))
	float TauntPriorityRadius = 2500.f;

	// --- Веса боевых решений (утилити-скоринг позиций, XCOM-подход) -----------
	//
	// Firaxis в XCOM гоняет дерево ОДИН раз на активацию юнита и выбирает
	// позицию по скорингу (укрытие/фланг/дистанция) — не реалтайм-стейты.
	// Здесь тот же принцип: FSM тревоги (Patrol/Investigate/Combat) остаётся
	// источником правды «что юнит знает», а ВНУТРИ Combat позиция выбирается
	// взвешенной оценкой. Новое поведение = новый вес/слагаемое, не новый флаг.

	/**
	 * Множитель НОРМИРОВАННОЙ ценности укрытия точки (A5).
	 *
	 * Ценность считается по XCOM: усреднение по ВСЕМ видимым угрозам, где
	 * открытость даёт резко отрицательный вклад. При дефолтах ниже полностью
	 * укрытая точка даёт ≈ +22, полностью открытая ≈ −80. Разрыв в 100 очков
	 * заведомо больше `LineOfFireBonus` — именно это заставляет бота искать
	 * укрытие, а не бежать в атаку напролом.
	 *
	 * ⚠️ Смысл поля изменился в A5. Раньше сюда умножались очки защиты (20/40),
	 * и вес 1.0 означал «укрытие ценно на 20–40». Теперь диапазон входа
	 * −4…+1.1, поэтому дефолт другого порядка.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float CoverDefenseWeight = 20.f;

	/**
	 * Вклад ОТКРЫТОЙ позиции против одной угрозы (XCOM `CALC_NO_COVER_FACTOR`).
	 * Резко отрицательный намеренно: «AI боится флангов» в XCOM получается не
	 * спецправилом, а этим коэффициентом. Открытость против ОДНОГО врага
	 * перевешивает укрытие против двух других.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float OpenCoverFactor = -4.f;

	/** Вклад половинчатого укрытия против одной угрозы (XCOM: 1.0). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float HalfCoverFactor = 1.f;

	/**
	 * Вклад полного укрытия (XCOM: 1.1). ⚠️ Всего на 10% выше половинчатого —
	 * это не опечатка: из полного укрытия труднее стрелять, поэтому XCOM ценит
	 * его лишь чуть выше. Не подгонять под соотношение защиты 40/20.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float FullCoverFactor = 1.1f;

	/** Бонус точке, из которой юнит ФЛАНКИРУЕТ хотя бы одну угрозу (A5). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float FlankPositionBonus = 25.f;

	/** Бонус за превышение над целью (порог берётся из DA_CoverTuning.HeightAdvantageZ). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float HeightPositionBonus = 15.f;

	/**
	 * Ближе этого расстояния к союзнику точка считается «в куче» (см). 0 — выкл.
	 *
	 * XCOM: `DEFAULT_AI_MIN_SPREAD_DISTANCE = 6.0` — это ТАЙЛЫ, а тайл XCOM 2 =
	 * 96 юнитов Unreal, то есть ≈576 см. Раньше здесь стояло 250: штраф за кучу
	 * срабатывал только когда бойцы стояли фактически вплотную, и «разойтись» он
	 * почти не заставлял. Сверено с `XComAI.ini` 2026-07-25.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float MinSpreadDistance = 576.f;

	/**
	 * Множитель ПОЛОЖИТЕЛЬНОГО скора для точки в куче
	 * (XCOM `DEFAULT_AI_SPREAD_WEIGHT_MULTIPLIER = 0.2`). Только положительного:
	 * иначе штраф превратился бы в поощрение для отрицательных скоров.
	 *
	 * ⚠️ Раньше стояло 0.4 с комментарием «как в XCOM» — число было не сверено.
	 * Verbatim-значение 0.2 (вдвое жёстче): кучная позиция теряет 80% ценности,
	 * а не 60%.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0", ClampMax = "1"))
	float SpreadPenaltyMultiplier = 0.2f;

	/**
	 * СПЛОЧЁННОСТЬ: вес «из точки видно СВОИХ» (XCOM `fAllyVisWeight`, в профилях
	 * 0.5–4.0, у Defensive максимум). Раньше этого члена не было вовсе, и у AI
	 * работал только анти-кучный штраф — то есть отряд умел РАЗБЕГАТЬСЯ, но не
	 * умел держать линию. В XCOM это два независимых механизма: минимальная
	 * дистанция разводит бойцов, а `fAllyVisWeight` не даёт им разойтись поодиночке
	 * туда, откуда никто не поддержит.
	 *
	 * Считается как доля видимых союзников (0..1) × вес.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float AllyVisibilityWeight = 10.f;

	/**
	 * Штраф за КАЖДУЮ угрозу в наблюдении, простреливающую точку (A7
	 * `SafeToMove`). Сопоставим с `LineOfFireBonus`: бежать под один овервотч
	 * ради выстрела ещё осмысленно, под два — уже нет.
	 *
	 * ⚠️ Проверяется КОНЕЧНАЯ точка, а не весь маршрут (в XCOM — маршрут).
	 * Обоснование упрощения — в `EvaluatePoint`.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float OverwatchExposurePenalty = 30.f;

	/**
	 * ⚠️ ОСОЗНАННОЕ ОТКЛОНЕНИЕ от XCOM: `CURR_TILE_LINGER_PENALTY = 0.75` не
	 * переносим. В XCOM скор ТЕКУЩЕЙ клетки умножается на 0.75 — то есть игра
	 * подталкивает врага двигаться. У нас противоположный знак (`RelocateBias`:
	 * переезжаем, только если заметно лучше), и это сделано намеренно: главная
	 * претензия к нашему боту по логам была «кровожадно бежит вперёд», а не
	 * «стоит столбом». Перенос множителя усилил бы ровно тот дефект, от которого
	 * мы уходили. Если бот начнёт залипать — сначала снижать `RelocateBias`.
	 */

	/** Бонус точке, из которой цель ПРОСТРЕЛИВАЕТСЯ (манёвр не теряет выстрел). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float LineOfFireBonus = 25.f;

	/** Штраф точке без линии огня (при отступлении не применяется). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float LoseLineOfFirePenalty = 45.f;

	/** Цена манёвра за см пути (короткие перебежки лучше марафонов). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float TravelCostPerCm = 0.015f;

	/**
	 * Вес близости к ИДЕАЛЬНОЙ дистанции боя (`AUnitBase::IdealCombatRange`).
	 * В XCOM это один из самых больших весов (`fDistanceWeight` 4–5 против
	 * `fCoverWeight` 1.7–2.0) — именно он не даёт ботам сбегаться вплотную.
	 *
	 * Пришёл на смену `OverextendPenaltyPerCm`, который считался от
	 * `0.75×AttackRange`. Поскольку `AttackRange` — щедрый технический cap
	 * (3000), порог был ~2250 см, и на нашей карте штраф не срабатывал НИ РАЗУ:
	 * дистанция фактически не влияла на выбор позиции вообще.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float IdealRangeWeight = 30.f;

	/**
	 * На сколько см отклонения от идеала оценка дистанции падает с 1 до 0
	 * (аналог `CALC_RANGE_LINEAR_DENOM` в XCOM). Дальше уходит в минус.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "100"))
	float IdealRangeFalloff = 1500.f;

	/** Порог значимости: манёвр только если он лучше текущей позиции на столько. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float RelocateBias = 10.f;

	/** Доля HP, ниже которой открытый юнит ОТСТУПАЕТ в укрытие, а не лезет вперёд. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0", ClampMax = "1"))
	float RetreatHealthFraction = 0.35f;

	/** При отступлении: награда за каждый см УДАЛЕНИЯ от угрозы. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights", meta = (ClampMin = "0"))
	float RetreatRewardPerCm = 0.01f;

	// --- Веса выбора ЦЕЛИ (A3) ------------------------------------------------
	//
	// Аддитивный скоринг вместо прежнего «ближайший видимый». Числа
	// пропорциональны XCOM 2 (docs/08_AI.md): там шанс попадания
	// доминирует над всем остальным — AI в первую очередь НЕ МАЖЕТ, и уже потом
	// умничает. Дистанция отдельным слагаемым НЕ входит: она уже учтена в шансе
	// попадания через AimByDistanceCurve, второй раз считать нельзя.

	/** Порог «уверенного» выстрела, % (XCOM: TargetHitChanceHigh). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights", meta = (ClampMin = "0", ClampMax = "100"))
	float TargetHitChanceHighThreshold = 65.f;

	/** Порог «сомнительного» выстрела, % (ниже него — минимальный бонус). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights", meta = (ClampMin = "0", ClampMax = "100"))
	float TargetHitChanceLowThreshold = 35.f;

	/** Бонус за высокий шанс попадания (XCOM: +70). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreHitChanceHigh = 70.f;

	/** Бонус за средний шанс (XCOM: +40). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreHitChanceMedium = 40.f;

	/** Бонус за низкий шанс (XCOM: +10) — стрелять всё ещё можно. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreHitChanceLow = 10.f;

	/**
	 * Провоцирующая цель (танк с State.Taunting в TauntPriorityRadius).
	 *
	 * ⚠️ Здесь мы СОЗНАТЕЛЬНО расходимся с XCOM. Там priority target даёт всего
	 * +60 — сопоставимо с бонусом за высокий шанс попадания, то есть это
	 * «сильное предпочтение», а не приказ. Наш GDD §7 формулирует жёстче: враг в
	 * радиусе провокации **обязан** бить именно провоцирующего. Поэтому вес
	 * заведомо перебивает любую комбинацию остальных слагаемых.
	 *
	 * Хотите XCOM-модель («танк оттягивает, но не гарантированно») — поставьте
	 * ~60 и синхронизируйте GDD §7.
	 *
	 * Бонус даётся ТОЛЬКО когда по провоцирующему реально можно выстрелить:
	 * «обязан бить» не означает «обязан бежать через полкарты к недостижимой
	 * цели, игнорируя тех, по кому есть линия огня».
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreTaunting = 1000.f;

	/** Цель флангирована мной — укрытие не спасает (XCOM: +50). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreFlanked = 50.f;

	/** Выстрел добивает цель (XCOM: +15 — заметно МЕНЬШЕ, чем за шанс попасть). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreKillShot = 15.f;

	/** Цель уже ранена (XCOM: +5). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreWounded = 5.f;

	/**
	 * Тяжелораненая цель: приём XCOM «−1000 вместо ветки-исключения» — не
	 * добивай лежачего, пока есть живые. Если живых целей нет, цель всё равно
	 * выберется (скор просто окажется наименее плохим).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreDowned = -1000.f;

	/**
	 * Цель, по которой ВЫСТРЕЛ НЕВОЗМОЖЕН (нет линии огня / вне дальности).
	 * `ComputeAttackHitChance` отдаёт для таких −1, и первая редакция скоринга
	 * честно считала это «низким шансом» и добавляла бонус +10 — в логе это
	 * видно как `цель ...: скор 65 (шанс -1%)`. В итоге недостижимая цель могла
	 * перебить достижимую.
	 *
	 * Штраф подобран так, чтобы ЛЮБАЯ достижимая цель (минимум +10) била любую
	 * недостижимую (максимум 10+60+50+15+5−200 < 0), но порядок между самими
	 * недостижимыми сохранялся — к кому сближаться, AI решает по ним же.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|TargetWeights")
	float TargetScoreNoLineOfFire = -200.f;

	// --- Поиск позиции (A5) ---------------------------------------------------

	/**
	 * «Прилипание к укрытию»: если точка-кандидат сама по себе открыта, ищем
	 * стену между ней и угрозой на этой дистанции (см) и переносим кандидата
	 * ВПЛОТНУЮ к стене с НАШЕЙ стороны.
	 *
	 * Зачем: кольцевой сэмплинг даёт 48 точек на всю зону хода, а «в укрытии»
	 * считается только полоса шириной CoverTraceDistance (120 см) вдоль стены —
	 * попасть в неё случайной точкой почти нельзя. Отсюда жалоба «бегут к
	 * укрытию, но встают не с той стороны и остаются под прямой линией огня».
	 * Прилипание превращает «рядом со стеной» в «за стеной» и само выбирает
	 * правильную сторону: точка всегда ставится со стороны юнита.
	 *
	 * 0 — выключить прилипание (прежнее поведение).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Positioning", meta = (ClampMin = "0"))
	float CoverSnapDistance = 600.f;

	// --- Утилити-выбор действия (ADR-1) ---------------------------------------

	/**
	 * НАБОР ВАРИАНТОВ ДЕЙСТВИЯ. Каждый оценивает себя сам; побеждает лучший скор.
	 * Новое поведение (овервотч, глухая оборона, граната, перезарядка) = новый
	 * наследник UAIActionEvaluator в этом списке, БЕЗ правок остальных.
	 *
	 * Дефолтный набор собирается в конструкторе. В BP-наследнике контроллера его
	 * можно дополнить или перевесить (Weight = 0 выключает вариант, не удаляя).
	 */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Tactics|AI|Actions")
	TArray<TObjectPtr<UAIActionEvaluator>> ActionEvaluators;

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
	 *
	 * PUBLIC, потому что его зовут оценщики действий (AIActionEvaluators.cpp).
	 * ⚠️ Метод ДОРОГОЙ (48 точек × трейсы LOS) — вызывать только из ScoreAction,
	 * после того как IsApplicable отсеял заведомо неподходящие варианты.
	 */
	bool FindCoverPoint(AUnitBase* Unit, const AActor* Threat, float PathBudget, bool bRetreat,
		FVector& OutPoint, bool bAdvance = false, FAICoverPointResult* OutDetails = nullptr);

	/**
	 * Сколько угроз максимум учитывать при оценке точки (аналог
	 * `MAX_EXPECTED_ENEMY_COUNT` в XCOM, там 4). Берутся ближайшие: проверка
	 * линии огня из каждой точки-кандидата по каждой угрозе — самая дорогая
	 * часть перебора, и её надо ограничивать сверху.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Positioning", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxScoredThreats = 4;

	/**
	 * Вес «сколько врагов видно из точки» (XCOM `fEnemyVisibility`). Точка, из
	 * которой не видно НИКОГО, получает −1 × вес: укрытие, из которого нельзя
	 * ответить, — это не позиция, а угол.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Weights")
	float EnemyVisibilityWeight = 20.f;

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

	/** Копирует профиль в runtime-поля и создаёт личные экземпляры оценщиков для контроллера. */
	void ApplyBehaviorProfile();

	/** Колбэк перцепции: увидел/потерял враждебного актора → смена тревоги. */
	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, struct FAIStimulus Stimulus);

	/** Один шаг хода: атака / движение по состоянию / завершение — пока есть AP. */
	void AdvanceTurnStep();

	/** Действия шага в состоянии Combat. true — шаг обработан (ждём таймер/движение). */
	bool StepCombat(AUnitBase* Unit);

	/**
	 * Собирает СНИМОК МИРА на одну активацию: видимые угрозы, AP, укрытие,
	 * можно ли стрелять. Считается один раз и раздаётся всем оценщикам —
	 * иначе каждый пересобирал бы списки (инвариант 1b).
	 */
	FAIDecisionContext BuildDecisionContext(AUnitBase* Unit, AActor* PrimaryThreat);

	/** Строит воспроизводимое зерно: карта + номер хода + имя юнита + шаг решения. */
	uint32 BuildDecisionSeed(const AUnitBase* Unit, FName Salt = NAME_None) const;

	/**
	 * Выбор лучшего действия перебором оценщиков. Перебор идёт по УБЫВАНИЮ
	 * верхней границы скора и обрывается, как только текущий лучший результат
	 * её достиг: дорогие поиски позиции не выполняются впустую.
	 * При включённом `xru1.AI.LogCombat` печатает ВСЕ рассмотренные варианты со
	 * скорами — без этого утилити-решение необъяснимо (ADR-1.6).
	 */
	FAIDecision DecideAction(const FAIDecisionContext& Context);

	/**
	 * Исполняет готовое решение. Разделение «решил / сделал» намеренное: решение
	 * можно залогировать и показать ДО того, как оно изменит мир.
	 * true — шаг обработан (ждём таймер/движение), false — активация окончена.
	 */
	bool ExecuteDecision(AUnitBase* Unit, const FAIDecision& Decision);

	/** Старт манёвра к точке: помечает ход и запоминает точку для продолжения. */
	bool StartManeuverTo(AUnitBase* Unit, const FVector& Point, const TCHAR* Reason);

	/**
	 * Выстрел AI по цели. Штатный путь — событие Event.Attack: те же правила,
	 * стоимость AP (включая XCOM-сжигание остатка) и BP-хуки (VFX/анимация),
	 * что и у выстрела игрока. Прямой ResolveShot-фолбэк запрещён: отсутствие
	 * AttackAbilityClass — configuration error, а не второй боевой pipeline.
	 * false — action не принят (способность отказала): ход юнита завершается,
	 * чтобы шаг не зациклился на неоплаченном действии.
	 */
	bool TryFireAtTarget(AUnitBase* Unit, AActor* Target);

	/**
	 * Активирует способность БЕЗ ЦЕЛИ на самом юните (наблюдение, глухая
	 * оборона). Успехом считается факт СПИСАНИЯ AP, а не возврат
	 * TryActivateAbilityByClass — тот же критерий, что у TryFireAtTarget:
	 * способность может активироваться и тут же отказаться по своим правилам,
	 * и тогда шаг хода повторился бы вхолостую.
	 */
	bool TryActivateSelfAbility(AUnitBase* Unit, TSubclassOf<class UTacticalAbility> AbilityClass);

public:
	/**
	 * ПРИОСТАНОВИТЬ/ПРОДОЛЖИТЬ текущее перемещение, не отменяя приказ. Нужно
	 * реакционному выстрелу: в XCOM овервотч ПРЕРЫВАЕТ бегущего — он замирает,
	 * выстрел разыгрывается, и только потом он бежит дальше.
	 *
	 * ⚠️ Именно `PauseMove`, а не `StopMovement`: последний отменил бы маршрут
	 * целиком, и боец потерял бы ход. Возвращает false, если приостанавливать
	 * нечего (юнит не в пути).
	 */
	bool SetMovementPaused(bool bPaused);

protected:

	/** Действия шага в состоянии Investigate. */
	bool StepInvestigate(AUnitBase* Unit);

	/** Действия шага в состоянии Patrol. */
	bool StepPatrol(AUnitBase* Unit);

	/** Движение к точке с обрезкой по бюджету пути юнита (1 AP). true — движение началось. */
	bool MoveWithBudget(AUnitBase* Unit, const FVector& Goal, float AcceptanceRadius,
		int32 MaxActionPoints = 1);

	/** Завершает ход юнита и уведомляет TurnManager. */
	void FinishUnitTurn();

	/** Планирует следующий шаг хода через ActionInterval. */
	void ScheduleNextStep();

	/** Директор групповой активации и общей памяти контактов (может быть nullptr). */
	class UTacticalAIDirectorSubsystem* GetAIDirector() const;

	/** Применяет текущие Sight-параметры к перцепции (после смены профиля). */
	void RefreshPerceptionConfig();

	/** Рисует принятое решение в мире при `xru1.AI.DebugDraw 1`. */
	void DrawDecisionDebug(const AUnitBase* Unit, const struct FAIDecision& Decision) const;

	/** Ждёт полного финиша route + cover-hug + turn-in-place. */
	void BeginMoveSettlement(AUnitBase* Unit);

public:
	/**
	 * Токен «этот маршрут заказал игрок». Ставится ATacticalPlayerController сразу
	 * после принятого MoveAlongRoute. Только помеченное перемещение публикует
	 * `Movement.Settled.*`: служебный подшаг стрелка и маршрут AI — не приказ.
	 */
	void MarkPlayerOrderedMove() { bPlayerOrderedMove = true; }

	/**
	 * Сценарный приказ обучения: в свою ближайшую активацию юнит обязан стрелять
	 * именно по этой цели, минуя utility-выбор. Выстрел при этом идёт обычным
	 * pipeline (GA_Attack → montage → FireCommit → урон), а не подменяет урон.
	 * Приказ одноразовый: он снимается в момент активации способности.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Scripted")
	void SetScriptedAttackOrder(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Tactics|AI|Scripted")
	void ClearScriptedAttackOrder();

	UFUNCTION(BlueprintPure, Category = "Tactics|AI|Scripted")
	bool HasScriptedAttackOrder() const { return ScriptedAttackTarget.IsValid(); }

protected:

	/** Таймерная проверка и единая финализация тактического перемещения. */
	void TryFinalizeMoveSettlement();

	/**
	 * Лучшая цель по АДДИТИВНОМУ СКОРИНГУ (A3, правила XCOM — см.
	 * docs/08_AI.md): шанс попадания + провокация + фланг +
	 * добивание + ранение, минус штраф за тяжелораненого.
	 *
	 * Раньше здесь было «ближайший видимый, провоцирующий вне очереди» — самое
	 * слабое место всего AI: расстояние не коррелирует ни с шансом попасть
	 * (у нас есть и профиль оружия, и укрытие), ни с реальной угрозой.
	 */
	AActor* FindVisibleTarget() const;

	/** Скор одной цели для FindVisibleTarget. Чистая функция, мир не меняет. */
	float ScoreTarget(const AUnitBase* Unit, const AActor* Candidate) const;

	/** Все живые враждебные акторы, видимые перцепцией (для скоринга против ВСЕХ угроз, фаза A5). */
	void GatherVisibleThreats(TArray<TObjectPtr<AActor>>& OutThreats) const;

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

	/** Порядковый номер фактического решения в текущей активации юнита. */
	int32 DecisionOrdinalThisTurn = 0;

	/**
	 * Цели, атака по которым НЕ АКТИВИРОВАЛАСЬ в этом ходу (нет линии огня из
	 * замороженной позиции и т.п.). Повтор бессмыслен — мир не менялся, а без
	 * блокировки детерминированное утилити повторяло отклонённый выстрел вечно.
	 * Сбрасывается в начале каждого хода.
	 */
	TArray<TWeakObjectPtr<AActor>> FailedAttackTargetsThisTurn;

	/** Сценарное сближение с невидимой целью уже пробовали в этом ходу. */
	bool bScriptedRepositionTried = false;

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

	/**
	 * Точка, которую AI ВЫБРАЛ (до всех обрезок бюджетом и выталкиваний из
	 * занятых клеток). Нужна ровно для одного: проверить по прибытии, что боец
	 * встал ТУДА, КУДА РЕШИЛ, а не «где получилось».
	 *
	 * Расхождение здесь — это расхождение плана и факта: укрытие оценивалось в
	 * одной точке, а юнит стоит в другой, где оно может не работать. Молча
	 * такое пропускать нельзя, поэтому при отклонении пишем в лог.
	 */
	FVector ChosenManeuverPoint = FVector::ZeroVector;
	bool bHasChosenManeuverPoint = false;

	/** Порог расхождения «решил / встал» (см), выше которого пишем в лог. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|AI|Positioning", meta = (ClampMin = "10"))
	float ManeuverArrivalTolerance = 120.f;

	FTimerHandle TurnStepTimerHandle;

	/** Отдельный таймер: новый AI-шаг нельзя запускать, пока юнит доводит позу у стены. */
	FTimerHandle MoveSettlementTimerHandle;
	TWeakObjectPtr<AUnitBase> PendingSettlementUnit;

	/** Поведенческие оси активного профиля (копия — профиль может быть заменён). */
	FAIStyleTuning Style;

	/** Исходные веса оценщиков до множителей стиля — чтобы они не накапливались. */
	TMap<TObjectPtr<UAIActionEvaluator>, float> BaseEvaluatorWeights;

	/** Текущий маршрут заказан игроком (см. MarkPlayerOrderedMove). */
	bool bPlayerOrderedMove = false;

	/**
	 * Все пригодные предложения последнего решения по убыванию скора.
	 * Нужен фолбэк: провалившееся исполнение лучшего варианта не должно съедать
	 * весь ход бойца (см. StepCombat).
	 */
	TArray<FAIDecision> RankedDecisions;

	/** Цель сценарного приказа обучения (см. SetScriptedAttackOrder). */
	TWeakObjectPtr<AActor> ScriptedAttackTarget;

	/**
	 * Стоимость запланированного перемещения в AP. Раньше финализация всегда
	 * списывала ровно 1 AP, поэтому манёвр приходилось планировать бюджетом в
	 * одно очко — и боец останавливался на полпути к укрытию.
	 */
	int32 PendingMoveActionPointCost = 1;

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
