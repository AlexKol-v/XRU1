#pragma once

#include "CoreMinimal.h"
#include "TacticalAbility.h"
#include "TacticsTypes.h"
#include "ActiveGameplayEffectHandle.h"
#include "TacticalClassAbilities.generated.h"

class UGameplayEffect;

/**
 * Общий предок способностей-«стоек», действующих до начала СВОЕГО следующего
 * хода (глухая оборона, провокация): вешает GE на себя, снимает его, когда ход
 * возвращается стороне юнита (паттерн как у GA_Overwatch).
 */
UCLASS(Abstract)
class XRU1_API UGA_SelfBuffUntilNextTurn : public UTacticalAbility
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	/** GE-«стойка», висящий на юните до его следующего хода. Задаёт наследник. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Buff")
	TSubclassOf<UGameplayEffect> BuffEffect;

	/** BP-хук активации (поза/VFX/звук). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Buff")
	void OnBuffApplied();

	/** Точка расширения: доп. логика после наложения баффа. */
	virtual void OnBuffActivated() {}

	/** Колбэк TurnManager'а: ход вернулся нашей стороне -> стойка снимается. */
	UFUNCTION()
	void HandleTurnStarted(ETurnPhase Phase);

	FActiveGameplayEffectHandle AppliedEffectHandle;
};

/**
 * «Глухая оборона» (все классы): 1 AP, завершает активацию; до начала своего
 * следующего хода бонус укрытия юнита удвоен (тег State.HunkeredDown,
 * учитывается в UTacticsCombatStatics::ComputeHitChance).
 */
UCLASS()
class XRU1_API UGA_HunkerDown : public UGA_SelfBuffUntilNextTurn
{
	GENERATED_BODY()

public:
	UGA_HunkerDown();
};

/**
 * «Провокация» танка (GDD §7): 1 AP, завершает активацию, 1 раз за миссию.
 * До своего следующего хода танк — приоритетная цель врагов (тег
 * State.Taunting в выборе цели AI) и получает −50% входящего урона
 * (обрабатывается в UTDAttributeSet::PostGameplayEffectExecute).
 */
UCLASS()
class XRU1_API UGA_Taunt : public UGA_SelfBuffUntilNextTurn
{
	GENERATED_BODY()

public:
	UGA_Taunt();
};

/**
 * «Полевая медицина» медика (GDD §7): 1 AP, 2 заряда за миссию. Цель — союзник
 * или сам медик в радиусе HealRange: живому +HealAmount HP, тяжело раненого
 * поднимает с ReviveHealth HP. Активируется событием Event.Heal с целью.
 */
UCLASS()
class XRU1_API UGA_Heal : public UTacticalAbility
{
	GENERATED_BODY()

public:
	UGA_Heal();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerData) override;

	/** Радиус применения (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Heal", meta = (ClampMin = "0"))
	float HealRange = 600.f;

	/** Лечение живого союзника (HP). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Heal", meta = (ClampMin = "0"))
	float HealAmount = 50.f;

	/** HP, с которыми поднимается тяжело раненый. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Heal", meta = (ClampMin = "1"))
	float ReviveHealth = 30.f;

	/** GE лечения (по умолчанию UGE_Heal с SetByCaller Data.Heal). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Heal")
	TSubclassOf<UGameplayEffect> HealEffect;

	/** Валидна ли цель для лечения прямо сейчас (для подсветки в HUD). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Heal")
	static bool CanHealTarget(const AUnitBase* Healer, const AActor* Target, float Range);

protected:
	/** BP-хук (анимация/VFX/звук лечения). bRevive — это был подъём раненого. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Heal")
	void OnHealApplied(AActor* Target, bool bRevive);
};

/**
 * «Рывок и удар» штурмовика (GDD §7): бесплатно, 1 раз за миссию. Немедленно
 * даёт +1 AP на этот ход — двойная дистанция И выстрел (Run & Gun).
 */
UCLASS()
class XRU1_API UGA_RunAndGun : public UTacticalAbility
{
	GENERATED_BODY()

public:
	UGA_RunAndGun();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerData) override;

	/** Сколько дополнительных AP выдаётся. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|RunAndGun", meta = (ClampMin = "1"))
	int32 ExtraActionPoints = 1;

protected:
	/** BP-хук (рывок-VFX/звук). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|RunAndGun")
	void OnRunAndGun();
};
