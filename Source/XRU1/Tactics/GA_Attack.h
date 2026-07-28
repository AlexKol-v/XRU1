#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "TacticalAbility.h"
#include "TacticalFireActionContext.h"
#include "TacticsTypes.h"
#include "GA_Attack.generated.h"

class UGameplayEffect;
class UAnimMontage;
class AUnitBase;

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

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** Штраф к точности при стрельбе через Squadsight (без собственной LOS). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Attack", meta = (ClampMin = "0"))
	float SquadsightAimPenalty = 10.f;

	/** GE урона (по умолчанию UGE_ShotDamage с SetByCaller Data.Damage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Attack")
	TSubclassOf<UGameplayEffect> DamageEffect;

	/** Аварийный timeout зависшего BP/montage; штатно действие закрывает callback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Attack", meta = (ClampMin = "1"))
	float FireActionTimeout = 10.f;

	/** Активна ли транзакция обычной атаки между reservation и terminal callback. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Attack|Action")
	bool IsFireActionInProgress() const { return FireAction.IsActive(); }

	/** Идентификатор текущей транзакции; invalid guid, если атака не активна. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Attack|Action")
	FGuid GetActiveFireActionId() const { return FireAction.ActionId; }

	/** Guard для любого latent BP callback: false для callback предыдущего ActionId. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Attack|Action")
	bool IsFireActionCurrent(const FGuid& ActionId) const { return FireAction.Matches(ActionId); }

	/**
	 * Неизменяемый план presentation для ActionId. Возвращает nullptr для stale
	 * callback. Home/Presentation — позиции ROOT капсулы, не точки глаз.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Tactics|Attack")
	UAnimMontage* GetFireActionPresentation(const FGuid& ActionId,
		EFiringStance& OutStance, FVector& OutHomeRootLocation,
		FVector& OutPresentationRootLocation) const;

	/**
	 * Query для controller/AI barrier без дублирования состояния в UnitBase.
	 * Проверяет только обычную атаку, а не долгоживущий parent Overwatch.
	 */
	static bool GetAttackActionInProgressFor(const AUnitBase* Unit, FGuid& OutActionId);

	/** C++ guard для native notify: навсегда связывает ActionId с одним montage instance. */
	bool AcceptFireCommitMontageInstance(const FGuid& ActionId, int32 MontageInstanceId);

	/**
	 * Единственная точка механического выстрела. Её вызывает FireCommit notify
	 * активного montage/coordinator; stale или повторный ActionId отклоняется.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Attack|Action")
	bool FireCommit(const FGuid& ActionId, bool& bOutHit);

	/** Presentation, включая ReturnToAnchor, штатно завершён. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Attack|Action")
	bool CompleteFireAction(const FGuid& ActionId);

	/** Presentation прерван/не смог построить route; до commit AP возвращаются. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Attack|Action")
	bool AbortFireAction(const FGuid& ActionId);

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
	/**
	 * Новый вход presentation. BP обязан выбрать montage/StepOut по уже
	 * зафиксированной цели и затем вызвать FireCommit(ActionId) из notify.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Attack|Action")
	void OnFireActionStarted(AActor* Target, FGuid ActionId);

	/** Post-commit BP-хук для VFX/звука; запускать из него второй montage нельзя. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Attack")
	void OnShotFired(AActor* Target, bool bHit);

	/** Terminal-хук для очистки BP presentation state. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Attack|Action")
	void OnFireActionTerminated(FGuid ActionId, bool bShotCommitted, bool bAborted);

private:
	/** Замороженный snapshot и guards текущей атаки. */
	FTacticalFireActionContext FireAction;

	/** Watchdog, не штатный сигнал окончания montage. */
	FTimerHandle FireActionWatchdogTimer;

	void HandleFireActionTimeout(FGuid ActionId);
	void ClearFireActionWatchdog();
	void RefundPreCommitActionPoints(const FTacticalFireActionContext& FinishedAction);
	bool IsFrozenFireCommitValid() const;
	void NotifyShotPresentation(AActor* Shooter, AActor* Target) const;
	void EndShotPresentation() const;
	void StopFireActionMontage(const FTacticalFireActionContext& FinishedAction) const;
	void CheckCombatOutcomeAfterAction() const;
};
