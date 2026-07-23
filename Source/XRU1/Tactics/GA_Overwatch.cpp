#include "GA_Overwatch.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "TacticsCombatStatics.h"
#include "TurnManagerSubsystem.h"
#include "GA_Attack.h"
#include "UnitBase.h"
#include "Perception/AIPerceptionComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

UGA_Overwatch::UGA_Overwatch()
{
	// Пока способность активна, юнит несёт тег State.Overwatch;
	// он же блокирует повторную активацию (двойной Overwatch невозможен).
	ActivationOwnedTags.AddTag(TacticsGameplayTags::State_Overwatch);
	ActivationBlockedTags.AddTag(TacticsGameplayTags::State_Overwatch);

	// XCOM-правило: наблюдение завершает активацию юнита (остаток AP сгорает).
	bConsumesAllRemainingAP = true;

	DamageEffect = UGE_ShotDamage::StaticClass();
}

void UGA_Overwatch::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerData)
{
	// CommitAbility проверит и спишет 1 AP (UTacticalAbility::CheckCost/ApplyCost).
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ReactionShotsUsed = 0;
	BoundPerception = GetOwnerPerception();
	if (BoundPerception)
	{
		BoundPerception->OnTargetPerceptionUpdated.AddDynamic(this, &UGA_Overwatch::HandlePerceptionUpdated);
	}

	// Способность снимается сама, когда ход возвращается нашей стороне или бой кончается.
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (const UWorld* World = Avatar ? Avatar->GetWorld() : nullptr)
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->OnTurnStarted.AddDynamic(this, &UGA_Overwatch::HandleTurnStarted);
			TurnManager->OnCombatEnded.AddDynamic(this, &UGA_Overwatch::HandleCombatEnded);
		}
	}

	if (AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
	{
		Unit->NotifyUnitStateChanged();
	}
}

void UGA_Overwatch::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (BoundPerception)
	{
		BoundPerception->OnTargetPerceptionUpdated.RemoveDynamic(this, &UGA_Overwatch::HandlePerceptionUpdated);
		BoundPerception = nullptr;
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (const UWorld* World = Avatar ? Avatar->GetWorld() : nullptr)
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->OnTurnStarted.RemoveDynamic(this, &UGA_Overwatch::HandleTurnStarted);
			TurnManager->OnCombatEnded.RemoveDynamic(this, &UGA_Overwatch::HandleCombatEnded);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Overwatch::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor || ReactionShotsUsed >= MaxReactionShots)
	{
		return;
	}

	// Реагируем только на факт «увидел» (успешно воспринятый стимул).
	if (!Stimulus.WasSuccessfullySensed())
	{
		return;
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}

	// Реакция — только в ЧУЖУЮ фазу хода (в свою юнит стреляет обычными действиями).
	if (const UWorld* World = Avatar->GetWorld())
	{
		if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			if (TurnManager->IsInCombat() && TurnManager->IsUnitOnActiveSide(Avatar))
			{
				return;
			}
		}
	}

	// Реакция подчиняется ОБЩЕМУ правилу выстрела (враждебность, живость,
	// дальность оружия, линия огня) — тому же, что у игрока и у AI. Радиус
	// перцепции шире дальности стрельбы: без этого юнит реагировал бы на цели,
	// в которые физически не может попасть.
	const AUnitBase* ShooterUnit = Cast<AUnitBase>(Avatar);
	if (!ShooterUnit || !UGA_Attack::CanTargetActor(ShooterUnit, Actor))
	{
		return;
	}

	FireReactionShot(Actor);
}

void UGA_Overwatch::HandleTurnStarted(ETurnPhase Phase)
{
	// Ход вернулся нашей стороне — режим наблюдения заканчивается.
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}
	if (const UWorld* World = Avatar->GetWorld())
	{
		if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			if (TurnManager->IsUnitOnActiveSide(Avatar))
			{
				EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
			}
		}
	}
}

void UGA_Overwatch::HandleCombatEnded(bool /*bPlayerWon*/)
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_Overwatch::FireReactionShot(AActor* Target)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Target)
	{
		return;
	}

	// Точность/урон — со статов юнита: общий расчёт (как у обычного выстрела)
	// плюс штраф реакции (GDD §5.4).
	const AUnitBase* Unit = Cast<AUnitBase>(Avatar);
	if (!Unit)
	{
		return;
	}
	const float Aim = FMath::Max(0.f, UGA_Attack::ComputeEffectiveAim(Unit, Target) - ReactionAimPenalty);
	const float Damage = Unit->ShotDamage;

	// Бросок против укрытия цели + урон через GE (SetByCaller Data.Damage).
	const bool bHit = UTacticsCombatStatics::ResolveShot(Avatar, Target, Aim, Damage, DamageEffect);
	++ReactionShotsUsed;
	OnReactionShot(Target, bHit);

	// Реакции исчерпаны — Overwatch снимается (как в XCOM: один выстрел за ход).
	if (ReactionShotsUsed >= MaxReactionShots)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

UAIPerceptionComponent* UGA_Overwatch::GetOwnerPerception() const
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	if (!Info)
	{
		return nullptr;
	}

	// Perception обычно живёт на AIController пешки; ищем и там, и на самом аватаре.
	if (const APawn* Pawn = Cast<APawn>(Info->AvatarActor.Get()))
	{
		if (AController* C = Pawn->GetController())
		{
			if (UAIPerceptionComponent* P = C->FindComponentByClass<UAIPerceptionComponent>())
			{
				return P;
			}
		}
	}
	if (AActor* Avatar = Info->AvatarActor.Get())
	{
		return Avatar->FindComponentByClass<UAIPerceptionComponent>();
	}
	return nullptr;
}
