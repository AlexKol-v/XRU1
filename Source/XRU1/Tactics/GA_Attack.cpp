#include "GA_Attack.h"
#include "UnitBase.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "TacticsCombatStatics.h"
#include "AbilitySystemComponent.h"

UGA_Attack::UGA_Attack()
{
	// Активация приходит событием Event.Attack с целью в payload.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = TacticsGameplayTags::Event_Attack;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);

	// XCOM-правило: выстрел стоит 1 AP и завершает активацию юнита.
	ActionPointCost = 1;
	bConsumesAllRemainingAP = true;

	DamageEffect = UGE_ShotDamage::StaticClass();
}

bool UGA_Attack::CanTargetActor(const AUnitBase* Shooter, const AActor* Target)
{
	if (!Shooter || !Target ||
		!UTacticsCombatStatics::AreHostile(Shooter, Target) ||
		!UTacticsCombatStatics::IsUnitAlive(Target))
	{
		return false;
	}

	const float Distance = FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation());
	if (Distance > Shooter->AttackRange)
	{
		return false;
	}

	// Прямая видимость либо Squadsight (цель видит любой союзник снайпера).
	if (UTacticsCombatStatics::HasLineOfSight(Shooter, Target))
	{
		return true;
	}
	return Shooter->bHasSquadsight && UTacticsCombatStatics::SquadHasLineOfSight(Shooter, Target);
}

void UGA_Attack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerData)
{
	AUnitBase* Shooter = Cast<AUnitBase>(GetAvatarActorFromActorInfo());
	AActor* Target = TriggerData ? const_cast<AActor*>(TriggerData->Target.Get()) : nullptr;

	// Все проверки ДО Commit: при провале AP не списываются.
	if (!Shooter || !CanTargetActor(Shooter, Target))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Squadsight-выстрел без собственной LOS идёт со штрафом к точности.
	float Aim = Shooter->BaseAim;
	if (!UTacticsCombatStatics::HasLineOfSight(Shooter, Target))
	{
		Aim = FMath::Max(0.f, Aim - SquadsightAimPenalty);
	}

	const bool bHit = UTacticsCombatStatics::ResolveShot(Shooter, Target, Aim, Shooter->ShotDamage, DamageEffect);
	OnShotFired(Target, bHit);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
