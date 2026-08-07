#include "TacticalClassAbilities.h"
#include "XRU1Log.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "TacticsCombatStatics.h"
#include "TacticalQuestEvents.h"
#include "TurnManagerSubsystem.h"
#include "CombatFeedbackSubsystem.h"
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
	if (!ASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	AppliedEffectHandle = ASC->ApplyGameplayEffectToSelf(
		BuffEffect->GetDefaultObject<UGameplayEffect>(), 1.f, Context);
	if (!AppliedEffectHandle.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
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
	if (AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
	{
		Unit->NotifyUnitStateChanged();
	}
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
	// Имя и описание — то, что игрок читает на кнопке: у всех классов там
	// стояло родовое «классовая способность».
	DisplayName = NSLOCTEXT("XRU1", "AbilityHunkerName", "Глухая оборона");
	Description = NSLOCTEXT("XRU1", "AbilityHunkerDesc",
		"Боец вжимается в укрытие: защита укрытия удваивается до начала его следующего хода.");

	ActionPointCost = 1;
	bConsumesAllRemainingAP = true; // завершает активацию юнита
	BuffEffect = UGE_HunkerDown::StaticClass();

	// Повторная «оборона», пока действует текущая, бессмысленна.
	ActivationBlockedTags.AddTag(TacticsGameplayTags::State_HunkeredDown);
}

bool UGA_HunkerDown::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	// XCOM 2: глухая оборона только в укрытии. Тег Cover.Half/Full вешает
	// UCoverDetectionComponent по BestCoverAround — без укрытия отказываем,
	// чтобы игрок не сжёг ход впустую.
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	return ASC && (ASC->HasMatchingGameplayTag(TacticsGameplayTags::Cover_Half) ||
		ASC->HasMatchingGameplayTag(TacticsGameplayTags::Cover_Full));
}

void UGA_HunkerDown::OnBuffActivated()
{
	AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo());
	if (Unit)
	{
		Unit->PlayUnitSound(EUnitSoundEvent::HunkerDown);
	}
	if (UTacticalQuestEvents::IsPlayerSideUnit(Unit, Unit))
	{
		UTacticalQuestEvents::BroadcastQuestEvent(
			Unit, TacticalQuestTags::Event_Tactical_Ability_Hunker_Activated, Unit);
	}
}

// --- UGA_Taunt ----------------------------------------------------------------

UGA_Taunt::UGA_Taunt()
{
	DisplayName = NSLOCTEXT("XRU1", "AbilityTauntName", "Провокация");
	Description = NSLOCTEXT("XRU1", "AbilityTauntDesc",
		"Штурмовик вызывает огонь на себя и получает щит: враги предпочтут его остальным.");

	ActionPointCost = 1;
	bConsumesAllRemainingAP = true;
	MaxUsesPerMission = 1;
	BuffEffect = UGE_TauntShield::StaticClass();

	ActivationBlockedTags.AddTag(TacticsGameplayTags::State_Taunting);
}

void UGA_Taunt::OnBuffActivated()
{
	AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo());
	if (Unit)
	{
		Unit->PlayUnitSound(EUnitSoundEvent::Taunt);
	}
	if (UTacticalQuestEvents::IsPlayerSideUnit(Unit, Unit))
	{
		UTacticalQuestEvents::BroadcastQuestEvent(
			Unit, TacticalQuestTags::Event_Tactical_Ability_Taunt_Activated, Unit);
	}
}

// --- UGA_Heal -----------------------------------------------------------------

UGA_Heal::UGA_Heal()
{
	DisplayName = NSLOCTEXT("XRU1", "AbilityHealName", "Полевая медицина");
	Description = NSLOCTEXT("XRU1", "AbilityHealDesc",
		"Лечит союзника вплотную или поднимает тяжело раненого. Ход бойца не завершает.");

	// Активация приходит событием Event.Heal с целью в payload.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = TacticsGameplayTags::Event_Heal;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);

	ActionPointCost = 1;
	MaxUsesPerMission = 2;
	bRequiresTargetActor = true;
	TargetedActivationEventTag = TacticsGameplayTags::Event_Heal;

	HealEffect = UGE_Heal::StaticClass();
}

bool UGA_Heal::IsValidTargetActor_Implementation(
	AUnitBase* SourceUnit, AActor* TargetActor) const
{
	return CanHealTarget(SourceUnit, TargetActor, HealRange);
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
	const AUnitBase* TargetUnit = Cast<AUnitBase>(Target);
	if (!TargetUnit || (!TargetUnit->IsDowned() &&
		TargetUnit->GetHealth() >= TargetUnit->GetMaxHealth() - KINDA_SMALL_NUMBER))
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

	AUnitBase* TargetUnit = CastChecked<AUnitBase>(Target);
	const bool bRevive = TargetUnit->IsDowned();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	UAbilitySystemComponent* TargetASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	FGameplayEffectSpecHandle HealSpec;
	if (!bRevive)
	{
		if (!HealEffect || !SourceASC || !TargetASC)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddInstigator(Healer, Healer);
		HealSpec = SourceASC->MakeOutgoingSpec(HealEffect, 1.f, Context);
		if (!HealSpec.IsValid())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		HealSpec.Data->SetSetByCallerMagnitude(TacticsGameplayTags::Data_Heal, HealAmount);
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const float HealthBefore = TargetUnit->GetHealth();
	bool bMechanicsApplied = false;

	if (bRevive)
	{
		// Подъём тяжело раненого.
		TargetUnit->ReviveFromDowned(ReviveHealth);
		bMechanicsApplied = !TargetUnit->IsDowned() && TargetUnit->GetHealth() > 0.f;
	}
	else
	{
		// Обычное лечение через GE (кламп по MaxHealth — в атрибут-сете).
		TargetASC->ApplyGameplayEffectSpecToSelf(*HealSpec.Data);
		bMechanicsApplied = TargetUnit->GetHealth() > HealthBefore + KINDA_SMALL_NUMBER;
	}

	if (bMechanicsApplied)
	{
		// Всплывающее «+N» над подлеченным/поднятым бойцом.
		if (UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(Target))
		{
			const float Healed = TargetUnit->GetHealth() - HealthBefore;
			Feedback->ShowHeal(Target, Healed > KINDA_SMALL_NUMBER ? Healed : ReviveHealth);
		}
		OnHealApplied(Target, bRevive);
		if (Healer)
		{
			Healer->PlayUnitSound(EUnitSoundEvent::Heal);
		}
		if (UTacticalQuestEvents::IsPlayerSideUnit(Healer, Healer))
		{
			UTacticalQuestEvents::BroadcastQuestEventEx(Healer,
				bRevive
					? TacticalQuestTags::Event_Tactical_Ability_Heal_Revive
					: TacticalQuestTags::Event_Tactical_Ability_Heal_Normal,
				Healer, Target);
		}
	}
	else
	{
		UE_LOG(LogXRU1Combat, Error, TEXT("[Heal] %s: Commit прошёл, но механика не изменила цель %s"),
			*GetNameSafe(Healer), *GetNameSafe(Target));
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bMechanicsApplied);
}

// --- UGA_RunAndGun --------------------------------------------------------------

UGA_RunAndGun::UGA_RunAndGun()
{
	DisplayName = NSLOCTEXT("XRU1", "AbilityRunGunName", "Рывок");
	Description = NSLOCTEXT("XRU1", "AbilityRunGunDesc",
		"Бесплатное действие: боец получает дополнительное очко действия.");

	ActionPointCost = 0; // способность бесплатная, лимит — 1 раз за миссию
	MaxUsesPerMission = 1;
}

void UGA_RunAndGun::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerData)
{
	AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo());
	UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
	if (!Unit || !ActionPoints || ExtraActionPoints <= 0)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActionPoints->GrantExtraPoints(ExtraActionPoints);
	OnRunAndGun();
	if (Unit)
	{
		Unit->PlayUnitSound(EUnitSoundEvent::RunAndGun);
	}
	if (UTacticalQuestEvents::IsPlayerSideUnit(Unit, Unit))
	{
		UTacticalQuestEvents::BroadcastQuestEvent(Unit,
			TacticalQuestTags::Event_Tactical_Ability_RunAndGun_Activated, Unit);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
