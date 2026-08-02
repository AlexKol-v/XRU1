#include "TacticalQuestTasks.h"
#include "XRU1Log.h"

#include "ActionPointsComponent.h"
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
#include "TacticalCameraPawn.h"
#include "TacticalPlayerController.h"
#include "TacticsCombatStatics.h"
#include "TacticalQuestEvents.h"
#include "TacticalQuestZone.h"
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
	UE_LOG(LogXRU1Quest, Display,
		TEXT("[Objective] НАЧАТ %s: ждём %s x%d (source=%s target=%s run=%d)"),
		*Inst.ObjectiveId.ToString(), *Inst.EventChannel.ToString(), Inst.RequiredCount,
		*Inst.RequiredSourceAnchor.ToString(), *Inst.RequiredTargetAnchor.ToString(),
		Inst.ScenarioRunId);

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
	Inst.ElapsedTime += DeltaTime;

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
	if (!Shooter->HasPendingScriptedShot() && Inst.bOrderIssued && !bActionInProgress)
	{
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
}

EStateTreeRunStatus FTacticalTask_TutorialBeat::EnterState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	Inst.ElapsedTime = 0.f;
	Inst.bBeatStarted = false;

	UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context);
	if (UTutorialPresentationSubsystem* Presentation = World
		? World->GetSubsystem<UTutorialPresentationSubsystem>() : nullptr)
	{
		Presentation->StartBeat(Inst.Beat);
		Inst.bBeatStarted = true;
		return EStateTreeRunStatus::Running;
	}
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FTacticalTask_TutorialBeat::Tick(
	FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	// Такт уже закрыт своим Tick — состояние ещё живёт (ждёт игрока), но
	// пересчитывать нечего.
	if (!Inst.bBeatStarted)
	{
		return EStateTreeRunStatus::Succeeded;
	}
	Inst.ElapsedTime += DeltaTime;

	if (Inst.ElapsedTime < FMath::Max(0.1f, Inst.Beat.Duration))
	{
		return EStateTreeRunStatus::Running;
	}

	// Такт закрываем ЗДЕСЬ, по своей длительности, а не в ExitState: выход из
	// состояния наступает только когда шаг целиком выполнен игроком (в C0 это
	// четыре цели), и субтитр с режиссёрским удержанием камеры жили всё это
	// время — камера переставала слушаться выбора бойца.
	UWorld* World = TacticalQuestTasks_Internal::GetWorld(Context);
	if (UTutorialPresentationSubsystem* Presentation = World
		? World->GetSubsystem<UTutorialPresentationSubsystem>() : nullptr)
	{
		Presentation->FinishBeat();
	}
	Inst.bBeatStarted = false; // ExitState уже нечего закрывать
	return EStateTreeRunStatus::Succeeded;
}

void FTacticalTask_TutorialBeat::ExitState(
	FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Такт, не доживший до показа (выход в паузе), нечем завершать — FinishBeat
	// снял бы чужой активный такт.
	FInstanceDataType& Inst = Context.GetInstanceData(*this);
	if (!Inst.bBeatStarted)
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
	Inst.ElapsedTime += DeltaTime;

	UTacticalScenarioSubsystem* Registry =
		TacticalQuestTasks_Internal::GetScenarioRegistry(Context);
	AUnitBase* Unit = Registry
		? Cast<AUnitBase>(Registry->FindScenarioActor(Inst.UnitAnchorId)) : nullptr;
	AUnitAIController* AI = Unit ? Cast<AUnitAIController>(Unit->GetController()) : nullptr;

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
