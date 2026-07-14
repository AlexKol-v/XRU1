#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "TacticalAbility.h"
#include "TacticsGameplayTags.h"
#include "TDAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"

AUnitBase::AUnitBase()
{
	ActionPoints = CreateDefaultSubobject<UActionPointsComponent>(TEXT("ActionPoints"));
	CoverDetection = CreateDefaultSubobject<UCoverDetectionComponent>(TEXT("CoverDetection"));
}

void AUnitBase::BeginPlay()
{
	Super::BeginPlay();
	GrantClassAbilities();

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
	OnUnitStateChanged.Broadcast();
}

void AUnitBase::Evacuate()
{
	if (bIsDead || bIsEvacuated)
	{
		return;
	}
	bIsEvacuated = true;

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
	OnUnitStateChanged.Broadcast();
}
