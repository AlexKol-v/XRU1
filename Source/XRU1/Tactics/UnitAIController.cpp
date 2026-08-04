#include "UnitAIController.h"
#include "AIActionEvaluators.h"
#include "AIBehaviorProfileDataAsset.h"
#include "UnitBase.h"
#include "TacticalPlayerController.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "CoverTuningDataAsset.h"
#include "FogOfWarSubsystem.h" // темп хода зависит от того, видит ли игрок бойца
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
#include "GameFramework/CharacterMovementComponent.h"
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
	RecentPositionPenalty = PositionTuning.RecentPositionPenalty;
	RecentPositionRadius = PositionTuning.RecentPositionRadius;

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
	ContactMemory.Reset();
	// Стартовая точка маршрута выбирается лениво, на первом шаге патруля:
	// см. ResolveInitialPatrolIndex (ближайшая точка + смещение группы).
	PatrolIndex = 0;
	bPatrolIndexResolved = false;
	PatrolDirection = 1;

	// Скорость скрытого хода переключается СОБЫТИЕМ, а не опросом: боец,
	// выбежавший из тумана посреди перебежки, обязан замедлиться в тот же кадр,
	// когда игрок его увидел, иначе на глазах отряда он промчится ускоренно.
	BaseWalkSpeed = 0.f;
	if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		Fog->OnActorVisibilityChanged.AddUniqueDynamic(
			this, &AUnitAIController::HandleFogVisibilityChanged);
	}
	// Новая пешка — новая зона удержания: якорь перезапишется её позицией.
	bPatrolAnchorSet = false;
	bReportedOffNavmesh = false;
	RecentTurnPositions.Reset();
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

void AUnitAIController::RememberContact(AActor* Target, const FVector& Location,
	EAIContactSource Source, float Confidence)
{
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	const int32 Turn = TurnManager ? TurnManager->GetTurnNumber() : 0;

	// Контакт про ОДНОГО и того же противника обновляется, а не дублируется.
	// Подозрения (без актора) объединяются по близости точки: три выстрела из
	// одного окна — это одна цель для разведки, а не три.
	for (FAIContact& Existing : ContactMemory)
	{
		const bool bSameActor = Target && Existing.bActorKnown && Existing.Target.Get() == Target;
		const bool bSameSpot = !Target && !Existing.bActorKnown &&
			FVector::Dist2D(Existing.LastKnownLocation, Location) < 400.f;
		if (!bSameActor && !bSameSpot)
		{
			continue;
		}
		// Менее достоверный источник не портит более достоверный в том же ходу:
		// увиденное своими глазами важнее услышанного.
		if (Confidence >= Existing.Confidence || Existing.LastUpdatedTurn < Turn)
		{
			Existing.LastKnownLocation = Location;
			Existing.Source = Source;
			Existing.Confidence = FMath::Max(Existing.Confidence, Confidence);
		}
		Existing.LastUpdatedTurn = Turn;
		return;
	}

	FAIContact Contact;
	Contact.Target = Target;
	Contact.bActorKnown = (Target != nullptr);
	Contact.LastKnownLocation = Location;
	Contact.LastUpdatedTurn = Turn;
	Contact.Source = Source;
	Contact.Confidence = Confidence;
	ContactMemory.Add(Contact);

	if (ContactMemory.Num() > MaxContactMemory)
	{
		// Вытесняем самый слабый, а не самый старый: свежий шум за стеной не
		// должен выбивать подтверждённого стрелка, по которому бойца жгут.
		int32 WeakestIndex = 0;
		for (int32 i = 1; i < ContactMemory.Num(); ++i)
		{
			if (ContactMemory[i].Confidence < ContactMemory[WeakestIndex].Confidence)
			{
				WeakestIndex = i;
			}
		}
		ContactMemory.RemoveAt(WeakestIndex);
	}
}

bool AUnitAIController::GetBestContact(FAIContact& OutContact) const
{
	const FAIContact* Best = nullptr;
	for (const FAIContact& Contact : ContactMemory)
	{
		if (!Contact.IsValidContact())
		{
			continue;
		}
		// Мёртвая цель контактом не считается: идти проверять труп незачем.
		if (Contact.bActorKnown && !UTacticsCombatStatics::IsUnitAlive(Contact.Target.Get()))
		{
			continue;
		}
		if (!Best || Contact.Confidence > Best->Confidence ||
			(Contact.Confidence == Best->Confidence && Contact.LastUpdatedTurn > Best->LastUpdatedTurn))
		{
			Best = &Contact;
		}
	}
	if (!Best)
	{
		return false;
	}
	OutContact = *Best;
	return true;
}

void AUnitAIController::AgeContactMemory()
{
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	const int32 Turn = TurnManager ? TurnManager->GetTurnNumber() : 0;
	const int32 MemoryTurns = FMath::Max(1, ContactMemoryTurns);

	// Старение измеряется ХОДАМИ, а не кадрами: иначе долгая анимация выстрела
	// «состарила» бы знание сильнее, чем целый ход противника.
	ContactMemory.RemoveAll([Turn, MemoryTurns](const FAIContact& Contact)
	{
		if (Contact.bActorKnown && !Contact.Target.IsValid())
		{
			return true;
		}
		return (Turn - Contact.LastUpdatedTurn) > MemoryTurns;
	});

	for (FAIContact& Contact : ContactMemory)
	{
		const int32 Age = FMath::Max(0, Turn - Contact.LastUpdatedTurn);
		Contact.Confidence = FMath::Clamp(
			1.f - static_cast<float>(Age) / static_cast<float>(MemoryTurns), 0.05f, Contact.Confidence);
	}
}

void AUnitAIController::RefreshInvestigateTarget()
{
	FAIContact Best;
	if (GetBestContact(Best))
	{
		LastKnownThreatLocation = Best.LastKnownLocation;
		bHasThreatLocation = true;
		return;
	}
	bHasThreatLocation = false;
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
		// Red alert: враг в прямой видимости. Точку берём с живого актора — это
		// единственный источник, где так делать ЧЕСТНО: мы его видим.
		AlertState = EUnitAlertState::Combat;
		RememberContact(Actor, Actor->GetActorLocation(), EAIContactSource::Sight, 1.f);
		LastKnownThreatLocation = Actor->GetActorLocation();
		bHasThreatLocation = true;
	}
	else
	{
		// ⚠️ Цель пропала из виду — точку берём из СТИМУЛА, а не с актора: его
		// текущая позиция бойцу больше не известна.
		//
		// И тревога понижается только если это была ЕДИНСТВЕННАЯ цель. Раньше
		// потеря любого противника роняла бойца в Investigate, даже когда рядом
		// стоял второй, отлично видимый (§4/AI-2: «потеря одной цели не затирает
		// более свежую память о другой»).
		RememberContact(Actor, Stimulus.StimulusLocation, EAIContactSource::Sight, 0.8f);
		if (AlertState == EUnitAlertState::Combat && !FindVisibleTarget())
		{
			AlertState = EUnitAlertState::Investigate;
		}
		RefreshInvestigateTarget();
	}
}

void AUnitAIController::NotifyNoiseHeard(const FVector& NoiseLocation)
{
	// Шум даёт ТОЧКУ, но не актора: имени стрелка боец не знает (`bActorKnown`
	// = false). Достоверность низкая — идти проверять, а не открывать огонь.
	RememberContact(nullptr, NoiseLocation, EAIContactSource::Noise, 0.4f);

	// Шум не понижает тревогу: в бою уже знаем больше, чем «где-то стреляли».
	if (AlertState != EUnitAlertState::Combat)
	{
		AlertState = EUnitAlertState::Investigate;
		RefreshInvestigateTarget();
	}
}

void AUnitAIController::ExecuteUnitTurn(FSimpleDelegate OnFinished)
{
	TurnFinishedDelegate = MoveTemp(OnFinished);
	// Темп хода зависит от того, видит ли игрок этого бойца (см. §«скрытый ход»).
	ApplyHiddenMovementSpeed();
	bTurnMoveInProgress = false;
	bCoverMoveDoneThisTurn = false;
	bManeuverInProgress = false;
	DecisionOrdinalThisTurn = 0;
	FailedAttackTargetsThisTurn.Reset(); // новый ход — новые попытки
	bScriptedRepositionTried = false;
	TurnIdleReasons.Reset();
	LastMoveFailure.Reset();
	// AI-2: знание стареет на границе активации, а не посреди неё — иначе
	// решения одного и того же хода считались бы по «плывущей» достоверности.
	AgeContactMemory();

	// Память позиций для `RecentPositionPenalty`: пишем ТОЧКУ НА НАЧАЛО ХОДА,
	// то есть место, где боец простоял ход противника. Именно возврат на неё
	// читается игроком как метание.
	//
	// Дубли не копим: боец стреляет ход через ход и стоит на месте, а список из
	// трёх одинаковых точек помнил бы всего одно место — ровно на шаг меньше,
	// чем нужно, чтобы разорвать маятник.
	if (const APawn* ControlledPawn = GetPawn())
	{
		const FVector TurnStart = ControlledPawn->GetActorLocation();
		const bool bSameAsLast = RecentTurnPositions.Num() > 0 &&
			FVector::Dist2D(RecentTurnPositions[0], TurnStart) <= RecentPositionRadius;
		if (bSameAsLast)
		{
			RecentTurnPositions[0] = TurnStart;
		}
		else
		{
			RecentTurnPositions.Insert(TurnStart, 0);
			if (RecentTurnPositions.Num() > MaxRecentPositions)
			{
				RecentTurnPositions.SetNum(MaxRecentPositions);
			}
		}
	}

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
		//
		// ⚠️ Печатаем ПРИЧИНУ, а не только состояние. Прежняя строка сообщала
		// alert/AP/число точек патруля — по ней нельзя было отличить «нет
		// навмеша» от «нет способности», и каждый следующий прогон начинался с
		// гадания (бриф AI §4.6). Отдельно проверяем, стоит ли боец вообще на
		// навмеше: боец вне навмеша не строит НИ ОДНОГО маршрута, и это самая
		// частая первопричина.
		const UTacticalAIDirectorSubsystem* Director = GetAIDirector();
		const FVector UnitLocation = Unit->GetActorLocation();
		FVector Projected = FVector::ZeroVector;
		const bool bOnNavmesh = ProjectOntoNavmesh(UnitLocation, Projected);
		const float NavDrift = bOnNavmesh ? FVector::Dist(UnitLocation, Projected) : -1.f;

		UE_LOG(LogXRU1AI, Warning,
			TEXT("[AI] %s закончил ход БЕЗ ДЕЙСТВИЙ: alert=%d, под вскрыт=%d, AP=%d, ")
			TEXT("видимая цель=%s, точек патруля=%d, поз=(%.0f, %.0f), навмеш=%s. ПРИЧИНА: %s"),
			*GetNameSafe(Unit), static_cast<int32>(AlertState),
			(Director && Director->IsUnitPodActivated(Unit)) ? 1 : 0,
			ActionPoints->CurrentActionPoints, *GetNameSafe(FindVisibleTarget()),
			Unit->PatrolPoints.Num(), UnitLocation.X, UnitLocation.Y,
			bOnNavmesh ? *FString::Printf(TEXT("да (%.0f см)"), NavDrift)
				: TEXT("НЕТ — боец вне навмеша, маршруты не строятся"),
			TurnIdleReasons.IsEmpty() ? TEXT("не записана (см. ветку без NoteIdleReason)")
				: *TurnIdleReasons);
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
		//
		// AI-2: знание пода СЛИВАЕТСЯ в личную память как «сообщил союзник» —
		// с пониженной достоверностью и отдельным источником. Так в логе видно,
		// что боец идёт по чужой наводке, а не по собственным глазам, и так
		// личная память не перетирается каждым обновлением группы.
		FAIContact PodContact;
		if (const UTacticalAIDirectorSubsystem* Director = GetAIDirector();
			Director && Director->GetBestContact(Unit, PodContact))
		{
			RememberContact(PodContact.Target.Get(), PodContact.LastKnownLocation,
				EAIContactSource::Ally, PodContact.Confidence * 0.9f);
		}
		RefreshInvestigateTarget();

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
			if (MoveWithBudget(Unit, PendingManeuverPoint, /*AcceptanceRadius=*/40.f, PointsLeft,
				/*RequiredGoalTolerance=*/ManeuverGoalTolerance))
			{
				bManeuverInProgress = true;
				return true;
			}
			// Продолжение сорвалось: боец остался в открытом поле, и это надо
			// видеть в логе, а не гадать «почему он не добежал». Точку сверки
			// снимаем: манёвр окончен, и сравнивать финальную позицию с целью,
			// до которой мы сознательно не пошли, значит врать в лог.
			bHasChosenManeuverPoint = false;
			// И отпускаем намерение: держать чужую точку занятой ради манёвра,
			// который не состоится, — прямой способ запереть соседа.
			if (UTacticalAIDirectorSubsystem* Director = GetAIDirector())
			{
				Director->ReleaseReservation(Unit);
			}
			UE_LOG(LogXRU1AI, Warning,
				TEXT("[AI] %s: не смог продолжить манёвр к (%.0f, %.0f) — остался на месте (%s)"),
				*GetNameSafe(Unit), PendingManeuverPoint.X, PendingManeuverPoint.Y,
				LastMoveFailure.IsEmpty() ? TEXT("причина не записана") : *LastMoveFailure);
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

	// Ни одно предложение не исполнилось. Записываем, ЧТО предлагалось: без
	// этого «бой, цель видит — и всё равно ничего не сделал» неразбираем.
	FString Offers;
	for (const FAIDecision& Ranked : RankedDecisions)
	{
		Offers += FString::Printf(TEXT("%s%s (%.1f)"), Offers.IsEmpty() ? TEXT("") : TEXT(", "),
			*Ranked.Reason, Ranked.Score);
	}
	NoteIdleReason(FString::Printf(TEXT("бой: ни один вариант не исполнился [%s]%s"),
		Offers.IsEmpty() ? TEXT("предложений не было") : *Offers,
		LastMoveFailure.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; движение: %s"), *LastMoveFailure)));

	// ⚠️ Боец В БОЮ тоже не имеет права закончить ход ничем: он стоит в поле,
	// видит противника и обязан хотя бы взять сектор под прицел. Ход при этом
	// обязан завершиться — иначе фаза врага повиснет на нём.
	return FallbackHoldOrSkip(Unit, TEXT("бой: исполнить решение не удалось"));
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

		// AI-4: оценщик отдаёт 0..N предложений. Для большинства это по-прежнему
		// ровно одно (база сводит метод к `ScoreAction`), но выстрел разворачивает
		// перебор по всем доступным целям — и они сравниваются общей шкалой, а не
		// проигрывают заранее выбранному `PrimaryThreat`.
		TArray<FAIDecision> Proposals;
		Evaluator->ProposeActions(Context, Proposals);

		if (Proposals.Num() == 0 && bLogAI)
		{
			UE_LOG(LogXRU1AI, Log, TEXT("[AI]   %s: предложений нет (отказ)"),
				*Evaluator->GetDebugName().ToString());
		}

		for (FAIDecision& Candidate : Proposals)
		{
			// Вес оценщика применяется здесь, а не внутри: «характер» юнита —
			// свойство набора, а не отдельного предложения.
			Candidate.Score *= Evaluator->Weight;

			if (bLogAI)
			{
				UE_LOG(LogXRU1AI, Log, TEXT("[AI]   %s: скор %.1f — %s"),
					*Evaluator->GetDebugName().ToString(), Candidate.Score,
					Candidate.Reason.IsEmpty() ? TEXT("—") : *Candidate.Reason);
			}

			// Запоминаем ВСЕ пригодные варианты, а не только лучший. Исполнение
			// может провалиться (маршрут не строится, точка занята), и тогда ход
			// обязан продолжиться следующим предложением, а не закончиться
			// ничем. Именно на этом бот с видимой целью и целым AP «стоял».
			RankedDecisions.Add(Candidate);
			if (Candidate.Score > BestScore)
			{
				BestScore = Candidate.Score;
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
			NoteIdleReason(FString::Printf(TEXT("выстрел по %s не принят способностью"),
				*GetNameSafe(Decision.Target)));
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
			NoteIdleReason(FString::Printf(TEXT("манёвр в (%.0f, %.0f): %s"),
				Decision.Destination.X, Decision.Destination.Y,
				LastMoveFailure.IsEmpty() ? TEXT("маршрут не построен") : *LastMoveFailure));
			return false;
		}
		if (MoveWithBudget(Unit, Decision.Destination, Decision.AcceptanceRadius))
		{
			return true;
		}
		NoteIdleReason(FString::Printf(TEXT("сближение в (%.0f, %.0f): %s"),
			Decision.Destination.X, Decision.Destination.Y,
			LastMoveFailure.IsEmpty() ? TEXT("маршрут не построен") : *LastMoveFailure));
		return false;

	case EAIActionKind::Overwatch:
		// Обе способности сжигают остаток AP (bConsumesAllRemainingAP), поэтому
		// следующий шаг увидит 0 очков, не найдёт применимого оценщика и штатно
		// завершит активацию. Планировать шаг всё равно НУЖНО: без него ход
		// повис бы (ни FinishUnitTurn, ни следующего AdvanceTurnStep).
		if (!TryActivateSelfAbility(Unit, Unit ? Unit->OverwatchAbilityClass : nullptr))
		{
			NoteIdleReason(TEXT("наблюдение как решение: способность не активировалась"));
			return false;
		}
		ScheduleNextStep();
		return true;

	case EAIActionKind::Hunker:
		if (!TryActivateSelfAbility(Unit, Unit ? Unit->HunkerAbilityClass : nullptr))
		{
			NoteIdleReason(TEXT("глухая оборона как решение: способность не активировалась"));
			return false;
		}
		ScheduleNextStep();
		return true;

	default:
		// Skip: ни один оценщик не предложил применимого варианта. Это НЕ ошибка
		// исполнения, но и не норма — именно здесь видно «утилити не нашло что
		// делать при живой цели».
		NoteIdleReason(TEXT("утилити не предложило ни одного применимого действия"));
		return false;
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
	if (MoveWithBudget(Unit, Point, /*AcceptanceRadius=*/40.f, ManeuverBudget,
		/*RequiredGoalTolerance=*/ManeuverGoalTolerance))
	{
		// Точка может быть дальше 1 AP (отступление/рывок): продолжение сделает
		// следующий шаг хода — MoveWithBudget за раз проходит максимум MoveRange.
		PendingManeuverPoint = Point;
		bManeuverInProgress = true;
		// AI-5: закрепляем НАМЕРЕНИЕ. Пока боец в пути, диск занятости он не
		// ставит, и следующий боец имеет полное право выбрать ту же клетку.
		if (UTacticalAIDirectorSubsystem* Director = GetAIDirector())
		{
			Director->ReservePosition(Unit, Point);
		}
		return true;
	}
	return false;
}

FAIPositionScoringTuning AUnitAIController::MakePositionTuningSnapshot() const
{
	// Копия RUNTIME-значений, а не ассета: к моменту боя веса уже прошли профиль
	// сложности и оси стиля (`FlankPositionBonus *= FlankWillingness`). Чистый
	// скорер обязан считать ровно тем, чем живёт игра.
	FAIPositionScoringTuning Out;
	Out.CoverDefenseWeight = CoverDefenseWeight;
	Out.OpenCoverFactor = OpenCoverFactor;
	Out.HalfCoverFactor = HalfCoverFactor;
	Out.FullCoverFactor = FullCoverFactor;
	Out.FlankPositionBonus = FlankPositionBonus;
	Out.HeightPositionBonus = HeightPositionBonus;
	Out.MinSpreadDistance = MinSpreadDistance;
	Out.SpreadPenaltyMultiplier = SpreadPenaltyMultiplier;
	Out.AllyVisibilityWeight = AllyVisibilityWeight;
	Out.OverwatchExposurePenalty = OverwatchExposurePenalty;
	Out.LineOfFireBonus = LineOfFireBonus;
	Out.LoseLineOfFirePenalty = LoseLineOfFirePenalty;
	Out.TravelCostPerCm = TravelCostPerCm;
	Out.IdealRangeWeight = IdealRangeWeight;
	Out.IdealRangeFalloff = IdealRangeFalloff;
	Out.RelocateBias = RelocateBias;
	Out.RetreatHealthFraction = RetreatHealthFraction;
	Out.RetreatRewardPerCm = RetreatRewardPerCm;
	Out.CoverSnapDistance = CoverSnapDistance;
	Out.MaxScoredThreats = MaxScoredThreats;
	Out.EnemyVisibilityWeight = EnemyVisibilityWeight;
	Out.RecentPositionPenalty = RecentPositionPenalty;
	Out.RecentPositionRadius = RecentPositionRadius;
	return Out;
}

float AUnitAIController::ScorePositionFacts(const FAIPositionFacts& Facts,
	const FAIPositionScoringTuning& T, float IdealCombatRange, bool bRetreat, bool bAdvance)
{
	// 1) ЦЕННОСТЬ УКРЫТИЯ — формула XCOM: среднее по всем угрозам, где открытость
	// даёт резко отрицательный вклад. Именно это, а не отдельное правило, и даёт
	// «AI боится флангов».
	float Score = (Facts.ThreatsScored > 0
		? Facts.CoverFactorSum / Facts.ThreatsScored
		: 0.f) * T.CoverDefenseWeight;

	if (Facts.ThreatsFlanked > 0)
	{
		Score += T.FlankPositionBonus;
	}

	// 2) ВИДИМОСТЬ ВРАГОВ (XCOM `fEnemyVisibility`). При отступлении знак
	// обратный: раненому нужен разрыв линии огня, а не новая точка для той же
	// перестрелки (§3.12).
	if (bRetreat)
	{
		Score += (Facts.ThreatsVisible > 0 ? 0.f : 1.f) * T.EnemyVisibilityWeight;
	}
	else
	{
		const float VisibilityScore = Facts.ThreatsVisible > 0
			? static_cast<float>(Facts.ThreatsVisible) / FMath::Max(1, T.MaxScoredThreats)
			: -1.f;
		Score += VisibilityScore * T.EnemyVisibilityWeight;
	}

	// 3) СПЛОЧЁННОСТЬ (XCOM `fAllyVisWeight`): доля видимых своих.
	if (Facts.AlliesTotal > 0)
	{
		Score += (static_cast<float>(Facts.AlliesVisible) / Facts.AlliesTotal) * T.AllyVisibilityWeight;
	}

	// 4) РИСК ОВЕРВОТЧА. Конечная точка и маршрут считаются РАЗДЕЛЬНО: встать под
	// прицелом опаснее, чем пробежать мимо, поэтому пробежка стоит половину.
	Score -= Facts.ThreatsOverwatching * T.OverwatchExposurePenalty;
	Score -= Facts.RouteOverwatchExposures * T.OverwatchExposurePenalty * 0.5f;

	// 5) ВЫСОТА — тот же порог, что даёт бонус к точности.
	if (Facts.bHeightAdvantage)
	{
		Score += T.HeightPositionBonus;
	}

	// 6) ЛИНИЯ ОГНЯ.
	//
	// ⚠️ ПРИ ОТСТУПЛЕНИИ ОНА НЕ СТОИТ НИЧЕГО — ни бонуса, ни штрафа.
	//
	// Это XCOM-профиль `Fallback`: там `fEnemyVisWeight = 0`, то есть у бегущего
	// возможность стрелять не влияет на выбор точки вообще. Первая редакция
	// правки «прячется где попало» инвертировала только вес видимости (+20 за
	// разрыв контакта), а бонус за линию огня оставила работать в полную силу
	// (+25) — и укрытая точка, откуда врага ВИДНО, по-прежнему выигрывала пять
	// очков у точки, где его не видно. Дефект пойман автотестом
	// `XRU1.AI.Position.RetreatBreaksLineOfSight`, а не прогоном: в бою разница
	// в пять очков выглядит просто «неудачным выбором».
	//
	// При наступлении отсутствие линии огня тоже не штрафуется — иначе бот
	// отвергает промежуточное укрытие без выстрела и бежит напролом.
	if (!bRetreat)
	{
		Score += Facts.ThreatsVisible > 0
			? T.LineOfFireBonus
			: (bAdvance ? 0.f : -T.LoseLineOfFirePenalty);
	}

	// 7) ЦЕНА ПЕРЕБЕЖКИ и ДИСТАНЦИЯ.
	Score -= T.TravelCostPerCm * Facts.TravelDistance;
	if (bRetreat)
	{
		Score += T.RetreatRewardPerCm * Facts.ThreatDistance;
	}
	else
	{
		// Близость к идеальной дистанции боя, формула XCOM:
		// 1 − |dist − ideal| / falloff, зажато [−1..1].
		const float Deviation = FMath::Abs(Facts.ThreatDistance - IdealCombatRange);
		const float RangeScore = FMath::Clamp(
			1.f - Deviation / FMath::Max(1.f, T.IdealRangeFalloff), -1.f, 1.f);
		Score += RangeScore * T.IdealRangeWeight;
	}

	// 8) ВОЗВРАТ НА ПРОШЛУЮ ПОЗИЦИЮ — против маятника (§3.12.1).
	if (Facts.bRecentlyOccupied)
	{
		Score -= T.RecentPositionPenalty;
	}

	// 9) КУЧНОСТЬ — множитель только к ПОЛОЖИТЕЛЬНОМУ скору (XCOM). К
	// отрицательному он работал бы наоборот, делая плохую точку лучше.
	if (Score > 0.f && Facts.bCrowded)
	{
		Score *= T.SpreadPenaltyMultiplier;
	}
	return Score;
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
	// СТАДИЯ «ФАКТЫ»: только измерения, ни одного веса. Всё, что дальше делает с
	// ними арифметику, живёт в чистой `ScorePositionFacts` и потому проверяемо
	// автотестом без мира (AI-3).
	auto BuildPositionFacts = [&](const FVector& FloorPoint, FAIPositionFacts& Facts,
		FAICoverPointResult& Out)
	{
		Facts = FAIPositionFacts();
		Out = FAICoverPointResult();
		Out.Point = FloorPoint;
		Facts.ThreatDistance = FVector::Dist(FloorPoint, ThreatLocation);
		Facts.TravelDistance = FVector::Dist2D(UnitLocation, FloorPoint);
		Facts.bHeightAdvantage = (FloorPoint.Z - ThreatLocation.Z) >= Tuning->HeightAdvantageZ;
		Facts.AlliesTotal = Allies.Num();
		if (Threats.Num() == 0)
		{
			return;
		}
		Facts.ThreatsScored = Threats.Num();

		const FVector EyeAtPoint = FloorPoint + FVector(0.f, 0.f, EyeHeight);

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
			case ECoverType::Full: Facts.CoverFactorSum += FullCoverFactor; ++Out.ThreatsCovered; break;
			case ECoverType::Half: Facts.CoverFactorSum += HalfCoverFactor; ++Out.ThreatsCovered; break;
			default:               Facts.CoverFactorSum += OpenCoverFactor; ++Out.ThreatsExposed;  break;
			}

			// 2) КОГО ОТТУДА ВИДНО. Тем же предикатом, что решает выстрел, —
			// иначе бот планирует по одним правилам, а стреляет по другим.
			if (FVector::Dist(FloorPoint, ThreatPos) <= Unit->AttackRange &&
				UTacticsCombatStatics::HasLineOfSightFromLocation(World, EyeAtPoint, ThreatActor, Unit))
			{
				++Out.ThreatsVisible;
				++Facts.ThreatsVisible;

				// 2b) A7 `SafeToMove`. Видимость взаимна: раз я вижу оттуда врага,
				// то и он видит меня — а если он В НАБЛЮДЕНИИ, то встретит меня
				// реакционным выстрелом. Считается ЗДЕСЬ, потому что стоит ровно
				// ноль: линия огня для этой пары уже посчитана строкой выше.
				if (const UAbilitySystemComponent* ThreatASC =
					UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ThreatActor))
				{
					if (ThreatASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Overwatch))
					{
						++Out.ThreatsOverwatching;
						++Facts.ThreatsOverwatching;
					}
				}
			}

			// 3) КОГО ФЛАНКИРУЕМ — считается В ОБРАТНУЮ сторону: не «прикрыт ли
			// я», а «работает ли укрытие врага против выстрела ИЗ ЭТОЙ ТОЧКИ».
			if (UTacticsCombatStatics::IsTargetFlankedByLocation(ThreatActor, FloorPoint))
			{
				++Out.ThreatsFlanked;
				++Facts.ThreatsFlanked;
			}
		}
		Facts.ThreatsCovered = Out.ThreatsCovered;
		Facts.ThreatsExposed = Out.ThreatsExposed;

		// 4) СПЛОЧЁННОСТЬ (XCOM `fAllyVisWeight`): видно ли отсюда своих. Быстрый
		// путь LOS без выглядывания — вопрос «поддержат ли меня», а не «попаду ли
		// я», поэтому края укрытий перебирать незачем.
		for (const AActor* Ally : Allies)
		{
			if (UTacticsCombatStatics::HasLineOfSightFromLocation(World, EyeAtPoint, Ally))
			{
				++Out.AlliesVisible;
				++Facts.AlliesVisible;
			}
		}

		// 5) КУЧНОСТЬ и 6) ВОЗВРАТ — тоже факты, а не веса.
		if (MinSpreadDistance > 0.f)
		{
			for (const FVector& AllyLocation : AllyLocations)
			{
				if (FVector::Dist2D(AllyLocation, FloorPoint) < MinSpreadDistance)
				{
					Facts.bCrowded = true;
					break;
				}
			}
		}
		if (RecentPositionPenalty > 0.f && RecentPositionRadius > 0.f)
		{
			const float RecentRadiusSq = FMath::Square(RecentPositionRadius);
			for (int32 i = 1; i < RecentTurnPositions.Num(); ++i)
			{
				if (FVector::DistSquared2D(RecentTurnPositions[i], FloorPoint) < RecentRadiusSq)
				{
					Facts.bRecentlyOccupied = true;
					break;
				}
			}
		}

		// 7) РИСК ПО МАРШРУТУ (AI-3). В XCOM `SafeToMove` проверяет весь путь; у
		// нас раньше оценивалась только конечная точка, и бот спокойно пробегал
		// через сектор чужого овервотча, лишь бы финиш был чистым.
		//
		// Полный перебор LOS по каждому отрезку каждого кандидата неподъёмен
		// (48 точек × угрозы × отрезки), поэтому берём СЕРЕДИНУ перебежки: одна
		// проба на кандидата, а именно середина чаще всего и есть открытый
		// участок между двумя укрытиями. Это дешёвое приближение, но оно ловит
		// ровно тот случай, ради которого правило существует.
		if (Facts.TravelDistance > 200.f)
		{
			const FVector MidPoint = (UnitLocation + FloorPoint) * 0.5f;
			const FVector MidEye = FVector(MidPoint.X, MidPoint.Y, FloorPoint.Z) +
				FVector(0.f, 0.f, EyeHeight);
			for (const TObjectPtr<AActor>& ThreatActor : Threats)
			{
				if (!ThreatActor)
				{
					continue;
				}
				const UAbilitySystemComponent* ThreatASC =
					UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ThreatActor);
				if (!ThreatASC ||
					!ThreatASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Overwatch))
				{
					continue; // не в наблюдении — по пути он не выстрелит
				}
				if (UTacticsCombatStatics::HasLineOfSightFromLocation(World, MidEye, ThreatActor, Unit))
				{
					++Facts.RouteOverwatchExposures;
				}
			}
			Out.ThreatsOverwatching += Facts.RouteOverwatchExposures;
		}
	};

	// Взвешенная оценка позиции (веса — Tactics|AI|Weights):
	//   укрытие против ВСЕХ × вес + фланг + высота + линия огня
	//   ± дистанция до цели − цена пути, затем штраф за кучность.
	// В режиме ОТСТУПЛЕНИЯ дистанция инвертируется (награда за удаление) и
	// потеря линии огня не штрафуется — выживание важнее выстрела.
	// Веса снимаются ОДИН раз на весь перебор: они не меняются между кандидатами,
	// а чистый скорер должен получать ровно то, чем живёт runtime.
	const FAIPositionScoringTuning ScoringTuning = MakePositionTuningSnapshot();

	// Обёртка «померить и посчитать». Вся арифметика — в статической
	// `ScorePositionFacts`, здесь только связка стадий.
	auto ScorePosition = [&](const FVector& FloorPoint, FAICoverPointResult& Out)
	{
		FAIPositionFacts Facts;
		BuildPositionFacts(FloorPoint, Facts, Out);
		return ScorePositionFacts(Facts, ScoringTuning, Unit->IdealCombatRange, bRetreat, bAdvance);
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

	// СЧЁТЧИКИ СТАДИЙ (AI-1). Без них «поиск позиции ничего не нашёл» —
	// неразличимая ситуация: то ли навмеш пуст, то ли всё занято, то ли ни один
	// кандидат не побил базовую линию. Каждая стадия отвечает за свой отказ.
	const double SearchStartTime = FPlatformTime::Seconds();
	int32 StatGenerated = 0;
	int32 StatOffNavmesh = 0;
	int32 StatOccupied = 0;
	int32 StatReserved = 0;
	int32 StatUnreachable = 0;
	int32 StatScored = 0;

	UTacticalAIDirectorSubsystem* Director = GetAIDirector();
	const float ReservationRadius = Director ? Director->ReservationRadius : 0.f;

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
			++StatGenerated;

			FNavLocation Projected;
			if (!NavSys->ProjectPointToNavigation(Candidate, Projected, FVector(100.f, 100.f, 300.f)))
			{
				++StatOffNavmesh;
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
				++StatOccupied;
				continue;
			}

			// AI-5: точку уже застолбил другой боец. Диски занятости этого не
			// ловят — тот, кто в пути, диск не ставит.
			if (Director && Director->IsPositionReserved(Unit, Projected.Location, ReservationRadius))
			{
				++StatReserved;
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
				++StatUnreachable;
				continue;
			}

			// Полная оценка точки: по каждой угрозе — закрыт ли, вижу ли, фланкую
			// ли (план == факт: линия огня считается тем же предикатом, что решает
			// выстрел, и из тех же огневых позиций peek).
			FAICoverPointResult Details;
			const float Score = ScorePosition(CandidatePoint, Details);
			++StatScored;
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

		// AI-1: цена и КПД перебора по стадиям. «Оценено 0 из 48» и «оценено 40,
		// но ни одна не побила базу» — совершенно разные диагнозы, и различить их
		// иначе нечем.
		UE_LOG(LogXRU1AI, Log,
			TEXT("[AI]     перебор: сгенерировано %d, вне навмеша %d, занято %d, ")
			TEXT("зарезервировано %d, не дойти %d, оценено %d за %.2f мс"),
			StatGenerated, StatOffNavmesh, StatOccupied, StatReserved, StatUnreachable,
			StatScored, (FPlatformTime::Seconds() - SearchStartTime) * 1000.0);
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

	// ПОВОДОК ЧАСОВОГО. Пост существует, чтобы его охраняли: боец у заряда не
	// имеет права уйти на звук за полкарты и оставить объект пустым. Проверка
	// стоит ДО расчёта дистанции до точки — если точка вне поводка, туда не идут
	// вообще, а не «идут, пока не устанут».
	//
	// После вскрытия пода поводок снимается: тогда это уже не охрана объекта, а
	// бой, и часовой обязан воевать наравне со всеми.
	const UTacticalAIDirectorSubsystem* Director = GetAIDirector();
	const bool bPodActivated = Director && Director->IsUnitPodActivated(Unit);
	if (!bPodActivated && PostLeashRadius > 0.f && IsPostSentry(Unit) && bPatrolAnchorSet)
	{
		const float FromPost = FVector::Dist2D(PatrolAnchorLocation, LastKnownThreatLocation);
		if (FromPost > PostLeashRadius)
		{
			if (TacticsDebug::IsAILogEnabled())
			{
				UE_LOG(LogXRU1AI, Log,
					TEXT("[AI] %s: точка интереса в %.0f см от поста (поводок %.0f) — "
						 "остаюсь охранять"),
					*GetNameSafe(Unit), FromPost, PostLeashRadius);
			}
			// Знание не выбрасываем: если противник подойдёт ближе, боец
			// среагирует на новый, уже близкий контакт.
			Unit->FaceTowardsSmooth(LastKnownThreatLocation, /*bPlayTurnAnimation=*/false);
			NoteIdleReason(FString::Printf(
				TEXT("часовой: шум в %.0f см от поста, дальше поводка %.0f — не иду"),
				FromPost, PostLeashRadius));
			return FallbackHoldOrSkip(Unit, TEXT("охрана поста: шум слишком далеко"));
		}
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
				// Разворот к точке угрозы — ПЛАВНЫЙ и идёт параллельно вставанию
				// в наблюдение (montage входа длится секунды). Мгновенный поворот
				// здесь читался тем же щелчком, что и убранный доворот выстрела.
				Unit->FaceTowardsSmooth(LastKnownThreatLocation,
					/*bPlayTurnAnimation=*/false);
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

		// Точка проверена — контакт, который сюда вёл, отработан и забывается.
		// Именно контакт, а не вся память: рядом может лежать более старое, но
		// всё ещё живое знание о другом противнике, и его боец обязан проверить
		// следующим (AI-2), а не начинать с чистого листа.
		const FVector CheckedPoint = LastKnownThreatLocation;
		const float ForgetRadius = FMath::Max(200.f, InvestigateAcceptanceRadius * 2.f);
		ContactMemory.RemoveAll([&CheckedPoint, ForgetRadius](const FAIContact& Contact)
		{
			return FVector::Dist2D(Contact.LastKnownLocation, CheckedPoint) <= ForgetRadius;
		});
		RefreshInvestigateTarget();
		if (bHasThreatLocation)
		{
			return StepInvestigate(Unit); // есть куда идти дальше — идём
		}

		AlertState = EUnitAlertState::Patrol;
		return StepPatrol(Unit);
	}

	// Точка интереса может быть дальше одного хода — это нормально, идём частями.
	if (MoveWithBudget(Unit, LastKnownThreatLocation, InvestigateAcceptanceRadius,
		/*MaxActionPoints=*/Unit->GetActionPoints() ? Unit->GetActionPoints()->CurrentActionPoints : 1))
	{
		return true;
	}

	// Маршрут к точке интереса не строится. Раньше здесь ход просто заканчивался
	// ничем: 14 пустых ходов группы `Post_7` в прогоне 2026-08-04 — это именно
	// он. Боец, который слышал бой и знает направление, обязан хотя бы держать
	// его под прицелом (в XCOM 2 этого как раз не хватает, см. заголовок
	// InvestigateOverwatchChance).
	NoteIdleReason(FString::Printf(
		TEXT("разведка точки (%.0f, %.0f), %.0f см: %s"),
		LastKnownThreatLocation.X, LastKnownThreatLocation.Y,
		FVector::Dist2D(Unit->GetActorLocation(), LastKnownThreatLocation),
		LastMoveFailure.IsEmpty() ? TEXT("маршрут не построен") : *LastMoveFailure));

	Unit->FaceTowardsSmooth(LastKnownThreatLocation, /*bPlayTurnAnimation=*/false);
	return FallbackHoldOrSkip(Unit, TEXT("разведка: к точке не пройти, держу направление"));
}

void AUnitAIController::NoteIdleReason(const FString& Reason)
{
	if (Reason.IsEmpty())
	{
		return;
	}
	// Цепочка, а не одна причина: разбирать надо весь спуск по лестнице
	// фолбэков. «Маршрут не построен → в зоне нет свободной точки → наблюдение
	// не активировалось» отвечает на вопрос сразу, одна последняя строка — нет.
	int32 Links = 0;
	for (int32 i = 0; i < TurnIdleReasons.Len(); ++i)
	{
		Links += (TurnIdleReasons[i] == TEXT('|')) ? 1 : 0;
	}
	if (Links >= MaxIdleReasons)
	{
		return;
	}
	if (!TurnIdleReasons.IsEmpty())
	{
		TurnIdleReasons += TEXT(" | ");
	}
	TurnIdleReasons += Reason;
}

bool AUnitAIController::IsPostSentry(const AUnitBase* Unit) const
{
	return Unit && Unit->PatrolPoints.Num() == 0 && Unit->PatrolRoamRadius <= 0.f;
}

bool AUnitAIController::HoldPositionOnPost(AUnitBase* Unit, const TCHAR* Reason)
{
	// Часовой на посту держит направление под прицелом. Раньше боец без маршрута
	// просто пропускал ход — на карте без расставленных PatrolPoints это
	// выглядело как «половина врагов сломана»: камера подлетала к ним, и они
	// ничего не делали.
	if (!Unit->OverwatchAbilityClass)
	{
		NoteIdleReason(FString::Printf(
			TEXT("%s: OverwatchAbilityClass не назначен у %s"), Reason, *GetNameSafe(Unit->GetClass())));
		return false;
	}
	if (TryActivateSelfAbility(Unit, Unit->OverwatchAbilityClass))
	{
		if (TacticsDebug::IsAILogEnabled())
		{
			UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: %s — встал в наблюдение"),
				*GetNameSafe(Unit), Reason);
		}
		ScheduleNextStep();
		return true;
	}
	NoteIdleReason(FString::Printf(
		TEXT("%s: наблюдение не активировалось (способность отказала или ОД не списаны)"), Reason));
	return false;
}

bool AUnitAIController::FallbackHoldOrSkip(AUnitBase* Unit, const TCHAR* Context)
{
	UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
	if (!ActionPoints)
	{
		return false;
	}

	// 1) Держать сектор — самое осмысленное, что можно сделать, никуда не дойдя.
	if (HoldPositionOnPost(Unit, Context))
	{
		return true;
	}

	// 2) Вжаться в укрытие, если оно тут есть. Не «на всякий случай»: боец, не
	// сумевший сдвинуться, стоит там, где стоит, и единственный способ сделать
	// это место лучше — использовать имеющуюся стену.
	if (Unit->HunkerAbilityClass)
	{
		const UCoverDetectionComponent* Cover = Unit->GetCoverDetection();
		if (Cover && Cover->BestCoverAround != ECoverType::None &&
			TryActivateSelfAbility(Unit, Unit->HunkerAbilityClass))
		{
			if (TacticsDebug::IsAILogEnabled())
			{
				UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: %s — глухая оборона в имеющемся укрытии"),
					*GetNameSafe(Unit), Context);
			}
			ScheduleNextStep();
			return true;
		}
	}

	// 3) ЧЕСТНЫЙ ПРОПУСК. Действия не будет, но ход обязан быть ЗАВЕРШИМЫМ: без
	// списания очка следующий AdvanceTurnStep повторил бы тот же отказ, и разбор
	// свёлся бы к «почему он думает вечно». Возвращаем false — вызывающий
	// напечатает Warning с накопленной цепочкой причин, а ОД уже списано, так
	// что зациклиться шаг не может.
	ActionPoints->TrySpendActionPoint(ActionPoints->CurrentActionPoints);
	return false;
}

bool AUnitAIController::StepRoamAroundAnchor(AUnitBase* Unit, const FVector& Anchor,
	float RadiusOverride)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav)
	{
		NoteIdleReason(TEXT("обход зоны: навигационной системы нет"));
		return FallbackHoldOrSkip(Unit, TEXT("удержание: навигации нет"));
	}

	const float Radius = RadiusOverride > 0.f
		? RadiusOverride
		: FMath::Max(100.f, Unit->PatrolRoamRadius);

	// Розыгрыш детерминированный (то же правило, что у Overwatch на
	// расследовании): зерно собирается из карты, хода, имени бойца и номера
	// решения. Случайность здесь — разнообразие маршрута, а не источник
	// невоспроизводимых прогонов.
	FRandomStream Stream(static_cast<int32>(BuildDecisionSeed(Unit, FName(TEXT("PatrolRoam")))));

	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const float Angle = Stream.FRandRange(0.f, 2.f * PI);
		// sqrt() даёт равномерное распределение ПО ПЛОЩАДИ круга: без него
		// боец кучкуется у центра зоны.
		const float Distance = Radius * FMath::Sqrt(Stream.FRand());
		const FVector Candidate = Anchor +
			FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);

		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(200.f, 200.f, 300.f)))
		{
			continue;
		}
		// Слишком близкая цель — шаг «на месте»: маршрут не построится, и ход
		// закончится без действий.
		if (FVector::Dist2D(Projected.Location, Unit->GetActorLocation()) < 200.f)
		{
			continue;
		}
		if (MoveWithBudget(Unit, Projected.Location, 100.f))
		{
			if (TacticsDebug::IsAILogEnabled())
			{
				UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: обход зоны удержания (радиус %.0f)"),
					*GetNameSafe(Unit), Radius);
			}
			return true;
		}
	}

	// В зоне не нашлось куда идти (тесно, всё занято) — держим сектор.
	NoteIdleReason(FString::Printf(
		TEXT("обход зоны (радиус %.0f): ни одна из 8 точек не подошла%s"),
		Radius, LastMoveFailure.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" — %s"), *LastMoveFailure)));
	return FallbackHoldOrSkip(Unit, TEXT("удержание: свободной точки в зоне нет"));
}

bool AUnitAIController::StepPatrol(AUnitBase* Unit)
{
	// Якорь зоны удержания: он же стартовая позиция бойца. Запоминается один
	// раз, иначе зона «уползала» бы вслед за самим бойцом.
	if (!bPatrolAnchorSet)
	{
		PatrolAnchorLocation = Unit->GetActorLocation();
		bPatrolAnchorSet = true;
	}

	const int32 PointCount = Unit->PatrolPoints.Num();

	// Ноль или одна точка — это не маршрут, а приказ удерживать место.
	// Полная таблица режимов — в комментарии к AUnitBase::PatrolPoints.
	if (PointCount <= 1)
	{
		const AActor* HoldPoint = PointCount == 1 ? Unit->PatrolPoints[0].Get() : nullptr;
		const FVector Anchor = HoldPoint ? HoldPoint->GetActorLocation() : PatrolAnchorLocation;
		const float DistanceToAnchor = FVector::Dist2D(Unit->GetActorLocation(), Anchor);
		const bool bAtAnchor = DistanceToAnchor <= 150.f;

		if (Unit->PatrolRoamRadius > 0.f)
		{
			// Боец далеко от своей зоны (сдвинули приказом, оттеснили) —
			// сначала возвращается, и только потом бродит внутри неё.
			if (DistanceToAnchor > Unit->PatrolRoamRadius)
			{
				if (MoveWithBudget(Unit, Anchor, 100.f))
				{
					return true;
				}
				NoteIdleReason(FString::Printf(
					TEXT("возврат в зону удержания (%.0f см до якоря): %s"),
					DistanceToAnchor, *LastMoveFailure));
				// Дойти до якоря не вышло — это не повод стоять: пробуем обойти
				// зону вокруг СЕБЯ, а не вокруг недостижимого якоря.
				return StepRoamAroundAnchor(Unit, Unit->GetActorLocation(),
					FMath::Max(Unit->PatrolRoamRadius, Unit->MoveRange));
			}
			return StepRoamAroundAnchor(Unit, Anchor);
		}

		// Без радиуса: дойти до точки удержания и держать сектор. Без этой
		// ветки боец каждый ход «шёл» к точке, на которой уже стоит, маршрут
		// не строился, и ход заканчивался без единого действия.
		//
		// ⚠️ ЧАСОВОЙ ВОЗВРАЩАЕТСЯ НА ПОСТ. Раньше боец без маршрута (`HoldPoint`
		// == nullptr — так настроен сторож у заряда) всегда уходил в наблюдение
		// ГДЕ СТОИТ: сместился на шум или его оттеснили — и объект остался без
		// охраны навсегда. Якорь у него есть (стартовая позиция), возвращаться
		// есть куда.
		if (!bAtAnchor)
		{
			if (MoveWithBudget(Unit, Anchor, 100.f))
			{
				if (TacticsDebug::IsAILogEnabled())
				{
					UE_LOG(LogXRU1AI, Log, TEXT("[AI] %s: возвращаюсь на пост (%.0f см)"),
						*GetNameSafe(Unit), DistanceToAnchor);
				}
				return true;
			}
			NoteIdleReason(FString::Printf(TEXT("возврат на пост (%.0f см): %s"),
				DistanceToAnchor, *LastMoveFailure));
		}
		return FallbackHoldOrSkip(Unit,
			HoldPoint ? TEXT("удержание точки") : TEXT("пост без маршрута"));
	}

	// Вход в маршрут считается ОДИН раз и лениво: к первому шагу патруля боец
	// уже стоит там, где его поставили (спавн мог скорректировать позицию).
	if (!bPatrolIndexResolved)
	{
		PatrolIndex = ResolveInitialPatrolIndex(Unit);
		bPatrolIndexResolved = true;
	}

	PatrolIndex = FMath::Clamp(PatrolIndex, 0, PointCount - 1);
	if (!Unit->PatrolPoints[PatrolIndex])
	{
		NoteIdleReason(FString::Printf(TEXT("точка маршрута №%d пустая (ссылка на удалённый актор)"),
			PatrolIndex));
		return FallbackHoldOrSkip(Unit, TEXT("маршрут повреждён"));
	}

	// У точки — переводим индекс на следующую: иначе боец «идёт» туда, где уже
	// стоит, маршрут не строится, и ход заканчивается без единого действия.
	if (FVector::Dist2D(Unit->GetActorLocation(),
		Unit->PatrolPoints[PatrolIndex]->GetActorLocation()) <= 150.f)
	{
		AdvancePatrolIndex(Unit);
	}

	// ⚠️ ПЕРЕБОР ПО МАРШРУТУ, а не одна попытка.
	//
	// Прежняя версия делала РОВНО ОДИН `MoveWithBudget` к текущей вершине и при
	// отказе возвращала false — без фолбэка вообще. Достаточно было одной
	// недостижимой вершины (точка на бордюре, за закрытой дверью, в дыре
	// навмеша), чтобы группа встала навсегда: индекс не двигался, и каждый
	// следующий ход повторял тот же отказ. Ровно это и дал прогон 2026-08-04 —
	// 62 пустых хода у группы `Post_3` с 12 точками.
	//
	// Теперь недостижимая вершина ПРОПУСКАЕТСЯ: обходим маршрут дальше, как и
	// сделал бы часовой, наткнувшийся на запертую дверь. Ограничение — число
	// точек: полный круг без успеха означает, что дело не в вершине.
	FString FirstFailure;
	const int32 MaxProbes = FMath::Min(PointCount, 6);
	for (int32 Probe = 0; Probe < MaxProbes; ++Probe)
	{
		const AActor* PatrolPoint = Unit->PatrolPoints[PatrolIndex];
		if (PatrolPoint && MoveWithBudget(Unit, PatrolPoint->GetActorLocation(), 100.f))
		{
			return true;
		}
		if (FirstFailure.IsEmpty())
		{
			FirstFailure = FString::Printf(TEXT("точка маршрута №%d (%s): %s"),
				PatrolIndex, *GetNameSafe(PatrolPoint),
				LastMoveFailure.IsEmpty() ? TEXT("недостижима") : *LastMoveFailure);
		}
		AdvancePatrolIndex(Unit);
	}

	NoteIdleReason(FString::Printf(TEXT("маршрут из %d точек: %d вершин подряд недостижимы — %s"),
		PointCount, MaxProbes, *FirstFailure));

	// Маршрут целиком не строится — боец не «сломан», он просто заперт. Ведёт
	// себя как часовой: обходит участок вокруг себя, а не стоит столбом.
	if (StepRoamAroundAnchor(Unit, Unit->GetActorLocation(), Unit->MoveRange))
	{
		return true;
	}
	return FallbackHoldOrSkip(Unit, TEXT("маршрут патруля недоступен"));
}

int32 AUnitAIController::ResolveInitialPatrolIndex(const AUnitBase* Unit) const
{
	const int32 PointCount = Unit->PatrolPoints.Num();
	if (PointCount <= 1)
	{
		return 0;
	}

	int32 Base = 0;
	if (Unit->bPatrolStartFromNearest)
	{
		// Ближайшая точка маршрута к месту, где боец реально стоит. Дистанция
		// по прямой: маршрут ещё не начат, и строить путь до каждой точки ради
		// выбора стартовой — неоправданно дорого на старте боя.
		float BestDistSq = TNumericLimits<float>::Max();
		for (int32 i = 0; i < PointCount; ++i)
		{
			const AActor* Point = Unit->PatrolPoints[i];
			if (!Point)
			{
				continue;
			}
			const float DistSq = FVector::DistSquared2D(
				Unit->GetActorLocation(), Point->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Base = i;
			}
		}
	}

	// Смещение растягивает группу по маршруту. У кольца оно заворачивается, у
	// незамкнутой линии — упирается в её конец (дальше идти всё равно некуда).
	const int32 Offset = FMath::Max(0, Unit->PatrolStartIndex);
	return Unit->PatrolRouteMode == EPatrolRouteMode::PingPong
		? FMath::Clamp(Base + Offset, 0, PointCount - 1)
		: (Base + Offset) % PointCount;
}

void AUnitAIController::AdvancePatrolIndex(const AUnitBase* Unit)
{
	const int32 PointCount = Unit->PatrolPoints.Num();
	if (PointCount <= 1)
	{
		PatrolIndex = 0;
		return;
	}

	if (Unit->PatrolRouteMode == EPatrolRouteMode::PingPong)
	{
		// На концах незамкнутого маршрута боец разворачивается и идёт назад по
		// тем же точкам. Кольцевой обход тут погнал бы его от последней точки к
		// первой через весь маршрут — вхолостую и мимо охраняемого участка.
		if (PatrolIndex + PatrolDirection < 0 || PatrolIndex + PatrolDirection >= PointCount)
		{
			PatrolDirection = -PatrolDirection;
		}
		PatrolIndex = FMath::Clamp(PatrolIndex + PatrolDirection, 0, PointCount - 1);
		return;
	}

	PatrolIndex = (PatrolIndex + 1) % PointCount;
}

bool AUnitAIController::ProjectOntoNavmesh(const FVector& Location, FVector& OutProjected,
	const FVector& Extent) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav)
	{
		return false;
	}
	FNavLocation Projected;
	if (!Nav->ProjectPointToNavigation(Location, Projected, Extent))
	{
		return false;
	}
	OutProjected = Projected.Location;
	return true;
}

bool AUnitAIController::MoveWithBudget(AUnitBase* Unit, const FVector& Goal, float AcceptanceRadius,
	int32 MaxActionPoints, float RequiredGoalTolerance)
{
	LastMoveFailure.Reset();
	if (!Unit || !Unit->GetActionPoints())
	{
		LastMoveFailure = TEXT("нет пешки или компонента ОД");
		return false;
	}
	MaxActionPoints = FMath::Max(1, MaxActionPoints);

	// ЦЕЛЬ ПРИВОДИТСЯ К НАВМЕШУ ОДИН РАЗ, до всех планировщиков. Патрульные точки
	// и якоря зон дизайнер ставит по геометрии, а не по навмешу: TargetPoint,
	// стоящий на бордюре или в 40 см над полом, отдельного маршрута не получает —
	// и весь режим патруля выглядел как «боец ничего не делает».
	FVector NavGoal = Goal;
	if (FVector Projected; ProjectOntoNavmesh(Goal, Projected, FVector(300.f, 300.f, 500.f)))
	{
		NavGoal = Projected;
	}

	// В бою враг использует ровно тот же occupancy-aware планировщик, что и игрок:
	// волна заранее огибает диски союзников, а не надеется на локальный Detour Crowd.
	const UTurnManagerSubsystem* TurnManager = GetWorld()
		? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (TurnManager && TurnManager->IsInCombat())
	{
		ATacticalPlayerController* PlayerController = GetWorld()
			? Cast<ATacticalPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
		FMoveOrderPlan Plan;
		if (!PlayerController)
		{
			LastMoveFailure = TEXT("нет ATacticalPlayerController — планировщик недоступен");
		}
		else if (!PlayerController->PlanMoveForUnit(Unit, NavGoal, MaxActionPoints, Plan) ||
			Plan.PathPoints.Num() < 2)
		{
			LastMoveFailure = TEXT("планировщик не построил маршрут");
		}
		else if (RequiredGoalTolerance > 0.f &&
			FVector::Dist2D(Plan.PathPoints.Last(), NavGoal) > RequiredGoalTolerance)
		{
			// Планировщик поля при неудаче подставляет БЛИЖАЙШИЙ достижимый
			// сэмпл. Для манёвра это подмена оценённой точки: укрытие считалось
			// в одном месте, а боец встал бы в другом.
			LastMoveFailure = FString::Printf(
				TEXT("план ведёт не в выбранную точку (промах %.0f см при допуске %.0f)"),
				FVector::Dist2D(Plan.PathPoints.Last(), NavGoal), RequiredGoalTolerance);
		}
		else
		{
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
			LastMoveFailure = TEXT("MoveAlongRoute отклонил готовый план");
		}
		// РАЗОВЫЙ ДИАГНОЗ. Поле дистанций проецирует позицию бойца с допуском в
		// одну ячейку (35 см). Не спроецировалась — боец физически стоит не на
		// проходимом полу, и это дефект РАССТАНОВКИ, а не AI: чинится переносом
		// точки спавна, а не весами. Печатаем один раз на вселение, иначе
		// строка повторится в каждом ходу до конца боя.
		if (!bReportedOffNavmesh && !LastMoveFailure.IsEmpty())
		{
			FVector Ignored;
			if (!ProjectOntoNavmesh(Unit->GetActorLocation(), Ignored, FVector(40.f, 40.f, 300.f)))
			{
				bReportedOffNavmesh = true;
				UE_LOG(LogXRU1AI, Warning,
					TEXT("[AI] %s стоит ВНЕ НАВМЕША (%.0f, %.0f, %.0f): планировщик поля не строит ")
					TEXT("для него ни одного маршрута, работает только прямой путь. ")
					TEXT("Проверь точку спавна энкаунтера — см. [Encounter] ... без подтверждения навмешем"),
					*GetNameSafe(Unit), Unit->GetActorLocation().X, Unit->GetActorLocation().Y,
					Unit->GetActorLocation().Z);
			}
		}

		// ⚠️ НЕ выходим: дальше идёт ПРЯМОЙ навмеш-путь как фолбэк.
		//
		// Планировщик игрока строит поле дистанций ВОКРУГ БОЙЦА и начинается с
		// проекции его собственной позиции на навмеш. Боец, поставленный на
		// неподтверждённую точку (`[Encounter] ... без подтверждения навмешем`),
		// не получает поля вообще — то есть теряет не «идеальный маршрут», а
		// ЛЮБОЕ перемещение до конца боя. Прямой путь хуже: он не знает о дисках
		// занятости и полагается на Detour Crowd. Но «пошёл неоптимально» — это
		// поведение, а «стоял 22 хода» — дефект.
	}

	// Дешёвый navmesh-путь: вне боя это штатный режим патруля, в бою — фолбэк.
	const float DirectBudget = Unit->MoveRange * static_cast<float>(MaxActionPoints);
	FVector BudgetedGoal;
	if (!UTacticsCombatStatics::GetPointAlongPathBudget(this, Unit, Unit->GetActorLocation(), NavGoal,
		DirectBudget, BudgetedGoal))
	{
		LastMoveFailure = LastMoveFailure.IsEmpty()
			? FString(TEXT("навмеш не построил путь до цели"))
			: LastMoveFailure + TEXT("; навмеш тоже не построил путь");
		return false;
	}

	// Не вставать в диск занятости другого юнита (замена навмеш-вырезов).
	if (!UTacticsCombatStatics::AdjustGoalOutOfUnits(GetWorld(), Unit, BudgetedGoal))
	{
		LastMoveFailure = TEXT("конечная точка занята бойцами, вытолкнуть некуда");
		return false;
	}

	// Бюджетная точка совпадает с текущей позицией — двигаться некуда.
	if (FVector::Dist2D(Unit->GetActorLocation(), BudgetedGoal) <= 50.f)
	{
		LastMoveFailure = TEXT("бюджетная точка совпала с текущей позицией");
		return false;
	}

	// «Дойти именно сюда»: прямой путь обрезается бюджетом хода и на обходном
	// маршруте кончается где угодно. Для манёвра такой приказ хуже отказа —
	// боец уйдёт от оценённого укрытия и встанет в открытом поле.
	if (RequiredGoalTolerance > 0.f &&
		FVector::Dist2D(BudgetedGoal, NavGoal) > RequiredGoalTolerance)
	{
		LastMoveFailure = FString::Printf(
			TEXT("%sпрямой путь не доводит до точки (промах %.0f см при допуске %.0f)"),
			LastMoveFailure.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("%s; "), *LastMoveFailure),
			FVector::Dist2D(BudgetedGoal, NavGoal), RequiredGoalTolerance);
		return false;
	}

	const EPathFollowingRequestResult::Type Result = MoveToLocation(BudgetedGoal, AcceptanceRadius);
	if (Result == EPathFollowingRequestResult::RequestSuccessful)
	{
		if (TurnManager && TurnManager->IsInCombat() && TacticsDebug::IsAILogEnabled())
		{
			UE_LOG(LogXRU1AI, Log,
				TEXT("[AI] %s: план поля не построился (%s) — иду прямым навмеш-путём"),
				*GetNameSafe(Unit), *LastMoveFailure);
		}
		LastMoveFailure.Reset();
		PendingMoveActionPointCost = 1;
		bTurnMoveInProgress = true; // AP спишется в OnMoveCompleted
		Unit->NotifyUnitStateChanged();
		return true;
	}
	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// У цели: тратим AP, чтобы ход гарантированно закончился, и продолжаем.
		LastMoveFailure.Reset();
		Unit->GetActionPoints()->TrySpendActionPoint();
		ScheduleNextStep();
		return true;
	}
	LastMoveFailure = TEXT("path following отклонил приказ (MoveToLocation)");
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
		// ⚠️ БЕЗ cvar — это регрессионный сторож, а не отладка.
		//
		// Пока предупреждение было под `xru1.AI.LogCombat`, расхождение плана и
		// факта жило в проекте незамеченным: боец уезжал за 21 метр от
		// оценённого укрытия и вставал в чистом поле, а снаружи это выглядело
		// «AI мечется» (§3.12.1). После введения `RequiredGoalTolerance` строка
		// обязана исчезнуть из логов совсем; если она появилась — сломалось
		// исполнение приказа, и узнать об этом надо без переключения cvar.
		if (Drift > ManeuverArrivalTolerance)
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
	// Пауза между шагами существует ради ЧИТАЕМОСТИ хода. Если бойца не видно,
	// читать нечего: интервал вырождается в тик. На карте с десятком врагов это
	// и превращает ход противника из «канители» в несколько секунд.
	const float Interval = IsHiddenFromSquad() ? 0.f : ActionInterval;
	if (Interval <= 0.f)
	{
		TurnStepTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
			this, &AUnitAIController::AdvanceTurnStep);
		return;
	}
	GetWorldTimerManager().SetTimer(TurnStepTimerHandle, this,
		&AUnitAIController::AdvanceTurnStep, Interval, false);
}

void AUnitAIController::HandleFogVisibilityChanged(AActor* Actor, bool /*bVisible*/)
{
	if (Actor && Actor == GetPawn())
	{
		ApplyHiddenMovementSpeed();
	}
}

bool AUnitAIController::IsHiddenFromSquad() const
{
	const AActor* Unit = GetPawn();
	if (!Unit)
	{
		return false;
	}
	if (const UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		return !Fog->IsActorCurrentlyVisible(Unit);
	}
	return false;
}

void AUnitAIController::ApplyHiddenMovementSpeed()
{
	ACharacter* MovingPawn = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = MovingPawn ? MovingPawn->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	if (BaseWalkSpeed <= 0.f)
	{
		BaseWalkSpeed = Movement->MaxWalkSpeed; // запоминаем «настоящую» скорость один раз
	}

	// XCOM 2 невидимую часть пути вообще не проигрывает (`X2Action_Move`
	// вырезает её, а полностью скрытый юнит телепортируется). Мы не телепортируем
	// сознательно: перемещение у нас — механика, на которой висят реакция
	// Overwatch и стимулы перцепции, и «прыжок» мимо зоны реакции был бы
	// нечестен. Вместо этого скрытый боец идёт свой путь ускоренно, а в момент
	// появления в поле зрения скорость мгновенно возвращается к обычной.
	const bool bUnseen = IsHiddenFromSquad();
	const float Desired = bUnseen
		? BaseWalkSpeed * FMath::Max(1.f, HiddenMovementSpeedMultiplier)
		: BaseWalkSpeed;
	if (!FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Desired))
	{
		Movement->MaxWalkSpeed = Desired;
	}
}

void AUnitAIController::RestoreMovementSpeed()
{
	ACharacter* MovingPawn = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = MovingPawn ? MovingPawn->GetCharacterMovement() : nullptr;
	if (Movement && BaseWalkSpeed > 0.f)
	{
		Movement->MaxWalkSpeed = BaseWalkSpeed;
	}
}

void AUnitAIController::FinishUnitTurn()
{
	GetWorldTimerManager().ClearTimer(TurnStepTimerHandle);
	GetWorldTimerManager().ClearTimer(MoveSettlementTimerHandle);
	PendingSettlementUnit.Reset();
	// Ускорение живёт ровно один ход: боец, замеченный в фазу игрока, не должен
	// бегать втрое быстрее на глазах у отряда.
	RestoreMovementSpeed();
	bTurnMoveInProgress = false;

	// AI-5: намерение живёт ровно до конца активации. Дальше боец уже стоит на
	// точке и держит её обычным диском занятости — держать ещё и резервацию
	// значило бы запрещать соседу подойти к тому же укрытию навсегда.
	if (UTacticalAIDirectorSubsystem* Director = GetAIDirector())
	{
		Director->ReleaseReservation(Cast<AUnitBase>(GetPawn()));
	}

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
