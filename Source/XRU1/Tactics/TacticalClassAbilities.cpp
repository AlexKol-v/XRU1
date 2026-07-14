#include "TacticalClassAbilities.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "TacticsCombatStatics.h"
#include "TurnManagerSubsystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"

// --- UGA_SelfBuffUntilNextTurn ----------------------------------------------

void UGA_SelfBuffUntilNextTurn::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerData)
{
	if (!BuffEffect || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(this);
		AppliedEffectHandle = ASC->ApplyGameplayEffectToSelf(
			BuffEffect->GetDefaultObject<UGameplayEffect>(), 1.f, Context);
	}

	// Стойка держится до начала следующего хода нашей стороны.
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (const UWorld* World = Avatar ? Avatar->GetWorld() : nullptr)
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->OnTurnStarted.AddDynamic(this, &UGA_SelfBuffUntilNextTurn::HandleTurnStarted);
		}
	}

	OnBuffApplied();
	OnBuffActivated();
	// Способность остаётся активной, EndAbility снимет GE.
}

void UGA_SelfBuffUntilNextTurn::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (AppliedEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(AppliedEffectHandle);
			AppliedEffectHandle.Invalidate();
		}
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (const UWorld* World = Avatar ? Avatar->GetWorld() : nullptr)
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->OnTurnStarted.RemoveDynamic(this, &UGA_SelfBuffUntilNextTurn::HandleTurnStarted);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_SelfBuffUntilNextTurn::HandleTurnStarted(ETurnPhase /*Phase*/)
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}
	if (const UWorld* World = Avatar->GetWorld())
	{
		if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			// Ход вернулся нашей стороне — стойка спадает.
			if (TurnManager->IsUnitOnActiveSide(Avatar))
			{
				EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
			}
		}
	}
}

// --- UGA_HunkerDown -----------------------------------------------------------

UGA_HunkerDown::UGA_HunkerDown()
{
	ActionPointCost = 1;
	bConsumesAllRemainingAP = true; // завершает активацию юнита
	BuffEffect = UGE_HunkerDown::StaticClass();

	// Повторная «оборона», пока действует текущая, бессмысленна.
	ActivationBlockedTags.AddTag(TacticsGameplayTags::State_HunkeredDown);
}

// --- UGA_Taunt ----------------------------------------------------------------

UGA_Taunt::UGA_Taunt()
{
	ActionPointCost = 1;
	bConsumesAllRemainingAP = true;
	MaxUsesPerMission = 1;
	BuffEffect = UGE_TauntShield::StaticClass();

	ActivationBlockedTags.AddTag(TacticsGameplayTags::State_Taunting);
}

// --- UGA_Heal -----------------------------------------------------------------

UGA_Heal::UGA_Heal()
{
	// Активация приходит событием Event.Heal с целью в payload.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = TacticsGameplayTags::Event_Heal;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);

	ActionPointCost = 1;
	MaxUsesPerMission = 2;
	bRequiresTargetActor = true;

	HealEffect = UGE_Heal::StaticClass();
}

bool UGA_Heal::CanHealTarget(const AUnitBase* Healer, const AActor* Target, float Range)
{
	if (!Healer || !Target)
	{
		return false;
	}
	// Союзник (не враг) или сам медик; мёртвых и эвакуированных не лечим.
	if (UTacticsCombatStatics::AreHostile(Healer, Target) || UTacticsCombatStatics::IsUnitEvacuated(Target))
	{
		return false;
	}
	const bool bAliveOrDowned = UTacticsCombatStatics::IsUnitAlive(Target) ||
		UTacticsCombatStatics::IsUnitDowned(Target);
	if (!bAliveOrDowned)
	{
		return false;
	}
	return FVector::Dist(Healer->GetActorLocation(), Target->GetActorLocation()) <= Range;
}

void UGA_Heal::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerData)
{
	AUnitBase* Healer = Cast<AUnitBase>(GetAvatarActorFromActorInfo());
	AActor* Target = TriggerData ? const_cast<AActor*>(TriggerData->Target.Get()) : nullptr;

	// Все проверки ДО Commit: при провале AP/заряды не тратятся.
	if (!Healer || !CanHealTarget(Healer, Target, HealRange))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AUnitBase* TargetUnit = Cast<AUnitBase>(Target);
	const bool bRevive = TargetUnit && TargetUnit->IsDowned();

	if (bRevive)
	{
		// Подъём тяжело раненого.
		TargetUnit->ReviveFromDowned(ReviveHealth);
	}
	else if (HealEffect)
	{
		// Обычное лечение через GE (кламп по MaxHealth — в атрибут-сете).
		if (UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo())
		{
			FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
			Context.AddInstigator(Healer, Healer);
			const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(HealEffect, 1.f, Context);
			if (Spec.IsValid())
			{
				Spec.Data->SetSetByCallerMagnitude(TacticsGameplayTags::Data_Heal, HealAmount);
				if (UAbilitySystemComponent* TargetASC =
					UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
				{
					TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
				}
			}
		}
	}

	OnHealApplied(Target, bRevive);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// --- UGA_RunAndGun --------------------------------------------------------------

UGA_RunAndGun::UGA_RunAndGun()
{
	ActionPointCost = 0; // способность бесплатная, лимит — 1 раз за миссию
	MaxUsesPerMission = 1;
}

void UGA_RunAndGun::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (const AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
	{
		if (UActionPointsComponent* ActionPoints = Unit->GetActionPoints())
		{
			ActionPoints->GrantExtraPoints(ExtraActionPoints);
		}
	}

	OnRunAndGun();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
