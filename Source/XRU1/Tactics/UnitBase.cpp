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
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NavigationInvokerComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

AUnitBase::AUnitBase()
{
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

	State.bMoving = UTacticsCombatStatics::IsUnitInTransit(this);
	State.bPlayerSide = GetGenericTeamId() == FGenericTeamId(TacticsTeamIds::Player);

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

	// (1) РАЗВОРОТ лицом к стене по её нормали. Именно к стене, а не от неё:
	// так читается «прижался», а выстрел всё равно делается из точки пика с
	// доворотом на цель (см. EFiringStance::StepOut).
	UTacticsCombatStatics::FaceActorTowards(this, GetActorLocation() + ToWall * 100.f);

	if (CoverHugMaxNudge <= 0.f)
	{
		return;
	}

	// (2) ПОДТЯГИВАНИЕ вплотную. Свипаем капсулой к стене и встаём вплотную с
	// зазором. Свип, а не телепорт: если между юнитом и стеной кто-то есть,
	// подтянемся ровно до него и не провалимся сквозь геометрию.
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	UWorld* World = GetWorld();
	if (!Capsule || !World)
	{
		return;
	}

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
	FVector NewLocation = End;
	if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
		Capsule->GetCollisionObjectType(), Shape, Params))
	{
		NewLocation = Start + ToWall * FMath::Max(0.f, Hit.Distance - CoverHugClearance);
	}
	SetActorLocation(NewLocation, /*bSweep=*/true);

	// Позиция сдвинулась — укрытие и HUD обязаны пересчитаться от НОВОЙ точки.
	if (UCoverDetectionComponent* MutableCover = GetCoverDetection())
	{
		MutableCover->EvaluateSurroundings();
	}
	NotifyUnitStateChanged();
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
}

void AUnitBase::ApplyDefeatedCollision(bool bDefeated)
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(bDefeated ? ECollisionEnabled::NoCollision
			: ECollisionEnabled::QueryAndPhysics);
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
