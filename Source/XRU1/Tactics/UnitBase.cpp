#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

AUnitBase::AUnitBase()
{
	ActionPoints = CreateDefaultSubobject<UActionPointsComponent>(TEXT("ActionPoints"));
	CoverDetection = CreateDefaultSubobject<UCoverDetectionComponent>(TEXT("CoverDetection"));
}

void AUnitBase::BeginPlay()
{
	Super::BeginPlay();
	GrantClassAbilities();
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

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : ClassAbilities)
	{
		if (AbilityClass)
		{
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}
}
