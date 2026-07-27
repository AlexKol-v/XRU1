#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "GA_Attack.h"
#include "GA_Overwatch.h"
#include "TacticalAbility.h"
#include "TacticalClassAbilities.h"
#include "TacticsGameplayTags.h"
#include "TacticsCombatStatics.h" // IsUnitInTransit / GetFiringStance для VisualState
#include "TDAttributeSet.h"
#include "UnitClasses.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimMontage.h"       // длительность DeathMontage для рагдолла
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"     // профили коллизии павших/рагдолла
#include "TimerManager.h"
#include "NavigationInvokerComponent.h"
#include "NavigationSystem.h"            // подшаг не должен выводить юнита за навмеш
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

AUnitBase::AUnitBase()
{
	// Тик нужен двум коротким визуальным движениям — подшагу к укрытию и
	// довороту на месте (см. Tick). В остальное время он выходит сразу.
	PrimaryActorTick.bCanEverTick = true;

	ActionPoints = CreateDefaultSubobject<UActionPointsComponent>(TEXT("ActionPoints"));
	CoverDetection = CreateDefaultSubobject<UCoverDetectionComponent>(TEXT("CoverDetection"));

	// Navigation Invoker: навмеш генерится вокруг юнита (RuntimeGeneration=Dynamic).
	// Радиусы применяются в BeginPlay (могут быть переопределены в BP до старта).
	NavInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavInvoker"));
	NavInvoker->SetGenerationRadii(NavInvokerGenerationRadius, NavInvokerRemovalRadius);

	// Общие способности — безопасные нативные дефолты. BP может заменить их
	// наследниками с монтажами, но новый BP-юнит больше не останется без действий.
	AttackAbilityClass = UGA_Attack::StaticClass();
	OverwatchAbilityClass = UGA_Overwatch::StaticClass();
	HunkerAbilityClass = UGA_HunkerDown::StaticClass();

	// Кольцо-декаль выбора: проекция вниз под ногами. Материал (M_SelectionRing)
	// и размер тюнингуются в BP; скрыто, пока контроллер не выберет юнита.
	SelectionDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("SelectionDecal"));
	SelectionDecal->SetupAttachment(RootComponent);
	SelectionDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f)); // проекция вниз, на пол
	SelectionDecal->SetRelativeLocation(FVector(0.f, 0.f, -88.f));   // к ногам (центр капсулы ~88 см над полом)
	SelectionDecal->DecalSize = FVector(120.f, 60.f, 60.f);         // X — глубина проекции, Y/Z — радиус кольца
	SelectionDecal->SetVisibility(false);

	// Юнитов двигает AIController (MoveToLocation), а не ввод игрока. По умолчанию
	// path following задаёт скорость НАПРЯМУЮ, не заполняя Acceleration — из-за
	// чего шаблонный локомоушен-ABP (условие Should Move = Speed>0 AND
	// Acceleration!=0) не переключался с idle при движении. Включаем
	// acceleration-driven follow: Acceleration заполняется, анимация бега играет,
	// плюс естественный разгон/торможение. Действует на все BP-юниты (свои и врагов).
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;
	}

	// Навмеш юниты НЕ трогают (никаких вырезов/динамических препятствий):
	// занятость решается на уровне запросов дисками UnitObstacleRadius
	// (см. UTacticsCombatStatics::GetUnitObstacles) — как клетки-occupancy
	// в XCOM. Капсула навигацию не затрагивает вовсе.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCanEverAffectNavigation(false);
	}
}

int32 AUnitBase::GetAbilityUsesRemaining(TSubclassOf<UTacticalAbility> AbilityClass) const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC || !AbilityClass)
	{
		return -1;
	}

	// InstancedPerActor: остаток применений живёт на primary-инстансе способности.
	// Фолбэк на Spec->Ability (CDO): если BP переопределил политику инстансинга,
	// ApplyCost пишет UsesRemaining именно туда — не путаем «нет инстанса» с «без лимита».
	if (const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass))
	{
		const UTacticalAbility* Instance = Cast<UTacticalAbility>(Spec->GetPrimaryInstance());
		if (!Instance)
		{
			Instance = Cast<UTacticalAbility>(Spec->Ability.Get());
		}
		if (Instance)
		{
			return Instance->GetUsesRemaining();
		}
	}
	return -1;
}

// --- Подсветка выбора/наведения ---------------------------------------------

void AUnitBase::SetSelectionHighlight(bool bSelected)
{
	if (SelectionDecal)
	{
		SelectionDecal->SetVisibility(bSelected && !bIsDead && !bIsEvacuated);
	}
}

void AUnitBase::SetHoverHighlight(bool bHovered)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	if (bHovered && !bIsDead && !bIsEvacuated)
	{
		// Цвет обводки выбирает post-process материал по stencil-значению.
		const bool bAlly =
			GetGenericTeamId().GetId() == TacticsTeamIds::Player;
		MeshComponent->SetCustomDepthStencilValue(bAlly ? HoverStencilAlly : HoverStencilEnemy);
		MeshComponent->SetRenderCustomDepth(true);
	}
	else
	{
		MeshComponent->SetRenderCustomDepth(false);
	}
}

void AUnitBase::NotifyUnitStateChanged()
{
	// Срез для анимаций пересобирается ЗДЕСЬ и только здесь: это единственная
	// точка, которую системы обязаны дёргать после фактического изменения
	// состояния. Значит ABP и HUD всегда видят одну и ту же картину.
	RebuildVisualState();
	OnUnitStateChanged.Broadcast();
}

void AUnitBase::RebuildVisualState()
{
	FUnitVisualState State;

	const UCoverDetectionComponent* Cover = GetCoverDetection();
	State.Cover = Cover ? Cover->BestCoverAround : ECoverType::None;
	State.CoverDirection = Cover ? Cover->BestCoverDirection : FVector::ZeroVector;
	if (!State.CoverDirection.IsNearlyZero())
	{
		// В систему координат юнита: ABP думает «стена справа/слева/спереди», а
		// не мировыми осями — иначе поза зависела бы от поворота камеры.
		State.CoverDirectionLocal = GetActorRotation().UnrotateVector(State.CoverDirection);
	}

	// СТОРОНА УКРЫТИЯ для выбора Left/Right-клипа. Юнит стоит вдоль стены
	// (HugCover), поэтому стена гарантированно сбоку и знак Y однозначен. Порог
	// отсекает вырожденный случай «стена почти спереди»: там сторона — шум.
	if (!State.CoverDirectionLocal.IsNearlyZero() &&
		FMath::Abs(State.CoverDirectionLocal.Y) > 0.35f)
	{
		State.PeekSideLocal = FMath::Sign(State.CoverDirectionLocal.Y);
	}

	// ЕСТЬ ЛИ КУДА ВЫГЛЯДЫВАТЬ — отдельный вопрос от стороны: у глухой стены
	// сторона известна, а края нет.
	bHasPeekEdge = false;
	if (Cover && Cover->BestCoverAround != ECoverType::None)
	{
		float EdgeDistance = 0.f;
		bHasPeekEdge = !Cover->FindPeekEdgeSide(EdgeDistance).IsNearlyZero();
	}

	// Подшаг к стене — тоже движение: иначе юнит ехал бы к укрытию в статичной
	// позе вместо шага (см. HugCover).
	State.bMoving = UTacticsCombatStatics::IsUnitInTransit(this) || bCoverHugStepping;
	State.bPlayerSide = GetGenericTeamId() == FGenericTeamId(TacticsTeamIds::Player);
	State.PendingTurnYaw = bTurningInPlace ? PendingTurnAmount : 0.f;
	State.bShouldPeek = bPeekActive;

	// ПОЗА — приоритетом сверху вниз, тем же порядком, что у статуса в HUD
	// (`UTacticalHUDStyleData::GetStatusForUnit`): один порядок на иконку и на
	// анимацию, иначе игрок видит «глухая оборона» в HUD и бег в кадре.
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	const auto HasTag = [ASC](const FGameplayTag& Tag)
	{
		return ASC && ASC->HasMatchingGameplayTag(Tag);
	};

	if (bIsDead)                                              { State.Pose = EUnitPose::Dead; }
	else if (bIsDowned)                                       { State.Pose = EUnitPose::Downed; }
	else if (HasTag(TacticsGameplayTags::State_HunkeredDown)) { State.Pose = EUnitPose::Hunkered; }
	else if (HasTag(TacticsGameplayTags::State_Overwatch))    { State.Pose = EUnitPose::Overwatch; }
	else if (State.bMoving)                                   { State.Pose = EUnitPose::Moving; }
	else if (State.Cover == ECoverType::Full)                 { State.Pose = EUnitPose::HighCover; }
	else if (State.Cover == ECoverType::Half)                 { State.Pose = EUnitPose::CrouchCover; }
	else                                                      { State.Pose = EUnitPose::Stand; }

	VisualState = State;
}

void AUnitBase::HugCover()
{
	const UCoverDetectionComponent* Cover = GetCoverDetection();
	if (!Cover || Cover->BestCoverAround == ECoverType::None)
	{
		return; // укрытия нет — прижиматься не к чему
	}
	FVector ToWall = Cover->BestCoverDirection;
	ToWall.Z = 0.f;
	if (!ToWall.Normalize())
	{
		return;
	}

	// (1) РАЗВОРОТ ВДОЛЬ СТЕНЫ, ЛИЦОМ К КРАЮ УКРЫТИЯ.
	//
	// ⚠️ Не «лицом в стену». Cover-анимации сняты для бойца, стоящего БОКОМ к
	// укрытию и смотрящего вдоль него на угол, из-за которого будет выглядывать.
	// При развороте носом в стену стороны вырождаются (стена спереди, слева и
	// справа одинаково), клип начинает доворачивать всё тело, компенсируя
	// неверную стойку, — отсюда «развернулся целиком» и «голова назад».
	//
	// Стоя вдоль стены, юнит имеет однозначную сторону укрытия
	// (`CoverDirectionLocal.Y`), и она же выбирает Left/Right-клип.
	//
	// ⚠️ БЕЗ анимации доворота: здесь уже играют подшаг и вход в позу укрытия,
	// а наложенный сверху `Turn_180` выглядит как вывернутое назад тело.
	float EdgeDistance = 0.f;
	const FVector EdgeSide = Cover->FindPeekEdgeSide(EdgeDistance);
	const FVector FaceDirection = EdgeSide.IsNearlyZero()
		? (bCoverHugFaceWall ? ToWall : -ToWall) // глухая стена — выглядывать некуда
		: EdgeSide;
	FaceTowardsSmooth(GetActorLocation() + FaceDirection * 100.f,
		/*bPlayTurnAnimation=*/false, CoverHugTurnRate);

	if (CoverHugMaxNudge <= 0.f)
	{
		return;
	}

	// (2) ПОДШАГ вплотную. Куда именно — решает свип капсулой: если между юнитом
	// и стеной кто-то есть, встаём ровно до него и не лезем сквозь геометрию.
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	UWorld* World = GetWorld();
	if (!Capsule || !World)
	{
		return;
	}

	FVector TargetLocation;
	const FVector Start = GetActorLocation();
	const FVector End = Start + ToWall * CoverHugMaxNudge;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverHug), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());

	// ⚠️ ЕДИНСТВЕННОЕ место в Tactics/, где трейс идёт ПО КАНАЛУ, и это верно:
	// здесь вопрос физический — «куда пролезет капсула», а не «что остановит
	// пулю». Значит юниты обязаны учитываться (в союзника вжиматься нельзя), то
	// есть `GetShotGeometryObjects` тут был бы ошибкой.
	TargetLocation = End;
	if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
		Capsule->GetCollisionObjectType(), Shape, Params))
	{
		TargetLocation = Start + ToWall * FMath::Max(0.f, Hit.Distance - CoverHugClearance);
	}

	// ⚠️ НЕ ВЫХОДИТЬ ЗА НАВМЕШ. Навмеш отступает от стен на радиус агента, а
	// подтяг идёт ВПЛОТНУЮ — вставший там боец оказывается вне навмеша, и тогда
	// у него пропадает зона хода целиком: волна в AMoveRangeVisualizer стартует
	// с проекции его позиции и без неё не строится вовсе. Поэтому от конечной
	// точки отступаем назад, пока проекция не найдётся.
	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		const float FloorOffset = Capsule->GetScaledCapsuleHalfHeight();
		const FVector StartFoot = Start - FVector(0.f, 0.f, FloorOffset);
		const FVector TargetFoot = TargetLocation - FVector(0.f, 0.f, FloorOffset);
		const FVector NavExtent(20.f, 20.f, 120.f);

		FVector Reachable = Start; // ноль подшага — всегда валиден, юнит уже стоит
		FNavLocation Projected;
		for (float Alpha = 1.f; Alpha > 0.f; Alpha -= 0.25f)
		{
			if (NavSys->ProjectPointToNavigation(FMath::Lerp(StartFoot, TargetFoot, Alpha),
				Projected, NavExtent))
			{
				Reachable = FMath::Lerp(Start, TargetLocation, Alpha);
				break;
			}
		}
		TargetLocation = Reachable;
	}

	// Идти уже некуда — не поднимаем шаг ради пары миллиметров.
	if (FVector::DistSquared2D(Start, TargetLocation) < FMath::Square(CoverHugArriveTolerance))
	{
		return;
	}

	// ⚠️ Точку ведёт `CharacterMovement`, а не `SetActorLocation`: только так у
	// юнита появляется настоящая `Velocity`, а значит и анимация шага. Телепорт
	// сюда возвращать нельзя — он и выглядит рывком, и оставляет ABP со Speed=0.
	// Скорость на время подшага занижаем до шага: 45 см на беговых 600 см/с —
	// это 0.07 с, за которые не читается вообще ничего.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// Сохраняем ИСХОДНУЮ скорость только на первом входе: повторный HugCover
		// во время подшага иначе «сохранил» бы уже заниженную и оставил юнита
		// шагающим до конца боя.
		if (!bCoverHugStepping)
		{
			CoverHugSavedMaxSpeed = Movement->MaxWalkSpeed;
		}
		Movement->MaxWalkSpeed = CoverHugStepSpeed;
	}
	CoverHugStepTarget = TargetLocation;
	CoverHugStepElapsed = 0.f;
	bCoverHugStepping = true;

	// Поза становится «идёт» ДО первого кадра подшага, иначе юнит успеет
	// проехать часть пути в статичной позе укрытия.
	NotifyUnitStateChanged();
}

void AUnitBase::FaceTowardsSmooth(const FVector& TargetLocation, bool bPlayTurnAnimation,
	float TurnRateOverride)
{
	FVector ToTarget = TargetLocation - GetActorLocation();
	ToTarget.Z = 0.f;
	if (!ToTarget.Normalize())
	{
		return;
	}

	const float CurrentYaw = GetActorRotation().Yaw;
	const float DesiredYaw = ToTarget.Rotation().Yaw;
	const float Delta = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);

	// Мелкий доворот проигрывать нечем: клипы начинаются с 45°, а на 10° любой
	// из них выглядит как подёргивание. Такие углы сводим сразу.
	if (FMath::Abs(Delta) < TurnInPlaceMinAngle)
	{
		UTacticsCombatStatics::FaceActorTowards(this, TargetLocation);
		return;
	}

	TurnTargetYaw = DesiredYaw;
	bTurningInPlace = true;
	ActiveTurnRate = TurnRateOverride > 0.f ? TurnRateOverride : TurnInPlaceRate;

	// ПОЛНАЯ дельта — снимок для ABP: по ней выбирается клип 45/090/135/180 и
	// сторона. Покадровой дельты для этого мало: она не говорит, сколько ещё
	// осталось повернуть.
	PendingTurnAmount = bPlayTurnAnimation ? Delta : 0.f;
	NotifyUnitStateChanged();
}

void AUnitBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCoverHugStepping)
	{
		UpdateCoverHugStep(DeltaSeconds);
	}
	if (bTurningInPlace)
	{
		UpdateTurnInPlace(DeltaSeconds);
	}
	UpdateCoverPeek(DeltaSeconds);
}

void AUnitBase::UpdateCoverPeek(float DeltaSeconds)
{
	// Выглядывать можно только стоя в укрытии, не двигаясь и имея КУДА выглянуть
	// (край укрытия в пределах трейса). В любом другом состоянии таймер сброшен.
	const bool bCanPeek =
		(VisualState.Pose == EUnitPose::CrouchCover || VisualState.Pose == EUnitPose::HighCover) &&
		!VisualState.bMoving && bHasPeekEdge &&
		!FMath::IsNearlyZero(VisualState.PeekSideLocal);

	if (!bCanPeek)
	{
		CoverIdleTime = 0.f;
		if (bPeekActive)
		{
			bPeekActive = false;
			FaceCoverWall();
			NotifyUnitStateChanged();
		}
		return;
	}

	// ⚠️ Доворот НЕ прерывает выглядывание и не сбрасывает таймер: сам peek его и
	// запускает (разворот к краю). Прерывать здесь означало бы гасить выглядывание
	// на первом же кадре после старта.
	if (!bPeekActive && bTurningInPlace)
	{
		return; // ждём, пока боец довернётся к цели, и только потом копим время
	}

	if (NextPeekDelay <= 0.f)
	{
		NextPeekDelay = FMath::FRandRange(CoverPeekDelayMin, CoverPeekDelayMax);
	}

	CoverIdleTime += DeltaSeconds;

	if (!bPeekActive)
	{
		if (CoverIdleTime >= NextPeekDelay)
		{
			bPeekActive = true;
			CoverIdleTime = 0.f;
			FrozenPeekSide = VisualState.PeekSideLocal; // до разворота, см. поле
			if (bTurnBodyOnPeek)
			{
				FacePeekEdge();
			}
			NotifyUnitStateChanged(); // ABP уходит в Look
		}
		return;
	}

	if (CoverIdleTime >= CoverPeekDuration)
	{
		bPeekActive = false;
		CoverIdleTime = 0.f;
		// Новая задержка каждый раз: иначе бойцы в отряде выглядывают синхронно,
		// как метрономы.
		NextPeekDelay = FMath::FRandRange(CoverPeekDelayMin, CoverPeekDelayMax);
		if (bTurnBodyOnPeek)
		{
			FaceCoverWall(); // возврат к укрытию нужен только если сами доворачивали
		}
		NotifyUnitStateChanged(); // ABP возвращается в Loop
	}
}

void AUnitBase::FacePeekEdge()
{
	const UCoverDetectionComponent* Cover = GetCoverDetection();
	if (!Cover)
	{
		return;
	}

	float EdgeDistance = 0.f;
	const FVector EdgeSide = Cover->FindPeekEdgeSide(EdgeDistance);
	FVector ToWall = Cover->BestCoverDirection;
	ToWall.Z = 0.f;
	if (EdgeSide.IsNearlyZero() || !ToWall.Normalize())
	{
		return;
	}

	// ⚠️ Доминирует направление К СТЕНЕ, край лишь подмешивается: боец
	// ДОВОРАЧИВАЕТСЯ к углу (≈35°), а не отворачивается вдоль стены. Полный
	// разворот вбок читается как «пошёл куда-то», а не «выглянул».
	const FVector LookDirection = (ToWall + EdgeSide * 0.7f).GetSafeNormal();
	FaceTowardsSmooth(GetActorLocation() + LookDirection * 200.f,
		/*bPlayTurnAnimation=*/false, CoverHugTurnRate);
}

void AUnitBase::FaceCoverWall()
{
	const UCoverDetectionComponent* Cover = GetCoverDetection();
	if (!Cover || Cover->BestCoverAround == ECoverType::None)
	{
		return;
	}
	FVector ToWall = Cover->BestCoverDirection;
	ToWall.Z = 0.f;
	if (!ToWall.Normalize())
	{
		return;
	}
	const FVector FaceDirection = bCoverHugFaceWall ? ToWall : -ToWall;
	FaceTowardsSmooth(GetActorLocation() + FaceDirection * 100.f,
		/*bPlayTurnAnimation=*/false, CoverHugTurnRate);
}

void AUnitBase::UpdateCoverHugStep(float DeltaSeconds)
{
	// Пришёл новый приказ на движение — подшаг больше не актуален, и держать
	// заниженную скорость нельзя: боец пополз бы весь ход шагом.
	if (UTacticsCombatStatics::IsUnitInTransit(this))
	{
		FinishCoverHugStep();
		return;
	}

	CoverHugStepElapsed += DeltaSeconds;

	FVector ToTarget = CoverHugStepTarget - GetActorLocation();
	ToTarget.Z = 0.f;
	const float Distance = ToTarget.Size();

	// Дошли — или уперлись во что-то и стоим (страховка по времени: путь короткий,
	// на него заведомо хватает нескольких десятых секунды).
	const float Timeout = FMath::Max(1.f, (CoverHugMaxNudge / FMath::Max(1.f, CoverHugStepSpeed)) * 3.f);
	if (Distance <= CoverHugArriveTolerance || CoverHugStepElapsed >= Timeout)
	{
		FinishCoverHugStep();
		return;
	}

	AddMovementInput(ToTarget / Distance, 1.f);
}

void AUnitBase::FinishCoverHugStep()
{
	bCoverHugStepping = false;
	CoverHugStepElapsed = 0.f;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (CoverHugSavedMaxSpeed > 0.f)
		{
			Movement->MaxWalkSpeed = CoverHugSavedMaxSpeed;
		}
		Movement->StopMovementImmediately(); // без этого юнит по инерции проезжает стену
	}

	// Позиция сдвинулась — укрытие и HUD обязаны пересчитаться от НОВОЙ точки.
	if (UCoverDetectionComponent* MutableCover = GetCoverDetection())
	{
		MutableCover->EvaluateSurroundings();
	}
	NotifyUnitStateChanged();
}

void AUnitBase::UpdateTurnInPlace(float DeltaSeconds)
{
	const FRotator Current = GetActorRotation();
	const float Delta = FMath::FindDeltaAngleDegrees(Current.Yaw, TurnTargetYaw);
	const float Step = (ActiveTurnRate > 0.f ? ActiveTurnRate : TurnInPlaceRate) * DeltaSeconds;

	if (FMath::Abs(Delta) <= Step)
	{
		SetActorRotation(FRotator(Current.Pitch, TurnTargetYaw, Current.Roll));
		bTurningInPlace = false;
		PendingTurnAmount = 0.f;
		NotifyUnitStateChanged(); // ABP гасит анимацию доворота
		return;
	}

	SetActorRotation(FRotator(Current.Pitch, Current.Yaw + FMath::Sign(Delta) * Step, Current.Roll));
}

UAnimMontage* AUnitBase::GetFireMontageFor(const AActor* Target, EFiringStance& OutStance,
	FVector& OutFiringEyeLocation) const
{
	// Стойка и точка выстрела берутся у ЕДИНОГО источника — того же, что решает
	// геометрию боя. Никакой отдельной «анимационной» логики укрытий быть не
	// должно: разойдётся с тем, что засчитала игра.
	OutStance = UTacticsCombatStatics::GetFiringStance(this, Target, OutFiringEyeLocation);
	switch (OutStance)
	{
	case EFiringStance::OverCover: return FireMontageOverCover ? FireMontageOverCover : FireMontageOpen;
	case EFiringStance::StepOut:   return FireMontageStepOut   ? FireMontageStepOut   : FireMontageOpen;
	default:                       return FireMontageOpen;
	}
}

void AUnitBase::PostInitializeComponents()
{
	// BaseMaxHealth уже содержит итоговый C++/BP-дефолт конкретного класса.
	// В APawn Super::PostInitializeComponents() создаёт default controller, а
	// PossessedBy затем применяет StartupEffects. Поэтому базу GAS намеренно
	// задаём ДО Super: будущие стартовые эффекты модифицируют её, а не стираются.
	const float InitialHealth = FMath::Max(1.f, BaseMaxHealth);
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->SetNumericAttributeBase(UTDAttributeSet::GetMaxHealthAttribute(), InitialHealth);
		ASC->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), InitialHealth);
	}
	else if (Attributes)
	{
		// Защитный fallback для нестандартного наследника без ASC.
		Attributes->InitMaxHealth(InitialHealth);
		Attributes->InitHealth(InitialHealth);
	}

	Super::PostInitializeComponents();
}

void AUnitBase::BeginPlay()
{
	Super::BeginPlay();

	// Класс юнита — источник истины для роли. Дополнительно восстанавливаем
	// канонический позывной, если новый BP оставили пустым либо он унаследовал
	// «Клин» после дублирования BP_Unit_Assault.
	const bool bHasCopiedAssaultName = UnitDisplayName.ToString() == TEXT("Клин");
	if (IsA<AUnit_Sniper>())
	{
		UnitRole = EUnitRole::Sniper;
		if (UnitDisplayName.IsEmpty() || bHasCopiedAssaultName)
		{
			UnitDisplayName = NSLOCTEXT("XRU1Units", "SniperName", "Оса");
		}
	}
	else if (IsA<AUnit_Healer>())
	{
		UnitRole = EUnitRole::Healer;
		if (UnitDisplayName.IsEmpty() || bHasCopiedAssaultName)
		{
			UnitDisplayName = NSLOCTEXT("XRU1Units", "HealerName", "Шприц");
		}
	}
	else if (IsA<AUnit_Tank>())
	{
		UnitRole = EUnitRole::Tank;
		if (UnitDisplayName.IsEmpty() || bHasCopiedAssaultName)
		{
			UnitDisplayName = NSLOCTEXT("XRU1Units", "TankName", "Молот");
		}
	}
	else if (IsA<AUnit_Assault>())
	{
		UnitRole = EUnitRole::Assault;
		if (UnitDisplayName.IsEmpty())
		{
			UnitDisplayName = NSLOCTEXT("XRU1Units", "AssaultName", "Клин");
		}
	}

	GrantClassAbilities();

	// Гарантируем выключенный Custom Depth на старте: если в BP-меше стоит галка
	// «Render CustomDepth Pass», юнит светился бы обводкой до первого наведения.
	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetRenderCustomDepth(false);
	}

	// Смерть/ранение отслеживаем по атрибуту Health (урон приходит только через GAS).
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetHealthAttribute())
			.AddUObject(this, &AUnitBase::HandleHealthChanged);
	}
}

void AUnitBase::GrantClassAbilities()
{
	if (!HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	auto Grant = [ASC, this](TSubclassOf<UGameplayAbility> AbilityClass)
	{
		if (AbilityClass)
		{
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	};

	Grant(AttackAbilityClass);
	Grant(OverwatchAbilityClass);
	Grant(HunkerAbilityClass);
	Grant(ClassAbilityClass);
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : ClassAbilities)
	{
		Grant(AbilityClass);
	}
}

void AUnitBase::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (bIsDead || bIsEvacuated)
	{
		return;
	}

	if (Data.NewValue <= 0.f)
	{
		if (bCanBeDowned)
		{
			if (!bIsDowned)
			{
				SetDowned(true);
			}
		}
		else
		{
			Die();
		}
		// SetDowned/Die уже отправили один итоговый refresh.
		return;
	}

	// Обычный урон/лечение живого юнита меняет HPBar портрета. Переход
	// 0 -> positive создаёт SetDowned(false), который уведомит один раз уже
	// после снятия тега Downed, поэтому промежуточный delegate здесь пропускаем.
	if (Data.OldValue > 0.f)
	{
		NotifyUnitStateChanged();
	}
}

void AUnitBase::SetDowned(bool bNewDowned, float ReviveHealth)
{
	if (bIsDead || bIsEvacuated || bIsDowned == bNewDowned)
	{
		return;
	}

	bIsDowned = bNewDowned;
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();

	if (bIsDowned)
	{
		// Падение: HP в 0 (если форс из скрипта), тег State.Downed, никаких действий.
		if (ASC)
		{
			if (const UTDAttributeSet* Attrs = ASC->GetSet<UTDAttributeSet>())
			{
				if (Attrs->GetHealth() > 0.f)
				{
					ASC->ApplyModToAttribute(UTDAttributeSet::GetHealthAttribute(),
						EGameplayModOp::Override, 0.f);
				}
			}
			ASC->AddLooseGameplayTag(TacticsGameplayTags::State_Downed);
			ASC->CancelAllAbilities();
		}
		if (ActionPoints)
		{
			ActionPoints->SpendAllRemaining();
		}
		// Лежащий раненый не мешает движению/навмешу (как и труп).
		ApplyDefeatedCollision(true);
	}
	else
	{
		// Подъём медиком/скриптом.
		if (ASC)
		{
			ASC->RemoveLooseGameplayTag(TacticsGameplayTags::State_Downed);
			ASC->ApplyModToAttribute(UTDAttributeSet::GetHealthAttribute(),
				EGameplayModOp::Override, FMath::Max(1.f, ReviveHealth));
		}
		ApplyDefeatedCollision(false); // встал — снова блокирует и режет навмеш
	}

	OnDownedChanged(bIsDowned);
	NotifyUnitStateChanged();
}

void AUnitBase::Die()
{
	if (bIsDead)
	{
		return;
	}
	bIsDead = true;

	// Труп не подсвечивается: гасим кольцо и обводку.
	SetSelectionHighlight(false);
	SetHoverHighlight(false);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}
	// Труп не блокирует выстрелы/перемещение и не режет навмеш; меш остаётся
	// для анимации смерти в BP.
	ApplyDefeatedCollision(true);
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	OnDied();
	NotifyUnitStateChanged();

	// Анимация смерти не доводит тело до пола (клип заканчивается «на весу», а на
	// склоне поза вообще висит в воздухе). Поэтому по её окончании тело уходит в
	// физику и доваливается само. Длительность берём у назначенного монтажа —
	// один источник правды с тем, что реально играет BP.
	const float Delay = DeathMontage ? DeathMontage->GetPlayLength() : RagdollDelay;
	if (Delay > 0.f)
	{
		GetWorldTimerManager().SetTimer(RagdollTimerHandle, this, &AUnitBase::StartRagdoll, Delay, false);
	}
	else
	{
		StartRagdoll();
	}
}

void AUnitBase::StartRagdoll()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp || MeshComp->IsSimulatingPhysics())
	{
		return;
	}

	// Движение выключаем ДО физики: иначе CharacterMovement продолжает считать
	// падение капсулы, у которой уже нет коллизии, и тащит актор сквозь пол.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// Профиль `Ragdoll`: тело сталкивается с миром (лежит на полу, а не проваливается),
	// но по каналу Pawn прозрачно — сквозь труп можно пробежать.
	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	MeshComp->SetCanEverAffectNavigation(false);
	MeshComp->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, /*bIncludeSelf=*/true);
	MeshComp->WakeAllRigidBodies();
}

void AUnitBase::ApplyDefeatedCollision(bool bDefeated)
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// ⚠️ НЕ `NoCollision`. Капсула остаётся query-объектом, который никого не
		// блокирует, но ловит трейсы: по упавшему бойцу игрок обязан попасть
		// курсором, иначе медик физически не может выбрать его целью подъёма.
		if (bDefeated)
		{
			// Block ТОЛЬКО по `Visibility`: этот канал не участвует в движении
			// (CharacterMovement свипает по `Pawn`), поэтому бегущие проходят
			// сквозь упавшего, а курсорный трейс в него попадает. `Overlap` здесь
			// не годится — одиночный трейс возвращает лишь блокирующие хиты.
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
		else
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
		}
		Capsule->SetCanEverAffectNavigation(!bDefeated);
	}
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// НЕ резать навмеш телом (критично при RuntimeGeneration=Dynamic) и
		// пропускать бегущих сквозь тело. На подъёме навмеш восстанавливаем;
		// отклик по Pawn на живом мертвецу и так не мешает (капсула снова блокирует).
		MeshComp->SetCanEverAffectNavigation(!bDefeated);
		if (bDefeated)
		{
			MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	}

	// Упавший (раненый или труп) не должен продолжать «идти»: движение с
	// отключённой физикой капсулы утаскивает тело сквозь пол, особенно на склоне.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (bDefeated)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		else if (Movement->MovementMode == MOVE_None)
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
}

void AUnitBase::Evacuate()
{
	if (bIsDead || bIsEvacuated)
	{
		return;
	}
	bIsEvacuated = true;

	SetSelectionHighlight(false);
	SetHoverHighlight(false);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	OnEvacuated();
	NotifyUnitStateChanged();
}
