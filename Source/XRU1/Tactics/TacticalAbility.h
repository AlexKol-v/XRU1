#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/LatentActionManager.h" // FLatentActionInfo для latent-узла доворота
#include "TacticalAbility.generated.h"

class UActionPointsComponent;
class AUnitBase;
struct FTacticalFireActionContext;

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

	/**
	 * Имя способности для игрока («Полевая медицина», «Провокация», «Рывок»).
	 *
	 * ⚠️ Пусто быть не должно: на кнопке у ВСЕХ классов иначе стоит родовое
	 * «классовая способность», и игрок не знает, что нажимает — при том что
	 * лимиты и стоимость у способностей разные (найдено на прогоне 2026-08-03).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Витрина")
	FText DisplayName;

	/** Короткое описание для подсказки: что делает и чем ограничено. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Витрина", meta = (MultiLine = true))
	FText Description;

	/**
	 * Строка для подсказки на кнопке: имя, остаток применений и стоимость.
	 * Собирается здесь, потому что все три числа — свойства самой способности;
	 * HUD не должен знать, как из них складывается текст.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Витрина")
	FText GetTooltipText() const;

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

	/**
	 * Дальность выбора цели, см (0 — не показывать). HUD рисует круг этого
	 * радиуса вокруг бойца на время targeting-режима: игрок видит, кого может
	 * достать, ДО клика — как подсказка радиуса аптечки в XCOM.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Tactics|Targeting")
	float GetTargetingRange() const;
	virtual float GetTargetingRange_Implementation() const { return 0.f; }

	/** Осталось применений в этой миссии (реплика UI; -1 = без лимита). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Cost")
	int32 GetUsesRemaining() const { return MaxUsesPerMission > 0 ? UsesRemaining : -1; }

	/**
	 * ФАЗА ДОВОРОТА ПЕРЕД ВЫСТРЕЛОМ (latent). Плавно разворачивает стрелка лицом
	 * к цели замороженной транзакции `ActionId` и продолжает BP-ветку только
	 * после того, как угол сведён (или истёк `AimTurnMaxWait`).
	 *
	 * ⚠️ Заменяет прежний `FaceActorTowards` в BP_GA_Attack/BP_GA_Overwatch: тот
	 * доворачивал корпус ОДНИМ КАДРОМ между приходом на позицию и стартом
	 * стрелкового montage — отсюда «выбежал вбок, щёлкнул на цель, выстрелил».
	 *
	 * Гарантии против гонок:
	 * - действие идентифицируется тем же `ActionId`, что и вся транзакция;
	 * - если транзакция закрылась (abort/watchdog/EndAbility), пока шёл доворот,
	 *   latent завершается БЕЗ продолжения ветки: montage устаревшей транзакции
	 *   не запустится, а саму транзакцию уже закрыл C++;
	 * - повторный вызов того же узла, пока ожидание живо, игнорируется;
	 * - ожидание всегда ограничено `AimTurnMaxWait` — зависнуть нельзя.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Presentation",
		meta = (Latent, LatentInfo = "LatentInfo", DisplayName = "Face Shot Target (Latent)"))
	void FaceShotTargetLatent(FGuid ActionId, FLatentActionInfo LatentInfo);

	/**
	 * Скорость доворота к цели перед выстрелом (град/с). Заметно быстрее
	 * обычного `TurnInPlaceRate` юнита (120 °/с): доворот идёт ВНУТРИ паузы
	 * наводки камеры, и растягивать его на секунду незачем — на 420 °/с
	 * разворот на 180° занимает 0.43 с, на 90° — 0.21 с.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Presentation", meta = (ClampMin = "10"))
	float AimTurnRate = 420.f;

	/**
	 * Потолок ожидания доворота (сек). Страховка: если поворот кто-то перебил
	 * или он идёт дольше расчётного, montage всё равно стартует, и транзакция
	 * не доживает до watchdog'а.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Presentation", meta = (ClampMin = "0.05"))
	float AimTurnMaxWait = 1.2f;

	/**
	 * Довороты меньше этого угла (град) выполняются мгновенно — на 2–3° плавность
	 * не читается, а лишний кадр ожидания есть. НЕ путать с `TurnInPlaceMinAngle`
	 * юнита (25°): тот порог отвечает за выбор анимации доворота, а здесь речь
	 * только о скорости поворота корпуса.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Presentation", meta = (ClampMin = "0"))
	float AimTurnMinAngle = 3.f;

	/**
	 * МИКРОПАУЗА между окончанием доворота и стартом стрелкового montage (сек).
	 * Без неё выстрел склеивался с последним кадром движения — боец «стрелял на
	 * ходу»: тело ещё гасило инерцию перебежки/поворота, а montage уже шёл.
	 *
	 * Ставится только там, где ей есть что разделять: после реального доворота
	 * и после StepOut (боец только что прибежал на огневую точку). Выстрелу с
	 * места без доворота она не нужна — его уже отделяет
	 * `PreShotCameraSettleDelay`.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Presentation", meta = (ClampMin = "0"))
	float AimTurnSettleDelay = 0.25f;

	/**
	 * СТРЕЛЬБА ПОВЕРХ УКРЫТИЯ: сколько ждать подъёма из-за укрытия, прежде чем
	 * доворачивать корпус (сек). Порядок «встал → довернулся → выстрелил»
	 * читается лучше, чем «довернулся сидя → встал → выстрелил».
	 *
	 * Фактическая задержка ещё и ограничена сверху моментом `FireCommit` в
	 * montage: доворот обязан закончиться ДО выстрела, поэтому при большом угле
	 * он стартует раньше, а при нехватке времени ускоряется (до
	 * `AimTurnRateMax`). Только для стойки OverCover: у Open вставать не надо,
	 * а у StepOut доворот и так идёт после прибытия на огневую точку.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Presentation", meta = (ClampMin = "0"))
	float AimTurnRiseDelay = 0.35f;

	/** Потолок ускорения доворота, когда он не успевает до выстрела (град/с). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Presentation", meta = (ClampMin = "10"))
	float AimTurnRateMax = 900.f;

	/**
	 * СТРАХОВКА НА САМОМ ВЫСТРЕЛЕ: если к моменту `FireCommit` корпус всё ещё
	 * отвёрнут от цели больше чем на этот угол (град), он доворачивается
	 * мгновенно. Инвариант «никто не стреляет в спину» обязан держаться, даже
	 * когда плавный доворот перебили или цель успела сместиться.
	 *
	 * ⚠️ Раньше эту роль играл узел `Face Actor Towards` в BP — но он стоял
	 * ПЕРЕД montage и потому ломал единственную стойку, где доворот идёт уже
	 * во время анимации (OverCover): корпус щёлкал до подъёма, и «встал →
	 * довернулся → выстрелил» превращалось в «щёлкнул → встал → выстрелил».
	 * Страховка должна срабатывать на выстреле, а не до него.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tactics|Presentation", meta = (ClampMin = "0"))
	float AimSnapMaxError = 8.f;

	/** Довернуть мгновенно, если к моменту выстрела корпус отвёрнут от цели. */
	void EnsureFacingAtCommit(const FGuid& ActionId);

	/**
	 * Транзакция презентации `ActionId` всё ещё текущая. Единый guard для latent
	 * фаз: наследник отвечает своим контекстом (атака — FireAction, овервотч —
	 * ReactionAction), базовый класс контекста не имеет.
	 */
	bool IsPresentationActionCurrent(const FGuid& ActionId) const
	{
		return GetPresentationAction(ActionId) != nullptr;
	}

	/** Замороженная цель транзакции презентации (nullptr, если она уже закрыта). */
	const AActor* GetPresentationTarget(const FGuid& ActionId) const;

	/**
	 * Сколько ещё секунд кадру нужно доехать, прежде чем можно запускать montage
	 * (0 — можно уже сейчас). Позволяет НЕ задерживать само действие: боец
	 * выходит на позицию сразу по команде, а стрелковая анимация ждёт камеру.
	 */
	virtual float GetCameraSettleRemaining(const FGuid& ActionId) const { return 0.f; }

	/**
	 * Удержание кадра после выстрела для ЭТОЙ транзакции (сек). Latent-узел
	 * `Wait Shot Hold` ждёт его перед возвратом StepOut, чтобы боец не убегал
	 * назад раньше, чем игрок дочитал урон.
	 */
	virtual float GetPresentationHoldDelay(const FGuid& ActionId) const { return 0.f; }

	/** Пометить, что удержание кадра для транзакции уже отработано (не повторять). */
	virtual void MarkPresentationHoldDone(const FGuid& ActionId) {}

	/**
	 * Стрелковый montage стартует ПРЯМО СЕЙЧАС. От этого момента отсчитываются
	 * фазы, живущие внутри анимации: доворот во время подъёма из-за укрытия
	 * планировать раньше нельзя — пока montage ждал кадр, боец успевал
	 * довернуться сидя.
	 *
	 * `false` — запускать анимацию НЕЛЬЗЯ (транзакция закрыта или замороженное
	 * решение протухло); ветка презентации на этом обрывается, транзакция уже
	 * отменена. Это последняя точка, где отказ ещё не стоит игроку отыгранного
	 * впустую выстрела.
	 */
	bool NotifyPresentationMontageStarting(const FGuid& ActionId);

	/**
	 * Живо ли ещё замороженное решение выстрела (стрелок, цель, дальность, линия
	 * огня из замороженной точки). База не знает про конкретную транзакцию —
	 * отвечают наследники: атака и реакция наблюдения.
	 */
	virtual bool IsFrozenPresentationSolutionValid() const { return true; }

	/** Отменить текущую транзакцию презентации (наследник знает, какую именно). */
	virtual bool AbortPresentation(const FGuid& ActionId) { return false; }

	/**
	 * Отпустить бойца в укрытие. Зовётся терминалом презентации — то есть когда
	 * кадр уже отдержан: посадка совпадает с уходом камеры, а не следует сразу
	 * за выстрелом (см. AUnitBase::SetPresentationStanding).
	 */
	void ReleasePresentationStanding();

	/**
	 * ФАЗА УДЕРЖАНИЯ КАДРА (latent). Ждёт `GetPresentationHoldDelay` и только
	 * потом продолжает BP-ветку — туда, где StepOut возвращается в укрытие.
	 * Так возврат совпадает с уходом камеры, а не происходит поверх цифр урона.
	 * Guard'ы те же, что у доворота: чужой/закрытый ActionId ветку не продолжает.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Presentation",
		meta = (Latent, LatentInfo = "LatentInfo", DisplayName = "Wait Shot Hold (Latent)"))
	void WaitShotHoldLatent(FGuid ActionId, FLatentActionInfo LatentInfo);

	/**
	 * Запустить плавный доворот стрелка к цели транзакции прямо сейчас.
	 * Возвращает планируемую длительность (сек) или -1, если доворачивать нечего
	 * (уже смотрит на цель) либо некого (транзакция закрыта).
	 *
	 * Зовётся из двух мест и намеренно идемпотентен:
	 * 1) при старте транзакции — для стоек «стреляю с места» (Open/OverCover),
	 *    чтобы поворот шёл ВНУТРИ паузы наводки камеры и ничего не удлинял;
	 * 2) из latent-узла BP — для StepOut, где доворачиваться можно только после
	 *    прибытия на огневую точку (по дороге корпус ведёт path following).
	 */
	float StartAimTurnTowardsTarget(const FGuid& ActionId, const TCHAR* Reason,
		float RateOverride = 0.f);

	/** Момент `FireCommit` внутри montage (сек от начала); 0 — notify не найден. */
	static float FindFireCommitTime(const UAnimMontage* Montage);

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
	/** Отложенный доворот во время подъёма из укрытия (см. AimTurnRiseDelay). */
	FTimerHandle AimTurnRiseTimer;

	/** Боец, которого презентация держит на ногах (снимаем ровно у него). */
	TWeakObjectPtr<AUnitBase> StandingUnit;

	/** Колбэк отложенного доворота: проверяет актуальность транзакции сам. */
	void StartDelayedAimTurn(FGuid ActionId, float RateOverride);

	/**
	 * Запланировать доворот на время подъёма из укрытия так, чтобы он завершился
	 * до `FireCommit` в montage (см. AimTurnRiseDelay).
	 */
	void ScheduleAimTurnAfterRise(const FGuid& ActionId,
		const FTacticalFireActionContext& Action, AUnitBase* Shooter, const FString& ShooterName);

	/**
	 * Замороженный контекст презентации по `ActionId` или nullptr, если такой
	 * транзакции уже нет. Переопределяют способности, у которых она есть
	 * (`UGA_Attack`, `UGA_Overwatch`); базовый класс презентацией не владеет.
	 */
	virtual const FTacticalFireActionContext* GetPresentationAction(const FGuid& ActionId) const
	{
		return nullptr;
	}

	/** Компонент очков действия на аватаре способности (nullptr, если его нет). */
	UActionPointsComponent* FindActionPoints(const FGameplayAbilityActorInfo* ActorInfo) const;

	/** Остаток применений за миссию (валиден при MaxUsesPerMission > 0). */
	int32 UsesRemaining = 0;
};
