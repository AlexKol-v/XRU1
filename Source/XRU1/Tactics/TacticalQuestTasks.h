#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"
#include "TutorialActionGate.h"
#include "TutorialPresentation.h"
#include "TacticsTypes.h"
#include "Templates/SubclassOf.h"
#include "TacticalQuestTasks.generated.h"

class UFogRevealableComponent;
class UTacticalAbility;

// Задачи обучения XRU1 для StateTree.
//
// Плагин STQuestSystem остаётся доменно-нейтральным: он умеет считать события
// на канале. Всё, что знает про бойцов, голограммы, укрытия и Action Gate,
// живёт здесь, в игровом модуле, и не тащит тактику в чужой плагин.

// --- Objective с проверкой payload --------------------------------------------

USTRUCT()
struct FTacticalTask_ObjectiveInstanceData
{
	GENERATED_BODY()

	/** Идентификатор цели (Quest.Objective.Tutorial.*) — для HUD и снимка прогресса. */
	UPROPERTY(EditAnywhere, Category = "Quest")
	FGameplayTag ObjectiveId;

	/** Точный leaf-канал подтверждённого результата. */
	UPROPERTY(EditAnywhere, Category = "Quest")
	FGameplayTag EventChannel;

	/**
	 * Второй допустимый leaf (пусто — только основной). Адаптивность шагов
	 * движения: точку можно ставить КУДА УГОДНО — перебежка засчитывается и
	 * `Settled.Open`, и `Settled.InCover`, если рядом с точкой оказалась
	 * преграда. Сравнение всегда точное.
	 */
	UPROPERTY(EditAnywhere, Category = "Quest")
	FGameplayTag EventChannelAlt;

	/**
	 * Tactical-шаги слушают leaf: один выстрел публикует Attack.* И
	 * Enemy.Eliminated, а последняя эвакуация — Evac.Unit И Evac.Squad.
	 */
	UPROPERTY(EditAnywhere, Category = "Quest")
	bool bRequireExactChannel = true;

	UPROPERTY(EditAnywhere, Category = "Quest", meta = (ClampMin = "1"))
	int32 RequiredCount = 1;

	/** Короткая инструкция шага для трекера HUD. */
	UPROPERTY(EditAnywhere, Category = "Quest")
	FText Description;

	/** AnchorId бойца, чьё действие засчитывается. Пусто — любой. */
	UPROPERTY(EditAnywhere, Category = "Quest|Payload")
	FName RequiredSourceAnchor;

	/** AnchorId цели действия (голограмма, зона, бомба). Пусто — любая. */
	UPROPERTY(EditAnywhere, Category = "Quest|Payload")
	FName RequiredTargetAnchor;

	/**
	 * Считать только РАЗНЫЕ источники. Без этого шаг «двое разных бойцов вошли
	 * в сектор» закрывается одним бойцом, вошедшим дважды.
	 */
	UPROPERTY(EditAnywhere, Category = "Quest|Payload")
	bool bRequireDistinctSources = false;

	UPROPERTY()
	int32 CurrentCount = 0;

	/** Источники, уже засчитанные при bRequireDistinctSources. */
	UPROPERTY()
	TArray<TWeakObjectPtr<UObject>> CountedSources;

	/** Поколение запуска на момент EnterState: поздние события старого run игнорируются. */
	UPROPERTY()
	int32 ScenarioRunId = 0;
};

/**
 * Цель обучения с проверкой payload: канал + конкретный боец + конкретная цель
 * + «разные источники». Обычной Quest Objective этого не хватает — она видит
 * только тег и потому не отличает Медика от Танка, а Holo A от Holo B.
 */
USTRUCT(meta = (DisplayName = "Tactical Objective", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_Objective : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_Objective();

	using FInstanceDataType = FTacticalTask_ObjectiveInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// --- Action Gate ---------------------------------------------------------------

USTRUCT()
struct FTacticalTask_ApplyActionGateInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Tutorial")
	FTutorialActionPolicy Policy;

	UPROPERTY()
	int32 PolicyToken = 0;
};

/**
 * Держит политику Action Gate, пока состояние активно. Enter применяет,
 * Exit снимает — именно поэтому StateTree не превращается в систему ввода:
 * он только объявляет политику, а разрешение считает один арбитр.
 */
USTRUCT(meta = (DisplayName = "Apply Action Gate", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_ApplyActionGate : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_ApplyActionGate();

	using FInstanceDataType = FTacticalTask_ApplyActionGateInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// --- Включение staged-актора ---------------------------------------------------

USTRUCT()
struct FTacticalTask_SetScenarioActorActiveInstanceData
{
	GENERATED_BODY()

	/** AnchorId акторов, состояние которых меняем. */
	UPROPERTY(EditAnywhere, Category = "Scenario")
	TArray<FName> AnchorIds;

	UPROPERTY(EditAnywhere, Category = "Scenario")
	bool bActive = true;

	/** Вернуть прежнее состояние при выходе из состояния (временная подсветка). */
	UPROPERTY(EditAnywhere, Category = "Scenario")
	bool bRestoreOnExit = false;
};

/**
 * Единая точка «проявления» голограммы: presentation, collision, tick и участие
 * в сторонах боя переключаются вместе. Скрытый, но живой для perception актор
 * ломал бы укрытия и AI.
 *
 * Задача МГНОВЕННАЯ. Пауза перед активацией делается отдельным состоянием с
 * движковым `Delay Task` (см. docs/03_ARCHITECTURE.md §9): задержка внутри задачи не
 * останавливает соседние задачи состояния и потому не является паузой шага.
 */
USTRUCT(meta = (DisplayName = "Set Scenario Actor Active", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_SetScenarioActorActive : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_SetScenarioActorActive();

	using FInstanceDataType = FTacticalTask_SetScenarioActorActiveInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// --- Восстановление очков действия ----------------------------------------------

USTRUCT()
struct FTacticalTask_SetActionPointsInstanceData
{
	GENERATED_BODY()

	/** AnchorId бойцов, которым восстановить очки (пусто — никому). */
	UPROPERTY(EditAnywhere, Category = "Scenario")
	TArray<FName> AnchorIds;
};

/**
 * Полный запас ОД названным бойцам при входе в состояние. Режиссёрская
 * «дозаправка»: Оса после выстрела B5 начинает C0 с полными очками, Кадет
 * после фланга C3 сразу идёт к бомбе — без лишнего «нажмите Enter, чтобы
 * продолжить». Возвращает Succeeded сразу (задача-действие).
 */
USTRUCT(meta = (DisplayName = "Set Action Points", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_SetActionPoints : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_SetActionPoints();

	using FInstanceDataType = FTacticalTask_SetActionPointsInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// --- Сценарный выстрел ---------------------------------------------------------

USTRUCT()
struct FTacticalTask_ScriptedShotInstanceData
{
	GENERATED_BODY()

	/** AnchorId стреляющей голограммы. */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FName ShooterAnchorId;

	/** AnchorId бойца-цели. */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FName TargetAnchorId;

	/** Форс исхода: 100% — гарантированное попадание, 0% — гарантированный промах. */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FScriptedShotOverride Shot;

	/**
	 * Сколько секунд ждать подтверждённого commit, прежде чем признать шаг
	 * сорванным. Без предела дерево зависло бы навсегда на несостоявшемся
	 * выстреле.
	 */
	UPROPERTY(EditAnywhere, Category = "Scripted", meta = (ClampMin = "1"))
	float Timeout = 20.f;

	UPROPERTY()
	float ElapsedTime = 0.f;

	UPROPERTY()
	bool bOrderIssued = false;
};

/**
 * Заказывает сценарный выстрел голограммы по бойцу через ОБЫЧНЫЙ attack
 * pipeline и ждёт подтверждённого результата: приказ ставится AI-контроллеру,
 * а форс меняет только числа snapshot'а. Прямого урона из задачи нет.
 *
 * Задача завершается, когда транзакция выстрела закрылась после commit.
 */
USTRUCT(meta = (DisplayName = "Scripted Shot", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_ScriptedShot : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_ScriptedShot();

	using FInstanceDataType = FTacticalTask_ScriptedShotInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// --- Авторский такт (реплика/субтитр/фокус) -----------------------------------

USTRUCT()
struct FTacticalTask_TutorialBeatInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Tutorial")
	FTacticalTutorialBeat Beat;

	/**
	 * Событие, ПОСЛЕ которого реплика звучит. Пусто — играет на входе в шаг.
	 *
	 * Половина реплик — реакции на то, что происходит внутри шага: «Ай! За
	 * что?!» после выстрела врага, «Щекотно» после попадания в Молота, «Есть
	 * контакт» после срабатывания наблюдения, доклад Кадета — после того как он
	 * добежал. На входе в состояние они звучали ДО события и ломали сцену.
	 */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Триггер")
	FGameplayTag TriggerEvent;

	/** Фильтр источника события (AnchorId). Пусто — любой источник. */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Триггер")
	FName TriggerSourceAnchorId;

	/**
	 * Страховка: не дождались события за столько секунд — играем реплику всё
	 * равно. Иначе один несработавший сценарный выстрел вешал бы шаг навсегда.
	 * Игнорируется при `bRequireTriggerEvent`.
	 */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Триггер", meta = (ClampMin = "1"))
	float TriggerTimeout = 25.f;

	/**
	 * Без события реплику НЕ играть вообще (страховочный таймаут выключен).
	 *
	 * Обучение ведёт игрока по сцене, и там реплика обязана прозвучать, даже
	 * если постановка сорвалась. В бою наоборот: реакции вроде «Минус один» или
	 * «Держись!» описывают то, чего может не случиться, — первого убийства
	 * может не быть вовсе. Такая реплика, выданная по таймауту, врёт игроку.
	 *
	 * Ставится вместе со снятым `bConsideredForCompletion`: фоновая реакция ждёт
	 * своё событие сколько угодно и не мешает состоянию завершиться.
	 */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Триггер")
	bool bRequireTriggerEvent = false;

	/**
	 * После реплики задача остаётся Running и больше ничего не делает.
	 *
	 * Зачем: состояние с ОДНИМИ фоновыми репликами не имеет ни одной задачи,
	 * учитываемой для завершения, и завершается сразу, как только первая из них
	 * отговорит. Для родительского состояния миссии это означало бесконечный
	 * перезапуск всего дерева — реплика Осы стартовала снова и снова.
	 * Реакция обязана «повиснуть» после своего одного раза:
	 * состояние живёт, пока живут его дочерние шаги.
	 */
	UPROPERTY(EditAnywhere, Category = "Tutorial|Триггер")
	bool bKeepRunningAfterBeat = false;

	UPROPERTY()
	float ElapsedTime = 0.f;

	/**
	 * Такт показан и ещё не закрыт. Закрытие происходит по `Beat.Duration` в
	 * Tick, а НЕ в ExitState: выход из состояния наступает только когда шаг
	 * выполнен игроком целиком, и субтитр с удержанием камеры жили бы всё это
	 * время.
	 */
	UPROPERTY()
	bool bBeatStarted = false;

	/** Играется ответная реплика обмена (см. FTacticalTutorialBeat). */
	UPROPERTY()
	bool bFollowUpStarted = false;

	/** Такт отговорил: повторно не запускаем, состояние уже может жить дальше. */
	UPROPERTY()
	bool bBeatFinished = false;
};

/**
 * Показывает реплику «Купола» и наводит камеру. Это presentation-задача: она
 * НЕ засчитывает objective — шаг C1 закрывается именно завершением такта, а
 * боевые шаги считают отдельные Tactical Objective.
 */
USTRUCT(meta = (DisplayName = "Tutorial Beat", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_TutorialBeat : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_TutorialBeat();

	using FInstanceDataType = FTacticalTask_TutorialBeatInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

private:
	/** Запустить реплику (общая точка входа и триггера). */
	bool StartBeatNow(FStateTreeExecutionContext& Context, FInstanceDataType& Inst) const;

	/**
	 * Камера занята кадром выстрела, а такту есть что показать (задан фокус).
	 * Такой такт ждёт: шаг уже сменился (это решает квест-логика), но реплика
	 * о раненом не должна звучать, пока камера доигрывает kill-cam.
	 */
	bool IsCameraBusyForBeat(FStateTreeExecutionContext& Context,
		const FInstanceDataType& Inst) const;
};

// --- Сценарное перемещение ------------------------------------------------------

USTRUCT()
struct FTacticalTask_ScriptedMoveInstanceData
{
	GENERATED_BODY()

	/** AnchorId бойца (свой или враг), который побежит. */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FName UnitAnchorId;

	/** AnchorId точки назначения (AScenarioAnchorPoint). */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FName DestinationAnchorId;

	/** Радиус приёмки прибытия, см. */
	UPROPERTY(EditAnywhere, Category = "Scripted", meta = (ClampMin = "30"))
	float AcceptanceRadius = 120.f;

	/** Провал шага, если боец не добежал за это время (сек). */
	UPROPERTY(EditAnywhere, Category = "Scripted", meta = (ClampMin = "1"))
	float Timeout = 30.f;

	/**
	 * Обнулить AP по прибытии: выбежавший статист («Кадет отступает») не должен
	 * светить полными пипсами и оставаться формально боеспособным в этот ход.
	 */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	bool bDrainActionPointsOnArrival = false;

	/**
	 * Камера сопровождает бегущего до прибытия. Автовозврат фокуса на отряд в
	 * это время подавлен (режиссура владеет камерой, пока активен lock-шаг).
	 */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	bool bCameraFollowUnit = true;

	UPROPERTY()
	float ElapsedTime = 0.f;

	UPROPERTY()
	bool bOrderIssued = false;

	UPROPERTY()
	bool bCameraAttached = false;

	/**
	 * Взятое у тумана войны удержание «показывать этого актора». Постановка
	 * главнее LOS: такт ведёт камеру за бойцом, и он обязан быть на экране, даже
	 * если отряд его формально не видит (у Firaxis это ветка `m_bInMatinee`
	 * в `ForceModelVisible`).
	 *
	 * Слабая ссылка на КОМПОНЕНТ, а не флаг: удержание надо снять именно с того
	 * актора, у которого взяли, а к `ExitState` поиск по AnchorId может уже не
	 * найти его (голограмма выключена следующим шагом) — и удержание утекло бы,
	 * оставив врага видимым навсегда.
	 */
	UPROPERTY()
	TWeakObjectPtr<UFogRevealableComponent> FogRevealHold;

	/**
	 * Парное к `FogRevealHold` раскрытие МЕСТНОСТИ вокруг актора
	 * (`UFogGridSubsystem::AddScriptedReveal`). Показать бойца мало: карта
	 * стартует чёрной, и постановка игралась бы в пустоте. 0 — не взято.
	 */
	UPROPERTY()
	int32 FogAreaRevealHandle = 0;
};

/**
 * Постановочная перебежка вне обычного хода: боец (союзник или враг) бежит к
 * якорю по навигации, задача держит состояние до фактического прибытия.
 * AP не тратятся и quest-события движения не публикуются — это режиссура
 * («штурмовик отступает», «враг выбегает на позицию»), а не приказ игрока.
 * Деактивированного актора сначала включает Set Scenario Actor Active.
 */
USTRUCT(meta = (DisplayName = "Scripted Move", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_ScriptedMove : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_ScriptedMove();

	using FInstanceDataType = FTacticalTask_ScriptedMoveInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// --- Сценарный ход врага ---------------------------------------------------------

USTRUCT()
struct FTacticalTask_ScriptedEnemyTurnInstanceData
{
	GENERATED_BODY()

	/** AnchorId врага, чей ближайший ход играется по программе. */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FName UnitAnchorId;

	/** Первый шаг: постановочный выход к якорю БЕЗ траты ОД (пусто — нет шага). */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FName FreeMoveAnchorId;

	/** Второй шаг: перебежка к якорю за 1 ОД (пусто — нет шага). */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FName PaidMoveAnchorId;

	/**
	 * Финальный шаг: способность на себе за свои ОД (обычно Глухая оборона).
	 * Недостающий грант выдаётся на лету; отказ способности шаг не валит.
	 */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	TSubclassOf<UTacticalAbility> FinishAbility;

	/** Провал шага, если программа не исполнилась за это время (сек). */
	UPROPERTY(EditAnywhere, Category = "Scripted", meta = (ClampMin = "1"))
	float Timeout = 60.f;

	/** Камера сопровождает врага, пока программа исполняется. */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	bool bCameraFollowUnit = true;

	UPROPERTY()
	float ElapsedTime = 0.f;

	UPROPERTY()
	bool bProgramSet = false;

	UPROPERTY()
	bool bCameraAttached = false;

	/**
	 * Взятое у тумана войны удержание «показывать этого актора». Постановка
	 * главнее LOS: такт ведёт камеру за бойцом, и он обязан быть на экране, даже
	 * если отряд его формально не видит (у Firaxis это ветка `m_bInMatinee`
	 * в `ForceModelVisible`).
	 *
	 * Слабая ссылка на КОМПОНЕНТ, а не флаг: удержание надо снять именно с того
	 * актора, у которого взяли, а к `ExitState` поиск по AnchorId может уже не
	 * найти его (голограмма выключена следующим шагом) — и удержание утекло бы,
	 * оставив врага видимым навсегда.
	 */
	UPROPERTY()
	TWeakObjectPtr<UFogRevealableComponent> FogRevealHold;

	/**
	 * Парное к `FogRevealHold` раскрытие МЕСТНОСТИ вокруг актора
	 * (`UFogGridSubsystem::AddScriptedReveal`). Показать бойца мало: карта
	 * стартует чёрной, и постановка игралась бы в пустоте. 0 — не взято.
	 */
	UPROPERTY()
	int32 FogAreaRevealHandle = 0;
};

/**
 * Сценарная программа ближайшего ХОДА врага (шаг C1): бесплатный «выход
 * из-за укрытия» (Наблюдение игрока реагирует штатно) → перебежка за 1 ОД →
 * способность (Глухая оборона). Программа исполняется в фазу врага вместо
 * utility-AI, свободного выстрела в остаток ОД не бывает; задача держит
 * состояние до полного исполнения. Арминг — в фазу игрока (инвариант §5.3-3).
 */
USTRUCT(meta = (DisplayName = "Scripted Enemy Turn", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_ScriptedEnemyTurn : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_ScriptedEnemyTurn();

	using FInstanceDataType = FTacticalTask_ScriptedEnemyTurnInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// --- Форс следующего выстрела (гарантированные попадания игрока) ----------------

USTRUCT()
struct FTacticalTask_ForceNextShotInstanceData
{
	GENERATED_BODY()

	/** AnchorId бойца, чей СЛЕДУЮЩИЙ выстрел форсится (обычно боец игрока). */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FName UnitAnchorId;

	/** Числа форса: 100% — учебное «попадание гарантировано». */
	UPROPERTY(EditAnywhere, Category = "Scripted")
	FScriptedShotOverride Shot;
};

/**
 * XCOM-туториал форсит ключевые выстрелы ИГРОКА: промах в A8/B5 иначе оставляет
 * шаг без AP и без права End Turn. Задача взводит форс на входе в состояние и
 * снимает непотраченный на выходе; сам выстрел игрок делает обычной атакой.
 */
USTRUCT(meta = (DisplayName = "Force Next Shot", Category = "XRU1 Tutorial"))
struct XRU1_API FTacticalTask_ForceNextShot : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_ForceNextShot();

	using FInstanceDataType = FTacticalTask_ForceNextShotInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// --- Вызов подкрепления (сценарный беат миссии) --------------------------------

USTRUCT()
struct FTacticalTask_CallReinforcementsInstanceData
{
	GENERATED_BODY()

	/**
	 * BeaconId маяка. Пусто — первый маяк на карте: на миссии с единственной
	 * точкой высадки лишний идентификатор только плодит рассинхрон.
	 */
	UPROPERTY(EditAnywhere, Category = "Reinforcements")
	FName BeaconId;
};

/**
 * Сценарный вызов подкрепления. Нужен там, где волна привязана к беату миссии
 * («заряд снят — отход становится гонкой»), а не к просадке вражеской стороны.
 *
 * В XCOM 2 это тот же путь: `bKismetInitiatedReinforcements` — подкрепление,
 * запрошенное скриптом уровня, а не расписанием. Сам маяк остаётся владельцем
 * правил: он решает, не превышен ли лимит волн и когда именно высаживать.
 */
USTRUCT(meta = (DisplayName = "Call Reinforcements", Category = "XRU1 Mission"))
struct XRU1_API FTacticalTask_CallReinforcements : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FTacticalTask_CallReinforcements();

	using FInstanceDataType = FTacticalTask_CallReinforcementsInstanceData;
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
