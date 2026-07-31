#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeTaskBase.h"
#include "TutorialActionGate.h"
#include "TutorialPresentation.h"
#include "TacticsTypes.h"
#include "TacticalQuestTasks.generated.h"

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

	UPROPERTY()
	float ElapsedTime = 0.f;
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
