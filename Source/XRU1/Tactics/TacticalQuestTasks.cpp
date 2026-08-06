#include "TacticalQuestTasks.h"
#include "XRU1Log.h"

#include "ActionPointsComponent.h"
#include "FogGridSubsystem.h"       // постановка раскрывает и МЕСТНОСТЬ вокруг бойца
#include "FogRevealableComponent.h" // постановка показывает бойца поверх правил тумана
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GA_Attack.h"
#include "Navigation/PathFollowingComponent.h"
#include "QuestRunnerActor.h"
#include "QuestTypes.h"
#include "ScenarioActorRegistry.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"
#include "MoveRangeVisualizer.h"
#include "TacticalEncounter.h" // маяк подкрепления для задачи Call Reinforcements
#include "TacticalCameraPawn.h"
#include "TacticalPlayerController.h"
#include "TacticsCombatStatics.h"
#include "TacticalQuestEvents.h"
#include "TacticalQuestZone.h"
#include "TurnManagerSubsystem.h" // watchdog сценарных задач тикает только в фазу врага
#include "UnitAIController.h"
#include "UnitBase.h"

namespace TacticalQuestTasks_Internal
{
	UWorld* GetWorld(FStateTreeExecutionContext& Context)
	{
		const AQuestRunnerActor* Runner = AQuestRunnerActor::GetFromContext(Context);
		return Runner ? Runner->GetWorld() : nullptr;
	}

	UTacticalScenarioSubsystem* GetScenarioRegistry(FStateTreeExecutionContext& Context)
	{
		UWorld* World = GetWorld(Context);
		return World ? World->GetSubsystem<UTacticalScenarioSubsystem>() : nullptr;
	}

	/** Снять удержание ровно с того компонента, у которого оно было взято. */
	void ReleaseFogReveal(UWorld* World, TWeakObjectPtr<UFogRevealableComponent>& InOutHold,
		int32& InOutAreaHandle)
	{
		if (UFogRevealableComponent* Reveal = InOutHold.Get())
		{
			Reveal->RemoveScriptedRevealHold();
		}
		InOutHold = nullptr;

		if (UFogGridSubsystem* FogGrid = World ? World->GetSubsystem<UFogGridSubsystem>() : nullptr)
		{
			FogGrid->RemoveScriptedReveal(InOutAreaHandle);
		}
		InOutAreaHandle = 0;
	}

	/**
	 * Взять у тумана войны удержание «показывать этого актора» на время такта — и
	 * заодно раскрыть вокруг него местность.
	 *
	 * Постановка главнее LOS: шаг ведёт камеру за бойцом, и пустой кадр вместо
	 * него — это не «правильно сработавший туман», а сломанное обучение. Ровно
	 * такое исключение есть у Firaxis: `XComGameState_Unit::ForceModelVisible`
	 * возвращает `eForceVisible`, пока юнит в matinee или в скампере.
	 *
	 * ⚠️ Показать самого бойца мало. Карта стартует чёрной, поэтому кадр вокруг
	 * него был бы «фигура в пустоте»: у XCOM ровно на этот случай раскрытие
	 * ОБЛАСТИ — `X2Action_RevealAIBegin.RevealFOWRadius`. Оба удержания парные и
	 * снимаются вместе, чтобы не разъехались.
	 */
	void HoldFogReveal(UWorld* World, AActor* Actor,
		TWeakObjectPtr<UFogRevealableComponent>& OutHold, int32& OutAreaHandle)
	{
		// Такт не может держать два удержания сразу.
		ReleaseFogReveal(World, OutHold, OutAreaHandle);
		if (UFogRevealableComponent* Reveal = Actor
			? Actor->FindComponentByClass<UFogRevealableComponent>() : nullptr)
		{
			Reveal->AddScriptedRevealHold();
			OutHold = Reveal;
		}
		// Раскрытие берётся только под РЕАЛЬНОГО актора: без него точка была бы
		// нулевой, и такт осветил бы центр карты вместо места действия.
		if (UFogGridSubsystem* FogGrid = (World && Actor) ? World->GetSubsystem<UFogGridSubsystem>() : nullptr)
		{
			// Раскрытие едет ЗА актором: сценарная перебежка проходит по секторам,
			// которых отряд не разведал, и застывшая на старте область показала бы
			// бойца, убегающего в темноту.
			OutAreaHandle = FogGrid->AddScriptedReveal(Actor, FVector::ZeroVector);
		}
	}

	// --- Страховочные таймеры сценария (watchdog) --------------------------------
	//
	// Правило: страховочный таймер считает НЕ астрономическое время, а время, в
	// течение которого ожидаемое событие вообще МОГЛО случиться.
	//
	// Зачем: таймауты сценарных задач существуют против зависшей постановки —
	// не выданного приказа, не заспавнившегося контроллера, недостижимой точки.
	// Но сценарный выстрел и сценарный ход врага исполняются ТОЛЬКО в фазу
	// врага, а фаза врага наступает лишь после того, как игрок завершит свой ход.
	// Пока таймер тикал вслепую, он отмерял в первую очередь раздумья игрока:
	// достаточно было отойти от компьютера на минуту, чтобы шаг обучения ушёл в
	// Failed и уронил весь сценарий — при полностью исправной постановке.
	// Игрок, который думает или отошёл, — это не сбой, и watchdog не имеет права
	// его наказывать.
	//
	// Поэтому таймер сценарной задачи замирает в чужую фазу: приказ поставлен в
	// ход игрока, исполнение возможно только в ход врага — до него ждать нечего.
	// Защита от реального зависания сохраняется полностью: как только условие
	// «событие могло бы произойти» выполнено, отсчёт идёт как раньше.
	//
	// ⚠️ У Tutorial Beat гейта НЕТ намеренно. Его таймаут — не приговор шагу, а
	// СПАСЕНИЕ: по нему реплика играет «как есть», когда триггер потерян или
	// камера не освободилась (пауза посреди кадра выстрела оставляла
	// IsPlayingPresentationFrame взведённым — лог 2026-08-05). Заморозка этого
	// таймаута по «игрок отошёл» превращала страховку в вечное ожидание: такт не
	// стартовал, состояние не завершалось, диалог вставал насмерть. Реплика,
	// сыгранная по таймауту при отошедшем игроке, — мелкая косметика; вставший
	// сценарий — поломка. Из двух зол выбрано меньшее.

	/** Фаза врага: только в ней исполняются сценарный выстрел и сценарный ход. */
	bool IsEnemyPhaseActive(FStateTreeExecutionContext& Context)
	{
		const UWorld* World = GetWorld(Context);
		const UTurnManagerSubsystem* Turns = World
			? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
		// Без менеджера ходов (тесты, недособранная карта) страховку не отключаем:
		// неизвестная фаза не должна превращать таймаут в вечное ожидание.
		return !Turns || Turns->GetCurrentPhase() == ETurnPhase::Enemy;
	}

	/**
	 * Один шаг страховочного таймера. `bCanProgress` — может ли ожидаемое
	 * событие случиться прямо сейчас; если нет, время не идёт вовсе.
	 * Раз в `IdleReportSeconds` печатает, ПОЧЕМУ таймер стоит: иначе замерший
	 * watchdog неотличим в логе от потерянного шага.
	 */
	void TickWatchdog(float& InOutElapsed, float DeltaTime, bool bCanProgress,
		const TCHAR* TaskName, const TCHAR* IdleReason)
	{
		if (bCanProgress)
		{
			InOutElapsed += DeltaTime;
			return;
		}

		constexpr double IdleReportSeconds = 30.0;
		static double LastReportTime = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastReportTime > IdleReportSeconds)
		{
			LastReportTime = Now;
			UE_LOG(LogXRU1Quest, Verbose,
				TEXT("[Watchdog] %s: отсчёт таймаута приостановлен (%s)"), TaskName, IdleReason);
		}
	}

	/**
	 * Печать входа в состояние, устойчивая к ПЕРЕЗАХОДАМ.
	 *
	 * `EnterState` обязан срабатывать один раз за вход, но неверно собранное
	 * дерево может перезаходить каждый тик — и тогда полезный лог перестаёт
	 * существовать: прогон 2026-08-04 дал 101 472 одинаковых блока подряд,
	 * 99.4 % файла. Первый вход печатается как обычно, повторы в пределах
	 * `RepeatWindow` копятся молча, а когда частота выдаёт цикл — ОДИН раз
	 * печатается предупреждение с числом входов. Диагностика от этого не
	 * теряется: наоборот, аномалия становится видна прямо в логе.
	 */
	bool ShouldLogStateEntry(const FString& Key, int32& OutSuppressed)
	{
		struct FEntryStat
		{
			double LastLogTime = 0.0;
			double WindowStart = 0.0;
			int32 CountInWindow = 0;
			int32 SuppressedSinceLog = 0;
			bool bLoopReported = false;
		};
		static TMap<FString, FEntryStat> Stats;

		// Порог «это цикл, а не активная сцена»: 20 входов в одно состояние за
		// секунду не бывает у корректного дерева даже на быстрых переходах.
		constexpr double RepeatWindow = 1.0;
		constexpr int32 LoopThreshold = 20;

		const double Now = FPlatformTime::Seconds();
		FEntryStat& Stat = Stats.FindOrAdd(Key);
		OutSuppressed = 0;

		if (Now - Stat.WindowStart > RepeatWindow)
		{
			Stat.WindowStart = Now;
			Stat.CountInWindow = 0;
			Stat.bLoopReported = false;
		}
		++Stat.CountInWindow;

		if (Stat.CountInWindow >= LoopThreshold)
		{
			if (!Stat.bLoopReported)
			{
				Stat.bLoopReported = true;
				UE_LOG(LogXRU1Quest, Error,
					TEXT("[Quest] %s перезаходит %d раз в секунду — состояние закрывается сразу "
						"после входа. Обычная причина: у состояния нет ни одной задачи, "
						"учитываемой для завершения (все фоновые). Дальнейшие входы не логируются."),
					*Key, Stat.CountInWindow);
			}
			++Stat.SuppressedSinceLog;
			return false;
		}

		// Обычный режим: печатаем, но не чаще раза в окно — с числом пропущенных.
		if (Stat.LastLogTime > 0.0 && Now - Stat.LastLogTime < RepeatWindow)
		{
			++Stat.SuppressedSinceLog;
			return false;
		}
		Stat.LastLogTime = Now;
		OutSuppressed = Stat.SuppressedSinceLog;
		Stat.SuppressedSinceLog = 0;
		return true;
	}

	/** Совпадает ли AnchorId объекта с требуемым. Пустой Anchor означает «любой». */
	bool MatchesAnchor(FName RequiredAnchor, const UObject* Object)
	{
		if (RequiredAnchor.IsNone())
		{
			return true;
		}
		const AActor* Actor = Cast<AActor>(Object);
		return Actor && UTacticalScenarioSubsystem::GetScenarioAnchorId(Actor) == RequiredAnchor;
	}
} // namespace TacticalQuestTasks_Internal

// --- FTacticalTask_Objective ---------------------------------------------------

FTacticalTask_Objective::FTacticalTask_Objective()
{
	// Цель событийная: без quest-события считать нечего, поллинг не нужен.
	bShouldCallTickOnlyOnEvents = true;
}

namespace
{
	/** Сообщает раннеру снимок прогресса цели — из него живёт трекер HUD. */
	void ReportTacticalObjective(FStateTreeExecutionContext& Context,
		const FTacticalTask_ObjectiveInstanceData& Inst)
	{
		AQuestRunnerActor* Runner = AQuestRunnerActor::GetFromContext(Context);
		if (!Runner)
		{
			return;
		}

		FObjectiveProgress Progress;
		Progress.ObjectiveId = Inst.ObjectiveId;
		Progress.Current = Inst.CurrentCount;
		Progress.Required = Inst.RequiredCount;
		Progress.Description = Inst.Description;
		Progress.State = (Inst.CurrentCount >= Inst.RequiredCount)
			? EObjectiveState::Completed
			: EObjectiveState::Active;
		Runner->ReportObjectiveProgress(Progress);
	}
}

EStateTreeRunStatus FTacticalTask_Objective::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	Inst.CurrentCount = 0;
	Inst.CountedSources.Reset();

	// Поколение запуска фиксируется на входе в шаг: всё, что придёт от прошлого
	// запуска общей карты, будет отброшено на этапе проверки payload.
	AQuestRunnerActor* Runner = AQuestRunnerActor::GetFromContext(Context);
	Inst.ScenarioRunId = Runner
		? UTacticalQuestEvents::GetScenarioRunId(Runner) : 0;

	if (!Inst.EventChannel.IsValid())
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[Quest] У цели %s не задан EventChannel"),
			*Inst.ObjectiveId.ToString());
		return EStateTreeRunStatus::Failed;
	}

	// Постоянный лог границ шага: без него «шаг не идёт дальше» неотличимо от
	// «событие не пришло» и «шаг вообще не начинался».
	int32 Suppressed = 0;
	if (TacticalQuestTasks_Internal::ShouldLogStateEntry(
			FString::Printf(TEXT("Objective %s"), *Inst.ObjectiveId.ToString()), Suppressed))
	{
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[Objective] НАЧАТ %s: ждём %s x%d (source=%s target=%s run=%d)%s"),
			*Inst.ObjectiveId.ToString(), *Inst.EventChannel.ToString(), Inst.RequiredCount,
			*Inst.RequiredSourceAnchor.ToString(), *Inst.RequiredTargetAnchor.ToString(),
			Inst.ScenarioRunId,
			Suppressed > 0 ? *FString::Printf(TEXT(" [+%d повторных входов]"), Suppressed) : TEXT(""));
	}

	// Цель-зона подсвечивается на время шага: игрок видит, КУДА бежать,
	// без отдельной задачи в дереве.
	if (UTacticalScenarioSubsystem* Registry =
			TacticalQuestTasks_Internal::GetScenarioRegistry(Context))
	{
		if (ATacticalQuestZone* Zone = Cast<ATacticalQuestZone>(
				Registry->FindScenarioActor(Inst.RequiredTargetAnchor)))
		{
			Zone->SetHighlighted(true);
		}
	}

	ReportTacticalObjective(Context, Inst);
	return EStateTreeRunStatus::Running;
}

void FTacticalTask_Objective::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Inst = Context.GetInstanceData(*this);
	if (UTacticalScenarioSubsystem* Registry =
			TacticalQuestTasks_Internal::GetScenarioRegistry(Context))
	{
		if (ATacticalQuestZone* Zone = Cast<ATacticalQuestZone>(
				Registry->FindScenarioActor(Inst.RequiredTargetAnchor)))
		{
			Zone->SetHighlighted(false);
		}
	}
}

EStateTreeRunStatus FTacticalTask_Objective::Tick(
	FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);

	Context.ForEachEvent([&Inst](const FStateTreeEvent& Event)
	{
		bool bChannelMatches = Inst.bRequireExactChannel
			? Event.Tag == Inst.EventChannel
			: Event.Tag.MatchesTag(Inst.EventChannel);
		// Второй допустимый leaf (адаптивность Open/InCover): всегда точное
		// сравнение — широкий parent здесь дал бы ложные зачёты.
		if (!bChannelMatches && Inst.EventChannelAlt.IsValid())
		{
			bChannelMatches = Event.Tag == Inst.EventChannelAlt;
		}
		if (!bChannelMatches)
		{
			return EStateTreeLoopEvents::Next;
		}

		const FQuestEventData* Data = Event.Payload.GetPtr<FQuestEventData>();
		if (!Data)
		{
			// Без payload проверить бойца и цель невозможно. Молча засчитывать
			// такое событие нельзя: шаг перестал бы быть проверяемым.
			UE_LOG(LogXRU1Quest, Warning,
				TEXT("[Quest] Событие %s пришло без payload — цель %s его не засчитала"),
				*Event.Tag.ToString(), *Inst.ObjectiveId.ToString());
			return EStateTreeLoopEvents::Next;
		}

		// Поздний callback предыдущего запуска общей карты не относится к этому.
		if (Inst.ScenarioRunId > 0 && Data->ScenarioRunId > 0 &&
			Data->ScenarioRunId != Inst.ScenarioRunId)
		{
			return EStateTreeLoopEvents::Next;
		}

		if (!TacticalQuestTasks_Internal::MatchesAnchor(Inst.RequiredSourceAnchor, Data->Source) ||
			!TacticalQuestTasks_Internal::MatchesAnchor(Inst.RequiredTargetAnchor, Data->Target))
		{
			return EStateTreeLoopEvents::Next;
		}

		if (Inst.bRequireDistinctSources)
		{
			const TWeakObjectPtr<UObject> SourceKey(Data->Source);
			// Повторный вход того же бойца не должен накручивать счётчик «двое
			// разных бойцов»; событие без источника при этом требовании не годится.
			if (!Data->Source || Inst.CountedSources.Contains(SourceKey))
			{
				return EStateTreeLoopEvents::Next;
			}
			Inst.CountedSources.Add(SourceKey);
		}

		++Inst.CurrentCount;
		UE_LOG(LogXRU1Quest, Display, TEXT("[Objective] %s: засчитано %d/%d (%s)"),
			*Inst.ObjectiveId.ToString(), Inst.CurrentCount, Inst.RequiredCount,
			*Event.Tag.ToString());
		return EStateTreeLoopEvents::Next;
	});

	ReportTacticalObjective(Context, Inst);

	if (Inst.CurrentCount < Inst.RequiredCount)
	{
		return EStateTreeRunStatus::Running;
	}
	UE_LOG(LogXRU1Quest, Display, TEXT("[Objective] ЗАКРЫТ %s"), *Inst.ObjectiveId.ToString());
	return EStateTreeRunStatus::Succeeded;
}

// --- FTacticalTask_ApplyActionGate ---------------------------------------------

FTacticalTask_ApplyActionGate::FTacticalTask_ApplyActionGate()
{
	// Политика живёт, пока состояние активно; тикать для этого не нужно.
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = false;
}

EStateTreeRunStatus FTacticalTask_ApplyActionGate::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context);
	UTutorialActionGateSubsystem* Gate = World
		? World->GetSubsystem<UTutorialActionGateSubsystem>() : nullptr;
	if (!Gate)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[Tutorial] Action Gate недоступен — шаг остался бы без ограничений"));
		return EStateTreeRunStatus::Failed;
	}

	Inst.PolicyToken = Gate->ApplyPolicy(Inst.Policy);

	// Succeeded, а НЕ Running. Состояние StateTree завершается только когда
	// завершены ВСЕ его задачи, поэтому вечно бегущий gate заблокировал бы шаг
	// навсегда. Политика при этом живёт до ExitState — он вызывается независимо
	// от run status задачи.
	return EStateTreeRunStatus::Succeeded;
}

void FTacticalTask_ApplyActionGate::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context);
	if (UTutorialActionGateSubsystem* Gate = World
		? World->GetSubsystem<UTutorialActionGateSubsystem>() : nullptr)
	{
		// Снимаем ТОЛЬКО свою политику: следующий шаг мог применить свою раньше,
		// чем сюда доехал ExitState вытесненного состояния.
		Gate->ClearPolicy(Inst.PolicyToken);
	}
	Inst.PolicyToken = 0;
}

// --- FTacticalTask_SetScenarioActorActive --------------------------------------

FTacticalTask_SetScenarioActorActive::FTacticalTask_SetScenarioActorActive()
{
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = false;
}

EStateTreeRunStatus FTacticalTask_SetScenarioActorActive::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	if (!Registry)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Мгновенная задача-действие. Пауза перед секцией — ОТДЕЛЬНОЕ состояние с
	// движковым Delay Task: задержка внутри задачи тормозит только её саму,
	// соседние задачи состояния (gate, подсказка) стартуют всё равно.
	for (const FName& AnchorId : Inst.AnchorIds)
	{
		Registry->SetScenarioActorActive(AnchorId, Inst.bActive);
	}

	// Удержание состояния — работа objective/beat, а не побочного эффекта.
	// Возврат прежнего состояния при bRestoreOnExit делает ExitState.
	return EStateTreeRunStatus::Succeeded;
}

void FTacticalTask_SetScenarioActorActive::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	if (!Inst.bRestoreOnExit)
	{
		return;
	}

	if (UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context))
	{
		for (const FName& AnchorId : Inst.AnchorIds)
		{
			Registry->SetScenarioActorActive(AnchorId, !Inst.bActive);
		}
	}
}

// --- FTacticalTask_SetActionPoints ----------------------------------------------

FTacticalTask_SetActionPoints::FTacticalTask_SetActionPoints()
{
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = false;
}

EStateTreeRunStatus FTacticalTask_SetActionPoints::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Inst = Context.GetInstanceData(*this);
	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	if (!Registry)
	{
		return EStateTreeRunStatus::Failed;
	}

	for (const FName& AnchorId : Inst.AnchorIds)
	{
		AUnitBase* Unit = Cast<AUnitBase>(Registry->FindScenarioActor(AnchorId));
		UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
		if (!ActionPoints)
		{
			UE_LOG(LogXRU1Quest, Error,
				TEXT("[Tutorial] Set Action Points: боец %s не найден или без ОД-компонента"),
				*AnchorId.ToString());
			continue;
		}
		ActionPoints->ResetForNewTurn();
		Unit->NotifyUnitStateChanged();
	}
	return EStateTreeRunStatus::Succeeded;
}

// --- FTacticalTask_ScriptedShot ------------------------------------------------

FTacticalTask_ScriptedShot::FTacticalTask_ScriptedShot()
{
	// Здесь нужен настоящий tick: задача ждёт terminal-состояния транзакции
	// выстрела, а оно не приходит событием квеста.
	bShouldCallTick = true;
}

EStateTreeRunStatus FTacticalTask_ScriptedShot::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	Inst.ElapsedTime = 0.f;
	Inst.bOrderIssued = false;

	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Shooter = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.ShooterAnchorId)) : nullptr;
	AUnitBase* Target = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.TargetAnchorId)) : nullptr;
	if (!Shooter || !Target)
	{
		UE_LOG(LogXRU1Quest, Error,
			TEXT("[Tutorial] Scripted Shot: не найден стрелок %s или цель %s"),
			*Inst.ShooterAnchorId.ToString(), *Inst.TargetAnchorId.ToString());
		return EStateTreeRunStatus::Failed;
	}

	AUnitAIController* ShooterAI = Cast<AUnitAIController>(Shooter->GetController());
	if (!ShooterAI)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[Tutorial] Scripted Shot: у %s нет UnitAIController"),
			*GetNameSafe(Shooter));
		return EStateTreeRunStatus::Failed;
	}

	// Форс и приказ ставятся ДО активации: в свой ближайший ход голограмма
	// выстрелит именно по этому бойцу и именно с этим исходом. Цель передаётся
	// вместе с форсом: если приказ сорвётся и AI выберет другую жертву,
	// постановочные числа на неё не перетекут.
	Shooter->SetPendingScriptedShot(Inst.Shot, Target);
	ShooterAI->SetScriptedAttackOrder(Target);
	Inst.bOrderIssued = true;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FTacticalTask_ScriptedShot::Tick(
	FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);

	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Shooter = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.ShooterAnchorId)) : nullptr;
	if (!Shooter)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Форс потреблён (значит, GA_Attack действительно стартовала) и транзакция
	// выстрела уже закрылась — presentation и урон доведены до конца.
	FGuid ActionId;
	const bool bActionInProgress = UGA_Attack::GetAttackActionInProgressFor(Shooter, ActionId);

	// Приказ ставится в ход ИГРОКА, а стреляет голограмма в свой — до фазы врага
	// ждать нечего, и отсчёт таймаута там измерял бы только раздумья игрока.
	// Начавшийся выстрел досчитывается в любой фазе: он уже идёт.
	TacticalQuestTasks_Internal::TickWatchdog(Inst.ElapsedTime, DeltaTime,
		TacticalQuestTasks_Internal::IsEnemyPhaseActive(Context) || bActionInProgress
			|| !Shooter->HasPendingScriptedShot(),
		TEXT("Scripted Shot"), TEXT("ход игрока — стрелять голограмме ещё не время"));
	if (!Shooter->HasPendingScriptedShot() && Inst.bOrderIssued && !bActionInProgress)
	{
		// Объявляем о завершении САМИ: обычные выстрелы врага quest-событий не
		// публикуют (иначе они закрывали бы шаги обучения), а реплики-реакции
		// обязаны звучать после попадания. Событие принадлежит сценарному
		// действию, а не боевой системе.
		AActor* Target = Registry ? Registry->FindScenarioActor(Inst.TargetAnchorId) : nullptr;
		UTacticalQuestEvents::BroadcastQuestEventEx(Shooter,
			TacticalQuestTags::Event_Tactical_Scripted_ShotFinished, Shooter, Target);
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[Tutorial] Scripted Shot: %s отстрелялся — событие ShotFinished"),
			*GetNameSafe(Shooter));
		return EStateTreeRunStatus::Succeeded;
	}

	if (Inst.ElapsedTime >= Inst.Timeout)
	{
		UE_LOG(LogXRU1Quest, Error,
			TEXT("[Tutorial] Scripted Shot: %s не выстрелил за %.1f с — шаг провален"),
			*GetNameSafe(Shooter), Inst.Timeout);
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

void FTacticalTask_ScriptedShot::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);

	// Невыполненный приказ нельзя оставлять на юните: иначе он выстрелит
	// «сценарно» уже в другом шаге.
	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	if (AUnitBase* Shooter = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.ShooterAnchorId)) : nullptr)
	{
		Shooter->ClearPendingScriptedShot();
		if (AUnitAIController* ShooterAI = Cast<AUnitAIController>(Shooter->GetController()))
		{
			ShooterAI->ClearScriptedAttackOrder();
		}
	}
	Inst.bOrderIssued = false;
}

// --- FTacticalTask_TutorialBeat ------------------------------------------------

FTacticalTask_TutorialBeat::FTacticalTask_TutorialBeat()
{
	bShouldCallTick = true;
#if WITH_EDITORONLY_DATA
	// Реплика-реакция боя живёт ФОНОМ рядом с целью шага: она ждёт своё событие
	// и не имеет права решать, когда состояние закончится. Разрешаем снимать
	// «учитывать для завершения» прямо в редакторе — без этого несколько тактов
	// в одном состоянии держали бы его до последней реплики.
	bCanEditConsideredForCompletion = true;
#endif
}

EStateTreeRunStatus FTacticalTask_TutorialBeat::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	Inst.ElapsedTime = 0.f;
	Inst.bBeatStarted = false;
	Inst.bFollowUpStarted = false;
	Inst.bBeatFinished = false;

	// Реплика-реакция ждёт своего события (выстрела, добега, срабатывания
	// наблюдения) — на входе в шаг она звучала бы до того, что комментирует.
	if (Inst.TriggerEvent.IsValid())
	{
		int32 SuppressedEntries = 0;
		if (TacticalQuestTasks_Internal::ShouldLogStateEntry(
				FString::Printf(TEXT("Beat %s"), *Inst.Beat.BeatId.ToString()), SuppressedEntries))
		{
			UE_LOG(LogXRU1Quest, Display, TEXT("[Beat] %s ждёт события %s (%s)%s"),
				*Inst.Beat.BeatId.ToString(), *Inst.TriggerEvent.ToString(),
				Inst.bRequireTriggerEvent
					? TEXT("без события не играет")
					: *FString::Printf(TEXT("не более %.0f с"), Inst.TriggerTimeout),
				SuppressedEntries > 0
					? *FString::Printf(TEXT(" [+%d повторных входов]"), SuppressedEntries) : TEXT(""));
		}
		return EStateTreeRunStatus::Running;
	}

	// Такт С ФОКУСОМ ждёт, пока камера освободится от кадра выстрела. Шаг при
	// этом уже сменился — очередь шагов решает квест-логика, а не презентация.
	// Иначе реплика говорит «его ранили», пока камера доигрывает kill-cam, и
	// показать раненого физически не может: фокус откладывается монополией
	// кадра и приезжает, когда фраза уже кончилась (лог A9).
	if (IsCameraBusyForBeat(Context, Inst))
	{
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[Beat] %s ждёт освобождения камеры (идёт кадр выстрела)"),
			*Inst.Beat.BeatId.ToString());
		return EStateTreeRunStatus::Running;
	}

	return StartBeatNow(Context, Inst) ? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}

bool FTacticalTask_TutorialBeat::IsCameraBusyForBeat(
	FStateTreeExecutionContext& Context, const FInstanceDataType& Inst) const
{
	// Без фокуса такту камера не нужна: реплика поверх кадра выстрела уместна и
	// держит темп. Ждёт только тот, кому есть что показать.
	if (Inst.Beat.FocusAnchorId.IsNone())
	{
		return false;
	}
	const UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context);
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	const ATacticalCameraPawn* Camera = PlayerController
		? Cast<ATacticalCameraPawn>(PlayerController->GetPawn()) : nullptr;
	return Camera && Camera->IsPlayingPresentationFrame();
}

bool FTacticalTask_TutorialBeat::StartBeatNow(
	FStateTreeExecutionContext& Context, FInstanceDataType& Inst) const
{
	UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context);
	if (UTutorialPresentationSubsystem* Presentation = World
		? World->GetSubsystem<UTutorialPresentationSubsystem>() : nullptr)
	{
		Presentation->StartBeat(Inst.Beat);
		Inst.bBeatStarted = true;
		Inst.ElapsedTime = 0.f;
		return true;
	}
	return false;
}

EStateTreeRunStatus FTacticalTask_TutorialBeat::Tick(
	FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	// Такт уже отговорил — состояние ещё живёт (ждёт игрока), пересчитывать нечего.
	if (Inst.bBeatFinished)
	{
		// Фоновая реакция боя ОСТАЁТСЯ Running: у состояния миссии нет других
		// задач, учитываемых для завершения, и «Succeeded» отсюда закрывал бы
		// его целиком — дерево перезапускалось по кругу.
		return Inst.bKeepRunningAfterBeat
			? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
	}

	// Фаза ожидания: события-триггера и/или свободной камеры.
	if (!Inst.bBeatStarted)
	{
		// Отсчёт БЕЗ гейтов, в отличие от сценарных задач: этот таймаут не валит
		// шаг, а спасает его — играет реплику «как есть», когда триггер потерян
		// или камера так и не освободилась. Замороженная страховка уже вставала
		// колом после паузы посреди кадра выстрела (2026-08-05).
		Inst.ElapsedTime += DeltaTime;

		bool bTriggered = !Inst.TriggerEvent.IsValid(); // без триггера ждём только камеру
		Context.ForEachEvent([&Inst, &bTriggered](const FStateTreeEvent& Event)
		{
			if (!Inst.TriggerEvent.IsValid() || Event.Tag != Inst.TriggerEvent)
			{
				return EStateTreeLoopEvents::Next;
			}
			if (!Inst.TriggerSourceAnchorId.IsNone())
			{
				const FQuestEventData* Data = Event.Payload.GetPtr<FQuestEventData>();
				if (!Data || !TacticalQuestTasks_Internal::MatchesAnchor(
						Inst.TriggerSourceAnchorId, Data->Source))
				{
					return EStateTreeLoopEvents::Next;
				}
			}
			bTriggered = true;
			return EStateTreeLoopEvents::Break;
		});

		// Камера — второе условие старта: такту с фокусом нечего показывать,
		// пока кадр выстрела держит взгляд.
		const bool bCameraBusy = IsCameraBusyForBeat(Context, Inst);
		// Фоновая реакция боя ждёт своё событие сколько угодно: «первого
		// убийства» может не случиться вовсе, и реплика по таймауту соврала бы.
		const bool bTimedOut = !Inst.bRequireTriggerEvent &&
			Inst.ElapsedTime >= Inst.TriggerTimeout;
		if ((!bTriggered || bCameraBusy) && !bTimedOut)
		{
			return EStateTreeRunStatus::Running;
		}
		if (bTimedOut && (!bTriggered || bCameraBusy))
		{
			UE_LOG(LogXRU1Quest, Warning,
				TEXT("[Beat] %s не дождался (%s) за %.0f с — играю реплику как есть"),
				*Inst.Beat.BeatId.ToString(),
				!bTriggered ? *Inst.TriggerEvent.ToString() : TEXT("свободной камеры"),
				Inst.TriggerTimeout);
		}
		if (!StartBeatNow(Context, Inst))
		{
			return EStateTreeRunStatus::Failed;
		}
		return EStateTreeRunStatus::Running;
	}

	Inst.ElapsedTime += DeltaTime;

	// Длительность текущей реплики обмена. Authored-значения НЕ мутируем:
	// instance data переживает повторный вход в состояние (retry сценария), и
	// подменённая длительность испортила бы второй прогон.
	const float CurrentDuration = Inst.bFollowUpStarted
		? FMath::Max(0.1f, Inst.Beat.FollowUpDuration)
		: FMath::Max(0.1f, Inst.Beat.Duration);

	// Пропуск реплики игроком (клик во время постановки) — как «Skip» диалога.
	UWorld* TickWorld = TacticalQuestTasks_Internal::GetWorld(Context);
	UTutorialPresentationSubsystem* Presentation = TickWorld
		? TickWorld->GetSubsystem<UTutorialPresentationSubsystem>() : nullptr;
	const bool bSkipped = Presentation && Presentation->ConsumeSkipRequest();

	if (!bSkipped && Inst.ElapsedTime < CurrentDuration)
	{
		return EStateTreeRunStatus::Running;
	}

	// Обмен репликами: первая отговорила — включаем ответную тем же тактом.
	// Отдельного состояния это не требует: последовательность реплик —
	// презентация ВНУТРИ шага, а не новая фаза обучения (отдельным состоянием
	// остаётся только пауза, обязанная задержать следующий шаг).
	if (Inst.Beat.HasFollowUp() && !Inst.bFollowUpStarted)
	{
		FTacticalTutorialBeat FollowUp = Inst.Beat;
		FollowUp.Speaker = Inst.Beat.FollowUpSpeaker;
		FollowUp.Subtitle = Inst.Beat.FollowUpSubtitle;
		FollowUp.Voice = Inst.Beat.FollowUpVoice;
		FollowUp.Duration = FMath::Max(0.1f, Inst.Beat.FollowUpDuration);
		FollowUp.FollowUpVoice.Reset(); // у ответа своего ответа уже нет
		FollowUp.FollowUpSubtitle = FText::GetEmpty();

		if (Presentation)
		{
			// StartBeat сам закрывает предыдущую реплику и продлевает
			// режиссёрское удержание камеры на длительность ответа.
			Presentation->StartBeat(FollowUp);
			Inst.bFollowUpStarted = true;
			Inst.ElapsedTime = 0.f;
			return EStateTreeRunStatus::Running;
		}
	}

	// Такт закрываем ЗДЕСЬ, по своей длительности, а не в ExitState: выход из
	// состояния наступает только когда шаг целиком выполнен игроком (в C0 это
	// четыре цели), и субтитр с режиссёрским удержанием камеры жили всё это
	// время — камера переставала слушаться выбора бойца.
	if (Presentation)
	{
		Presentation->FinishBeat();
	}
	Inst.bBeatFinished = true; // ExitState уже нечего закрывать
	return Inst.bKeepRunningAfterBeat
		? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}

void FTacticalTask_TutorialBeat::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Не доживший до показа (ждал триггер) или уже закрытый такт завершать
	// нечем — FinishBeat снял бы чужую активную реплику.
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	if (!Inst.bBeatStarted || Inst.bBeatFinished)
	{
		return;
	}
	UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context);
	if (UTutorialPresentationSubsystem* Presentation = World
		? World->GetSubsystem<UTutorialPresentationSubsystem>() : nullptr)
	{
		Presentation->FinishBeat();
	}
}

// --- FTacticalTask_ScriptedMove -------------------------------------------------

namespace
{
	/**
	 * Выдать постановочную пробежку ОБЩИМ пайплайном перемещения: тот же
	 * occupancy-aware план и тот же MoveAlongRoute, что у клика игрока, а значит
	 * и общий финиш — прижатие к укрытию, EvaluateSurroundings, доворот.
	 * AP и quest-события не участвуют: bTurnMoveInProgress/bPlayerOrderedMove
	 * этим путём не взводятся. Прямой MoveToLocation остаётся только фолбэком —
	 * он оставлял юнита с cover-кэшем от СТАРОЙ позиции (два источника правды).
	 */
	bool TryIssueScriptedRunOrder(AUnitBase* Unit, const AActor* Destination)
	{
		AUnitAIController* AI = Unit
			? Cast<AUnitAIController>(Unit->GetController()) : nullptr;
		if (!AI || !Destination)
		{
			return false;
		}

		UWorld* World = Unit->GetWorld();
		ATacticalPlayerController* PlayerController = World
			? Cast<ATacticalPlayerController>(World->GetFirstPlayerController()) : nullptr;
		FMoveOrderPlan Plan;
		if (PlayerController && PlayerController->PlanMoveForUnit(Unit,
				Destination->GetActorLocation(), /*MaxActionPoints=*/9, Plan) &&
			Plan.PathPoints.Num() >= 2)
		{
			if (AI->MoveAlongRoute(Plan.PathPoints, 20.f) ==
				EPathFollowingRequestResult::RequestSuccessful)
			{
				return true;
			}
		}

		// Цель вне общего плана (изолированный навмеш и т.п.) — прямой запрос.
		const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(
			Destination->GetActorLocation(), 20.f,
			/*bStopOnOverlap=*/false, /*bUsePathfinding=*/true);
		return Result == EPathFollowingRequestResult::RequestSuccessful ||
			Result == EPathFollowingRequestResult::AlreadyAtGoal;
	}
}

FTacticalTask_ScriptedMove::FTacticalTask_ScriptedMove()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FTacticalTask_ScriptedMove::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	Inst.ElapsedTime = 0.f;
	Inst.bOrderIssued = false;
	Inst.bCameraAttached = false;

	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr;
	const AActor* Destination = Registry
		? Registry->FindScenarioActor(Inst.DestinationAnchorId) : nullptr;
	if (!Unit || !Destination)
	{
		UE_LOG(LogXRU1Quest, Error,
			TEXT("[Tutorial] Scripted Move: не найден боец %s или точка %s"),
			*Inst.UnitAnchorId.ToString(), *Inst.DestinationAnchorId.ToString());
		return EStateTreeRunStatus::Failed;
	}

	// «Выбегает из тумана» почти всегда означает деактивированного staged-актора:
	// включаем сами — у выключенного не тикает movement, и приказ бежать молча
	// висел бы до таймаута.
	if (!UTacticalScenarioSubsystem::IsActorScenarioActive(Unit))
	{
		Registry->SetActorScenarioActive(Unit, true);
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[Tutorial] Scripted Move: %s был деактивирован — включён автоматически"),
			*Inst.UnitAnchorId.ToString());
	}

	// Приказ может не приняться с первого раза: только что включённый боец ещё
	// «оседает» (settlement guard в MoveAlongRoute) или контроллер спавнится.
	// Это не провал шага — Tick повторяет выдачу, Timeout общий.
	Inst.bOrderIssued = TryIssueScriptedRunOrder(Unit, Destination);
	if (!Inst.bOrderIssued)
	{
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[Tutorial] Scripted Move: приказ %s → %s не принят сразу — повторю"),
			*Inst.UnitAnchorId.ToString(), *Inst.DestinationAnchorId.ToString());
	}

	// Камера сопровождает бегущего: игрок видит перебежку, а не пустую точку.
	// Возврат фокуса на отряд в это время подавлен (lock-шаг владеет камерой).
	if (Inst.bCameraFollowUnit)
	{
		// Раз камера летит за бойцом — туман обязан его показать. Удержание берём
		// на ВЕСЬ такт, а не на время привязки камеры: она может временно уступить
		// кадру реакции, а исчезать при этом боец не должен.
		TacticalQuestTasks_Internal::HoldFogReveal(TacticalQuestTasks_Internal::GetWorld(Context),
			Unit, Inst.FogRevealHold, Inst.FogAreaRevealHandle);

		if (const UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context))
		{
			const APlayerController* PlayerController = World->GetFirstPlayerController();
			if (ATacticalCameraPawn* Camera = PlayerController
					? Cast<ATacticalCameraPawn>(PlayerController->GetPawn()) : nullptr)
			{
				Camera->SetFollowTarget(Unit);
				Inst.bCameraAttached = true;
			}
		}
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FTacticalTask_ScriptedMove::Tick(
	FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	Inst.ElapsedTime += DeltaTime;

	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	const AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr;
	const AActor* Destination = Registry
		? Registry->FindScenarioActor(Inst.DestinationAnchorId) : nullptr;
	if (!Unit || !Destination)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Диагностика «скольжения»: раз в секунду печатаем скорость и состояние
	// анимации. Ключевые подозреваемые: не тикающий меш-компонент и застрявший
	// full-body монтаж (он перекрывает локомоцию — пешка едет в замершей позе).
	if (FMath::FloorToInt(Inst.ElapsedTime) != FMath::FloorToInt(Inst.ElapsedTime - DeltaTime))
	{
		const USkeletalMeshComponent* Mesh = Unit->GetMesh();
		const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[Tutorial] Scripted Move %s: vel=%.0f animClass=%s meshTick=%d pause=%d visTick=%d montage=%s globalRate=%.2f"),
			*Inst.UnitAnchorId.ToString(),
			Unit->GetVelocity().Size2D(),
			Anim ? *Anim->GetClass()->GetName() : TEXT("НЕТ"),
			Mesh && Mesh->IsComponentTickEnabled() ? 1 : 0,
			Mesh && Mesh->bPauseAnims ? 1 : 0,
			Mesh ? static_cast<int32>(Mesh->VisibilityBasedAnimTickOption) : -1,
			Montage ? *Montage->GetName() : TEXT("-"),
			Mesh ? Mesh->GlobalAnimRateScale : -1.f);
	}

	// Приказ ещё не принят (боец оседал/контроллер спавнился) — повторяем
	// каждые полсекунды, пока не примется или не истечёт Timeout.
	if (!Inst.bOrderIssued &&
		FMath::FloorToInt(Inst.ElapsedTime * 2.f) != FMath::FloorToInt((Inst.ElapsedTime - DeltaTime) * 2.f))
	{
		Inst.bOrderIssued = TryIssueScriptedRunOrder(
			const_cast<AUnitBase*>(Unit), Destination);
	}

	const AAIController* Controller = Cast<AAIController>(Unit->GetController());
	const bool bMoveIdle = !Controller ||
		Controller->GetMoveStatus() != EPathFollowingStatus::Moving;
	// Прибытие = маршрут завершён И осадка (подшаг к стене, доворот) закончена:
	// раньше задача выходила до прижатия, и укрытие оставалось от старой позиции.
	if (Inst.bOrderIssued && bMoveIdle && !Unit->IsMoveSettlementInProgress() &&
		FVector::Dist2D(Unit->GetActorLocation(), Destination->GetActorLocation())
		<= Inst.AcceptanceRadius)
	{
		if (Inst.bDrainActionPointsOnArrival)
		{
			if (UActionPointsComponent* ActionPoints =
					const_cast<AUnitBase*>(Unit)->GetActionPoints())
			{
				ActionPoints->SpendAllRemaining();
			}
		}
		// Как и сценарный выстрел, перебежка объявляет о своём завершении сама:
		// движение по приказу задачи не публикует боевых quest-событий, а
		// реплика «Не стреляйте, свои!» обязана звучать, КОГДА боец уже добежал
		// и встал, а не когда он только сорвался с места.
		UTacticalQuestEvents::BroadcastQuestEventEx(const_cast<AUnitBase*>(Unit),
			TacticalQuestTags::Event_Tactical_Scripted_MoveFinished,
			const_cast<AUnitBase*>(Unit), const_cast<AActor*>(Destination));
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[Tutorial] Scripted Move: %s добежал до %s — событие MoveFinished"),
			*GetNameSafe(Unit), *Inst.DestinationAnchorId.ToString());
		return EStateTreeRunStatus::Succeeded;
	}

	if (Inst.ElapsedTime >= Inst.Timeout)
	{
		UE_LOG(LogXRU1Quest, Error,
			TEXT("[Tutorial] Scripted Move: %s не добежал до %s за %.1f с"),
			*GetNameSafe(Unit), *Inst.DestinationAnchorId.ToString(), Inst.Timeout);
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

void FTacticalTask_ScriptedMove::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Камера и остановка НЕ прячутся за bOrderIssued: follow цепляется на входе
	// независимо от принятия приказа, и не отпущенный при раннем выходе follow
	// оставался бы на бойце навсегда.
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	// Недобежавшего останавливаем: чужой шаг не должен получить бегущего бойца.
	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr;
	if (AAIController* Controller = Unit ? Cast<AAIController>(Unit->GetController()) : nullptr;
		Controller && Inst.bOrderIssued)
	{
		Controller->StopMovement();
	}

	// Удержание тумана снимается ВСЕГДА и строго по своей ссылке: дальше видимость
	// решает фактический LOS.
	TacticalQuestTasks_Internal::ReleaseFogReveal(TacticalQuestTasks_Internal::GetWorld(Context),
		Inst.FogRevealHold, Inst.FogAreaRevealHandle);

	// Камеру отпускаем строго парно к своему SetFollowTarget: следующий шаг
	// (второй Scripted Move или Beat) сразу поставит свой фокус поверх.
	if (Inst.bCameraAttached)
	{
		if (const UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context))
		{
			const APlayerController* PlayerController = World->GetFirstPlayerController();
			if (ATacticalCameraPawn* Camera = PlayerController
					? Cast<ATacticalCameraPawn>(PlayerController->GetPawn()) : nullptr)
			{
				Camera->ClearFollowTarget();
			}
		}
	}
}

// --- FTacticalTask_ScriptedEnemyTurn --------------------------------------------

namespace
{
	/** Собрать программу хода из непустых полей задачи. */
	TArray<FScriptedTurnStep> BuildScriptedTurnProgram(
		const FTacticalTask_ScriptedEnemyTurnInstanceData& Inst,
		UTacticalScenarioSubsystem* Registry)
	{
		TArray<FScriptedTurnStep> Steps;
		if (!Inst.FreeMoveAnchorId.IsNone())
		{
			FScriptedTurnStep Step;
			Step.Type = EScriptedTurnStepType::MoveTo;
			Step.Destination = Registry->FindScenarioActor(Inst.FreeMoveAnchorId);
			Step.bFreeMove = true;
			Steps.Add(Step);
		}
		if (!Inst.PaidMoveAnchorId.IsNone())
		{
			FScriptedTurnStep Step;
			Step.Type = EScriptedTurnStepType::MoveTo;
			Step.Destination = Registry->FindScenarioActor(Inst.PaidMoveAnchorId);
			Steps.Add(Step);
		}
		if (Inst.FinishAbility)
		{
			FScriptedTurnStep Step;
			Step.Type = EScriptedTurnStepType::SelfAbility;
			Step.AbilityClass = Inst.FinishAbility;
			Steps.Add(Step);
		}
		return Steps;
	}
}

FTacticalTask_ScriptedEnemyTurn::FTacticalTask_ScriptedEnemyTurn()
{
	bShouldCallTick = true;
}

EStateTreeRunStatus FTacticalTask_ScriptedEnemyTurn::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	Inst.ElapsedTime = 0.f;
	Inst.bProgramSet = false;
	Inst.bCameraAttached = false;

	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr;
	if (!Unit)
	{
		UE_LOG(LogXRU1Quest, Error,
			TEXT("[Tutorial] Scripted Enemy Turn: боец %s не найден"),
			*Inst.UnitAnchorId.ToString());
		return EStateTreeRunStatus::Failed;
	}

	// Деактивированного включаем сами — как Scripted Move: у выключенного нет
	// контроллера/тика, и программа молча висела бы до таймаута.
	if (!UTacticalScenarioSubsystem::IsActorScenarioActive(Unit))
	{
		Registry->SetActorScenarioActive(Unit, true);
		UE_LOG(LogXRU1Quest, Display,
			TEXT("[Tutorial] Scripted Enemy Turn: %s был деактивирован — включён автоматически"),
			*Inst.UnitAnchorId.ToString());
	}

	// Постановочный ход врага показывается игроку целиком: камера ведёт его от
	// выхода до отбегания. Туман на это время отступает — иначе половина такта
	// C0/C1 обучения игралась бы за кадром.
	if (Inst.bCameraFollowUnit)
	{
		TacticalQuestTasks_Internal::HoldFogReveal(TacticalQuestTasks_Internal::GetWorld(Context),
			Unit, Inst.FogRevealHold, Inst.FogAreaRevealHandle);
	}

	// Контроллер мог ещё не заспавниться после активации — Tick повторит.
	if (AUnitAIController* AI = Cast<AUnitAIController>(Unit->GetController()))
	{
		AI->SetScriptedTurnProgram(BuildScriptedTurnProgram(Inst, Registry));
		Inst.bProgramSet = true;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FTacticalTask_ScriptedEnemyTurn::Tick(
	FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);

	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr;
	AUnitAIController* AI = Unit ? Cast<AUnitAIController>(Unit->GetController()) : nullptr;

	// Программа армируется в ход игрока (инвариант §5.3-3), а исполняется в ход
	// врага. Отсчёт до фазы врага измерял бы, сколько игрок думает над своим
	// ходом, — и шаг C1 обучения разваливался от одной отлучки за чаем.
	// Начатая программа досчитывается: она уже идёт и обязана дойти до конца.
	TacticalQuestTasks_Internal::TickWatchdog(Inst.ElapsedTime, DeltaTime,
		TacticalQuestTasks_Internal::IsEnemyPhaseActive(Context)
			|| (AI && AI->IsScriptedTurnProgramStarted()),
		TEXT("Scripted Enemy Turn"), TEXT("ход игрока — программа врага ещё не начиналась"));

	// Смерть исполнителя посреди программы — постановка сорвана: дальше шаги
	// секции без него не сыграть, честный Failed уронит сценарий на рестарт.
	if (!Unit || !UTacticsCombatStatics::IsUnitAlive(Unit))
	{
		UE_LOG(LogXRU1Quest, Error,
			TEXT("[Tutorial] Scripted Enemy Turn: %s погиб до завершения программы"),
			*Inst.UnitAnchorId.ToString());
		return EStateTreeRunStatus::Failed;
	}

	// Программа обязана СТОЯТЬ, пока не исполнена, — декларативно, каждый тик.
	// Одноразовый флаг терял постановку: OnPossess только что заспавненного
	// контроллера вызывал ClearScriptedTurnProgram ПОСЛЕ первой постановки, и
	// ход Holo_D шёл штатным AI (Investigate на шум — лог 2026-08-02).
	if (AI && !AI->WasScriptedTurnProgramExecuted() && !AI->HasScriptedTurnProgram())
	{
		AI->SetScriptedTurnProgram(BuildScriptedTurnProgram(Inst, Registry));
		Inst.bProgramSet = true;
	}

	// Камера цепляется в момент, когда программа реально пошла (его фаза), а не
	// при армировании в ход игрока — иначе фокус улетал бы на стоящего врага.
	// Реакционный выстрел ВЛАДЕЕТ камерой монопольно: на время реакции follow
	// отпускается (иначе он перетягивал кадр обратно на бегущего врага и
	// реакция «дёргалась»), после — цепляется заново этим же Tick'ом.
	if (const UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context))
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const ATacticalPlayerController* TacticalPC = Cast<ATacticalPlayerController>(PC);
		ATacticalCameraPawn* Camera = PC
			? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
		const bool bReaction = TacticalPC && TacticalPC->IsReactionShotPlaying();
		if (Camera && Inst.bCameraFollowUnit)
		{
			if (bReaction && Inst.bCameraAttached)
			{
				Camera->ClearFollowTarget();
				Inst.bCameraAttached = false;
			}
			else if (!bReaction && !Inst.bCameraAttached &&
				AI && AI->IsScriptedTurnProgramStarted())
			{
				Camera->SetFollowTarget(Unit);
				Inst.bCameraAttached = true;
			}
		}
	}

	if (Inst.bProgramSet && AI && AI->WasScriptedTurnProgramExecuted())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (Inst.ElapsedTime >= Inst.Timeout)
	{
		UE_LOG(LogXRU1Quest, Error,
			TEXT("[Tutorial] Scripted Enemy Turn: %s не исполнил программу за %.1f с — шаг провален"),
			*Inst.UnitAnchorId.ToString(), Inst.Timeout);
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

void FTacticalTask_ScriptedEnemyTurn::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr;

	// Недоигранную программу снимаем ВМЕСТЕ с ходом: чужой шаг не должен
	// получить врага с живым приказом, а остаток его ОД — не достаётся
	// utility-AI (свободный выстрел по Кадету после провала, лог 2026-08-02).
	if (AUnitAIController* AI = Unit ? Cast<AUnitAIController>(Unit->GetController()) : nullptr)
	{
		if (AI->HasScriptedTurnProgram())
		{
			AI->CancelScriptedTurnProgram();
		}
	}

	// Такт кончился — туман снова решает сам.
	TacticalQuestTasks_Internal::ReleaseFogReveal(TacticalQuestTasks_Internal::GetWorld(Context),
		Inst.FogRevealHold, Inst.FogAreaRevealHandle);

	if (Inst.bCameraAttached)
	{
		if (const UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context))
		{
			const APlayerController* PlayerController = World->GetFirstPlayerController();
			if (ATacticalCameraPawn* Camera = PlayerController
					? Cast<ATacticalCameraPawn>(PlayerController->GetPawn()) : nullptr)
			{
				Camera->ClearFollowTarget();
			}
		}
	}
}

// --- FTacticalTask_ForceNextShot ------------------------------------------------

FTacticalTask_ForceNextShot::FTacticalTask_ForceNextShot()
{
	// Взводит форс и сразу Succeeded: состояние держат objectives шага.
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = false;
}

EStateTreeRunStatus FTacticalTask_ForceNextShot::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Inst = Context.GetInstanceData(*this);
	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr;
	if (!Unit)
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[Tutorial] Force Next Shot: боец %s не найден"),
			*Inst.UnitAnchorId.ToString());
		return EStateTreeRunStatus::Failed;
	}
	// Цель не фиксируем: игрок выбирает её сам, gate уже сузил допустимых.
	Unit->SetPendingScriptedShot(Inst.Shot, nullptr);
	UE_LOG(LogXRU1Quest, Display,
		TEXT("[Tutorial] Force Next Shot: следующий выстрел %s форсирован (hit=%.0f)"),
		*Inst.UnitAnchorId.ToString(), Inst.Shot.HitChancePercent);
	return EStateTreeRunStatus::Succeeded;
}

void FTacticalTask_ForceNextShot::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Inst = Context.GetInstanceData(*this);
	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	if (AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr)
	{
		// Непотраченный форс не должен утечь в следующий шаг.
		Unit->ClearPendingScriptedShot();
	}
}

// --- Вызов подкрепления ---------------------------------------------------------

FTacticalTask_CallReinforcements::FTacticalTask_CallReinforcements()
{
	// Запрос мгновенный: маяк дальше живёт сам (отсчёт ходов врага и высадка),
	// поэтому держать состояние StateTree незачем.
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = false;
}

EStateTreeRunStatus FTacticalTask_CallReinforcements::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Inst = Context.GetInstanceData(*this);
	UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context);
	ATacticalReinforcementBeacon* Beacon = World
		? ATacticalReinforcementBeacon::FindBeacon(World, Inst.BeaconId) : nullptr;
	if (!Beacon)
	{
		UE_LOG(LogXRU1Quest, Error,
			TEXT("[Mission] Call Reinforcements: маяк '%s' не найден на карте"),
			*Inst.BeaconId.ToString());
		return EStateTreeRunStatus::Failed;
	}

	// Отказ маяка (лимит волн исчерпан, волна уже в пути) — не ошибка сценария:
	// шаг миссии не должен вставать из-за того, что подкрепление уже идёт.
	const bool bAccepted = Beacon->RequestWave();
	UE_LOG(LogXRU1Quest, Display,
		TEXT("[Mission] Call Reinforcements: маяк '%s' — запрос %s"),
		*Beacon->BeaconId.ToString(), bAccepted ? TEXT("принят") : TEXT("отклонён"));
	return EStateTreeRunStatus::Succeeded;
}
