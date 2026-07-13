// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestReward.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "QuestSubsystem.h"

void UQuestReward::GrantTo_Implementation(AActor* /*Recipient*/)
{
    // Базовая награда ничего не выдаёт — конкретное поведение задают подклассы.
}

void UQuestReward_Item::GrantTo_Implementation(AActor* Recipient)
{
    if (!ItemActorClass || !Recipient)
    {
        return;
    }

    UWorld* World = Recipient->GetWorld();
    if (!World)
    {
        return;
    }

    const FTransform SpawnTransform = Recipient->GetActorTransform();
    for (int32 Index = 0; Index < Quantity; ++Index)
    {
        World->SpawnActor<AActor>(ItemActorClass, SpawnTransform);
    }
}

void UQuestReward_GameplayEffect::GrantTo_Implementation(AActor* Recipient)
{
    if (!EffectClass || !Recipient)
    {
        return;
    }

    const IAbilitySystemInterface* AbilityActor = Cast<IAbilitySystemInterface>(Recipient);
    UAbilitySystemComponent* AbilitySystem = AbilityActor ? AbilityActor->GetAbilitySystemComponent() : nullptr;
    if (!AbilitySystem)
    {
        return;
    }

    const FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
    const FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(EffectClass, EffectLevel, Context);
    if (Spec.IsValid())
    {
        AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    }
}

void UQuestReward_UnlockQuest::GrantTo_Implementation(AActor* Recipient)
{
    if (!QuestToUnlock.IsValid() || !Recipient)
    {
        return;
    }

    const UWorld* World = Recipient->GetWorld();
    const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    if (UQuestSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr)
    {
        Subsystem->MakeQuestAvailable(QuestToUnlock, Recipient);
    }
}

void UQuestReward_GameplayTag::GrantTo_Implementation(AActor* Recipient)
{
    if (TagsToGrant.IsEmpty() || !Recipient)
    {
        return;
    }

    const IAbilitySystemInterface* AbilityActor = Cast<IAbilitySystemInterface>(Recipient);
    if (UAbilitySystemComponent* AbilitySystem = AbilityActor ? AbilityActor->GetAbilitySystemComponent() : nullptr)
    {
        AbilitySystem->AddLooseGameplayTags(TagsToGrant);
    }
}
