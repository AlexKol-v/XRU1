#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "AbilitySystemInterface.h"
#include "GASCharacter.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UGameplayEffect;

/**
 * Промежуточный слой иерархии персонажей: добавляет GAS-плату (ASC,
 * жизненный цикл стартовых способностей и эффектов) поверх Team-скелета
 * ABaseCharacter. Не знает про конкретные AttributeSet'ы проекта — это
 * ответственность наследников (см. ATDCombatant).
 */
UCLASS(Abstract)
class XRU1_API AGASCharacter
    : public ABaseCharacter
    , public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    AGASCharacter();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
    virtual void PossessedBy(AController* NewController) override;

    void InitAbilityActorInfo();
    void GrantStartupAbilities();
    void ApplyStartupEffects();

    UPROPERTY(VisibleAnywhere, Category = "GAS")
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;

    UPROPERTY(EditDefaultsOnly, Category = "GAS|Startup")
    TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

    UPROPERTY(EditDefaultsOnly, Category = "GAS|Startup")
    TArray<TSubclassOf<UGameplayEffect>> StartupEffects;
};
