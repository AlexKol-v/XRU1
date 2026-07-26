#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h" // ключи трекинга движущихся целей без удержания ссылок
#include "Engine/EngineTypes.h" // FTimerHandle
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
 * ДВА ТРИГГЕРА (W1, 2026-07-25), второй — надмножество первого:
 *  1. `OnTargetPerceptionUpdated` — враг ВОШЁЛ в зону видимости (было и раньше);
 *  2. периодическая проверка ДВИЖУЩИХСЯ врагов (`ReactionCheckInterval`).
 *
 * ⚠️ Зачем понадобился второй. `OnTargetPerceptionUpdated` стреляет только на
 * СМЕНУ состояния восприятия. Враг, который уже видим в момент постановки
 * наблюдения, стимула больше не порождает — и перемещение у него на глазах
 * реакции не давало НИКОГДА. Для бота это означало, что овервотч не срабатывает
 * вообще: он видит отряд, встаёт в наблюдение, отряд ходит — тишина.
 *
 * Аналог тайловой дискретизации XCOM («прошёл клетку под прицелом»): реакция
 * проверяется раз в `ReactionCheckInterval` и только после того, как цель
 * отошла на `ReactionMinTravel` от точки, где начала текущее перемещение.
 * Факт движения берётся у ЕДИНОГО предиката `IsUnitInTransit` (статус path
 * following), а не у velocity — тормозящий после финиша боец уже стоит.
 *
 * ⚠️ Одна реакция на одно НЕПРЕРЫВНОЕ перемещение: цель, по которой отработали,
 * помечается и снова становится «свежей» только когда остановится.
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

	/**
	 * Как часто опрашивать движущихся врагов (сек). Не каждый кадр: предикат
	 * гоняет линию огня с выглядыванием по каждому врагу.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Overwatch", meta = (ClampMin = "0.02"))
	float ReactionCheckInterval = 0.1f;

	/**
	 * Сколько цель должна пройти от начала перемещения, чтобы это считалось
	 * «заметно шевельнулся» (см). Наш аналог клетки XCOM.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Overwatch", meta = (ClampMin = "0"))
	float ReactionMinTravel = 50.f;

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

	/**
	 * Периодический опрос: кто из врагов ДВИЖЕТСЯ у нас на глазах и уже прошёл
	 * `ReactionMinTravel`. Единственная точка, где рождается реакция на движение.
	 */
	void CheckMovingTargets();

	/**
	 * ЕДИНЫЙ вход в реакцию из обоих триггеров: проверяет фазу хода, лимит
	 * реакций, право на выстрел (`CanTargetActor`) и «по этому перемещению уже
	 * стреляли». Оба триггера обязаны идти сюда, иначе появление врага и его
	 * движение дадут два выстрела за один шаг.
	 */
	bool TryReactTo(AActor* Target);

	/** Реакционный выстрел по цели: бросок против укрытия + урон через GAS. */
	void FireReactionShot(AActor* Target);

	/** Находит AIPerceptionComponent у аватара способности (контроллер или пешка). */
	UAIPerceptionComponent* GetOwnerPerception() const;

	int32 ReactionShotsUsed = 0;

	UPROPERTY(Transient)
	TObjectPtr<UAIPerceptionComponent> BoundPerception;

	/** Таймер опроса движущихся целей (снимается в EndAbility). */
	FTimerHandle ReactionCheckTimer;

	/**
	 * Точка, с которой цель начала ТЕКУЩЕЕ непрерывное перемещение. Запись
	 * появляется, когда цель замечена в движении, и стирается, когда та встала.
	 * `TObjectKey` — ключ без удержания ссылки: способность живёт один ход, а
	 * цель за это время может погибнуть.
	 */
	TMap<TObjectKey<AActor>, FVector> MoveStartLocations;

	/** По кому уже отработали в ТЕКУЩЕМ его перемещении. */
	TSet<TObjectKey<AActor>> ReactedThisMove;
};
