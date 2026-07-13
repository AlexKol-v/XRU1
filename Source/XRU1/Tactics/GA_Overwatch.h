#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Overwatch.generated.h"

class UAIPerceptionComponent;
struct FAIStimulus;

/**
 * Способность «Наблюдение» (Overwatch). Активируется в конце хода игрока: юнит
 * встаёт в режим ожидания и, пока идёт ход врага, автоматически стреляет по
 * первому противнику, попавшему в его зону видимости (AIPerception Sight).
 *
 * Каркас: жизненный цикл способности + подписка на perception заложены;
 * сам reaction-shot (расчёт попадания с учётом укрытия, трата реакции) —
 * точка расширения FireReactionShot().
 */
UCLASS()
class XRU1_API UGA_Overwatch : public UGameplayAbility
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Overwatch")
	int32 MaxReactionShots = 1;

protected:
	/** Колбэк AIPerception: враг замечен -> пробуем реакционный выстрел. */
	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Реакционный выстрел по цели. Точка расширения боевой логики. */
	void FireReactionShot(AActor* Target);

	/** Находит AIPerceptionComponent у аватара способности. */
	UAIPerceptionComponent* GetOwnerPerception() const;

	int32 ReactionShotsUsed = 0;

	UPROPERTY(Transient)
	TObjectPtr<UAIPerceptionComponent> BoundPerception;
};
