#include "UnitAIController.h"
#include "AIActionEvaluators.h"
#include "AIBehaviorProfileDataAsset.h"
#include "UnitBase.h"
#include "TacticalPlayerController.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "CoverTuningDataAsset.h"
#include "TacticalAIDirectorSubsystem.h"
#include "TacticalQuestEvents.h"
#include "TutorialActionGate.h"
#include "TacticsDebug.h"
#include "DrawDebugHelpers.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameplayTags.h"
#include "TurnManagerSubsystem.h"
#include "GA_Attack.h"
#include "GA_Overwatch.h"
#include "MoveRangeVisualizer.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "XRU1Log.h"
#include "Math/RandomStream.h"

/**
 * Диагностика решений AI в бою: почему враг стреляет/бежит/стоит. Под cvar,
 * выключено по умолчанию. Включить в консоли: `xru1.AI.LogCombat 1`.
 * Отвечает на «понимают ли враги, где прятаться» — печатает, нашёл ли манёвр
 * укрытие и с какой оценкой (0 найдено = на карте нет укрытий рядом).
 */
// Переключатель живёт в общем реестре TacticsDebug: два объявления одного и
// того же cvar в разных .cpp конфликтуют при регистрации.

namespace
{
	bool IsFireSubactionInProgress(const AUnitBase* Unit, FGuid* OutActionId = nullptr)
	{
		FGuid ActionId;
		const bool bInProgress = UGA_Attack::GetAttackActionInProgressFor(Unit, ActionId) ||
			UGA_Overwatch::GetReactionActionInProgressFor(Unit, ActionId);
		if (OutActionId)
		{
			*OutActionId = bInProgress ? ActionId : FGuid();
		}
		return bInProgress;
	}
}

AUnitAIController::AUnitAIController(const FObjectInitializer& ObjectInitializer)
	// Detour Crowd вместо стокового path following: агенты знают друг о друге
	// и бегущий огибает стоящих (обход юнитов в ДВИЖЕНИИ; занятость точек —
	// дисками в UTacticsCombatStatics).
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
	Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionHalfAngle;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	Perception->ConfigureSense(*SightConfig);
	Perception->SetDominantSense(UAISense_Sight::StaticClass());
	SetPerceptionComponent(*Perception);

	// Дефолтный набор вариантов действия (ADR-1). Порядок в массиве роли не
	// играет — перебор сортируется по потолку скора; здесь он лишь читаемый.
	// Новое поведение добавляется НАСЛЕДНИКОМ UAIActionEvaluator и строкой сюда
	// (или в BP-наследнике контроллера), существующие оценщики не правятся.
	ActionEvaluators.Add(CreateDefaultSubobject<UAIEval_Retreat>(TEXT("Eval_Retreat")));
	ActionEvaluators.Add(CreateDefaultSubobject<UAIEval_Shoot>(TEXT("Eval_Shoot")));
	ActionEvaluators.Add(CreateDefaultSubobject<UAIEval_HunkerDown>(TEXT("Eval_HunkerDown")));
	ActionEvaluators.Add(CreateDefaultSubobject<UAIEval_MoveToCover>(TEXT("Eval_MoveToCover")));
	ActionEvaluators.Add(CreateDefaultSubobject<UAIEval_AdvanceToCover>(TEXT("Eval_AdvanceToCover")));
	ActionEvaluators.Add(CreateDefaultSubobject<UAIEval_Overwatch>(TEXT("Eval_Overwatch")));
	ActionEvaluators.Add(CreateDefaultSubobject<UAIEval_CloseDistance>(TEXT("Eval_CloseDistance")));
}

void AUnitAIController::BeginPlay()
{
	Super::BeginPlay();

	// Профиль применяется после сериализации BP/DataAsset, но до настройки Perception и Crowd.
	// Так значения в ассете действительно становятся runtime-источником истины.
	ApplyBehaviorProfile();
	SightRadius = FMath::Max(0.f, SightRadius);
	LoseSightRadius = FMath::Max(SightRadius, LoseSightRadius);
	PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.f, 180.f);
	CrowdSeparationWeight = FMath::Max(0.f, CrowdSeparationWeight);
	RouteCornerAcceptance = FMath::Clamp(RouteCornerAcceptance, 5.f, 25.f);
	ManeuverArrivalTolerance = FMath::Max(10.f, ManeuverArrivalTolerance);
	InvestigateOverwatchChance = FMath::Clamp(InvestigateOverwatchChance, 0.f, 1.f);
	MaxScoredThreats = FMath::Clamp(MaxScoredThreats, 1, 8);
	ActionInterval = FMath::Max(0.01f, ActionInterval);
	TargetHitChanceLowThreshold = FMath::Clamp(TargetHitChanceLowThreshold, 0.f, 100.f);
	TargetHitChanceHighThreshold = FMath::Clamp(TargetHitChanceHighThreshold, 0.f, 100.f);
	if (TargetHitChanceHighThreshold < TargetHitChanceLowThreshold)
	{
		Swap(TargetHitChanceHighThreshold, TargetHitChanceLowThreshold);
		UE_LOG(LogXRU1AI, Warning, TEXT("[AI] %s: пороги hit chance были инвертированы и переставлены"),
			*GetNameSafe(this));
	}

	// Качество объезда повыше: юнитов мало, стоимость незаметна, а обход
	// стоящих капсул становится заметно плавнее.
	if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::High);
		Crowd->SetCrowdCollisionQueryRange(600.f);

		// ⚠️ РАЗДЕЛЕНИЕ АГЕНТОВ — включается ОТДЕЛЬНО от избегания и по умолчанию
		// ВЫКЛЮЧЕНО. Avoidance предсказывает столкновения по скоростям и работает
		// только между ДВИЖУЩИМИСЯ агентами; separation расталкивает и от
		// стоящих. У нас ходы строго последовательные — движется ровно один
		// боец, — поэтому без separation Detour Crowd не мешал ему упереться в
		// строй и застрять. Это и наблюдалось после снятия усечения пути (N1):
		// маршрут честно идёт сквозь своих, а расталкивать было нечему.
		Crowd->SetCrowdSeparation(true);
		Crowd->SetCrowdSeparationWeight(CrowdSeparationWeight);

		// Путь пересчитывается, когда агента сносит с коридора, — иначе он
		// «прилипает» к обойдённому препятствию вместо возврата на маршрут.
		Crowd->SetCrowdPathOptimizationRange(600.f);
	}

	// Конфиг зрения применяем ЗДЕСЬ, а не только в конструкторе. Значения
	// SightConfig сериализуются в CDO, поэтому BP-наследник контроллера,
	// созданный до правки, унёс бы с собой старый конус 120° — и враг
	// по-прежнему «терял» бойца, зашедшего за спину. Здесь же гарантируем, что
	// в игре стоит ровно то, что написано в свойствах.
	RefreshPerceptionConfig();

	if (Perception)
	{
		Perception->OnTargetPerceptionUpdated.AddDynamic(this, &AUnitAIController::HandlePerceptionUpdated);
	}
}

void AUnitAIController::RefreshPerceptionConfig()
{
	// Конфиг зрения применяется здесь, а не только в конструкторе: значения
	// SightConfig сериализуются в CDO, поэтому BP-наследник контроллера, созданный
	// до правки, унёс бы старый конус. Метод отдельный, потому что профиль
	// сложности назначается уже ПОСЛЕ BeginPlay — без повторного вызова разница в
	// дальности обзора между Easy и Hard просто не применилась бы.
	if (!SightConfig || !Perception)
	{
		return;
	}
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionHalfAngle;
	Perception->ConfigureSense(*SightConfig);
	Perception->RequestStimuliListenerUpdate();
}

void AUnitAIController::SetBehaviorProfile(UAIBehaviorProfileDataAsset* NewProfile)
{
	if (!NewProfile || BehaviorProfile == NewProfile)
	{
		return;
	}
	BehaviorProfile = NewProfile;
	ApplyBehaviorProfile();
	SightRadius = FMath::Max(0.f, SightRadius);
	LoseSightRadius = FMath::Max(SightRadius, LoseSightRadius);
	PeripheralVisionHalfAngle = FMath::Clamp(PeripheralVisionHalfAngle, 1.f, 180.f);
	RefreshPerceptionConfig();
	UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: профиль поведения = %s"),
		*GetNameSafe(GetPawn()), *GetNameSafe(NewProfile));
}

void AUnitAIController::ApplyBehaviorProfile()
{
	if (!BehaviorProfile)
	{
		return;
	}

	const FAIPerceptionTuning& PerceptionTuning = BehaviorProfile->Perception;
	SightRadius = PerceptionTuning.SightRadius;
	LoseSightRadius = FMath::Max(PerceptionTuning.LoseSightRadius, SightRadius);
	PeripheralVisionHalfAngle = PerceptionTuning.PeripheralVisionHalfAngle;

	const FAINavigationTuning& NavigationTuning = BehaviorProfile->Navigation;
	CrowdSeparationWeight = NavigationTuning.CrowdSeparationWeight;
	RouteCornerAcceptance = NavigationTuning.RouteCornerAcceptance;
	ManeuverArrivalTolerance = NavigationTuning.ManeuverArrivalTolerance;

	const FAIAlertTuning& AlertTuning = BehaviorProfile->Alert;
	InvestigateAcceptanceRadius = AlertTuning.InvestigateAcceptanceRadius;
	InvestigateOverwatchChance = AlertTuning.InvestigateOverwatchChance;
	ActionInterval = AlertTuning.ActionInterval;
	TauntPriorityRadius = AlertTuning.TauntPriorityRadius;

	const FAIPositionScoringTuning& PositionTuning = BehaviorProfile->Position;
	CoverDefenseWeight = PositionTuning.CoverDefenseWeight;
	OpenCoverFactor = PositionTuning.OpenCoverFactor;
	HalfCoverFactor = PositionTuning.HalfCoverFactor;
	FullCoverFactor = PositionTuning.FullCoverFactor;
	FlankPositionBonus = PositionTuning.FlankPositionBonus;
	HeightPositionBonus = PositionTuning.HeightPositionBonus;
	MinSpreadDistance = PositionTuning.MinSpreadDistance;
	SpreadPenaltyMultiplier = PositionTuning.SpreadPenaltyMultiplier;
	AllyVisibilityWeight = PositionTuning.AllyVisibilityWeight;
	OverwatchExposurePenalty = PositionTuning.OverwatchExposurePenalty;
	LineOfFireBonus = PositionTuning.LineOfFireBonus;
	LoseLineOfFirePenalty = PositionTuning.LoseLineOfFirePenalty;
	TravelCostPerCm = PositionTuning.TravelCostPerCm;
	IdealRangeWeight = PositionTuning.IdealRangeWeight;
	IdealRangeFalloff = PositionTuning.IdealRangeFalloff;
	RelocateBias = PositionTuning.RelocateBias;
	RetreatHealthFraction = PositionTuning.RetreatHealthFraction;
	RetreatRewardPerCm = PositionTuning.RetreatRewardPerCm;
	CoverSnapDistance = PositionTuning.CoverSnapDistance;
	MaxScoredThreats = PositionTuning.MaxScoredThreats;
	EnemyVisibilityWeight = PositionTuning.EnemyVisibilityWeight;

	const FAITargetScoringTuning& TargetTuning = BehaviorProfile->Target;
	TargetHitChanceHighThreshold = TargetTuning.HitChanceHighThreshold;
	TargetHitChanceLowThreshold = TargetTuning.HitChanceLowThreshold;
	TargetScoreHitChanceHigh = TargetTuning.HitChanceHighScore;
	TargetScoreHitChanceMedium = TargetTuning.HitChanceMediumScore;
	TargetScoreHitChanceLow = TargetTuning.HitChanceLowScore;
	TargetScoreTaunting = TargetTuning.TauntingScore;
	TargetScoreFlanked = TargetTuning.FlankedScore;
	TargetScoreKillShot = TargetTuning.KillShotScore;
	TargetScoreWounded = TargetTuning.WoundedScore;
	TargetScoreDowned = TargetTuning.DownedScore;
	TargetScoreNoLineOfFire = TargetTuning.NoLineOfFireScore;

	if (BehaviorProfile->bOverrideActionEvaluators)
	{
		BaseEvaluatorWeights.Reset();
		ActionEvaluators.Reset(BehaviorProfile->ActionEvaluators.Num());
		for (const TObjectPtr<UAIActionEvaluator>& Template : BehaviorProfile->ActionEvaluators)
		{
			if (Template)
			{
				ActionEvaluators.Add(DuplicateObject<UAIActionEvaluator>(Template, this));
			}
		}
	}

	Style = BehaviorProfile->Style;

	// FlankPositionBonus и TargetScoreWounded только что присвоены из профиля,
	// поэтому множитель к ним применяется от чистого значения.
	FlankPositionBonus *= FMath::Max(0.f, Style.FlankWillingness);
	TargetScoreWounded *= FMath::Max(0.f, Style.FinishWoundedWillingness);

	// А вот веса оценщиков живут в самих объектах и профилем не переприсваиваются.
	// Множитель обязан считаться от ИСХОДНОГО веса: профиль назначается второй раз
	// (BeginPlay, затем выбор сложности в GameMode), и умножение «поверх» дало бы
	// квадрат множителя и необъяснимо агрессивного врага.
	for (const TObjectPtr<UAIActionEvaluator>& Evaluator : ActionEvaluators)
	{
		if (!Evaluator)
		{
			continue;
		}
		const float BaseWeight = BaseEvaluatorWeights.FindOrAdd(Evaluator, Evaluator->Weight);
		const float* Multiplier = Style.EvaluatorWeightMultipliers.Find(Evaluator->GetClass());
		Evaluator->Weight = BaseWeight * (Multiplier ? FMath::Max(0.f, *Multiplier) : 1.f);
	}
}

void AUnitAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Боец попадает в свой под сразу при возможнии: групповая активация должна
	// работать ещё до первого хода, иначе первый же выстрел разбудит одного.
	if (UTacticalAIDirectorSubsystem* Director = GetAIDirector())
	{
		Director->RegisterUnit(Cast<AUnitBase>(InPawn));
	}


	// Тревога — состояние конкретного бойца: новый пешка = новый пост.
	AlertState = EUnitAlertState::Patrol;
	bHasThreatLocation = false;
	PatrolIndex = 0;
	ClearScriptedTurnProgram();

	// Валидация настройки BP — один раз при вселении. Второго прямого
	// damage-pipeline нет: без GA атака безопасно отклоняется.
	const AUnitBase* Unit = Cast<AUnitBase>(InPawn);
	if (Unit && !Unit->AttackAbilityClass)
	{
		UE_LOG(LogXRU1AI, Error, TEXT("[AI] У %s не назначен AttackAbilityClass — атака запрещена. ")
			TEXT("Задай BP_GA_Attack в Class Defaults BP юнита."),
			*GetNameSafe(InPawn));
	}
}

void AUnitAIController::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Actor || !UTacticsCombatStatics::AreHostile(MyPawn, Actor))
	{
		return;
	}

	if (Stimulus.WasSuccessfullySensed())
	{
		// Red alert: враг в прямой видимости.
		AlertState = EUnitAlertState::Combat;
		LastKnownThreatLocation = Actor->GetActorLocation();
		bHasThreatLocation = true;
	}
	else if (AlertState == EUnitAlertState::Combat)
	{
		// Цель пропала из виду: red → yellow, идём к последней известной точке.
		AlertState = EUnitAlertState::Investigate;
		LastKnownThreatLocation = Stimulus.StimulusLocation;
		bHasThreatLocation = true;
	}
}

void AUnitAIController::NotifyNoiseHeard(const FVector& NoiseLocation)
{
	// Шум не понижает тревогу: в бою уже знаем больше, чем «где-то стреляли».
	if (AlertState != EUnitAlertState::Combat)
	{
		AlertState = EUnitAlertState::Investigate;
		LastKnownThreatLocation = NoiseLocation;
		bHasThreatLocation = true;
	}
}

void AUnitAIController::ExecuteUnitTurn(FSimpleDelegate OnFinished)
{
	TurnFinishedDelegate = MoveTemp(OnFinished);
	bTurnMoveInProgress = false;
	bCoverMoveDoneThisTurn = false;
	bManeuverInProgress = false;
	DecisionOrdinalThisTurn = 0;
	FailedAttackTargetsThisTurn.Reset(); // новый ход — новые попытки
	bScriptedRepositionTried = false;
	AdvanceTurnStep();
}

void AUnitAIController::AdvanceTurnStep()
{
	AUnitBase* Unit = Cast<AUnitBase>(GetPawn());
	UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;

	// Нет пешки/очков действия или юнит выбыл — заканчиваем.
	if (!Unit || !ActionPoints || !UTacticsCombatStatics::IsUnitAlive(Unit))
	{
		FinishUnitTurn();
		return;
	}

	// AP обычного выстрела уже списаны в Begin, но следующий decision/finish
	// разрешён только после montage + ReturnToAnchor/Abort terminal callback.
	if (IsFireSubactionInProgress(Unit))
	{
		ScheduleNextStep();
		return;
	}

	// Сценарная ПРОГРАММА хода доминирует над всем остальным AI и проверяется
	// ДО ОД: бесплатный шаг не тратит очки, а завершение программы обязано
	// выставить флаг для задачи обучения даже при полностью сгоревших ОД.
	if (ScriptedTurnProgram.Num() > 0)
	{
		if (const UTurnManagerSubsystem* TurnManager = GetWorld()
			? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
			TurnManager && (!TurnManager->IsInCombat() || !TurnManager->IsUnitOnActiveSide(Unit)))
		{
			FinishUnitTurn();
			return;
		}
		if (ScriptedTurnStepIndex >= ScriptedTurnProgram.Num())
		{
			// Все шаги исполнены: ход завершается, штатный AI в остаток ОД
			// не подключается — иначе постановка кончалась бы свободным выстрелом.
			ScriptedTurnProgram.Reset();
			ScriptedTurnStepIndex = 0;
			bScriptedTurnProgramExecuted = true;
			FinishUnitTurn();
			return;
		}
		if (StepScriptedProgram(Unit))
		{
			ScriptedStepFailedAttempts = 0;
			return;
		}
		// Шаг не начался (точка недостижима бюджетом/контроллер занят). После
		// нескольких попыток шаг ПРОПУСКАЕТСЯ: вечный повтор доводил задачу до
		// Timeout, валил квест и отдавал остаток хода utility-AI (v2.9).
		++ScriptedStepFailedAttempts;
		if (ScriptedStepFailedAttempts >= 6)
		{
			const FScriptedTurnStep& FailedStep = ScriptedTurnProgram[ScriptedTurnStepIndex];
			const AActor* Destination = FailedStep.Destination.Get();
			UE_LOG(LogXRU1AI, Warning,
				TEXT("[AI] %s: шаг %d программы не начался за %d попыток — ПРОПУСКАЮ ")
				TEXT("(точка %s, прямая дистанция %.0f см; путь дороже бюджета?)"),
				*GetNameSafe(Unit), ScriptedTurnStepIndex, ScriptedStepFailedAttempts,
				*GetNameSafe(Destination),
				Destination ? FVector::Dist2D(Unit->GetActorLocation(),
					Destination->GetActorLocation()) : -1.f);
			ScriptedStepFailedAttempts = 0;
			++ScriptedTurnStepIndex;
			ScheduleNextStep();
			return;
		}
		if (ScriptedStepFailedAttempts == 1)
		{
			UE_LOG(LogXRU1AI, Warning,
				TEXT("[AI] %s: шаг %d сценарной программы хода не начался — повторяю"),
				*GetNameSafe(Unit), ScriptedTurnStepIndex);
		}
		ScheduleNextStep();
		return;
	}

	if (!ActionPoints->HasActionsLeft())
	{
		FinishUnitTurn();
		return;
	}

	// Бой мог закончиться (или фаза смениться) во время нашего перемещения —
	// например, реакционный выстрел Overwatch снял последнего юнита стороны.
	const UWorld* World = GetWorld();
	if (const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr)
	{
		if (!TurnManager->IsInCombat() || !TurnManager->IsUnitOnActiveSide(Unit))
		{
			FinishUnitTurn();
			return;
		}
	}

	// Увеличиваем только перед реальным выбором действия. Ожидание montage,
	// отсутствие AP и завершившийся бой не меняют воспроизводимую последовательность.
	++DecisionOrdinalThisTurn;

	// Сценарный приказ обучения важнее utility: шаги A4/A7/B4 обязаны показать
	// ровно один предсказуемый выстрел по заранее известному бойцу. Приказ идёт
	// через тот же GA_Attack, поэтому урон, montage и FireCommit — настоящие.
	if (AActor* ScriptedTarget = ScriptedAttackTarget.Get())
	{
		if (!UTacticsCombatStatics::IsUnitAlive(ScriptedTarget))
		{
			ClearScriptedAttackOrder();
		}
		else if (TryFireAtTarget(Unit, ScriptedTarget))
		{
			ClearScriptedAttackOrder();
			ScheduleNextStep();
			return;
		}
		else if (!bScriptedRepositionTried)
		{
			// Из текущей позиции линии огня нет (расстановка со свободным допуском
			// это допускает). Приказ НЕ снимается: боец штатным манёвром хода
			// сближается с целью — навигация сама огибает стену, — и следующий
			// шаг этого же хода повторяет выстрел. Один ретрай за ход.
			bScriptedRepositionTried = true;
			FailedAttackTargetsThisTurn.Remove(ScriptedTarget);
			UE_LOG(LogXRU1AI, Warning,
				TEXT("[AI] %s: сценарной цели %s не видно — сближаюсь и повторю выстрел"),
				*GetNameSafe(Unit), *GetNameSafe(ScriptedTarget));
			// Сама позиция цели занята её же occupancy-диском — планировщик такую
			// точку честно отвергает. Выталкиваем цель манёвра из дисков юнитов.
			FVector ApproachGoal = ScriptedTarget->GetActorLocation();
			UTacticsCombatStatics::AdjustGoalOutOfUnits(GetWorld(), Unit, ApproachGoal);
			if (StartManeuverTo(Unit, ApproachGoal, TEXT("сценарное сближение")))
			{
				return;
			}
			// Полный маршрут не влез в бюджет AP (цель за длинным обходом) —
			// частичный шаг: точка на навмеш-пути к цели в пределах досягаемости.
			// Следующий ход повторит выстрел уже с более близкой позиции.
			FVector PartialGoal;
			if (UTacticsCombatStatics::GetPointAlongPathBudget(this, Unit,
					Unit->GetActorLocation(), ApproachGoal,
					Unit->MoveRange * FMath::Max(1,
						Unit->GetActionPoints() ? Unit->GetActionPoints()->CurrentActionPoints : 1),
					PartialGoal) &&
				UTacticsCombatStatics::AdjustGoalOutOfUnits(GetWorld(), Unit, PartialGoal) &&
				FVector::Dist2D(PartialGoal, Unit->GetActorLocation()) > 100.f &&
				StartManeuverTo(Unit, PartialGoal, TEXT("сценарное сближение (частичный шаг)")))
			{
				return;
			}
			ClearScriptedAttackOrder();
			UE_LOG(LogXRU1AI, Error,
				TEXT("[AI] %s: сценарное сближение с %s не началось (маршрут не найден "
					 "даже частично) — шаг обучения не закроется"),
				*GetNameSafe(Unit), *GetNameSafe(ScriptedTarget));
		}
		else
		{
			ClearScriptedAttackOrder();
			UE_LOG(LogXRU1AI, Error,
				TEXT("[AI] %s: сценарный выстрел по %s невозможен даже после сближения — шаг обучения не закроется"),
				*GetNameSafe(Unit), *GetNameSafe(ScriptedTarget));
		}
	}

	// Видимая цель мгновенно поднимает red alert (перцепция могла отстать на кадр).
	if (FindVisibleTarget())
	{
		AlertState = EUnitAlertState::Combat;
	}
	// Вскрытый под остаётся в бою, даже если конкретно этот боец сейчас никого
	// не видит: иначе половина группы каждый ход сваливалась бы обратно в патруль.
	else if (const UTacticalAIDirectorSubsystem* Director = GetAIDirector())
	{
		if (Director->IsUnitPodActivated(Unit))
		{
			AlertState = EUnitAlertState::Combat;
		}
	}

	bool bStepHandled = false;
	switch (AlertState)
	{
	case EUnitAlertState::Combat:      bStepHandled = StepCombat(Unit);      break;
	case EUnitAlertState::Investigate: bStepHandled = StepInvestigate(Unit); break;
	default:                           bStepHandled = StepPatrol(Unit);      break;
	}

	if (!bStepHandled)
	{
		// Ход закончен без единого действия. Это ЧАСТО симптом дефекта (боец не
		// знает о противнике, не смог построить путь, у него нет способностей),
		// поэтому причина печатается всегда, а не под cvar: именно её ищут,
		// когда «враги стоят столбом».
		const UTacticalAIDirectorSubsystem* Director = GetAIDirector();
		UE_LOG(LogXRU1AI, Warning,
			TEXT("[AI] %s закончил ход БЕЗ ДЕЙСТВИЙ: alert=%d, под вскрыт=%d, AP=%d, ")
			TEXT("видимая цель=%s, точек патруля=%d"),
			*GetNameSafe(Unit), static_cast<int32>(AlertState),
			(Director && Director->IsUnitPodActivated(Unit)) ? 1 : 0,
			ActionPoints->CurrentActionPoints, *GetNameSafe(FindVisibleTarget()),
			Unit->PatrolPoints.Num());
		FinishUnitTurn();
	}
}

bool AUnitAIController::StepCombat(AUnitBase* Unit)
{
	AActor* Target = FindVisibleTarget();
	if (!Target)
	{
		// Цели не видно, но под мог знать точку от союзника, попадания или шума.
		// Идём к самому достоверному контакту вместо возврата в патруль: боец,
		// по которому только что стреляли, обязан двигаться, а не стоять.
		FAIContact Contact;
		if (const UTacticalAIDirectorSubsystem* Director = GetAIDirector();
			Director && Director->GetBestContact(Unit, Contact))
		{
			LastKnownThreatLocation = Contact.LastKnownLocation;
			bHasThreatLocation = true;
		}

		AlertState = EUnitAlertState::Investigate;
		return StepInvestigate(Unit);
	}

	LastKnownThreatLocation = Target->GetActorLocation();
	bHasThreatLocation = true;

	const UActionPointsComponent* ActionPoints = Unit->GetActionPoints();
	const int32 PointsLeft = ActionPoints ? ActionPoints->CurrentActionPoints : 0;

	// 0) Продолжение НАЧАТОГО манёвра (отступление/рывок длиннее 1 AP): вторая
	// нога к той же точке. Это не новый выбор — bCoverMoveDoneThisTurn уже стоит.
	if (bManeuverInProgress)
	{
		bManeuverInProgress = false; // однократное продолжение за шаг; при успехе взводится снова
		if (PointsLeft > 0 && FVector::Dist2D(Unit->GetActorLocation(), PendingManeuverPoint) > 75.f)
		{
			if (MoveWithBudget(Unit, PendingManeuverPoint, /*AcceptanceRadius=*/40.f, PointsLeft))
			{
				bManeuverInProgress = true;
				return true;
			}
			// Продолжение сорвалось: боец остался в открытом поле, и это надо
			// видеть в логе, а не гадать «почему он не добежал».
			UE_LOG(LogXRU1AI, Warning,
				TEXT("[AI] %s: не смог продолжить манёвр к (%.0f, %.0f) — остался на месте"),
				*GetNameSafe(Unit), PendingManeuverPoint.X, PendingManeuverPoint.Y);
		}
	}

	// Снимок мира → выбор действия → исполнение (ADR-1). Три шага раздельны,
	// чтобы решение можно было напечатать ДО того, как оно изменит мир, — без
	// этого утилити-выбор неотлаживаем.
	const FAIDecisionContext Context = BuildDecisionContext(Unit, Target);
	const FAIDecision Decision = DecideAction(Context);
	if (ExecuteDecision(Unit, Decision))
	{
		return true;
	}

	// Лучший вариант не исполнился. Пробуем следующие по убыванию скора: у бойца
	// с видимой целью почти всегда остаётся хотя бы выстрел, и терять из-за
	// несостоявшейся перебежки весь ход он не должен.
	for (const FAIDecision& Fallback : RankedDecisions)
	{
		if (Fallback.Kind == Decision.Kind && Fallback.Score >= Decision.Score)
		{
			continue; // уже пробовали именно этот вариант
		}
		if (TacticsDebug::IsAILogEnabled())
		{
			UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: основной вариант не исполнился — пробую «%s» (скор %.1f)"),
				*GetNameSafe(Unit), *Fallback.Reason, Fallback.Score);
		}
		if (ExecuteDecision(Unit, Fallback))
		{
			return true;
		}
	}
	return false;
}

FAIDecisionContext AUnitAIController::BuildDecisionContext(AUnitBase* Unit, AActor* PrimaryThreat)
{
	FAIDecisionContext Context;
	Context.Unit = Unit;
	Context.Controller = this;
	Context.PrimaryThreat = PrimaryThreat;
	GatherVisibleThreats(Context.VisibleThreats);

	const UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
	Context.ActionPointsLeft = ActionPoints ? ActionPoints->CurrentActionPoints : 0;

	const UCoverDetectionComponent* Cover = Unit ? Unit->GetCoverDetection() : nullptr;
	Context.bExposed = !Cover || !PrimaryThreat ||
		Cover->GetCoverAgainst(PrimaryThreat) == ECoverType::None;

	Context.bCanShootNow = UGA_Attack::CanTargetActor(Unit, PrimaryThreat);
	Context.bLowHealth = Unit && Unit->GetHealth() <= Unit->GetMaxHealth() * RetreatHealthFraction;
	Context.bCoverMoveDoneThisTurn = bCoverMoveDoneThisTurn;
	Context.DecisionSeed = BuildDecisionSeed(Unit);

	// A8: лимит одновременно атакующих. Счётчик общий на сторону и живёт в
	// TurnManager — контроллер знает только про своего юнита.
	if (const UWorld* World = GetWorld())
	{
		if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			Context.bAttackThrottled = TurnManager->IsAttackThrottled(Unit);
		}
	}
	return Context;
}

uint32 AUnitAIController::BuildDecisionSeed(const AUnitBase* Unit, FName Salt) const
{
	// GetTypeHash(FName) зависит от process-local name index и не годится для
	// воспроизведения между запусками. CRC считается по стабильным строкам.
	uint32 Seed = FCrc::StrCrc32(Unit ? *Unit->GetName() : TEXT("None"));

	if (const UWorld* World = GetWorld())
	{
		const FString MapName = UGameplayStatics::GetCurrentLevelName(World, true);
		Seed = HashCombine(Seed, FCrc::StrCrc32(*MapName));
		if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			Seed = HashCombine(Seed, GetTypeHash(TurnManager->GetTurnNumber()));
		}
	}

	Seed = HashCombine(Seed, GetTypeHash(DecisionOrdinalThisTurn));
	return Salt.IsNone() ? Seed : HashCombine(Seed, FCrc::StrCrc32(*Salt.ToString()));
}

FAIDecision AUnitAIController::DecideAction(const FAIDecisionContext& Context)
{
	const bool bLogAI = TacticsDebug::IsAILogEnabled();
	if (bLogAI)
	{
		UE_LOG(LogXRU1AI, Log,
			TEXT("[AI] %s: контекст Alert=%d AP=%d Threat=%s Seed=%u"),
			*GetNameSafe(Context.Unit), static_cast<int32>(AlertState),
			Context.ActionPointsLeft, *GetNameSafe(Context.PrimaryThreat), Context.DecisionSeed);
	}

	// Перебираем по УБЫВАНИЮ верхней границы скора. Это не косметика: как только
	// текущий лучший результат достиг потолка следующего кандидата, остальные
	// заведомо не выиграют — и дорогой FindCoverPoint (48 точек × трейсы LOS)
	// для них не выполняется. Именно это отсечение сохраняет время хода врага
	// таким же, каким оно было у прежнего приоритетного списка.
	TArray<UAIActionEvaluator*> Ordered;
	Ordered.Reserve(ActionEvaluators.Num());
	for (const TObjectPtr<UAIActionEvaluator>& Evaluator : ActionEvaluators)
	{
		if (!Evaluator)
		{
			continue;
		}

		// Weight=0 — явный дизайнерский выключатель. Не полагаемся на то, что
		// нулевой потолок когда-нибудь отсечётся после сортировки: дорогой
		// ScoreAction такого evaluator вообще не должен попасть в перебор.
		if (Evaluator->Weight <= 0.f || Evaluator->GetMaxPossibleScore() <= 0.f)
		{
			if (bLogAI)
			{
				UE_LOG(LogXRU1AI, Log, TEXT("[AI]   %s: выключен весом/потолком"),
					*Evaluator->GetDebugName().ToString());
			}
			continue;
		}

		Ordered.Add(Evaluator);
	}
	Ordered.Sort([](const UAIActionEvaluator& A, const UAIActionEvaluator& B)
	{
		return A.GetMaxPossibleScore() > B.GetMaxPossibleScore();
	});

	FAIDecision Best;      // Kind == Skip — терминальный фолбэк, активация не зависнет
	float BestScore = 0.f; // ноль и ниже = «вариант не предлагается»
	RankedDecisions.Reset();

	for (UAIActionEvaluator* Evaluator : Ordered)
	{
		if (BestScore >= Evaluator->GetMaxPossibleScore())
		{
			break; // список отсортирован — дальше потолки только ниже
		}

		if (!Evaluator->IsApplicable(Context))
		{
			if (bLogAI)
			{
				UE_LOG(LogXRU1AI, Log, TEXT("[AI]   %s: неприменим"),
					*Evaluator->GetDebugName().ToString());
			}
			continue;
		}

		FAIDecision Candidate;
		const float RawScore = Evaluator->ScoreAction(Context, Candidate);
		const float Score = RawScore * Evaluator->Weight;

		if (bLogAI)
		{
			UE_LOG(LogXRU1AI, Log, TEXT("[AI]   %s: скор %.1f%s — %s"),
				*Evaluator->GetDebugName().ToString(), Score,
				RawScore <= 0.f ? TEXT(" (отказ)") : TEXT(""),
				Candidate.Reason.IsEmpty() ? TEXT("—") : *Candidate.Reason);
		}

		if (RawScore > 0.f)
		{
			// Запоминаем ВСЕ пригодные варианты, а не только лучший. Исполнение
			// может провалиться (маршрут не строится, точка занята), и тогда ход
			// обязан продолжиться следующим предложением, а не закончиться
			// ничем. Именно на этом бот с видимой целью и целым AP «стоял».
			Candidate.Score = Score;
			RankedDecisions.Add(Candidate);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Candidate;
			}
		}
	}

	RankedDecisions.Sort([](const FAIDecision& A, const FAIDecision& B)
	{
		return A.Score > B.Score;
	});

	if (bLogAI)
	{
		UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: РЕШЕНИЕ — %s (скор %.1f)"),
			*GetNameSafe(Context.Unit),
			Best.Reason.IsEmpty() ? TEXT("пропуск активации") : *Best.Reason, Best.Score);
	}
	return Best;
}

void AUnitAIController::DrawDecisionDebug(const AUnitBase* Unit, const FAIDecision& Decision) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!Unit || !World)
	{
		return;
	}

	static const TCHAR* KindNames[] = { TEXT("Shoot"), TEXT("Move"), TEXT("Overwatch"), TEXT("Hunker"), TEXT("Skip") };
	const int32 KindIndex = FMath::Clamp(static_cast<int32>(Decision.Kind), 0,
		UE_ARRAY_COUNT(KindNames) - 1);

	const FString Label = FString::Printf(TEXT("%s\n%s  score=%.1f\n%s"),
		*Unit->GetName(), KindNames[KindIndex], Decision.Score, *Decision.Reason);
	UTacticsDebugLibrary::DrawUnitDebugText(Unit, Label, FLinearColor(1.f, 0.8f, 0.2f));

	const float Duration = TacticsDebug::GetDebugDrawDuration();
	const FVector From = Unit->GetActorLocation();

	// Красная линия — по кому стреляем, зелёная — куда идём: два самых частых
	// вопроса при разборе хода врага.
	if (Decision.Target)
	{
		DrawDebugLine(World, From, Decision.Target->GetActorLocation(),
			FColor::Red, false, Duration, 0, 3.f);
	}
	if (Decision.Kind == EAIActionKind::Move)
	{
		DrawDebugLine(World, From, Decision.Destination, FColor::Green, false, Duration, 0, 3.f);
		DrawDebugSphere(World, Decision.Destination, 45.f, 12,
			Decision.bIsCoverManeuver ? FColor::Cyan : FColor::Green, false, Duration, 0, 2.f);
	}
#endif
}

bool AUnitAIController::ExecuteDecision(AUnitBase* Unit, const FAIDecision& Decision)
{
	// Визуальная отладка принятого решения: без неё «почему он побежал туда»
	// приходится реконструировать по тексту лога, сопоставляя координаты на глаз.
	if (TacticsDebug::IsAIDebugDrawEnabled())
	{
		DrawDecisionDebug(Unit, Decision);
	}

	switch (Decision.Kind)
	{
	case EAIActionKind::Shoot:
		if (!TryFireAtTarget(Unit, Decision.Target))
		{
			return false; // способность отказала — активацию завершаем, без зацикливания
		}
		ScheduleNextStep();
		return true;

	case EAIActionKind::Move:
		// Манёвр в укрытие взводит «один выбор на ход» и запоминает точку для
		// продолжения; простое сближение — нет (иначе бот, не дошедший до цели,
		// потерял бы право на манёвр в следующем шаге).
		if (Decision.bIsCoverManeuver)
		{
			// Приказ идёт РОВНО в выбранную точку: она уже проверена на
			// достижимость и занятость внутри FindCoverPoint, поэтому подменять
			// её здесь нечем и не нужно.
			// ⚠️ Точку для сверки прибытия запоминаем ТОЛЬКО если приказ принят:
			// иначе повисший флаг сравнил бы позицию с точкой несостоявшегося
			// манёвра и дал ложное «встал не туда».
			if (StartManeuverTo(Unit, Decision.Destination, *Decision.Reason))
			{
				ChosenManeuverPoint = Decision.Destination;
				bHasChosenManeuverPoint = true;
				return true;
			}
			return false;
		}
		return MoveWithBudget(Unit, Decision.Destination, Decision.AcceptanceRadius);

	case EAIActionKind::Overwatch:
		// Обе способности сжигают остаток AP (bConsumesAllRemainingAP), поэтому
		// следующий шаг увидит 0 очков, не найдёт применимого оценщика и штатно
		// завершит активацию. Планировать шаг всё равно НУЖНО: без него ход
		// повис бы (ни FinishUnitTurn, ни следующего AdvanceTurnStep).
		if (!TryActivateSelfAbility(Unit, Unit ? Unit->OverwatchAbilityClass : nullptr))
		{
			return false;
		}
		ScheduleNextStep();
		return true;

	case EAIActionKind::Hunker:
		if (!TryActivateSelfAbility(Unit, Unit ? Unit->HunkerAbilityClass : nullptr))
		{
			return false;
		}
		ScheduleNextStep();
		return true;

	default:
		return false; // Skip: делать нечего — активация окончена
	}
}

bool AUnitAIController::StartManeuverTo(AUnitBase* Unit, const FVector& Point, const TCHAR* Reason)
{
	if (TacticsDebug::IsAILogEnabled())
	{
		UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: решение — манёвр: %s → (%.0f, %.0f)"),
			*GetNameSafe(Unit), Reason, Point.X, Point.Y);
	}
	bCoverMoveDoneThisTurn = true;
	// Точка укрытия выбиралась с бюджетом MoveRange * ActionPointsLeft, поэтому
	// и планировать маршрут надо на тот же остаток AP: иначе выбранная цель
	// заведомо недостижима за одно очко и боец замирает в открытом поле.
	const UActionPointsComponent* ManeuverAP = Unit ? Unit->GetActionPoints() : nullptr;
	const int32 ManeuverBudget = ManeuverAP ? FMath::Max(1, ManeuverAP->CurrentActionPoints) : 1;
	if (MoveWithBudget(Unit, Point, /*AcceptanceRadius=*/40.f, ManeuverBudget))
	{
		// Точка может быть дальше 1 AP (отступление/рывок): продолжение сделает
		// следующий шаг хода — MoveWithBudget за раз проходит максимум MoveRange.
		PendingManeuverPoint = Point;
		bManeuverInProgress = true;
		return true;
	}
	return false;
}

bool AUnitAIController::FindCoverPoint(AUnitBase* Unit, const AActor* Threat, float PathBudget,
	bool bRetreat, FVector& OutPoint, bool bAdvance, FAICoverPointResult* OutDetails)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const UCoverDetectionComponent* Cover = Unit ? Unit->GetCoverDetection() : nullptr;
	if (!World || !NavSys || !Cover || !Threat || PathBudget <= 0.f)
	{
		return false;
	}

	// Тюнинг укрытий/LOS этого юнита (Ф3): TuningOverride → глобальный → CDO.
	const UCoverTuningDataAsset* Tuning = Cover->GetTuning();

	const FVector UnitLocation = Unit->GetActorLocation();
	const FVector ThreatLocation = Threat->GetActorLocation();

	// Половина капсулы нужна только для высоты ГЛАЗ (LOS): глаза = пол +
	// пол-капсулы + EyeHeightOffset. Укрытие теперь считается от точки ПОЛА
	// навмеша напрямую (§II.3, Ф2), поэтому в Base укрытия капсулу не прибавляем.
	float CapsuleHalfHeight = 88.f;
	float EyeHeight = CapsuleHalfHeight + Tuning->EyeHeightOffset;
	if (const ACharacter* UnitCharacter = Cast<ACharacter>(Unit))
	{
		if (const UCapsuleComponent* Capsule = UnitCharacter->GetCapsuleComponent())
		{
			CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			EyeHeight = CapsuleHalfHeight + Tuning->EyeHeightOffset;
		}
	}

	// Занятость: один снимок на весь перебор.
	TArray<FVector> Obstacles;
	UTacticsCombatStatics::GetUnitObstacles(World, Unit, Obstacles);
	const double ClearanceSq = FMath::Square(static_cast<double>(UTacticsCombatStatics::GetUnitClearance(Unit)));

	// A5: все видимые угрозы, а не одна. Укрытие оценивается ПРОТИВ ВСЕХ —
	// иначе бот прячется от одного врага, подставляясь остальным.
	TArray<TObjectPtr<AActor>> Threats;
	GatherVisibleThreats(Threats);
	if (Threats.Num() == 0 && Threat)
	{
		Threats.Add(const_cast<AActor*>(Threat)); // перцепция могла отстать на кадр
	}

	// Союзники — для штрафа за кучность (не лезть в одно укрытие втроём) И для
	// члена сплочённости (XCOM `fAllyVisWeight`): это два РАЗНЫХ механизма,
	// минимальная дистанция разводит, видимость своих не даёт разбрестись.
	TArray<AActor*> Allies;
	TArray<FVector> AllyLocations;
	if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
	{
		for (AActor* Ally : TurnManager->GetSideUnits(Unit))
		{
			if (Ally && Ally != Unit && UTacticsCombatStatics::IsUnitAlive(Ally))
			{
				Allies.Add(Ally);
				AllyLocations.Add(Ally->GetActorLocation());
			}
		}
	}

	// Ограничиваем число оцениваемых угроз ближайшими (аналог
	// MAX_EXPECTED_ENEMY_COUNT в XCOM): проверка линии огня из каждой точки по
	// каждой угрозе — самая дорогая часть перебора.
	if (Threats.Num() > MaxScoredThreats)
	{
		// Предикат берёт РАЗЫМЕНОВАННЫЕ элементы: TArray::Sort для массива
		// указателей сам их разыменовывает, а версия с TObjectPtr& заставляла
		// движок конструировать TObjectPtr из ссылки — это deprecated и сломает
		// сборку на следующей версии UE.
		Threats.Sort([&UnitLocation](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared(A.GetActorLocation(), UnitLocation) <
				FVector::DistSquared(B.GetActorLocation(), UnitLocation);
		});
		Threats.SetNum(MaxScoredThreats);
	}

	/**
	 * ОСОЗНАННАЯ ОЦЕНКА ТОЧКИ: по КАЖДОЙ угрозе отдельно — закрыт ли я от неё и
	 * вижу ли я её оттуда. Никакого «среднего балла вслепую»: в момент выбора
	 * юнит обязан знать, от кого прячется и кого сможет обстрелять.
	 *
	 * Ценность укрытия — формула XCOM:
	 *   (N_откр*OpenCoverFactor + N_half*HalfCoverFactor + N_full*FullCoverFactor) / N
	 * Резко отрицательный OpenCoverFactor и даёт «AI боится флангов»: открытость
	 * против одного врага перевешивает укрытие против двух других.
	 *
	 * ⚠️ ЦЕНА. На каждую угрозу здесь идёт трейс укрытия ПЛЮС проверка линии
	 * огня с перебором позиций выглядывания. Это самая дорогая часть всего
	 * перебора, поэтому: число угроз ограничено `MaxScoredThreats`, вызов идёт
	 * ПОСЛЕ проверки достижимости, а сам `FindCoverPoint` не запускается вовсе,
	 * когда у бота есть уверенный выстрел (отсечение по потолку скора в
	 * DecideAction).
	 */
	auto EvaluatePoint = [&](const FVector& FloorPoint, FAICoverPointResult& Out)
	{
		Out = FAICoverPointResult();
		Out.Point = FloorPoint;
		if (Threats.Num() == 0)
		{
			return 0.f;
		}

		const FVector EyeAtPoint = FloorPoint + FVector(0.f, 0.f, EyeHeight);

		float CoverSum = 0.f;
		for (const TObjectPtr<AActor>& ThreatActor : Threats)
		{
			if (!ThreatActor)
			{
				continue;
			}
			const FVector ThreatPos = ThreatActor->GetActorLocation();

			// 1) ОТ КОГО ЗАКРЫТ — той же физикой, что решает реальный выстрел
			// (толстый луч на высотах half/full), а не углом к стене.
			switch (Cover->EvaluateCoverAtLocation(FloorPoint, ThreatPos))
			{
			case ECoverType::Full: CoverSum += FullCoverFactor; ++Out.ThreatsCovered; break;
			case ECoverType::Half: CoverSum += HalfCoverFactor; ++Out.ThreatsCovered; break;
			default:               CoverSum += OpenCoverFactor; ++Out.ThreatsExposed;  break;
			}

			// 2) КОГО ОТТУДА ВИДНО. Тем же предикатом, что решает выстрел, —
			// иначе бот планирует по одним правилам, а стреляет по другим.
			if (FVector::Dist(FloorPoint, ThreatPos) <= Unit->AttackRange &&
				UTacticsCombatStatics::HasLineOfSightFromLocation(World, EyeAtPoint, ThreatActor, Unit))
			{
				++Out.ThreatsVisible;

				// 2b) A7 `SafeToMove`. Видимость взаимна: раз я вижу оттуда врага,
				// то и он видит меня — а если он В НАБЛЮДЕНИИ, то встретит меня
				// реакционным выстрелом. Считается ЗДЕСЬ, потому что стоит ровно
				// ноль: линия огня для этой пары уже посчитана строкой выше.
				//
				// ⚠️ Осознанное упрощение против XCOM: там проверяется весь
				// МАРШРУТ, у нас — только КОНЕЧНАЯ точка. Полная проверка пути
				// стоила бы отдельного перебора LOS по каждому отрезку каждого из
				// 48 кандидатов. Конечная точка — доминирующий член: именно на ней
				// юнит остаётся стоять до конца хода противника.
				if (const UAbilitySystemComponent* ThreatASC =
					UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ThreatActor))
				{
					if (ThreatASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Overwatch))
					{
						++Out.ThreatsOverwatching;
					}
				}
			}

			// 3) КОГО ФЛАНКИРУЕМ — считается В ОБРАТНУЮ сторону: не «прикрыт ли
			// я», а «работает ли укрытие врага против выстрела ИЗ ЭТОЙ ТОЧКИ».
			if (UTacticsCombatStatics::IsTargetFlankedByLocation(ThreatActor, FloorPoint))
			{
				++Out.ThreatsFlanked;
			}
		}

		// 4) СПЛОЧЁННОСТЬ (XCOM `fAllyVisWeight`): видно ли отсюда своих. Быстрый
		// путь LOS без выглядывания — вопрос «поддержат ли меня», а не «попаду ли
		// я», поэтому края укрытий перебирать незачем.
		for (const AActor* Ally : Allies)
		{
			if (UTacticsCombatStatics::HasLineOfSightFromLocation(World, EyeAtPoint, Ally))
			{
				++Out.AlliesVisible;
			}
		}

		return CoverSum / Threats.Num();
	};

	// Взвешенная оценка позиции (веса — Tactics|AI|Weights):
	//   укрытие против ВСЕХ × вес + фланг + высота + линия огня
	//   ± дистанция до цели − цена пути, затем штраф за кучность.
	// В режиме ОТСТУПЛЕНИЯ дистанция инвертируется (награда за удаление) и
	// потеря линии огня не штрафуется — выживание важнее выстрела.
	auto ScorePosition = [&](const FVector& FloorPoint, FAICoverPointResult& Out)
	{
		const float ThreatDistance = FVector::Dist(FloorPoint, ThreatLocation);

		float Score = EvaluatePoint(FloorPoint, Out) * CoverDefenseWeight;
		if (Out.ThreatsFlanked > 0)
		{
			Score += FlankPositionBonus;
		}

		// «Сколько врагов видно» (XCOM fEnemyVisibility): не видно никого → −1.
		// Укрытие, из которого нельзя ответить, — это не позиция, а угол.
		const float VisibilityScore = Out.ThreatsVisible > 0
			? static_cast<float>(Out.ThreatsVisible) / FMath::Max(1, MaxScoredThreats)
			: -1.f;
		Score += VisibilityScore * EnemyVisibilityWeight;

		// Сплочённость: доля видимых своих × вес (XCOM `fAllyVisWeight`). Без
		// этого члена у AI был только анти-кучный штраф — отряд умел разбегаться,
		// но не умел держать линию.
		if (Allies.Num() > 0)
		{
			Score += (static_cast<float>(Out.AlliesVisible) / Allies.Num()) * AllyVisibilityWeight;
		}

		// A7 `SafeToMove`: точка под чужим наблюдением. Штраф за КАЖДОГО
		// наблюдателя — два овервотча на одном направлении вдвое опаснее одного.
		Score -= Out.ThreatsOverwatching * OverwatchExposurePenalty;

		// Линию огня считаем по ЛЮБОЙ угрозе, а не только по основной цели:
		// иначе точка, откуда простреливается сосед, но не «главный», честно
		// считалась бы бесполезной.
		const bool bCanShoot = Out.ThreatsVisible > 0;

		// Превышение над целью: тот же порог, что даёт бонус к точности.
		if (FloorPoint.Z - ThreatLocation.Z >= Tuning->HeightAdvantageZ)
		{
			Score += HeightPositionBonus;
		}

		// При отступлении И при наступлении отсутствие линии огня НЕ штрафуется:
		// иначе бот отвергает промежуточное укрытие без выстрела и бежит напролом.
		Score += bCanShoot ? LineOfFireBonus : ((bRetreat || bAdvance) ? 0.f : -LoseLineOfFirePenalty);
		Score -= TravelCostPerCm * FVector::Dist2D(UnitLocation, FloorPoint);
		if (bRetreat)
		{
			Score += RetreatRewardPerCm * ThreatDistance;
		}
		else
		{
			// Близость к ИДЕАЛЬНОЙ дистанции боя (A4), формула XCOM:
			//   1 − |dist − ideal| / falloff, зажато [−1..1].
			// Это единственное, что удерживает бота от «добежать вплотную»:
			// укрытие тянет прятаться, линия огня — видеть цель, а дистанцию до
			// A4 не оценивал никто.
			const float Deviation = FMath::Abs(ThreatDistance - Unit->IdealCombatRange);
			const float RangeScore = FMath::Clamp(1.f - Deviation / FMath::Max(1.f, IdealRangeFalloff), -1.f, 1.f);
			Score += RangeScore * IdealRangeWeight;
		}

		// Кучность: множитель только к ПОЛОЖИТЕЛЬНОМУ скору (XCOM). К
		// отрицательному он бы работал наоборот — делал плохую точку лучше.
		if (Score > 0.f && MinSpreadDistance > 0.f)
		{
			for (const FVector& AllyLocation : AllyLocations)
			{
				if (FVector::Dist2D(AllyLocation, FloorPoint) < MinSpreadDistance)
				{
					Score *= SpreadPenaltyMultiplier;
					break;
				}
			}
		}
		return Score;
	};

	// Базовая линия — ТЕКУЩАЯ позиция теми же правилами + порог значимости:
	// не дёргаемся ради косметики.
	// ⚠️ Базовую линию считаем от ТОЧКИ ПОЛА юнита, как и всех кандидатов
	// (у них это спроецированная на навмеш точка). Подставить сюда ActorLocation
	// (центр капсулы) значило бы трассировать укрытие на 88 см выше — базовая
	// линия и кандидаты считались бы разными правилами.
	const FVector UnitFloor = UnitLocation - FVector(0.f, 0.f, CapsuleHalfHeight);
	FAICoverPointResult BaselineDetails;
	const float BaselineScore = ScorePosition(UnitFloor, BaselineDetails);
	float BestScore = BaselineScore + RelocateBias;
	FAICoverPointResult BestDetails;
	bool bFound = false;

	// Кольцевой сэмплинг вокруг юнита в пределах бюджета пути (1 AP — манёвр
	// с выстрелом, 2 AP — отступление/рывок: поле поиска шире).
	const float Radii[] = {0.35f, 0.6f, 0.85f, 1.0f};
	constexpr int32 AngleSteps = 12;
	for (const float RadiusFactor : Radii)
	{
		const float Radius = PathBudget * RadiusFactor;
		for (int32 Step = 0; Step < AngleSteps; ++Step)
		{
			const float Angle = 2.f * PI * Step / AngleSteps;
			const FVector Candidate = UnitLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;

			FNavLocation Projected;
			if (!NavSys->ProjectPointToNavigation(Candidate, Projected, FVector(100.f, 100.f, 300.f)))
			{
				continue;
			}

			// Точка занята/впритык к другому юниту — не кандидат.
			bool bBlocked = false;
			for (const FVector& Obstacle : Obstacles)
			{
				if (FVector::DistSquared2D(Obstacle, Projected.Location) < ClearanceSq)
				{
					bBlocked = true;
					break;
				}
			}
			if (bBlocked)
			{
				continue;
			}

			// Base укрытия — точка пола навмеша напрямую (§II.3, Ф2).
			FVector CandidatePoint = Projected.Location;
			ECoverType CoverType = Cover->EvaluateCoverAtLocation(CandidatePoint, ThreatLocation);

			// ПРИЛИПАНИЕ К УКРЫТИЮ. Точка открыта — ищем стену между ней и
			// угрозой и переносим кандидата вплотную к стене С НАШЕЙ СТОРОНЫ.
			// Сторона получается правильной по построению: мы отступаем от точки
			// попадания НАЗАД по лучу к цели, то есть всегда на свою половину.
			// Без этого 48 точек кольца почти никогда не попадали в узкую
			// (CoverTraceDistance) полосу «в укрытии», и бот вставал рядом со
			// стеной, оставаясь под прямой линией огня.
			if (CoverType == ECoverType::None && CoverSnapDistance > 0.f)
			{
				const FVector ToThreat = (ThreatLocation - CandidatePoint).GetSafeNormal2D();
				if (!ToThreat.IsNearlyZero())
				{
					// Трейс на высоте низкого укрытия: ищем ЛЮБУЮ стену, годную
					// хотя бы под half. Высокая стена этим же лучом тоже ловится.
					const FVector TraceStart = CandidatePoint + FVector(0.f, 0.f, Tuning->HalfCoverHeight);
					FHitResult WallHit;
					FCollisionQueryParams SnapParams(SCENE_QUERY_STAT(CoverSnap), false, Unit);
					// Та же геометрия, что у LOS и укрытия: иначе «стеной» для
					// прилипания оказывался стоящий на пути юнит.
					if (World->LineTraceSingleByObjectType(WallHit, TraceStart,
						TraceStart + ToThreat * CoverSnapDistance,
						UTacticsCombatStatics::GetShotGeometryObjects(), SnapParams))
					{
						// Отступаем от стены на клиренс: встать В стену нельзя,
						// а трейс укрытия должен доставать до неё (< CoverTraceDistance).
						const float StandOff = FMath::Min(UTacticsCombatStatics::GetUnitClearance(Unit),
							Tuning->CoverTraceDistance * 0.75f);
						const FVector SnapGoal = FVector(WallHit.ImpactPoint.X, WallHit.ImpactPoint.Y,
							CandidatePoint.Z) - ToThreat * StandOff;

						FNavLocation SnapProjected;
						if (NavSys->ProjectPointToNavigation(SnapGoal, SnapProjected, FVector(80.f, 80.f, 300.f)))
						{
							const ECoverType SnapCover =
								Cover->EvaluateCoverAtLocation(SnapProjected.Location, ThreatLocation);
							if (SnapCover != ECoverType::None)
							{
								bool bSnapBlocked = false;
								for (const FVector& Obstacle : Obstacles)
								{
									if (FVector::DistSquared2D(Obstacle, SnapProjected.Location) < ClearanceSq)
									{
										bSnapBlocked = true;
										break;
									}
								}
								if (!bSnapBlocked)
								{
									CandidatePoint = SnapProjected.Location;
									CoverType = SnapCover;
								}
							}
						}
					}
				}
			}
			// ДОСТИЖИМОСТЬ — ДО оценки. Раньше было наоборот («дорогая проверка
			// последней»), и это было верно, пока оценка стоила один трейс. После
			// A5 оценка — это по каждой угрозе трейс укрытия ПЛЮС линия огня с
			// перебором позиций выглядывания, то есть на порядок дороже одного
			// запроса пути. Порядок инвертирован осознанно: сначала отсеиваем
			// точки, куда всё равно не дойти.
			FVector Reachable;
			if (!UTacticsCombatStatics::GetPointAlongPathBudget(this, Unit, UnitLocation,
					CandidatePoint, PathBudget, Reachable) ||
				FVector::Dist2D(Reachable, CandidatePoint) > 75.f)
			{
				continue;
			}

			// Полная оценка точки: по каждой угрозе — закрыт ли, вижу ли, фланкую
			// ли (план == факт: линия огня считается тем же предикатом, что решает
			// выстрел, и из тех же огневых позиций peek).
			FAICoverPointResult Details;
			const float Score = ScorePosition(CandidatePoint, Details);
			if (Score <= BestScore)
			{
				continue;
			}

			BestScore = Score;
			OutPoint = CandidatePoint;
			Details.Score = Score;
			BestDetails = Details;
			bFound = true;
		}
	}

	if (OutDetails)
	{
		*OutDetails = BestDetails;
	}

	// Почему манёвр выбран (или не выбран) — и ЧТО именно даёт выбранная точка.
	// Без этого «осознанность» укрытия проверить нечем.
	if (TacticsDebug::IsAILogEnabled())
	{
		if (bFound)
		{
			UE_LOG(LogXRU1AI, Log, TEXT("[AI]     позиция найдена (%.1f, %.1f): %s | было: %s | ")
				TEXT("скор %.1f против базы %.1f (порог %.1f)"),
				BestDetails.Point.X, BestDetails.Point.Y, *BestDetails.Describe(),
				*BaselineDetails.Describe(), BestScore, BaselineScore, BaselineScore + RelocateBias);
		}
		else
		{
			UE_LOG(LogXRU1AI, Log, TEXT("[AI]     позиция НЕ найдена: сейчас %s, скор %.1f, ")
				TEXT("порог %.1f (ничего лучше в бюджете)"),
				*BaselineDetails.Describe(), BaselineScore, BaselineScore + RelocateBias);
		}
	}
	return bFound;
}

bool AUnitAIController::TryActivateSelfAbility(AUnitBase* Unit, TSubclassOf<UTacticalAbility> AbilityClass)
{
	UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
	UAbilitySystemComponent* ASC = Unit ? Unit->GetAbilitySystemComponent() : nullptr;
	if (!ActionPoints || !ASC || !AbilityClass)
	{
		return false;
	}

	// Признак успеха — СПИСАННЫЕ AP, а не возврат TryActivateAbilityByClass: тот
	// отвечает true и когда способность активировалась, но тут же отказалась по
	// своим правилам (например, hunker без укрытия). Тот же критерий, что у
	// TryFireAtTarget, — иначе шаг хода повторялся бы вхолостую.
	const int32 PointsBefore = ActionPoints->CurrentActionPoints;
	ASC->TryActivateAbilityByClass(AbilityClass);
	return ActionPoints->CurrentActionPoints < PointsBefore;
}

bool AUnitAIController::TryFireAtTarget(AUnitBase* Unit, AActor* Target)
{
	UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
	if (!ActionPoints || !Target)
	{
		return false;
	}

	// ПРЕДОХРАНИТЕЛЬ ОТ ЗАЦИКЛИВАНИЯ: атака по этой цели уже не активировалась в
	// этом ходу — повторять её бессмысленно, мир с тех пор не менялся. Утилити
	// уходит на fallback-решения (перебежка/overwatch) вместо вечного повтора.
	if (FailedAttackTargetsThisTurn.Contains(Target))
	{
		if (TacticsDebug::IsAILogEnabled())
		{
			UE_LOG(LogXRU1AI, Log,
				TEXT("[AI] %s: атака по %s уже отклонялась в этом ходу — пропускаю"),
				*GetNameSafe(Unit), *GetNameSafe(Target));
		}
		return false;
	}

	// Штатный путь: то же событие, что шлёт контроллер игрока. Стоимость AP,
	// XCOM-сжигание остатка и BP-хуки выстрела живут в одном месте — GA_Attack.
	UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	const bool bHasAttackAbility = ASC && Unit->AttackAbilityClass &&
		ASC->FindAbilitySpecFromClass(Unit->AttackAbilityClass) != nullptr;

	if (!bHasAttackAbility)
	{
		UE_LOG(LogXRU1AI, Error, TEXT("[AI] %s: AttackAbilityClass не назначен/не выдан; "
			"прямой ResolveShot запрещён"), *GetNameSafe(Unit));
		return false;
	}

	FGameplayEventData Payload;
	Payload.Instigator = Unit;
	Payload.Target = Target;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Unit, TacticsGameplayTags::Event_Attack, Payload);

	// Принятие действия подтверждает ActionId, а не мгновенная дельта AP.
	FGuid ActionId;
	const bool bAccepted = UGA_Attack::GetAttackActionInProgressFor(Unit, ActionId);
	if (bAccepted)
	{
		UE_LOG(LogXRU1AI, Verbose, TEXT("[AI] %s: attack accepted id=%s"),
			*GetNameSafe(Unit), *ActionId.ToString(EGuidFormats::Digits));
	}
	else
	{
		// Активация отклонена (например, из замороженной позиции нет линии
		// огня). Цель блокируется до конца хода — см. предохранитель выше.
		FailedAttackTargetsThisTurn.Add(Target);
		UE_LOG(LogXRU1AI, Warning,
			TEXT("[AI] %s: атака по %s не активировалась — цель заблокирована до конца хода"),
			*GetNameSafe(Unit), *GetNameSafe(Target));
	}
	return bAccepted;
}

bool AUnitAIController::SetMovementPaused(bool bPaused)
{
	UPathFollowingComponent* PathComp = GetPathFollowingComponent();
	if (!PathComp)
	{
		return false;
	}
	const FAIRequestID RequestID = PathComp->GetCurrentRequestId();
	if (!RequestID.IsValid())
	{
		return false; // не в пути — приостанавливать нечего
	}

	if (bPaused)
	{
		if (PathComp->GetStatus() != EPathFollowingStatus::Moving)
		{
			return false;
		}
		PathComp->PauseMove(RequestID, EPathFollowingVelocityMode::Reset);
		return true;
	}

	if (PathComp->GetStatus() == EPathFollowingStatus::Paused)
	{
		PathComp->ResumeMove(RequestID);
	}
	return true;
}

bool AUnitAIController::StepInvestigate(AUnitBase* Unit)
{
	if (!bHasThreatLocation)
	{
		AlertState = EUnitAlertState::Patrol;
		return StepPatrol(Unit);
	}

	// Дошли до точки интереса и никого не нашли.
	if (FVector::Dist2D(Unit->GetActorLocation(), LastKnownThreatLocation) <= InvestigateAcceptanceRadius * 2.f)
	{
		// ⚠️ ЗДЕСЬ МЫ СОЗНАТЕЛЬНО ЛУЧШЕ XCOM 2.
		//
		// Самая частая претензия игроков к AI XCOM 2: «враги практически никогда
		// не встают в овервотч, если СЕЙЧАС не видят отряд» — то есть боец,
		// который слышал стрельбу и знает, откуда она, всё равно тупо ходит по
		// маршруту вместо того, чтобы держать направление под прицелом. Именно
		// это чинят популярные моды вроде «All Pods Active».
		//
		// У нас данные для правильного поведения уже есть: `Investigate`
		// означает «знаю точку, не вижу цель». Разворачиваемся к ней и с
		// вероятностью `InvestigateOverwatchChance` встаём в наблюдение.
		// Розыгрыш — детерминированное зерно (как у `UAIEval_Overwatch`): решение
		// обязано быть воспроизводимым, иначе лог решений врёт.
		if (Unit->OverwatchAbilityClass && InvestigateOverwatchChance > 0.f)
		{
			const uint32 Seed = BuildDecisionSeed(Unit, FName(TEXT("InvestigateOverwatch")));
			if (FRandomStream(static_cast<int32>(Seed)).GetFraction() < InvestigateOverwatchChance)
			{
				UTacticsCombatStatics::FaceActorTowards(Unit, LastKnownThreatLocation);
				if (TryActivateSelfAbility(Unit, Unit->OverwatchAbilityClass))
				{
					if (TacticsDebug::IsAILogEnabled())
					{
						UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: разведка — встал в наблюдение ")
							TEXT("на последнюю известную точку врага"), *GetNameSafe(Unit));
					}
					// Наблюдение сжигает остаток AP; шаг планируем, чтобы ход
					// штатно завершился (как в ExecuteDecision).
					ScheduleNextStep();
					return true;
				}
			}
		}

		bHasThreatLocation = false;
		AlertState = EUnitAlertState::Patrol;
		return StepPatrol(Unit);
	}

	return MoveWithBudget(Unit, LastKnownThreatLocation, InvestigateAcceptanceRadius);
}

bool AUnitAIController::StepPatrol(AUnitBase* Unit)
{
	if (Unit->PatrolPoints.Num() == 0)
	{
		// Пост без маршрута. Раньше боец просто пропускал ход — на карте без
		// расставленных PatrolPoints это выглядело как «половина врагов сломана»:
		// камера подлетала к ним, и они ничего не делали.
		// Часовой на посту держит направление под прицелом; это и полезнее, и
		// читается как осмысленное поведение.
		if (Unit->OverwatchAbilityClass && TryActivateSelfAbility(Unit, Unit->OverwatchAbilityClass))
		{
			if (TacticsDebug::IsAILogEnabled())
			{
				UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: пост без маршрута — встал в наблюдение"),
					*GetNameSafe(Unit));
			}
			ScheduleNextStep();
			return true;
		}
		return false;
	}

	const AActor* PatrolPoint = Unit->PatrolPoints[PatrolIndex % Unit->PatrolPoints.Num()];
	if (!PatrolPoint)
	{
		return false;
	}

	// У точки — идём к следующей на этом же ходу.
	if (FVector::Dist2D(Unit->GetActorLocation(), PatrolPoint->GetActorLocation()) <= 150.f)
	{
		++PatrolIndex;
		const AActor* Next = Unit->PatrolPoints[PatrolIndex % Unit->PatrolPoints.Num()];
		return Next ? MoveWithBudget(Unit, Next->GetActorLocation(), 100.f) : false;
	}

	return MoveWithBudget(Unit, PatrolPoint->GetActorLocation(), 100.f);
}

bool AUnitAIController::MoveWithBudget(AUnitBase* Unit, const FVector& Goal, float AcceptanceRadius,
	int32 MaxActionPoints)
{
	if (!Unit || !Unit->GetActionPoints())
	{
		return false;
	}
	MaxActionPoints = FMath::Max(1, MaxActionPoints);

	// В бою враг использует ровно тот же occupancy-aware планировщик, что и игрок:
	// волна заранее огибает диски союзников, а не надеется на локальный Detour Crowd.
	if (const UTurnManagerSubsystem* TurnManager = GetWorld()
		? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
		TurnManager && TurnManager->IsInCombat())
	{
		ATacticalPlayerController* PlayerController = GetWorld()
			? Cast<ATacticalPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
		FMoveOrderPlan Plan;
		if (!PlayerController ||
			!PlayerController->PlanMoveForUnit(Unit, Goal, MaxActionPoints, Plan) ||
			Plan.PathPoints.Num() < 2)
		{
			return false;
		}

		// Реальная стоимость плана: манёвр в укрытие за 2 AP теперь исполняется
		// ОДНИМ маршрутом и оплачивается целиком, как приказ игрока. Планирование
		// бюджетом в 1 AP оставляло бойца стоять на полпути посреди поля.
		PendingMoveActionPointCost = FMath::Max(1, Plan.ActionPointCost);
		bTurnMoveInProgress = true; // AP спишется в OnMoveCompleted.
		const EPathFollowingRequestResult::Type Result = MoveAlongRoute(
			Plan.PathPoints, FMath::Min(AcceptanceRadius, 40.f));
		if (Result == EPathFollowingRequestResult::RequestSuccessful)
		{
			Unit->NotifyUnitStateChanged();
			return true;
		}
		bTurnMoveInProgress = false;
		return false;
	}

	// Вне пошагового боя оставляем дешёвый navmesh-путь для патруля.
	FVector BudgetedGoal;
	if (!UTacticsCombatStatics::GetPointAlongPathBudget(this, Unit, Unit->GetActorLocation(), Goal,
		Unit->MoveRange, BudgetedGoal))
	{
		return false;
	}

	// Не вставать в диск занятости другого юнита (замена навмеш-вырезов).
	if (!UTacticsCombatStatics::AdjustGoalOutOfUnits(GetWorld(), Unit, BudgetedGoal))
	{
		return false;
	}

	// Бюджетная точка совпадает с текущей позицией — двигаться некуда.
	if (FVector::Dist2D(Unit->GetActorLocation(), BudgetedGoal) <= 50.f)
	{
		return false;
	}

	const EPathFollowingRequestResult::Type Result = MoveToLocation(BudgetedGoal, AcceptanceRadius);
	if (Result == EPathFollowingRequestResult::RequestSuccessful)
	{
		PendingMoveActionPointCost = 1;
		bTurnMoveInProgress = true; // AP спишется в OnMoveCompleted
		Unit->NotifyUnitStateChanged();
		return true;
	}
	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// У цели: тратим AP, чтобы ход гарантированно закончился, и продолжаем.
		Unit->GetActionPoints()->TrySpendActionPoint();
		ScheduleNextStep();
		return true;
	}
	return false;
}

// --- Движение по ломаной маршрута ------------------------------------------------

bool AUnitAIController::IsMoving() const
{
	// Выбывший боец никуда не идёт, чем бы ни кончился его приказ. Без этого
	// падение посреди маршрута (реакция Overwatch по бегущему) оставляло бы
	// bFollowingRoute висеть навсегда: юнит вечно «в пути», диск занятости не
	// ставится, зона не перестраивается — залипание без выхода.
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !UTacticsCombatStatics::IsUnitAlive(ControlledPawn))
	{
		return false;
	}
	const AUnitBase* Unit = Cast<AUnitBase>(ControlledPawn);
	return bFollowingRoute || PendingSettlementUnit.IsValid() ||
		(Unit && Unit->IsMoveSettlementInProgress()) ||
		GetMoveStatus() != EPathFollowingStatus::Idle;
}

bool AUnitAIController::IsFollowingPath() const
{
	// Только фактическое перемещение: маршрут в работе или активный path
	// following. Settlement (подшаг/доворот) сюда НЕ входит — см. комментарий
	// в заголовке о том, почему это не то же самое, что IsMoving.
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !UTacticsCombatStatics::IsUnitAlive(ControlledPawn))
	{
		return false;
	}
	return bFollowingRoute || GetMoveStatus() != EPathFollowingStatus::Idle;
}

EPathFollowingRequestResult::Type AUnitAIController::MoveAlongRoute(const TArray<FVector>& RoutePoints,
	float AcceptanceRadius)
{
	const AUnitBase* UnitAtStart = Cast<AUnitBase>(GetPawn());
	if (PendingSettlementUnit.IsValid() ||
		(UnitAtStart && UnitAtStart->IsMoveSettlementInProgress()))
	{
		return EPathFollowingRequestResult::Failed;
	}

	StopRoute();
	if (RoutePoints.Num() < 2 || !GetPawn())
	{
		return EPathFollowingRequestResult::Failed;
	}
	if (AUnitBase* Unit = Cast<AUnitBase>(GetPawn()))
	{
		if (UCoverDetectionComponent* Cover = Unit->GetCoverDetection())
		{
			Cover->RequestActiveCoverReselection();
		}
	}

	RouteLegs = RoutePoints;
	RouteLegIndex = 1; // [0] — точка старта, идём со второй вершины
	RouteAcceptanceRadius = AcceptanceRadius;
	bFollowingRoute = true;

	if (!RequestNextRouteLeg())
	{
		StopRoute();
		return EPathFollowingRequestResult::Failed;
	}
	if (AUnitBase* Unit = Cast<AUnitBase>(GetPawn()))
	{
		Unit->NotifyUnitStateChanged();
	}
	return EPathFollowingRequestResult::RequestSuccessful;
}

bool AUnitAIController::RequestNextRouteLeg()
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return false;
	}

	// Синхронный ответ движка на наш же запрос не должен рекурсивно листать
	// вершины — разбираем его здесь, в цикле.
	TGuardValue<bool> ReentryGuard(bRequestingRouteLeg, true);

	while (RouteLegs.IsValidIndex(RouteLegIndex))
	{
		const FVector Leg = RouteLegs[RouteLegIndex];
		const bool bFinalLeg = (RouteLegIndex == RouteLegs.Num() - 1);
		++RouteLegIndex;

		// Промежуточные углы стоят у занятых клеток с запасом всего ~26 см — их
		// проходим с малым радиусом приёмки (RouteCornerAcceptance), это ровно то,
		// на сколько path following срежет угол. Финальную вершину проходим ТОЧНО
		// в цель клика: у неё запаса клиренса нет — это ровно та точка, куда игрок
		// ткнул, и юнит обязан встать на неё (в т.ч. вплотную к укрытию).
		const float Acceptance = bFinalLeg ? RouteAcceptanceRadius : RouteCornerAcceptance;

		// bStopOnOverlap для ФИНАЛА выключаем. Движок считает достижение как
		// AcceptanceRadius + MinAgentRadiusPct * радиус капсулы (MinAgentRadiusPct
		// = 0.05, т.е. всего ~2 см) — вклад мал, но на финальной точке не нужен
		// вообще никакой люфт от капсулы: боец обязан встать ровно в точку клика.
		// Промежуточные углы оставляем с прибавкой (true) — там небольшой люфт
		// как раз помогает срезать угол мимо занятых клеток.
		const bool bStopOnOverlap = !bFinalLeg;

		// Вершина фактически уже пройдена (боец стартовал рядом с ней) — пропускаем,
		// иначе получили бы приказ «идти туда, где стоим» и ложный финиш отрезка.
		if (!bFinalLeg && FVector::Dist2D(ControlledPawn->GetActorLocation(), Leg) <= Acceptance)
		{
			continue;
		}

		if (MoveToLocation(Leg, Acceptance, bStopOnOverlap) == EPathFollowingRequestResult::RequestSuccessful)
		{
			return true;
		}
	}
	return false;
}

void AUnitAIController::NotifyPodActivated()
{
	// Под вскрыт: боец переходит в бой, даже если сам ещё никого не видит.
	// Именно это отличает группу от четырёх независимых слепых часовых.
	if (AlertState != EUnitAlertState::Combat)
	{
		AlertState = EUnitAlertState::Combat;
		if (AUnitBase* Unit = Cast<AUnitBase>(GetPawn()))
		{
			Unit->NotifyUnitStateChanged();
		}
	}
}

UTacticalAIDirectorSubsystem* AUnitAIController::GetAIDirector() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UTacticalAIDirectorSubsystem>() : nullptr;
}

void AUnitAIController::SetScriptedAttackOrder(AActor* Target)
{
	ScriptedAttackTarget = Target;
	bScriptedRepositionTried = false;
}

void AUnitAIController::ClearScriptedAttackOrder()
{
	ScriptedAttackTarget.Reset();
}

void AUnitAIController::SetScriptedTurnProgram(TArray<FScriptedTurnStep> Steps)
{
	ScriptedTurnProgram = MoveTemp(Steps);
	ScriptedTurnStepIndex = 0;
	ScriptedStepFailedAttempts = 0;
	bScriptedTurnProgramExecuted = false;
}

void AUnitAIController::ClearScriptedTurnProgram()
{
	ScriptedTurnProgram.Reset();
	ScriptedTurnStepIndex = 0;
	ScriptedStepFailedAttempts = 0;
	bScriptedTurnProgramExecuted = false;
}

void AUnitAIController::CancelScriptedTurnProgram()
{
	const bool bTurnRunning = TurnFinishedDelegate.IsBound();
	ClearScriptedTurnProgram();
	// Ход постановочного юнита не отдаётся utility-AI: провал режиссуры не
	// должен превращаться в свободный выстрел по отряду.
	if (bTurnRunning)
	{
		StopMovement();
		FinishUnitTurn();
	}
}

bool AUnitAIController::StepScriptedProgram(AUnitBase* Unit)
{
	const FScriptedTurnStep& Step = ScriptedTurnProgram[ScriptedTurnStepIndex];
	switch (Step.Type)
	{
	case EScriptedTurnStepType::MoveTo:
	{
		const AActor* Destination = Step.Destination.Get();
		if (!Destination)
		{
			// Точка исчезла из мира — шаг невыполним, пропускаем с ошибкой в лог.
			UE_LOG(LogXRU1AI, Error,
				TEXT("[AI] %s: у шага %d программы хода нет точки — пропускаю"),
				*GetNameSafe(Unit), ScriptedTurnStepIndex);
			++ScriptedTurnStepIndex;
			ScheduleNextStep();
			return true;
		}

		if (Step.bFreeMove)
		{
			// Постановочный «выход»: общий occupancy-план без бюджета ОД.
			// bTurnMoveInProgress взводится, чтобы settlement продолжил ход,
			// но стоимость 0 — финализация ничего не спишет.
			ATacticalPlayerController* PlayerController = GetWorld()
				? Cast<ATacticalPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
			FMoveOrderPlan Plan;
			if (!PlayerController || !PlayerController->PlanMoveForUnit(Unit,
					Destination->GetActorLocation(), /*MaxActionPoints=*/9, Plan) ||
				Plan.PathPoints.Num() < 2)
			{
				return false;
			}
			PendingMoveActionPointCost = 0;
			bTurnMoveInProgress = true;
			if (MoveAlongRoute(Plan.PathPoints, 40.f) != EPathFollowingRequestResult::RequestSuccessful)
			{
				bTurnMoveInProgress = false;
				PendingMoveActionPointCost = 1;
				return false;
			}
			++ScriptedTurnStepIndex;
			Unit->NotifyUnitStateChanged();
			return true;
		}

		// Обычное перемещение в бюджет 1 ОД — «отбегание» оплачивается честно.
		if (!MoveWithBudget(Unit, Destination->GetActorLocation(), 100.f))
		{
			return false;
		}
		++ScriptedTurnStepIndex;
		return true;
	}

	case EScriptedTurnStepType::SelfAbility:
	{
		if (!Step.AbilityClass)
		{
			++ScriptedTurnStepIndex;
			ScheduleNextStep();
			return true;
		}

		// Голограммам на чистом AUnitBase способность могла не выдаваться при
		// спавне (HunkerAbilityClass пуст) — выдаём грант на лету.
		if (UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent())
		{
			if (!ASC->FindAbilitySpecFromClass(Step.AbilityClass))
			{
				ASC->GiveAbility(FGameplayAbilitySpec(Step.AbilityClass, 1, INDEX_NONE, Unit));
			}
		}

		if (TryActivateSelfAbility(Unit, Step.AbilityClass))
		{
			++ScriptedTurnStepIndex;
			ScheduleNextStep();
			return true;
		}

		// Отказ по правилам способности (например, оборона без укрытия) — шаг
		// пропускаем, а не валим постановку: добивание C3 форсировано и от
		// буффа не зависит. Причина остаётся в логе для расстановщика.
		UE_LOG(LogXRU1AI, Warning,
			TEXT("[AI] %s: сценарная способность %s отклонена (нет укрытия/ОД?) — пропускаю шаг"),
			*GetNameSafe(Unit), *GetNameSafe(Step.AbilityClass));
		++ScriptedTurnStepIndex;
		ScheduleNextStep();
		return true;
	}
	}
	return false;
}

void AUnitAIController::StopRoute()
{
	RouteLegs.Reset();
	RouteLegIndex = 0;
	bFollowingRoute = false;
}

void AUnitAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	// Пришли ВНУТРЬ собственного запроса отрезка (движок ответил синхронно) —
	// решение примет цикл RequestNextRouteLeg, здесь делать нечего.
	if (bRequestingRouteLeg)
	{
		return;
	}

	// Снимок назначения берём ДО Super: latent AI Move To может разбудить BP и
	// завершить ability прямо внутри callback, но это не превращает service move
	// задним числом в обычный tactical move.
	const AUnitBase* UnitBeforeCallback = Cast<AUnitBase>(GetPawn());
	const bool bFireActionServiceMove = !bTurnMoveInProgress &&
		IsFireSubactionInProgress(UnitBeforeCallback);

	Super::OnMoveCompleted(RequestID, Result);

	// Дошли до промежуточной вершины маршрута — продолжаем ломаную. Ход НЕ
	// завершён: ни AP, ни уведомления о финише здесь быть не должно.
	if (bFollowingRoute)
	{
		if (Result.IsSuccess() && RequestNextRouteLeg())
		{
			return;
		}
		StopRoute(); // финальная вершина или срыв — дальше общий разбор финиша
	}

	if (bFireActionServiceMove)
	{
		if (AUnitBase* Unit = Cast<AUnitBase>(GetPawn()))
		{
			Unit->NotifyUnitStateChanged();
		}
		return;
	}

	// PathFollowing завершён, но само тактическое перемещение ещё может быть не
	// закончено: HugCover запускает latent-подшаг, а затем latent-доворот.
	// Следующий AP и HUD нельзя будить до окончания всей этой последовательности.
	AUnitBase* Unit = Cast<AUnitBase>(GetPawn());
	if (!Unit)
	{
		if (bTurnMoveInProgress)
		{
			bTurnMoveInProgress = false;
			ScheduleNextStep();
		}
		return;
	}

	if (UCoverDetectionComponent* Cover = Unit->GetCoverDetection())
	{
		Cover->EvaluateSurroundings();
		Unit->HugCover();
	}
	BeginMoveSettlement(Unit);
}

void AUnitAIController::BeginMoveSettlement(AUnitBase* Unit)
{
	GetWorldTimerManager().ClearTimer(MoveSettlementTimerHandle);
	PendingSettlementUnit = Unit;

	if (Unit && Unit->IsMoveSettlementInProgress())
	{
		GetWorldTimerManager().SetTimer(MoveSettlementTimerHandle, this,
			&AUnitAIController::TryFinalizeMoveSettlement, 0.02f, true);
		return;
	}

	TryFinalizeMoveSettlement();
}

void AUnitAIController::TryFinalizeMoveSettlement()
{
	AUnitBase* Unit = PendingSettlementUnit.Get();
	if (Unit && Unit->IsMoveSettlementInProgress())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(MoveSettlementTimerHandle);
	PendingSettlementUnit.Reset();

	// Токен приказа потребляется ровно один раз независимо от исхода: сорвавшийся
	// маршрут не должен позже засчитаться другому перемещению.
	const bool bWasPlayerOrderedMove = bPlayerOrderedMove;
	bPlayerOrderedMove = false;

	if (!Unit || Unit != GetPawn())
	{
		if (bTurnMoveInProgress)
		{
			bTurnMoveInProgress = false;
			ScheduleNextStep();
		}
		return;
	}

	// Финальный пересчёт делается после микро-сдвига, а не от старой точки
	// PathFollowing. Теперь cover, occupancy, HUD и следующий AI-шаг видят один
	// и тот же завершённый transform.
	UCoverDetectionComponent* Cover = Unit->GetCoverDetection();
	if (Cover)
	{
		Cover->EvaluateSurroundings();
	}

	// КОНТРОЛЬ «ВСТАЛ ТУДА, КУДА РЕШИЛ» — только после полного settlement.
	if (bHasChosenManeuverPoint && !bManeuverInProgress)
	{
		const float Drift = FVector::Dist2D(Unit->GetActorLocation(), ChosenManeuverPoint);
		bHasChosenManeuverPoint = false;
		if (Drift > ManeuverArrivalTolerance && TacticsDebug::IsAILogEnabled())
		{
			UE_LOG(LogXRU1AI, Warning, TEXT("[AI] %s: встал НЕ в выбранную точку — расхождение %.0f см ")
				TEXT("(решил (%.0f, %.0f), стоит (%.0f, %.0f)). Укрытие на месте: %d"),
				*GetNameSafe(Unit), Drift, ChosenManeuverPoint.X, ChosenManeuverPoint.Y,
				Unit->GetActorLocation().X, Unit->GetActorLocation().Y,
				Cover ? static_cast<int32>(Cover->BestCoverAround) : -1);
		}
	}

	Unit->NotifyUnitStateChanged();
	if (ATacticalPlayerController* PlayerController =
		Cast<ATacticalPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PlayerController->NotifyUnitMoveFinished(Unit);
	}

	// ЕДИНСТВЕННАЯ точка публикации перемещения в квест: маршрут доведён, поза у
	// стены и финальный доворот завершены, cover уже пересчитан от итогового
	// transform. Публикуем ровно ОДИН leaf — Open ЛИБО InCover, не оба.
	if (bWasPlayerOrderedMove)
	{
		Unit->PlayUnitSound(EUnitSoundEvent::MoveSettled);
		const bool bInCover = Cover && Cover->BestCoverAround != ECoverType::None;
		UTacticalQuestEvents::BroadcastQuestEventEx(this,
			bInCover
				? TacticalQuestTags::Event_Tactical_Movement_Settled_InCover
				: TacticalQuestTags::Event_Tactical_Movement_Settled_Open,
			Unit, Unit);

		// Точка маршрута шага засчитывается ровно здесь — вместе с quest-событием,
		// а не по факту выдачи приказа: отменённое перемещение маршрут не двигает.
		// Гасим по ТОЧКЕ ПРИКАЗА, а не по финальной позиции: прижатие к укрытию
		// и наклонная местность смещают бойца дальше допуска (жалоба по Осе).
		if (UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this))
		{
			Gate->NotifyDestinationReached(PlayerOrderedDestination, Unit);
		}
	}

	if (!bTurnMoveInProgress)
	{
		return;
	}
	bTurnMoveInProgress = false;

	if (UActionPointsComponent* ActionPoints = Unit->GetActionPoints())
	{
		// Стоимость 0 — легальный «бесплатный» шаг сценарной программы хода
		// (постановочный выход C1): финализация тогда ничего не списывает.
		if (PendingMoveActionPointCost > 0)
		{
			ActionPoints->TrySpendActionPoint(PendingMoveActionPointCost);
		}
	}
	PendingMoveActionPointCost = 1;
	ScheduleNextStep();
}

void AUnitAIController::ScheduleNextStep()
{
	if (ActionInterval <= 0.f)
	{
		TurnStepTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
			this, &AUnitAIController::AdvanceTurnStep);
		return;
	}
	GetWorldTimerManager().SetTimer(TurnStepTimerHandle, this,
		&AUnitAIController::AdvanceTurnStep, ActionInterval, false);
}

void AUnitAIController::FinishUnitTurn()
{
	GetWorldTimerManager().ClearTimer(TurnStepTimerHandle);
	GetWorldTimerManager().ClearTimer(MoveSettlementTimerHandle);
	PendingSettlementUnit.Reset();
	bTurnMoveInProgress = false;

	// Сначала сбрасываем делегат, потом зовём: колбэк может тут же начать новый ход.
	FSimpleDelegate Finished = MoveTemp(TurnFinishedDelegate);
	TurnFinishedDelegate.Unbind();
	Finished.ExecuteIfBound();
}

float AUnitAIController::ScoreTarget(const AUnitBase* Unit, const AActor* Candidate) const
{
	if (!Unit || !Candidate)
	{
		return -FLT_MAX;
	}

	float Score = 0.f;

	// 1) ШАНС ПОПАДАНИЯ — доминирующее слагаемое (XCOM: 70 против 50 за фланг и
	// 15 за добивание). Именно поэтому AI XCOM «всегда бьёт самого открытого»:
	// это не баг, а следствие весов. Считаем ТЕМ ЖЕ методом, что показывает HUD
	// игроку, — иначе AI жил бы по своей арифметике.
	// ⚠️ Отрицательный шанс = «выстрел невозможен» (нет линии огня / вне
	// дальности), а НЕ «маленький шанс». Без этой ветки недостижимая цель
	// получала бонус за «низкий шанс» и могла перебить достижимую.
	const float HitChance = UGA_Attack::ComputeAttackHitChance(Unit, Candidate);
	if (HitChance < 0.f)
	{
		Score += TargetScoreNoLineOfFire;
	}
	else if (HitChance >= TargetHitChanceHighThreshold)
	{
		Score += TargetScoreHitChanceHigh;
	}
	else if (HitChance >= TargetHitChanceLowThreshold)
	{
		Score += TargetScoreHitChanceMedium;
	}
	else
	{
		Score += TargetScoreHitChanceLow;
	}

	// 2) ПРОВОКАЦИЯ танка (GDD §7): в радиусе провокации враг ОБЯЗАН бить
	// провоцирующего — вес подобран так, чтобы перебить любую комбинацию
	// остальных слагаемых. Радиус нужен, иначе «крик» перетягивал бы врагов
	// через всю карту.
	//
	// ⚠️ Только если по нему реально можно выстрелить (HitChance >= 0): иначе
	// провокация заставляла бы врага игнорировать достижимые цели ради
	// недостижимой, а «обязан бить» этого не означает.
	if (HitChance >= 0.f)
	{
		if (const UAbilitySystemComponent* ASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate))
		{
			if (ASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Taunting) &&
				FVector::Dist(Unit->GetActorLocation(), Candidate->GetActorLocation()) <= TauntPriorityRadius)
			{
				Score += TargetScoreTaunting;
			}
		}
	}

	// 2.5) СВЕДЕНИЕ ОГНЯ: цель, по которой в этом ходу уже стрелял союзник,
	// приоритетнее. Это отрядное поведение, и именно оно делает высокую сложность
	// опасной: отряд добивает одного бойца вместо равномерного размазывания урона.
	if (HitChance >= 0.f && Style.FocusFireBonus > 0.f)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
			{
				Score += Style.FocusFireBonus * TurnManager->GetTimesTargetedThisTurn(Candidate);
			}
		}
	}

	// 3) ФЛАНГ: цель в укрытии, но против нас оно не работает (Ф8).
	if (UTacticsCombatStatics::IsTargetFlankedBy(Candidate, Unit))
	{
		Score += TargetScoreFlanked;
	}

	// 4) СОСТОЯНИЕ ЦЕЛИ. Добивание ценится заметно ниже шанса попасть —
	// иначе AI фокусит одного бойца до смерти и игра ломается.
	if (const AUnitBase* CandidateUnit = Cast<AUnitBase>(Candidate))
	{
		const float Health = CandidateUnit->GetHealth();
		const float MaxHealth = CandidateUnit->GetMaxHealth();
		if (Health <= Unit->ShotDamage)
		{
			Score += TargetScoreKillShot;
		}
		if (MaxHealth > 0.f && Health < MaxHealth)
		{
			Score += TargetScoreWounded;
		}
	}

	// 5) Тяжелораненый — «никогда, если есть выбор» через большой штраф, а не
	// через ветку-исключение (приём XCOM). Если других целей нет, он всё равно
	// выберется: скор окажется наименее плохим.
	if (UTacticsCombatStatics::IsUnitDowned(Candidate))
	{
		Score += TargetScoreDowned;
	}

	return Score;
}

AActor* AUnitAIController::FindVisibleTarget() const
{
	// Всё, что боец увидел сам, немедленно становится знанием всего пода.
	if (Perception)
	{
		if (AUnitBase* SelfUnit = Cast<AUnitBase>(GetPawn()))
		{
			if (UTacticalAIDirectorSubsystem* Director = GetAIDirector())
			{
				TArray<AActor*> Seen;
				Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);
				for (AActor* Actor : Seen)
				{
					if (UTacticsCombatStatics::AreHostile(SelfUnit, Actor) &&
						UTacticsCombatStatics::IsUnitAlive(Actor))
					{
						Director->NotifyEnemySpotted(SelfUnit, Actor);
					}
				}
			}
		}
	}

	const AUnitBase* MyUnit = Cast<AUnitBase>(GetPawn());
	if (!MyUnit || !Perception)
	{
		return nullptr;
	}

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);

	// К собственному зрению добавляем ОБЩИЕ контакты пода. Без этого боец знал
	// только то, что видит прямо сейчас: противник, стрелявший с дистанции
	// больше радиуса зрения, для него не существовал, и он пропускал ход под
	// огнём. Права стрелять контакт не даёт — ниже это решает CanTargetActor.
	if (const UTacticalAIDirectorSubsystem* Director = GetAIDirector())
	{
		TArray<FAIContact> Contacts;
		Director->GetPodContacts(UTacticalAIDirectorSubsystem::ResolvePodId(MyUnit), Contacts);
		for (const FAIContact& Contact : Contacts)
		{
			AActor* ContactActor = Contact.Target.Get();
			// В кандидаты попадает только тот, по кому реально можно выстрелить
			// прямо сейчас: память даёт знание, а не всеведение.
			if (ContactActor && !Perceived.Contains(ContactActor) &&
				UGA_Attack::CanTargetActor(MyUnit, ContactActor))
			{
				Perceived.Add(ContactActor);
			}
		}
	}

	const bool bLogAI = TacticsDebug::IsAILogEnabled();

	AActor* Best = nullptr;
	float BestScore = -FLT_MAX;

	for (AActor* Actor : Perceived)
	{
		if (!UTacticsCombatStatics::AreHostile(MyUnit, Actor) ||
			!UTacticsCombatStatics::IsUnitAlive(Actor))
		{
			continue;
		}

		const float Score = ScoreTarget(MyUnit, Actor);
		if (bLogAI)
		{
			// ⚠️ Отдельно проговариваем ПРОВОКАЦИЮ. Без этого «танк крикнул, а по
			// нему не стреляют» неотличимо на глаз от трёх разных причин: тега
			// нет, цель вне радиуса, выстрела по ней нет. В логе была видна
			// только итоговая цифра, и разобрать было нечем.
			const TCHAR* TauntNote = TEXT("");
			if (const UAbilitySystemComponent* CandidateASC =
				UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
			{
				if (CandidateASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Taunting))
				{
					const float TauntDist = FVector::Dist(MyUnit->GetActorLocation(), Actor->GetActorLocation());
					TauntNote = TauntDist <= TauntPriorityRadius
						? TEXT(" [ПРОВОКАЦИЯ активна]")
						: TEXT(" [провоцирует, но ВНЕ радиуса]");
				}
			}
			UE_LOG(LogXRU1AI, Log, TEXT("[AI]   цель %s: скор %.0f (шанс %.0f%%)%s"),
				*GetNameSafe(Actor), Score, UGA_Attack::ComputeAttackHitChance(MyUnit, Actor), TauntNote);
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Actor;
		}
	}
	return Best;
}

void AUnitAIController::GatherVisibleThreats(TArray<TObjectPtr<AActor>>& OutThreats) const
{
	OutThreats.Reset();

	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Perception)
	{
		return;
	}

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);
	for (AActor* Actor : Perceived)
	{
		if (UTacticsCombatStatics::AreHostile(MyPawn, Actor) &&
			UTacticsCombatStatics::IsUnitAlive(Actor))
		{
			OutThreats.AddUnique(Actor);
		}
	}

	// Скоринг позиции обязан учитывать ВСЕ известные угрозы, иначе боец прячется
	// от одного стрелка, подставляясь под второго, о котором под уже знает.
	if (const UTacticalAIDirectorSubsystem* Director = GetAIDirector())
	{
		TArray<FAIContact> Contacts;
		Director->GetPodContacts(
			UTacticalAIDirectorSubsystem::ResolvePodId(Cast<AUnitBase>(MyPawn)), Contacts);
		for (const FAIContact& Contact : Contacts)
		{
			if (AActor* ContactActor = Contact.Target.Get())
			{
				if (UTacticsCombatStatics::IsUnitAlive(ContactActor))
				{
					OutThreats.AddUnique(ContactActor);
				}
			}
		}
	}
}
