#pragma once

#include "CoreMinimal.h"
#include "TacticalAbility.h"
#include "TacticsTypes.h"
#include "GA_Overwatch.generated.h"

class UAIPerceptionComponent;
class UGameplayEffect;
struct FAIStimulus;

/**
 * Способность «Наблюдение» (Overwatch). Активируется в свою фазу хода за 1 AP
 * (кост списывает UTacticalAbility через CommitAbility): юнит встаёт в режим
 * ожидания и, пока ходит противник, автоматически стреляет по первому врагу,
 * вошедшему в его зону видимости (AIPerception Sight). Право на выстрел даёт
 * ОБЩИЙ предикат UGA_Attack::CanTargetActor (враждебность, живость, дальность
 * оружия, линия огня) — тот же, что у выстрела игрока и AI: радиус перцепции
 * шире дальности стрельбы, реагировать на недостижимые цели нельзя. Расчёт
 * выстрела — UTacticsCombatStatics::ResolveShot (укрытие цели учитывается там).
 *
 * Пока способность активна, на юните висит тег State.Overwatch
 * (ActivationOwnedTags); он же блокирует повторную активацию. Способность
 * завершается сама: после израсходования реакций или когда ход возвращается
 * стороне юнита.
 *
 * Ограничение каркаса: реакция срабатывает на ВХОД врага в зону видимости;
 * перемещение врага целиком внутри зоны повторного стимула не даёт.
 */
UCLASS()
class XRU1_API UGA_Overwatch : public UTacticalAbility
{
	GENERATED_BODY()

public:
	UGA_Overwatch();

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

	/** Сколько реакционных выстрелов разрешено за один ход врага (обычно 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Overwatch", meta = (ClampMin = "1"))
	int32 MaxReactionShots = 1;

	/**
	 * Штраф к точности реакционного выстрела (GDD §5.4: −10). Точность и урон
	 * берутся со статов юнита (AUnitBase::BaseAim / ShotDamage).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Overwatch", meta = (ClampMin = "0"))
	float ReactionAimPenalty = 10.f;

	/** GE урона (по умолчанию UGE_ShotDamage с SetByCaller Data.Damage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Overwatch")
	TSubclassOf<UGameplayEffect> DamageEffect;

protected:
	/** BP-хук для VFX/звука/анимации реакционного выстрела. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Overwatch")
	void OnReactionShot(AActor* Target, bool bHit);

	/** Колбэк AIPerception: враг замечен -> пробуем реакционный выстрел. */
	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Колбэк TurnManager'а: ход вернулся нашей стороне -> Overwatch снимается. */
	UFUNCTION()
	void HandleTurnStarted(ETurnPhase Phase);

	/** Колбэк TurnManager'а: бой кончился -> Overwatch снимается. */
	UFUNCTION()
	void HandleCombatEnded(bool bPlayerWon);

	/** Реакционный выстрел по цели: бросок против укрытия + урон через GAS. */
	void FireReactionShot(AActor* Target);

	/** Находит AIPerceptionComponent у аватара способности (контроллер или пешка). */
	UAIPerceptionComponent* GetOwnerPerception() const;

	int32 ReactionShotsUsed = 0;

	UPROPERTY(Transient)
	TObjectPtr<UAIPerceptionComponent> BoundPerception;
};
