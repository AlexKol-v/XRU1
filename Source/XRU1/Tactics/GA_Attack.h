#pragma once

#include "CoreMinimal.h"
#include "TacticalAbility.h"
#include "TacticsTypes.h"
#include "GA_Attack.generated.h"

class UGameplayEffect;

/**
 * Способность «Выстрел». Активируется GameplayEvent'ом Event.Attack
 * (payload.Target = цель) — событие шлёт ATacticalPlayerController по клику
 * на врага (или скрипт туториала).
 *
 * Правила (GDD §5.4): точность/урон/дальность — со статов юнита (AUnitBase);
 * нужна линия видимости ИЛИ Squadsight (пассивка снайпера: цель видит любой
 * союзник, штраф −10); стоит 1 AP и по XCOM-правилу сжигает весь остаток AP.
 */
UCLASS()
class XRU1_API UGA_Attack : public UTacticalAbility
{
	GENERATED_BODY()

public:
	UGA_Attack();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerData) override;

	/** Штраф к точности при стрельбе через Squadsight (без собственной LOS). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Attack", meta = (ClampMin = "0"))
	float SquadsightAimPenalty = 10.f;

	/** GE урона (по умолчанию UGE_ShotDamage с SetByCaller Data.Damage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Attack")
	TSubclassOf<UGameplayEffect> DamageEffect;

	/**
	 * Почему нельзя (или можно — Valid) выстрелить по цели прямо сейчас
	 * (враждебность/жива/дальность/LOS-или-Squadsight, без учёта AP).
	 * ЕДИНСТВЕННЫЙ источник причины для HUD — различает «слишком далеко» и
	 * «нет линии огня» (раньше обе схлопывались в один bool).
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Attack")
	static EAttackTargetStatus GetTargetStatus(const AUnitBase* Shooter, const AActor* Target);

	/**
	 * Может ли юнит выстрелить по цели прямо сейчас (дальность/LOS/Squadsight,
	 * без учёта AP). Для подсветки целей и серых кнопок в HUD.
	 * Тонкая обёртка над GetTargetStatus — для мест, где причина не нужна.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Attack")
	static bool CanTargetActor(const AUnitBase* Shooter, const AActor* Target);

	/** Есть ли у юнита хотя бы одна цель, по которой он может стрелять прямо сейчас. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Attack")
	static bool HasAnyValidTarget(const AUnitBase* Shooter);

	/**
	 * Итоговый шанс выстрела юнита по цели, % (или -1 — стрелять нельзя).
	 * ЕДИНСТВЕННЫЙ источник прогноза для HUD: считает ровно то, что сделает
	 * ActivateAbility — BaseAim, штраф Squadsight (из CDO AttackAbilityClass
	 * стрелка), укрытие цели против стрелка и глухую оборону.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Attack")
	static float ComputeAttackHitChance(const AUnitBase* Shooter, const AActor* Target);

	/** Точность юнита по цели до укрытия: BaseAim минус штраф Squadsight (если нет своей LOS). */
	static float ComputeEffectiveAim(const AUnitBase* Shooter, const AActor* Target);

protected:
	/** BP-хук для анимации/VFX/звука выстрела. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Attack")
	void OnShotFired(AActor* Target, bool bHit);
};
