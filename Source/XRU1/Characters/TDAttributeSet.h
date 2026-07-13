#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "TDAttributeSet.generated.h"

#define TD_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName)            \
GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName)            \
GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class XRU1_API UTDAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UTDAttributeSet();

	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData Health;
	TD_ATTRIBUTE_ACCESSORS(UTDAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxHealth;
	TD_ATTRIBUTE_ACCESSORS(UTDAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MoveSpeed;
	TD_ATTRIBUTE_ACCESSORS(UTDAttributeSet, MoveSpeed)

	/** Декларируемый «потолок» MoveSpeed — используется виджетом для нормализации полосы. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxMoveSpeed;
	TD_ATTRIBUTE_ACCESSORS(UTDAttributeSet, MaxMoveSpeed)

	/**
	 * Оставшееся время действия защитного щита в секундах. > 0 = щит активен.
	 * Тикается вниз ATDCombatant::Tick. При активном щите урон по Health
	 * поглощается в PostGameplayEffectExecute.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData Shield;
	TD_ATTRIBUTE_ACCESSORS(UTDAttributeSet, Shield)
};