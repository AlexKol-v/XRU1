#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "GA_Attack.h"
#include "GA_Overwatch.h"
#include "TacticalAbility.h"
#include "TacticalClassAbilities.h"
#include "TacticsGameplayTags.h"
#include "TDAttributeSet.h"
#include "UnitClasses.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

AUnitBase::AUnitBase()
{
	ActionPoints = CreateDefaultSubobject<UActionPointsComponent>(TEXT("ActionPoints"));
	CoverDetection = CreateDefaultSubobject<UCoverDetectionComponent>(TEXT("CoverDetection"));

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
	OnUnitStateChanged.Broadcast();
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
	// Труп не блокирует выстрелы/перемещение; меш остаётся для анимации смерти в BP.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	OnDied();
	NotifyUnitStateChanged();
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
