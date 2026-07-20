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
#include "Engine/World.h"
#include "TimerManager.h"

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

	// 1) Цель поражаема прямо сейчас — стреляем. Предикат ОБЩИЙ с игроком
	// (дальность + LOS/Squadsight): AI не может выстрелить там, где HUD игрока
	// показал бы «нет линии огня», и наоборот.
	if (Target)
	{
		LastKnownThreatLocation = Target->GetActorLocation();
		bHasThreatLocation = true;

		if (UGA_Attack::CanTargetActor(Unit, Target))
		{
			if (!TryFireAtTarget(Unit, Target))
			{
				return false; // способность отказала — ход завершаем, без зацикливания
			}
			ScheduleNextStep();
			return true;
		}

		// 2) Цель видна, но стрелять нельзя (далеко/нет линии) — сближение.
		return MoveWithBudget(Unit, Target->GetActorLocation(), Unit->AttackRange * 0.8f);
	}

	// 3) Цели не видно — переходим к разведке последней известной точки.
	AlertState = EUnitAlertState::Investigate;
	return StepInvestigate(Unit);
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

void AUnitAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

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
