#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "TacticsGameplayEffects.generated.h"

/**
 * GameplayEffect'ы тактического ядра, собранные в C++ (без ассетов), чтобы
 * компоненты могли ссылаться на них по умолчанию. При необходимости дизайнер
 * может подменить их BP-наследниками через свойства компонентов/способностей.
 */

/** Бессрочный GE половинчатого укрытия: выдаёт юниту тег Cover.Half. */
UCLASS()
class XRU1_API UGE_CoverHalf : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_CoverHalf();
};

/** Бессрочный GE полного укрытия: выдаёт юниту тег Cover.Full. */
UCLASS()
class XRU1_API UGE_CoverFull : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_CoverFull();
};

/**
 * Мгновенный GE урона от выстрела. Величина приходит через SetByCaller
 * (тег Data.Damage, отрицательное значение — вычитается из Health).
 * Поглощение щитом обрабатывает UTDAttributeSet::PostGameplayEffectExecute.
 */
UCLASS()
class XRU1_API UGE_ShotDamage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_ShotDamage();
};

/** Мгновенный GE лечения: Health += SetByCaller(Data.Heal), клампится атрибут-сетом. */
UCLASS()
class XRU1_API UGE_Heal : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_Heal();
};

/** Бессрочный GE глухой обороны: тег State.HunkeredDown (удвоенный бонус укрытия). */
UCLASS()
class XRU1_API UGE_HunkerDown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_HunkerDown();
};

/** Бессрочный GE провокации танка: тег State.Taunting (приоритетная цель, −50% урона). */
UCLASS()
class XRU1_API UGE_TauntShield : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_TauntShield();
};
