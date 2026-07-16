#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "TacticalAbility.h"
#include "TacticsGameplayTags.h"
#include "TDAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "NavAreas/NavArea_Null.h"

AUnitBase::AUnitBase()
{
	ActionPoints = CreateDefaultSubobject<UActionPointsComponent>(TEXT("ActionPoints"));
	CoverDetection = CreateDefaultSubobject<UCoverDetectionComponent>(TEXT("CoverDetection"));

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

	// Юнит — динамическое препятствие навмеша (NavArea_Null = полный вырез):
	// чужие пути, зоны хода и превью огибают стоящих юнитов, встать «в» юнита
	// нельзя. Требует Runtime Generation = Dynamic (см. DefaultEngine.ini).
	// У действующего юнита вырез временно снимается (SetNavObstacleEnabled).
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCanEverAffectNavigation(true);
		Capsule->bDynamicObstacle = true;
		Capsule->SetAreaClassOverride(UNavArea_Null::StaticClass()); // сам сбросит bUseSystemDefaultObstacleAreaClass
	}
}

void AUnitBase::SetNavObstacleEnabled(bool bEnabled)
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCanEverAffectNavigation(bEnabled);
	}
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
		const bool bAlly = GetGenericTeamId().GetId() == 1;
		MeshComponent->SetCustomDepthStencilValue(bAlly ? HoverStencilAlly : HoverStencilEnemy);
		MeshComponent->SetRenderCustomDepth(true);
	}
	else
	{
		MeshComponent->SetRenderCustomDepth(false);
	}
}

void AUnitBase::BeginPlay()
{
	Super::BeginPlay();
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

	// До анимаций/HUD состояние иначе не видно: ранение «тихое» — юнит стоит,
	// но выпадает из выбора Tab/слотами (не «жив») и не принимает приказы.
	UE_LOG(LogTemp, Warning, TEXT("[Unit] %s %s"), *GetName(),
		bIsDowned ? TEXT("ТЯЖЕЛО РАНЕН (Downed: без действий, лечится медиком)")
		          : TEXT("поднят на ноги"));

	OnDownedChanged(bIsDowned);
	OnUnitStateChanged.Broadcast();
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
	SetNavObstacleEnabled(false); // и навмеш под трупом возвращается
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	UE_LOG(LogTemp, Warning, TEXT("[Unit] %s ПОГИБ (выбывает из отряда/очереди)"), *GetName());
	OnDied();
	OnUnitStateChanged.Broadcast();
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
	SetNavObstacleEnabled(false); // место эвакуации не остаётся «занятым»
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	OnEvacuated();
	OnUnitStateChanged.Broadcast();
}
