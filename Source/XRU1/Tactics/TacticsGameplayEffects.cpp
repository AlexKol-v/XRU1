#include "TacticsGameplayEffects.h"
#include "TacticsGameplayTags.h"
#include "TDAttributeSet.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

namespace
{
	/** Настраивает компонент так, чтобы GE выдавал цели один granted-тег. */
	void GrantTagViaComponent(UTargetTagsGameplayEffectComponent& TargetTags, const FGameplayTag& CoverTag)
	{
		FInheritedTagContainer TagChanges;
		TagChanges.Added.AddTag(CoverTag);
		TargetTags.SetAndApplyTargetTagChanges(TagChanges);
	}
}

UGE_CoverHalf::UGE_CoverHalf()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	// В конструкторе CDO компоненты GE создаются как default-subobject'ы
	// (NewObject/FindOrAddComponent тут падает: пустое имя внутри конструктора).
	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	GrantTagViaComponent(*TargetTags, TacticsGameplayTags::Cover_Half);
	GEComponents.Add(TargetTags);
}

UGE_CoverFull::UGE_CoverFull()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	GrantTagViaComponent(*TargetTags, TacticsGameplayTags::Cover_Full);
	GEComponents.Add(TargetTags);
}

namespace
{
	/** Общий модификатор Health через SetByCaller указанного тега. */
	void AddHealthSetByCallerModifier(UGameplayEffect& Effect, const FGameplayTag& DataTag)
	{
		FGameplayModifierInfo HealthMod;
		HealthMod.Attribute = UTDAttributeSet::GetHealthAttribute();
		HealthMod.ModifierOp = EGameplayModOp::Additive;

		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;
		HealthMod.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);

		Effect.Modifiers.Add(HealthMod);
	}
}

UGE_ShotDamage::UGE_ShotDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	// Health += Data.Damage (стрелок передаёт отрицательную величину).
	AddHealthSetByCallerModifier(*this, TacticsGameplayTags::Data_Damage);
}

UGE_Heal::UGE_Heal()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	// Health += Data.Heal (положительная величина; кламп по MaxHealth — в атрибут-сете).
	AddHealthSetByCallerModifier(*this, TacticsGameplayTags::Data_Heal);
}

UGE_HunkerDown::UGE_HunkerDown()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	GrantTagViaComponent(*TargetTags, TacticsGameplayTags::State_HunkeredDown);
	GEComponents.Add(TargetTags);
}

UGE_TauntShield::UGE_TauntShield()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTagsComponent"));
	GrantTagViaComponent(*TargetTags, TacticsGameplayTags::State_Taunting);
	GEComponents.Add(TargetTags);
}
