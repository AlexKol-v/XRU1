// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Dash.h"

#include "GameFramework/Character.h"

UGA_Dash::UGA_Dash()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Dash::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerData);
	if (ACharacter* Avatar = Cast<ACharacter>(ActorInfo->AvatarActor.Get()))
	{
		const FVector Impulse = Avatar->GetActorForwardVector() * 1500.f;
		Avatar->LaunchCharacter(Impulse, /*XYOverride*/true, /*ZOverride*/false);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

