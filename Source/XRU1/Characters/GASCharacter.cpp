#include "GASCharacter.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbility.h"

AGASCharacter::AGASCharacter()
{
    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
}

UAbilitySystemComponent* AGASCharacter::GetAbilitySystemComponent() const
{
    return AbilitySystem;
}

void AGASCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    InitAbilityActorInfo();
    GrantStartupAbilities();
    ApplyStartupEffects();
}

void AGASCharacter::InitAbilityActorInfo()
{
    if (AbilitySystem)
    {
        AbilitySystem->InitAbilityActorInfo(this, this);
    }
}

void AGASCharacter::GrantStartupAbilities()
{
    if (!AbilitySystem) return;

    for (const TSubclassOf<UGameplayAbility>& AbilityClass : StartupAbilities)
    {
        if (*AbilityClass)
        {
            AbilitySystem->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
        }
    }
}

void AGASCharacter::ApplyStartupEffects()
{
    if (!AbilitySystem) return;

    FGameplayEffectContextHandle Ctx = AbilitySystem->MakeEffectContext();
    Ctx.AddSourceObject(this);
    for (const TSubclassOf<UGameplayEffect>& EffectClass : StartupEffects)
    {
        if (!*EffectClass) continue;
        const FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(EffectClass, 1.f, Ctx);
        if (Spec.IsValid())
        {
            AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        }
    }
}
