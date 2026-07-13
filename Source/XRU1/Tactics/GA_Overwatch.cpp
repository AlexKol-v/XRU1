#include "GA_Overwatch.h"
#include "Perception/AIPerceptionComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

UGA_Overwatch::UGA_Overwatch()
{
	// Overwatch «висит» весь ход врага, поэтому явно неинстансируем как InstancedPerActor.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Overwatch::ActivateAbility(
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

	ReactionShotsUsed = 0;
	BoundPerception = GetOwnerPerception();
	if (BoundPerception)
	{
		BoundPerception->OnTargetPerceptionUpdated.AddDynamic(this, &UGA_Overwatch::HandlePerceptionUpdated);
	}
	// Способность остаётся активной до конца хода врага; EndAbility вызывается TurnManager'ом.
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
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Overwatch::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor || ReactionShotsUsed >= MaxReactionShots)
	{
		return;
	}

	// Реагируем только на факт «увидел» (успешно воспринятый стимул).
	if (Stimulus.WasSuccessfullySensed())
	{
		FireReactionShot(Actor);
	}
}

void UGA_Overwatch::FireReactionShot(AActor* Target)
{
	// TODO(next phase): расчёт попадания с учётом ECoverType цели, применение GameplayEffect урона,
	// проигрыш монтажа/VFX выстрела. Здесь только учёт лимита реакций.
	++ReactionShotsUsed;
}

UAIPerceptionComponent* UGA_Overwatch::GetOwnerPerception() const
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	if (!Info)
	{
		return nullptr;
	}

	// Perception обычно живёт на AIController пешки; ищем и там, и на самом аваторе.
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
