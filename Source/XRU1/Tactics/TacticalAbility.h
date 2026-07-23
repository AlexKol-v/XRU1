#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "TacticalAbility.generated.h"

class UActionPointsComponent;
class AUnitBase;

/**
 * Базовая способность тактического юнита. Привязывает экономику Action Points
 * к каноническому циклу GAS: стоимость в AP проверяется в CheckCost и
 * списывается в ApplyCost — то есть автоматически внутри CommitAbility(),
 * без ручных вызовов TrySpendActionPoint из игрового кода.
 *
 * Дополнительно запрещает активацию в чужую фазу хода (через TurnManager).
 * Все боевые способности юнитов (Overwatch, атака, классовые) наследуются отсюда.
 */
UCLASS(Abstract)
class XRU1_API UTacticalAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UTacticalAbility();

	/** Стоимость активации в очках действия (0 = бесплатная, напр. свободные действия). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cost", meta = (ClampMin = "0"))
	int32 ActionPointCost = 1;

	/**
	 * XCOM-правило «действие завершает активацию юнита»: после оплаты стоимости
	 * сжигается ВЕСЬ остаток AP (атака, overwatch, глухая оборона).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cost")
	bool bConsumesAllRemainingAP = false;

	/** Лимит применений за миссию (0 = без лимита). Сбрасывается при новом аватаре. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cost", meta = (ClampMin = "0"))
	int32 MaxUsesPerMission = 0;

	/** Нужна ли способности цель-актор (медик выбирает союзника кликом). Подсказка для контроллера. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Targeting")
	bool bRequiresTargetActor = false;

	/**
	 * Gameplay Event, которым контроллер активирует способность после выбора цели.
	 * Заполняется только у bRequiresTargetActor-способностей; контроллер не должен
	 * знать конкретный класс способности или хардкодить Event.Heal.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Targeting",
		meta = (EditCondition = "bRequiresTargetActor", EditConditionHides, Categories = "Event"))
	FGameplayTag TargetedActivationEventTag;

	/**
	 * Единый контракт проверки цели до закрытия targeting-режима. Финальная
	 * проверка всё равно повторяется внутри ActivateAbility перед CommitAbility.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Tactics|Targeting")
	bool IsValidTargetActor(AUnitBase* SourceUnit, AActor* TargetActor) const;

	/** Осталось применений в этой миссии (реплика UI; -1 = без лимита). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Cost")
	int32 GetUsesRemaining() const { return MaxUsesPerMission > 0 ? UsesRemaining : -1; }

	//~ UGameplayAbility: встраиваем AP в стандартный цикл стоимости.
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilitySpec& Spec) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	/** Компонент очков действия на аватаре способности (nullptr, если его нет). */
	UActionPointsComponent* FindActionPoints(const FGameplayAbilityActorInfo* ActorInfo) const;

	/** Остаток применений за миссию (валиден при MaxUsesPerMission > 0). */
	int32 UsesRemaining = 0;
};
