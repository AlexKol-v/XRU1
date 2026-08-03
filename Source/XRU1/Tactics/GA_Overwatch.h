#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h" // ключи трекинга движущихся целей без удержания ссылок
#include "Engine/EngineTypes.h" // FTimerHandle
#include "TacticalAbility.h"
#include "TacticalFireActionContext.h"
#include "TacticsTypes.h"
#include "GA_Overwatch.generated.h"

class UAIPerceptionComponent;
class UGameplayEffect;
class UAnimMontage;
class AUnitBase;
struct FAIStimulus;

/**
 * Способность «Наблюдение» (Overwatch). Активируется в свою фазу хода за 1 AP
 * (кост списывает UTacticalAbility через CommitAbility): юнит встаёт в режим
 * ожидания и, пока ходит противник, автоматически стреляет по первому врагу,
 * вошедшему в его зону видимости (AIPerception Sight). Право на выстрел даёт
 * ОБЩИЙ предикат UGA_Attack::CanTargetActor (враждебность, живость, дальность
 * оружия, линия огня) — тот же, что у выстрела игрока и AI: радиус перцепции
	 * шире дальности стрельбы, реагировать на недостижимые цели нельзя. Необратимая
	 * механика выполняется только `FireCommit` из montage notify; укрытие цели уже
	 * учтено в замороженном шансе reaction-action.
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

	/**
	 * Пауза между наводкой камеры реакции и стартом стрелковой анимации (сек).
	 * Реакция — внезапное событие: цель уже остановлена (mover на паузе), кадр
	 * должен доехать, глаз — зафиксировать сцену; потом выстрел. Паузы ПОСЛЕ
	 * выстрела не добавляем (урок мода Stop Wasting My Time для XCOM 2).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Overwatch", meta = (ClampMin = "0"))
	float PreReactionCameraSettleDelay = 0.9f;

	/**
	 * Удержание кадра ПОСЛЕ реакционного выстрела (сек): цель всё это время
	 * остаётся замершей (mover на паузе), урон читается — потом ход врага
	 * продолжается. Тюнится в BP_GA_Overwatch.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Overwatch", meta = (ClampMin = "0"))
	float PostReactionHoldDelay = 0.9f;

	/**
	 * Удержание кадра, когда реакция УБИЛА цель (сек) — симметрично
	 * `PostKillHoldDelay` у атаки: смерть это анимация падения, и уходить с
	 * кадра раньше неё нельзя.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Overwatch", meta = (ClampMin = "0"))
	float PostReactionKillHoldDelay = 1.8f;

	/** GE урона (по умолчанию UGE_ShotDamage с SetByCaller Data.Damage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Overwatch")
	TSubclassOf<UGameplayEffect> DamageEffect;

	/** Аварийный timeout зависшего reaction montage/coordinator. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Overwatch", meta = (ClampMin = "1"))
	float ReactionActionTimeout = 10.f;

	/** Активна только вложенная reaction-транзакция, не parent Overwatch целиком. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Overwatch|Action")
	bool IsReactionActionInProgress() const { return ReactionAction.IsActive(); }

	/** ActionId текущей реакции; invalid guid между реакциями. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Overwatch|Action")
	FGuid GetActiveReactionActionId() const { return ReactionAction.ActionId; }

	/** Guard для любого latent BP callback: false для callback предыдущей реакции. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Overwatch|Action")
	bool IsReactionActionCurrent(const FGuid& ActionId) const
	{
		return ReactionAction.Matches(ActionId);
	}

	/** Immutable montage/stance/root plan одной reaction-action. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false,  Category = "Tactics|Overwatch|Action")
	UAnimMontage* GetReactionActionPresentation(const FGuid& ActionId,
		EFiringStance& OutStance, FVector& OutHomeRootLocation,
		FVector& OutPresentationRootLocation) const;

	/** Query для общего controller/AI barrier; parent Overwatch здесь не считается. */
	static bool GetReactionActionInProgressFor(const AUnitBase* Unit, FGuid& OutActionId);

	/** C++ guard для native notify: навсегда связывает reaction ActionId с одним montage instance. */
	bool AcceptFireCommitMontageInstance(const FGuid& ActionId, int32 MontageInstanceId);

	/** Guarded mechanics commit из FireCommit notify reaction montage. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Overwatch|Action")
	bool FireCommit(const FGuid& ActionId, bool& bOutHit);

	/** Montage/return завершены; только теперь снимается пауза mover и слот очереди. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Overwatch|Action")
	bool CompleteReactionAction(const FGuid& ActionId);

	/** Реакция сорвана; лимит увеличивается только если FireCommit уже состоялся. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Overwatch|Action")
	bool AbortReactionAction(const FGuid& ActionId);

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

	/** Удержание кадра реакции: убитая цель держится дольше живой. */
	virtual float GetPresentationHoldDelay(const FGuid& ActionId) const override;

	/** Hold отработан latent-фазой возврата — терминал его повторять не должен. */
	virtual void MarkPresentationHoldDone(const FGuid& ActionId) override
	{
		if (ReactionAction.Matches(ActionId))
		{
			ReactionPostHoldDoneId = ActionId;
		}
	}

protected:
	/** Контекст для общих latent-фаз презентации (доворот перед выстрелом). */
	virtual const FTacticalFireActionContext* GetPresentationAction(
		const FGuid& ActionId) const override
	{
		return ReactionAction.Matches(ActionId) ? &ReactionAction : nullptr;
	}

	/** Новый pre-presentation вход одной вложенной reaction-транзакции. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Overwatch|Action")
	void OnReactionActionStarted(AActor* Target, FGuid ActionId);

	/** Post-commit BP-хук для VFX/звука; второй montage отсюда не запускать. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Overwatch")
	void OnReactionShot(AActor* Target, bool bHit);

	/** Terminal-хук очистки presentation state одной реакции. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Overwatch|Action")
	void OnReactionActionTerminated(FGuid ActionId, bool bShotCommitted, bool bAborted);

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

	/** Создать reaction snapshot, занять очередь и приостановить движущуюся цель. */
	bool BeginReactionAction(AActor* Target);

	/** Находит AIPerceptionComponent у аватара способности (контроллер или пешка). */
	UAIPerceptionComponent* GetOwnerPerception() const;

	int32 ReactionShotsUsed = 0;

	UPROPERTY(Transient)
	TObjectPtr<UAIPerceptionComponent> BoundPerception;

	/** Таймер опроса движущихся целей (снимается в EndAbility). */
	FTimerHandle ReactionCheckTimer;

	/** Watchdog вложенной реакции; не штатное окончание кадра/движения. */
	FTimerHandle ReactionActionWatchdogTimer;

	/** Отложенный старт презентации реакции (см. PreReactionCameraSettleDelay). */
	FTimerHandle ReactionPresentationDelayTimer;

	/** Удержание кадра после реакции (см. PostReactionHoldDelay). */
	FTimerHandle ReactionPostHoldTimer;

	/** Реакция, чей post-hold уже отработан. */
	FGuid ReactionPostHoldDoneId;

	void StartReactionPresentation(FGuid ActionId);
	void FinishReactionPostHold(FGuid ActionId);

	/** Движущаяся цель, которую эта реакция обязана отпустить ровно один раз. */
	TWeakObjectPtr<AActor> PausedReactionMover;

	/** Snapshot/guards только текущей вложенной реакции. */
	FTacticalFireActionContext ReactionAction;

	/**
	 * Точка, с которой цель начала ТЕКУЩЕЕ непрерывное перемещение. Запись
	 * появляется, когда цель замечена в движении, и стирается, когда та встала.
	 * `TObjectKey` — ключ без удержания ссылки: способность живёт один ход, а
	 * цель за это время может погибнуть.
	 */
	TMap<TObjectKey<AActor>, FVector> MoveStartLocations;

	/**
	 * По кому уже отработали. v2.9: очищается ТОЛЬКО на границе фазы
	 * (Activate/HandleTurnStarted), а не «когда цель встала»: пауза мовера
	 * реакцией выглядела как остановка, метка стиралась, и после аборта
	 * сорванного монтажа шла ВТОРАЯ реакция по той же цели — уже без форса
	 * (промах 54.9% в логе 2026-08-02). Одна реакция на цель за фазу.
	 */
	TSet<TObjectKey<AActor>> ReactedThisMove;

	/** Форс, потреблённый текущей реакцией: abort без commit возвращает его юниту. */
	FScriptedShotOverride ConsumedScriptedShot;
	bool bConsumedScriptedShotValid = false;

	void HandleReactionActionTimeout(FGuid ActionId);
	void ClearReactionActionWatchdog();
	void ResumeReactionMover();
	bool IsFrozenReactionCommitValid() const;
	void EndReactionPresentation() const;
	void StopReactionMontage(const FTacticalFireActionContext& FinishedAction) const;
	void CheckCombatOutcomeAfterReaction() const;
};
