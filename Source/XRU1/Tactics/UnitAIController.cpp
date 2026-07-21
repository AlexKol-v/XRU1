#include "UnitAIController.h"
#include "UnitBase.h"
#include "TacticalPlayerController.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameplayEffects.h"
#include "TacticsGameplayTags.h"
#include "TurnManagerSubsystem.h"
#include "GA_Attack.h"
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

/**
 * Диагностика решений AI в бою: почему враг стреляет/бежит/стоит. Под cvar,
 * выключено по умолчанию. Включить в консоли: `xru1.AI.LogCombat 1`.
 * Отвечает на «понимают ли враги, где прятаться» — печатает, нашёл ли манёвр
 * укрытие и с какой оценкой (0 найдено = на карте нет укрытий рядом).
 */
static TAutoConsoleVariable<int32> CVarLogAICombat(
	TEXT("xru1.AI.LogCombat"),
	0,
	TEXT("1 — логировать боевые решения вражеского AI (укрытие/выстрел/сближение)."),
	ECVF_Default);

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

	DamageEffect = UGE_ShotDamage::StaticClass();
}

void AUnitAIController::BeginPlay()
{
	Super::BeginPlay();

	// Качество объезда повыше: юнитов мало, стоимость незаметна, а обход
	// стоящих капсул становится заметно плавнее.
	if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::High);
		Crowd->SetCrowdCollisionQueryRange(600.f);
	}

	if (Perception)
	{
		Perception->OnTargetPerceptionUpdated.AddDynamic(this, &AUnitAIController::HandlePerceptionUpdated);
	}
}

void AUnitAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Тревога — состояние конкретного бойца: новый пешка = новый пост.
	AlertState = EUnitAlertState::Patrol;
	bHasThreatLocation = false;
	PatrolIndex = 0;

	// Валидация настройки BP — один раз при вселении, а не на каждом выстреле:
	// без AttackAbilityClass юнит стреляет фолбэком (мимо GA_Attack, а значит
	// без BP-хуков анимации/VFX выстрела).
	const AUnitBase* Unit = Cast<AUnitBase>(InPawn);
	if (Unit && !Unit->AttackAbilityClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AI] У %s не назначен AttackAbilityClass — выстрелы пойдут ")
			TEXT("в обход GA_Attack (без хуков анимации/VFX). Задай класс в Class Defaults BP юнита."),
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
	AdvanceTurnStep();
}

void AUnitAIController::AdvanceTurnStep()
{
	AUnitBase* Unit = Cast<AUnitBase>(GetPawn());
	UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;

	// Нет пешки/очков действия или юнит выбыл — заканчиваем.
	if (!Unit || !ActionPoints || !ActionPoints->HasActionsLeft() ||
		!UTacticsCombatStatics::IsUnitAlive(Unit))
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

	// Видимая цель мгновенно поднимает red alert (перцепция могла отстать на кадр).
	if (FindVisibleTarget())
	{
		AlertState = EUnitAlertState::Combat;
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
		// Состоянию нечего делать (нет точек патруля, некуда идти) — ход окончен.
		FinishUnitTurn();
	}
}

bool AUnitAIController::StepCombat(AUnitBase* Unit)
{
	AActor* Target = FindVisibleTarget();
	if (!Target)
	{
		// Цели не видно — переходим к разведке последней известной точки.
		AlertState = EUnitAlertState::Investigate;
		return StepInvestigate(Unit);
	}

	LastKnownThreatLocation = Target->GetActorLocation();
	bHasThreatLocation = true;

	const UActionPointsComponent* ActionPoints = Unit->GetActionPoints();
	const int32 PointsLeft = ActionPoints ? ActionPoints->CurrentActionPoints : 0;
	const bool bCanShootNow = UGA_Attack::CanTargetActor(Unit, Target);
	const bool bLogAI = CVarLogAICombat.GetValueOnGameThread() != 0;

	// 0) Продолжение НАЧАТОГО манёвра (отступление/рывок длиннее 1 AP): вторая
	// нога к той же точке. Это не новый выбор — bCoverMoveDoneThisTurn уже стоит.
	if (bManeuverInProgress)
	{
		bManeuverInProgress = false; // однократное продолжение за шаг; при успехе взводится снова
		if (PointsLeft > 0 && FVector::Dist2D(Unit->GetActorLocation(), PendingManeuverPoint) > 75.f)
		{
			if (MoveWithBudget(Unit, PendingManeuverPoint, /*AcceptanceRadius=*/40.f))
			{
				bManeuverInProgress = true;
				return true;
			}
		}
	}

	const UCoverDetectionComponent* Cover = Unit->GetCoverDetection();
	const bool bExposed = !Cover || Cover->GetCoverAgainst(Target) == ECoverType::None;

	// Решения ниже — приоритетный список с утилити-оценкой точек внутри
	// (веса Tactics|AI|Weights). Порядок — XCOM-логика хода.

	// 1) ОТСТУПЛЕНИЕ: мало HP и стоим открытыми — уходим в укрытие ПОДАЛЬШЕ от
	// угрозы, на это можно потратить весь ход (бюджет = все AP).
	const bool bLowHealth = Unit->GetHealth() <= Unit->GetMaxHealth() * RetreatHealthFraction;
	if (!bCoverMoveDoneThisTurn && bLowHealth && bExposed && PointsLeft >= 1)
	{
		FVector RetreatPoint;
		if (FindCoverPoint(Unit, Target, Unit->MoveRange * PointsLeft, /*bRetreat=*/true, RetreatPoint) &&
			StartManeuverTo(Unit, RetreatPoint, TEXT("отступление (мало HP)")))
		{
			return true; // не удалось стартовать — ниже обычная логика (выстрел/сближение)
		}
	}

	// 2) МАНЁВР «в укрытие с линией огня» на 1 AP — выстрел вторым AP (XCOM).
	if (!bCoverMoveDoneThisTurn && PointsLeft >= 2 && (bExposed || !bCanShootNow))
	{
		FVector CoverPoint;
		if (FindCoverPoint(Unit, Target, Unit->MoveRange, /*bRetreat=*/false, CoverPoint) &&
			StartManeuverTo(Unit, CoverPoint, TEXT("укрытие с линией огня (1 AP)")))
		{
			return true;
		}

		// 3) РЫВОК: рядом стрелять неоткуда — идём к укрытию ближе к цели на
		// оба AP (в этом ходу уже не выстрелим, зато следующий начнём в укрытии).
		if (!bCanShootNow && !bCoverMoveDoneThisTurn &&
			FindCoverPoint(Unit, Target, Unit->MoveRange * PointsLeft, /*bRetreat=*/false, CoverPoint) &&
			StartManeuverTo(Unit, CoverPoint, TEXT("рывок к дальнему укрытию (2 AP)")))
		{
			return true;
		}

		if (bLogAI)
		{
			UE_LOG(LogTemp, Log, TEXT("[AI] %s: открыт=%d, можно_стрелять=%d — укрытие не найдено ")
				TEXT("(нужны WorldStatic-стены высотой Half/Full CoverDetection в радиусе хода)"),
				*GetNameSafe(Unit), bExposed, bCanShootNow);
		}
	}

	// 4) ВЫСТРЕЛ. Предикат ОБЩИЙ с игроком (дальность + LOS/Squadsight): AI не
	// может выстрелить там, где HUD игрока показал бы «нет линии огня».
	if (bCanShootNow)
	{
		if (bLogAI)
		{
			UE_LOG(LogTemp, Log, TEXT("[AI] %s: решение — ВЫСТРЕЛ (шанс %.0f%%)"),
				*GetNameSafe(Unit), UGA_Attack::ComputeAttackHitChance(Unit, Target));
		}
		if (!TryFireAtTarget(Unit, Target))
		{
			return false; // способность отказала — ход завершаем, без зацикливания
		}
		ScheduleNextStep();
		return true;
	}

	// 5) СБЛИЖЕНИЕ: стрелять нельзя и укрытие не нашлось.
	if (bLogAI)
	{
		UE_LOG(LogTemp, Log, TEXT("[AI] %s: решение — сближение с целью"), *GetNameSafe(Unit));
	}
	return MoveWithBudget(Unit, Target->GetActorLocation(), Unit->AttackRange * 0.8f);
}

bool AUnitAIController::StartManeuverTo(AUnitBase* Unit, const FVector& Point, const TCHAR* Reason)
{
	if (CVarLogAICombat.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[AI] %s: решение — манёвр: %s → (%.0f, %.0f)"),
			*GetNameSafe(Unit), Reason, Point.X, Point.Y);
	}
	bCoverMoveDoneThisTurn = true;
	if (MoveWithBudget(Unit, Point, /*AcceptanceRadius=*/40.f))
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
	bool bRetreat, FVector& OutPoint)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const UCoverDetectionComponent* Cover = Unit ? Unit->GetCoverDetection() : nullptr;
	if (!World || !NavSys || !Cover || !Threat || PathBudget <= 0.f)
	{
		return false;
	}

	const FVector UnitLocation = Unit->GetActorLocation();
	const FVector ThreatLocation = Threat->GetActorLocation();

	// Половина капсулы: Base оценки укрытия = точка пола + пол-капсулы — ровно
	// там окажется ActorLocation, когда юнит туда встанет (план = факт).
	float CapsuleHalfHeight = 88.f;
	float EyeHeight = CapsuleHalfHeight + UTacticsCombatStatics::EyeHeightOffset;
	if (const ACharacter* UnitCharacter = Cast<ACharacter>(Unit))
	{
		if (const UCapsuleComponent* Capsule = UnitCharacter->GetCapsuleComponent())
		{
			CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			EyeHeight = CapsuleHalfHeight + UTacticsCombatStatics::EyeHeightOffset;
		}
	}

	// Занятость: один снимок на весь перебор.
	TArray<FVector> Obstacles;
	UTacticsCombatStatics::GetUnitObstacles(World, Unit, Obstacles);
	const double ClearanceSq = FMath::Square(static_cast<double>(UTacticsCombatStatics::GetUnitClearance(Unit)));

	// Ценность укрытия — числами защиты самого компонента (Full=40, Half=20):
	// оценка манёвра в тех же единицах, что и модификатор шанса попадания.
	auto CoverValue = [Cover](ECoverType Type)
	{
		switch (Type)
		{
		case ECoverType::Full: return Cover->FullCoverDefenseBonus;
		case ECoverType::Half: return Cover->HalfCoverDefenseBonus;
		default:               return 0.f;
		}
	};

	// Взвешенная оценка позиции (веса — Tactics|AI|Weights):
	//   укрытие × вес + линия огня ± дистанция до цели − цена пути.
	// В режиме ОТСТУПЛЕНИЯ дистанция инвертируется (награда за удаление) и
	// потеря линии огня не штрафуется — выживание важнее выстрела.
	auto ScorePosition = [&](const FVector& FloorPoint, ECoverType CoverType, bool bCanShoot)
	{
		const float ThreatDistance = FVector::Dist(FloorPoint, ThreatLocation);
		float Score = CoverValue(CoverType) * CoverDefenseWeight;
		Score += bCanShoot ? LineOfFireBonus : (bRetreat ? 0.f : -LoseLineOfFirePenalty);
		Score -= TravelCostPerCm * FVector::Dist2D(UnitLocation, FloorPoint);
		if (bRetreat)
		{
			Score += RetreatRewardPerCm * ThreatDistance;
		}
		else
		{
			Score -= OverextendPenaltyPerCm * FMath::Max(0.f, ThreatDistance - Unit->AttackRange * 0.75f);
		}
		return Score;
	};

	// Базовая линия — ТЕКУЩАЯ позиция теми же правилами + порог значимости:
	// не дёргаемся ради косметики.
	const float BaselineScore = ScorePosition(UnitLocation,
		Cover->GetCoverAgainst(Threat), UGA_Attack::CanTargetActor(Unit, Threat));
	float BestScore = BaselineScore + RelocateBias;
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

			const FVector StandBase = Projected.Location + FVector(0.f, 0.f, CapsuleHalfHeight);
			const ECoverType CoverType = Cover->EvaluateCoverAtLocation(StandBase, ThreatLocation);
			const bool bCanShoot =
				FVector::Dist(Projected.Location, ThreatLocation) <= Unit->AttackRange &&
				UTacticsCombatStatics::HasLineOfSightFromLocation(World,
					Projected.Location + FVector(0.f, 0.f, EyeHeight), Threat);

			const float Score = ScorePosition(Projected.Location, CoverType, bCanShoot);
			if (Score <= BestScore)
			{
				continue;
			}

			// Дорогая проверка — последней: точка должна быть ДОСТИЖИМА в переданный
			// бюджет пути (с занятостью), иначе манёвр оборвётся на полпути.
			FVector Reachable;
			if (!UTacticsCombatStatics::GetPointAlongPathBudget(this, Unit, UnitLocation,
					Projected.Location, PathBudget, Reachable) ||
				FVector::Dist2D(Reachable, Projected.Location) > 75.f)
			{
				continue;
			}

			BestScore = Score;
			OutPoint = Projected.Location;
			bFound = true;
		}
	}
	return bFound;
}

bool AUnitAIController::TryFireAtTarget(AUnitBase* Unit, AActor* Target)
{
	UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
	if (!ActionPoints || !Target)
	{
		return false;
	}

	// Штатный путь: то же событие, что шлёт контроллер игрока. Стоимость AP,
	// XCOM-сжигание остатка и BP-хуки выстрела живут в одном месте — GA_Attack.
	UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
	const bool bHasAttackAbility = ASC && Unit->AttackAbilityClass &&
		ASC->FindAbilitySpecFromClass(Unit->AttackAbilityClass) != nullptr;

	if (bHasAttackAbility)
	{
		const int32 PointsBefore = ActionPoints->CurrentActionPoints;

		FGameplayEventData Payload;
		Payload.Instigator = Unit;
		Payload.Target = Target;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			Unit, TacticsGameplayTags::Event_Attack, Payload);

		// Способность не заплатила AP (отказала по своим правилам) — считаем,
		// что выстрела не было: иначе шаг хода повторялся бы бесконечно.
		return ActionPoints->CurrentActionPoints < PointsBefore;
	}

	// Фолбэк: у юнита не назначен AttackAbilityClass (не настроен BP; о чём
	// предупредили один раз в OnPossess). Правила те же — точность через общий
	// расчёт, выстрел завершает активацию.
	ActionPoints->TrySpendActionPoint();
	ActionPoints->SpendAllRemaining();
	UTacticsCombatStatics::ResolveShot(Unit, Target,
		UGA_Attack::ComputeEffectiveAim(Unit, Target), Unit->ShotDamage, DamageEffect);
	return true;
}

bool AUnitAIController::StepInvestigate(AUnitBase* Unit)
{
	if (!bHasThreatLocation)
	{
		AlertState = EUnitAlertState::Patrol;
		return StepPatrol(Unit);
	}

	// Дошли до точки интереса и никого не нашли — успокаиваемся до патруля.
	if (FVector::Dist2D(Unit->GetActorLocation(), LastKnownThreatLocation) <= InvestigateAcceptanceRadius * 2.f)
	{
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
		// Пост без маршрута: стоит на месте, ход не тратим.
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

bool AUnitAIController::MoveWithBudget(AUnitBase* Unit, const FVector& Goal, float AcceptanceRadius)
{
	// Обрезаем путь бюджетом 1 AP (MoveRange по длине пути навмеша, не по прямой).
	// GetPointAlongPathBudget заодно УСЕКАЕТ путь там, где не протиснуться мимо
	// стоящего юнита: AI подходит насколько может, а не упирается в толпу и не
	// теряет ход. Поле дистанций (как у игрока) здесь не строим — оно дорогое,
	// а врагу достаточно «дойти насколько можно» без точной зоны.
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
		bTurnMoveInProgress = true; // AP спишется в OnMoveCompleted
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
	return bFollowingRoute || GetMoveStatus() != EPathFollowingStatus::Idle;
}

EPathFollowingRequestResult::Type AUnitAIController::MoveAlongRoute(const TArray<FVector>& RoutePoints,
	float AcceptanceRadius)
{
	StopRoute();
	if (RoutePoints.Num() < 2 || !GetPawn())
	{
		return EPathFollowingRequestResult::Failed;
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

		// Промежуточные вершины проходим ТОЧНЕЕ финальной: радиус приёмки — это
		// ровно то, на сколько path following срежет угол, а поворотные вершины
		// стоят у занятых клеток с запасом всего ~26 см (см. RouteCornerAcceptance).
		const float Acceptance = bFinalLeg ? RouteAcceptanceRadius : RouteCornerAcceptance;

		// Вершина фактически уже пройдена (боец стартовал рядом с ней) — пропускаем,
		// иначе получили бы приказ «идти туда, где стоим» и ложный финиш отрезка.
		if (!bFinalLeg && FVector::Dist2D(ControlledPawn->GetActorLocation(), Leg) <= Acceptance)
		{
			continue;
		}

		if (MoveToLocation(Leg, Acceptance) == EPathFollowingRequestResult::RequestSuccessful)
		{
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

	// Юнит встал на новую позицию — пересчитать укрытие (и для приказов игрока тоже).
	if (AUnitBase* Unit = Cast<AUnitBase>(GetPawn()))
	{
		if (UCoverDetectionComponent* Cover = Unit->GetCoverDetection())
		{
			Cover->EvaluateSurroundings();
		}

		// Диск занятости юнита встал на новую позицию — контроллер игрока
		// пересчитает зону хода выбранного бойца (синхронно, без задержек).
		if (ATacticalPlayerController* PlayerController =
			Cast<ATacticalPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			PlayerController->NotifyUnitMoveFinished(Unit);
		}
	}

	if (!bTurnMoveInProgress)
	{
		return;
	}
	bTurnMoveInProgress = false;

	if (AUnitBase* Unit = Cast<AUnitBase>(GetPawn()))
	{
		if (UActionPointsComponent* ActionPoints = Unit->GetActionPoints())
		{
			ActionPoints->TrySpendActionPoint();
		}
	}

	ScheduleNextStep();
}

void AUnitAIController::ScheduleNextStep()
{
	GetWorldTimerManager().SetTimer(TurnStepTimerHandle, this,
		&AUnitAIController::AdvanceTurnStep, ActionInterval, false);
}

void AUnitAIController::FinishUnitTurn()
{
	GetWorldTimerManager().ClearTimer(TurnStepTimerHandle);
	bTurnMoveInProgress = false;

	// Сначала сбрасываем делегат, потом зовём: колбэк может тут же начать новый ход.
	FSimpleDelegate Finished = MoveTemp(TurnFinishedDelegate);
	TurnFinishedDelegate.Unbind();
	Finished.ExecuteIfBound();
}

AActor* AUnitAIController::FindVisibleTarget() const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Perception)
	{
		return nullptr;
	}

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);

	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	bool bBestIsTaunting = false;

	for (AActor* Actor : Perceived)
	{
		if (!UTacticsCombatStatics::AreHostile(MyPawn, Actor) ||
			!UTacticsCombatStatics::IsUnitAlive(Actor))
		{
			continue;
		}

		// Провокация танка (GDD §7): провоцирующая цель приоритетнее обычных,
		// но только пока враг в радиусе её действия.
		bool bTaunting = false;
		if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
		{
			bTaunting = ASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Taunting) &&
				FVector::Dist(MyPawn->GetActorLocation(), Actor->GetActorLocation()) <= TauntPriorityRadius;
		}
		if (bBestIsTaunting && !bTaunting)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyPawn->GetActorLocation(), Actor->GetActorLocation());
		if ((bTaunting && !bBestIsTaunting) || DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Actor;
			bBestIsTaunting = bTaunting;
		}
	}
	return Best;
}
