#include "GA_Attack.h"
#include "UnitBase.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "TacticsCombatStatics.h"
#include "CoverTuningDataAsset.h"
#include "TurnManagerSubsystem.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"

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

float UGA_Attack::ComputeEffectiveAim(const AUnitBase* Shooter, const AActor* Target)
{
	if (!Shooter)
	{
		return 0.f;
	}

	float Aim = Shooter->BaseAim;
	if (Target)
	{
		// Модификаторы XCOM 2 (GDD §5.4). Считаются ЗДЕСЬ и только здесь:
		// через ComputeEffectiveAim идут выстрел игрока, AI, Overwatch (со своим
		// штрафом поверх) и HUD-прогноз — расходиться им негде.

		// 1) Дистанция: профиль оружия (кривая юнита или встроенная винтовка).
		const float Distance = FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation());
		Aim += UTacticsCombatStatics::GetAimDistanceModifier(Shooter, Distance);

		// 2) Высота — СИММЕТРИЧНО: стрелок заметно выше цели → +20, заметно ниже
		// → −20. (В XCOM 2 штрафа снизу нет, только бонус сверху; симметрия —
		// осознанное отклонение, зафиксировано в GDD §5.4: позиция на высоте
		// должна читаться как преимущество с обеих сторон.)
		const UCoverTuningDataAsset* Tuning = UTacticsCombatStatics::GetCoverTuning(Shooter->GetWorld());
		const float HeightDelta = Shooter->GetActorLocation().Z - Target->GetActorLocation().Z;
		if (HeightDelta >= Tuning->HeightAdvantageZ)
		{
			Aim += Tuning->HeightAdvantageAimBonus;
		}
		else if (Tuning->bSymmetricHeightPenalty && HeightDelta <= -Tuning->HeightAdvantageZ)
		{
			Aim -= Tuning->HeightAdvantageAimBonus;
		}

		// 3) Squadsight-выстрел без собственной LOS — штраф. Берём из CDO
		// способности атаки ЭТОГО юнита: HUD и выстрел считают одно и то же
		// даже при перенастроенном BP-наследнике GA_Attack.
		if (!UTacticsCombatStatics::HasLineOfSight(Shooter, Target))
		{
			const UGA_Attack* AttackCDO = nullptr;
			if (Shooter->AttackAbilityClass && Shooter->AttackAbilityClass->IsChildOf(UGA_Attack::StaticClass()))
			{
				AttackCDO = Shooter->AttackAbilityClass->GetDefaultObject<UGA_Attack>();
			}
			Aim -= AttackCDO
				? AttackCDO->SquadsightAimPenalty
				: GetDefault<UGA_Attack>()->SquadsightAimPenalty;
		}
	}
	return FMath::Max(0.f, Aim);
}

float UGA_Attack::ComputeAttackHitChance(const AUnitBase* Shooter, const AActor* Target)
{
	if (!CanTargetActor(Shooter, Target))
	{
		return -1.f;
	}
	return UTacticsCombatStatics::ComputeHitChance(Shooter, Target, ComputeEffectiveAim(Shooter, Target));
}

EAttackTargetStatus UGA_Attack::GetTargetStatus(const AUnitBase* Shooter, const AActor* Target)
{
	if (!Shooter || !Target || !UTacticsCombatStatics::AreHostile(Shooter, Target))
	{
		return EAttackTargetStatus::NotHostile;
	}
	if (!UTacticsCombatStatics::IsUnitAlive(Target))
	{
		return EAttackTargetStatus::Dead;
	}

	const float Distance = FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation());
	if (Distance > Shooter->AttackRange)
	{
		return EAttackTargetStatus::OutOfRange;
	}

	// Прямая видимость либо Squadsight (цель видит любой союзник снайпера).
	if (UTacticsCombatStatics::HasLineOfSight(Shooter, Target))
	{
		return EAttackTargetStatus::Valid;
	}
	if (Shooter->bHasSquadsight && UTacticsCombatStatics::SquadHasLineOfSight(Shooter, Target))
	{
		return EAttackTargetStatus::Valid;
	}
	return EAttackTargetStatus::NoLineOfSight;
}

bool UGA_Attack::CanTargetActor(const AUnitBase* Shooter, const AActor* Target)
{
	return GetTargetStatus(Shooter, Target) == EAttackTargetStatus::Valid;
}

bool UGA_Attack::HasAnyValidTarget(const AUnitBase* Shooter)
{
	if (!Shooter)
	{
		return false;
	}
	const UWorld* World = Shooter->GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return false;
	}
	for (const AActor* Enemy : TurnManager->GetOpposingUnits(Shooter))
	{
		if (CanTargetActor(Shooter, Enemy))
		{
			return true;
		}
	}
	return false;
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

	// Точность — единым расчётом с HUD-прогнозом (Squadsight-штраф внутри).
	const float Aim = ComputeEffectiveAim(Shooter, Target);

	const bool bHit = UTacticsCombatStatics::ResolveShot(Shooter, Target, Aim, Shooter->ShotDamage, DamageEffect);
	OnShotFired(Target, bHit);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
