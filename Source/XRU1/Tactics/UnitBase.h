#pragma once

#include "CoreMinimal.h"
#include "TDCombatant.h"
#include "TacticsTypes.h"
#include "CoverTypes.h"       // EFiringStance для выбора монтажа выстрела
#include "UnitVisualState.h"  // единая точка состояния для ABP (S2)
#include "UnitBase.generated.h"

class UActionPointsComponent;
class UAnimMontage;
class UCoverDetectionComponent;
class UCurveFloat;
class UDecalComponent;
class UNavigationInvokerComponent;
class UGameplayAbility;
class UTacticalAbility;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUnitStateChanged);

/**
 * Базовый тактический юнит. Наследует GAS-иерархию донора (ATDCombatant: ASC,
 * UTDAttributeSet, HUD над головой) и добавляет пошаговый слой: Action Points,
 * детекцию укрытий, боевые статы (aim/урон/дальности из GDD §7), общий набор
 * способностей и жизненный цикл смерти/тяжёлого ранения/эвакуации.
 *
 * Blueprintable — из него сделаны 4 класса ростера и враг-мародёр.
 */
UCLASS(Blueprintable)
class XRU1_API AUnitBase : public ATDCombatant
{
	GENERATED_BODY()

public:
	AUnitBase();

	// --- Компоненты и роль ---------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Tactics|Unit")
	EUnitRole GetUnitRole() const { return UnitRole; }

	/** Позывной для HUD (портреты, панель цели): «Шприц», «Оса», «Клин», «Молот»… */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Unit")
	FText UnitDisplayName;

	UFUNCTION(BlueprintPure, Category = "Tactics|Unit")
	UActionPointsComponent* GetActionPoints() const { return ActionPoints; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Unit")
	UCoverDetectionComponent* GetCoverDetection() const { return CoverDetection; }

	// --- Боевые статы (GDD §7/§10; правятся в BP-наследниках) ----------------

	/** Максимальное и стартовое здоровье класса. Применяется до BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Stats", meta = (ClampMin = "1"))
	float BaseMaxHealth = 100.f;

	/** Базовый шанс попадания до модификаторов укрытия, %. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Stats", meta = (ClampMin = "0", ClampMax = "100"))
	float BaseAim = 75.f;

	/**
	 * Профиль оружия: модификатор точности от дистанции до цели (X = см,
	 * Y = ±aim). Пусто — встроенный профиль винтовки (+10 в упор → −15 вдали,
	 * см. UTacticsCombatStatics::GetAimDistanceModifier). Дробовику — крутой
	 * бонус вблизи и большой штраф вдали, снайперке — наоборот (XCOM 2).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Stats")
	TObjectPtr<UCurveFloat> AimByDistanceCurve;

	/** Урон выстрела (HP). Разброс ±10% применяется при выстреле. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Stats", meta = (ClampMin = "0"))
	float ShotDamage = 25.f;

	/**
	 * Дальность стрельбы (см). В настоящем XCOM 2 у оружия нет жёсткого предела
	 * дальности — стрелять можно куда угодно в пределах LOS, дистанция влияет
	 * только на точность у part оружия (что мы сознательно не моделируем, см.
	 * GDD §5.4). Поэтому значение здесь — щедрый technical cap, заведомо больше
	 * всех радиусов зрения/тревоги в бою (SquadVisionRange=2500,
	 * AI SightRadius=1400..1600, TauntPriorityRadius=1200), чтобы реальным
	 * ограничителем почти всегда была линия видимости, а не число дальности.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Stats", meta = (ClampMin = "0"))
	float AttackRange = 3000.f;

	/** Длина пути (см по навмешу), проходимая за 1 AP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Stats", meta = (ClampMin = "0"))
	float MoveRange = 800.f;

	/**
	 * ИДЕАЛЬНАЯ БОЕВАЯ ДИСТАНЦИЯ (см) — где юниту хочется находиться. Читает
	 * только AI (скоринг позиции, фаза A4); на выстрел не влияет.
	 *
	 * Зачем отдельное поле, а не `AttackRange`: тот — щедрый технический cap
	 * (3000), реальным ограничителем должна быть линия видимости. Использовать
	 * его как «комфортную дистанцию» бессмысленно — почти вся карта оказывается
	 * «в комфорте», и у бота нет никакого стимула держать позицию. Ровно поэтому
	 * враги сближались вплотную.
	 *
	 * ⚠️ И НЕ выводим идеал из `AimByDistanceCurve`, хотя это выглядело
	 * логичным: у встроенного профиля винтовки максимум точности — в упор
	 * (+10), то есть «идеалом» оказался бы нулевой зазор. Дистанция боя —
	 * решение дизайнера, а не побочный эффект кривой оружия.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Stats", meta = (ClampMin = "0"))
	float IdealCombatRange = 900.f;

	/** Пассивка снайпера: может стрелять по целям, которые видит любой союзник. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Stats")
	bool bHasSquadsight = false;

	/** При 0 HP юнит не умирает, а падает тяжело раненым (юниты игрока). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Stats")
	bool bCanBeDowned = false;

	// --- Общий набор способностей (GDD §6; классы задаются в BP) -------------

	/** Атака (GA_Attack или BP-наследник). Активируется событием Event.Attack. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Abilities")
	TSubclassOf<UTacticalAbility> AttackAbilityClass;

	/** Overwatch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Abilities")
	TSubclassOf<UTacticalAbility> OverwatchAbilityClass;

	/** Глухая оборона. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Abilities")
	TSubclassOf<UTacticalAbility> HunkerAbilityClass;

	/** Уникальная способность класса (лечение/рывок/провокация; у снайпера пусто — пассивка). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Abilities")
	TSubclassOf<UTacticalAbility> ClassAbilityClass;

	/** Дополнительные способности (выдаются вместе с основными). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> ClassAbilities;

	// --- Патруль (мирное состояние AI; точки расставляются на карте) ---------

	/** Маршрут патруля этого юнита (TargetPoint'ы уровня). Пусто = стоит на месте. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tactics|AI")
	TArray<TObjectPtr<AActor>> PatrolPoints;

	// --- Состояние: смерть / тяжёлое ранение / эвакуация ---------------------

	UFUNCTION(BlueprintPure, Category = "Tactics|State")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintPure, Category = "Tactics|State")
	bool IsDowned() const { return bIsDowned; }

	UFUNCTION(BlueprintPure, Category = "Tactics|State")
	bool IsEvacuated() const { return bIsEvacuated; }

	/**
	 * Форс тяжёлого ранения (true) или подъём (false, HP = ReviveHealth).
	 * Используется медиком и скриптами туториала.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|State")
	void SetDowned(bool bNewDowned, float ReviveHealth = 30.f);

	/** Поднимает тяжело раненого с указанным HP (обёртка для медика). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|State")
	void ReviveFromDowned(float ReviveHealth = 30.f) { SetDowned(false, ReviveHealth); }

	/** Эвакуация: юнит покидает поле боя (скрывается, выбывает из очереди). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|State")
	void Evacuate();

	/** Смена состояния (смерть/ранение/подъём/эвакуация) — для HUD. */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|State")
	FOnUnitStateChanged OnUnitStateChanged;

	/**
	 * Сообщает HUD, что изменился любой отображаемый статус юнита: жизненное
	 * состояние, стойка GAS, Overwatch или движение. Сами системы меняют
	 * состояние первыми и вызывают этот метод только после фактического изменения.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|State")
	void NotifyUnitStateChanged();

	/**
	 * ЕДИНЫЙ СРЕЗ СОСТОЯНИЯ ДЛЯ АНИМАЦИЙ (фаза S2). Anim Blueprint читает ЭТО
	 * одним узлом вместо опроса шести источников (теги ASC, флаги смерти,
	 * компонент укрытий, статус path following).
	 *
	 * Пересобирается в `NotifyUnitStateChanged` — то есть ровно тогда, когда
	 * что-то изменилось, а не каждый кадр. `bMoving` — единственное поле,
	 * которое ABP имеет право уточнять сам по скорости, если нужна плавность.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Visual")
	const FUnitVisualState& GetVisualState() const { return VisualState; }

	/**
	 * Какой МОНТАЖ выстрела играть по цели прямо сейчас: стойка берётся из
	 * `UTacticsCombatStatics::GetFiringStance`, а конкретный ассет — из
	 * `FireMontage*` этого юнита. BP-хук `OnShotFired` зовёт это и играет
	 * результат, вместо того чтобы разбирать стойку у себя.
	 *
	 * OutFiringEyeLocation — точка, ОТКУДА реально идёт выстрел (для VFX дула и
	 * для визуального сдвига при `StepOut`). ⚠️ Сдвиг только визуальный, с
	 * возвратом: позиция юнита, его укрытие и диск занятости не меняются.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Visual")
	UAnimMontage* GetFireMontageFor(const AActor* Target, EFiringStance& OutStance,
		FVector& OutFiringEyeLocation) const;

	/**
	 * ПРИЖАТЬСЯ К УКРЫТИЮ: развернуться лицом к стене по её нормали и подшагнуть
	 * к ней вплотную. Зовётся по прибытии на позицию.
	 *
	 * Поза за укрытием читается САМА, если боец стоит вплотную и лицом к стене, —
	 * поэтому отдельной анимации «вжаться» не нужно.
	 *
	 * ⚠️ И подшаг, и разворот идут ПЛАВНО (не телепортом): движение выполняет
	 * `CharacterMovement` со скоростью `CoverHugStepSpeed`, поэтому у юнита есть
	 * реальная `Velocity` и локомоция ABP сама играет шаг. На время подшага
	 * `VisualState.bMoving` = true; по завершении укрытие пересчитывается от
	 * новой точки и поза переключается на укрытие.
	 *
	 * ⚠️ Подшаг НЕ меняет тактическую клетку: сдвиг ограничен `CoverHugMaxNudge`,
	 * диск занятости и укрытие считаются от новой позиции тем же кодом (сдвиг мал
	 * и остаётся внутри своей клетки).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Visual")
	void HugCover();

	/**
	 * ПЛАВНЫЙ ДОВОРОТ НА МЕСТЕ лицом к точке (только yaw, позиция не меняется).
	 *
	 * `bPlayTurnAnimation = true` — полная дельта кладётся в
	 * `VisualState.PendingTurnYaw`, и ABP играет анимацию доворота
	 * (`Turn_045/090/135/180`, сторона по знаку). Так зовётся наводка на цель:
	 * игрок видит, как боец разворачивается, а к моменту выстрела угол сведён.
	 *
	 * ⚠️ `bPlayTurnAnimation = false` — поворот идёт, но АНИМАЦИИ доворота нет.
	 * Так разворачивается прижимание к укрытию (`HugCover`): там юнит уже играет
	 * подшаг и вход в позу укрытия, а наложенный сверху разворот на 180° выглядит
	 * как вывернутое назад тело.
	 *
	 * Мелкие довороты (меньше `TurnInPlaceMinAngle`) выполняются мгновенно —
	 * анимации на них всё равно не хватит.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Visual")
	void FaceTowardsSmooth(const FVector& TargetLocation, bool bPlayTurnAnimation = true,
		float TurnRateOverride = 0.f);

	/** Идёт ли сейчас плавный доворот на месте (для BP-логики и отладки). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Visual")
	bool IsTurningInPlace() const { return bTurningInPlace; }

	/** Максимальный подтяг к стене при `HugCover` (см). 0 — только разворот. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "0"))
	float CoverHugMaxNudge = 45.f;

	/** Зазор между капсулой и стеной при прижимании (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "0"))
	float CoverHugClearance = 6.f;

	/**
	 * Скорость подшага к стене (см/с). Совпадает со скоростью ходьбы в
	 * `BS_Rifle_Locomotion` (сэмплы Walk на ~150), чтобы играл шаг, а не бег.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "10"))
	float CoverHugStepSpeed = 150.f;

	/**
	 * Скорость доворота на месте (град/с). Подбирается под длительность клипов
	 * `M_Neutral_Crouch_Idle_Turn_*`: при 120 °/с поворот на 90° занимает 0.75 с.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "10"))
	float TurnInPlaceRate = 120.f;

	/** Довороты меньше этого угла (град) делаются мгновенно, без анимации. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "0"))
	float TurnInPlaceMinAngle = 25.f;

	/** Насколько близко к цели подшага считать, что дошли (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "0.5"))
	float CoverHugArriveTolerance = 3.f;

	/**
	 * Скорость разворота к стене при прижимании (град/с). Быстрее обычного
	 * доворота: он идёт вместе с подшагом и без анимации, растягивать его незачем.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "10"))
	float CoverHugTurnRate = 540.f;

	/**
	 * Куда юнит повёрнут, прижавшись к укрытию: `true` — ЛИЦОМ к стене,
	 * `false` — СПИНОЙ (лицом от стены).
	 *
	 * По умолчанию `true` — как в XCOM: укрытие стоит МЕЖДУ бойцом и врагом,
	 * значит смотреть боец должен на укрытие (и на врага за ним), а не от него.
	 * Разворот спиной читается как «убегает» — особенно сразу после перебежки.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual")
	bool bCoverHugFaceWall = true;

	/**
	 * Доворачивать ли КОРПУС при выглядывании (в дополнение к анимации).
	 *
	 * ⚠️ `false` — потому что `anim_CoverDown_Look_*` уже содержит поворот к краю.
	 * Складывать его с доворотом актора нельзя: получается двойной угол и боец
	 * стоит отвёрнутым от укрытия.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual")
	bool bTurnBodyOnPeek = false;

	/** Через сколько секунд ожидания в укрытии юнит выглядывает (мин/макс). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "0.5"))
	float CoverPeekDelayMin = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "0.5"))
	float CoverPeekDelayMax = 8.f;

	/**
	 * Сколько длится одно выглядывание (с). Должно быть НЕ МЕНЬШЕ длины клипа
	 * `anim_CoverDown_Look_*` плюс два blend-времени перехода, иначе `bShouldPeek`
	 * гаснет раньше, чем клип доиграл, и выглядывание обрывается на середине.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "0.1"))
	float CoverPeekDuration = 2.5f;

	/**
	 * РАГДОЛЛ: тело переходит в физику через столько секунд после смерти
	 * (обычно — длительность монтажа смерти). Если `DeathMontage` назначен,
	 * берётся его длина, а это значение работает запасным.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual", meta = (ClampMin = "0"))
	float RagdollDelay = 1.2f;

	/**
	 * Переводит тело в физику (рагдолл): меш падает и остаётся лежать, капсула
	 * отключена, движение выключено. Сквозь тело можно пробежать — по каналу
	 * `Pawn` оно не блокирует.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Visual")
	void StartRagdoll();

	/**
	 * Остаток применений способности за миссию (для серости кнопки HUD).
	 * -1 = без лимита или способность юниту не выдана.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Abilities")
	int32 GetAbilityUsesRemaining(TSubclassOf<UTacticalAbility> AbilityClass) const;

	// --- Подсветка выбора/наведения (зовёт ATacticalPlayerController) --------

	/** Кольцо-декаль под ногами: юнит выбран/снят с выбора. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Highlight")
	void SetSelectionHighlight(bool bSelected);

	/**
	 * Обводка при наведении курсора: Render Custom Depth на меше.
	 * Stencil: 1 — юнит отряда, 2 — враг (цвета задаёт post-process материал).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Highlight")
	void SetHoverHighlight(bool bHovered);

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	/** Ведёт плавный подшаг к укрытию и доворот на месте; в остальное время пуст. */
	virtual void Tick(float DeltaSeconds) override;

	/** Выдаёт общий набор + классовые способности через ASC. */
	void GrantClassAbilities();

	/** Реакция на Health: обновляет HUD; 0 → смерть или тяжёлое ранение. */
	void HandleHealthChanged(const struct FOnAttributeChangeData& Data);

	/** Переход в смерть: коллизия/AI выключаются, BP играет анимацию. */
	void Die();

	// --- BP-хуки для анимаций/VFX/звука --------------------------------------

	/** Юнит погиб. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|State")
	void OnDied();

	/** Юнит тяжело ранен (упал). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|State")
	void OnDownedChanged(bool bNowDowned);

	/** Юнит эвакуирован (дым/звук). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|State")
	void OnEvacuated();

	/** Роль в ростере. Переопределяется в подклассах/BP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Unit")
	EUnitRole UnitRole = EUnitRole::Assault;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Unit")
	TObjectPtr<UActionPointsComponent> ActionPoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Unit")
	TObjectPtr<UCoverDetectionComponent> CoverDetection;

	/**
	 * Navigation Invoker: при RuntimeGeneration=Dynamic навмеш генерится вокруг
	 * юнита. На демо-карте инертен (навмеш строится по всему NavMeshBoundsVolume);
	 * при включении bGenerateNavigationOnlyAroundNavigationInvokers=True масштабирует
	 * навмеш на большие карты — тайлы собираются только в окрестности юнитов.
	 * Радиусы (Tactics|Nav) заданы с запасом под зону хода (MoveRange×2) и обзор.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Unit")
	TObjectPtr<UNavigationInvokerComponent> NavInvoker;

	/** Радиус генерации навмеша вокруг юнита (см). Покрывает зону хода и обзор. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Nav", meta = (ClampMin = "500"))
	float NavInvokerGenerationRadius = 4000.f;

	/** Радиус удаления тайлов навмеша (см). Должен быть больше радиуса генерации. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Nav", meta = (ClampMin = "600"))
	float NavInvokerRemovalRadius = 5500.f;

	/** Кольцо-декаль выбранного юнита (скрыто по умолчанию; материал — в BP). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Highlight")
	TObjectPtr<UDecalComponent> SelectionDecal;

	/** Stencil-значения обводки (совпадают с ветками в M_OutlinePP). */
	static constexpr int32 HoverStencilAlly = 1;
	static constexpr int32 HoverStencilEnemy = 2;

	/**
	 * Делает выбывшего юнита (труп/тяжелораненый) «проходимым»: капсула без
	 * коллизии, меш пропускает Pawn, и — ГЛАВНОЕ при RuntimeGeneration=Dynamic —
	 * тело НЕ режет навмеш (иначе вокруг трупа появляется дыра и живые об него
	 * спотыкаются). bDefeated=false восстанавливает коллизию/навмеш при подъёме.
	 */
	void ApplyDefeatedCollision(bool bDefeated);

	bool bIsDead = false;
	bool bIsDowned = false;
	bool bIsEvacuated = false;

	/** Кэш среза состояния для ABP; пересобирается в NotifyUnitStateChanged. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Visual",
		meta = (AllowPrivateAccess = "true"))
	FUnitVisualState VisualState;

	/** Пересобрать VisualState из фактического состояния (единственное место). */
	void RebuildVisualState();

	// --- Плавные микро-движения позы (подшаг к стене, доворот на месте) -------

	/** Один кадр подшага к стене: держит ввод движения, ловит прибытие/срыв. */
	void UpdateCoverHugStep(float DeltaSeconds);

	/** Один кадр доворота на месте: крутит yaw к цели, гасит остаток. */
	void UpdateTurnInPlace(float DeltaSeconds);

	/** Завершить подшаг: вернуть скорость, пересчитать укрытие, обновить позу. */
	void FinishCoverHugStep();

	/** Идёт подшаг к стене (в это время `bMoving` = true). */
	bool bCoverHugStepping = false;

	/** Куда подшагиваем (мировая точка центра капсулы). */
	FVector CoverHugStepTarget = FVector::ZeroVector;

	/** `MaxWalkSpeed` до подшага — на время подшага он занижается до шага. */
	float CoverHugSavedMaxSpeed = 0.f;

	/** Сколько идёт подшаг (с) — страховка от зависания у препятствия. */
	float CoverHugStepElapsed = 0.f;

	/** Идёт плавный доворот на месте. */
	bool bTurningInPlace = false;

	/** Целевой yaw доворота (мировой, градусы). */
	float TurnTargetYaw = 0.f;

	/** Полная дельта начатого доворота — источник `VisualState.PendingTurnYaw`. */
	float PendingTurnAmount = 0.f;

	/** Темп текущего доворота (град/с): у прижимания к стене он свой. */
	float ActiveTurnRate = 0.f;

	/** Сколько юнит уже стоит в укрытии без движения (с) — таймер выглядывания. */
	float CoverIdleTime = 0.f;

	/** Задержка до следующего выглядывания (с), перевыбирается после каждого. */
	float NextPeekDelay = 0.f;

	/** Сейчас идёт выглядывание — источник `VisualState.bShouldPeek`. */
	bool bPeekActive = false;

	/**
	 * Есть ли у укрытия край в пределах `PeekEdgeMaxDistance`. Отдельно от
	 * стороны: у глухой стены сторона известна, а выглядывать некуда.
	 */
	bool bHasPeekEdge = false;

	/** Один кадр таймера выглядывания в укрытии. */
	void UpdateCoverPeek(float DeltaSeconds);

	/**
	 * Довернуть корпус к краю укрытия на время выглядывания.
	 *
	 * ⚠️ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНО (`bTurnBodyOnPeek`). Клип `Look_*` уже сам
	 * поворачивает голову и корпус в сторону края; если вдобавок довернуть
	 * актора, углы складываются и боец оказывается отвёрнут почти на 180°.
	 * Включать только если решим играть выглядывание неповоротной позой.
	 */
	void FacePeekEdge();

	/** Вернуть корпус к укрытию (лицом или спиной — по `bCoverHugFaceWall`). */
	void FaceCoverWall();

	/**
	 * Сторона выглядывания, зафиксированная на старте.
	 *
	 * ⚠️ Пока идёт выглядывание, юнит развёрнут вдоль стены — а `PeekSideLocal`
	 * считается в ЕГО осях и после разворота показал бы другую сторону. ABP
	 * переключил бы клип на противоположный прямо посреди движения.
	 */
	float FrozenPeekSide = 0.f;

	/** Таймер перехода тела в рагдолл после смерти. */
	FTimerHandle RagdollTimerHandle;

public:
	// --- Монтажи действий (дизайнерские слоты, Ф10) ---------------------------
	//
	// ⚠️ ПОЗА живёт в стейт-машине ABP и берётся из `VisualState`; здесь только
	// ДЕЙСТВИЯ — то, что играется один раз через Default Slot. Разделение
	// обязательное: смешивание даёт залипшие состояния.

	/** Выстрел стоя (нет укрытия между мной и целью). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual|Montages")
	TObjectPtr<UAnimMontage> FireMontageOpen;

	/** Привстать над низким укрытием, выстрелить, сесть обратно. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual|Montages")
	TObjectPtr<UAnimMontage> FireMontageOverCover;

	/** Выйти за край высокого укрытия, выстрелить, вернуться. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual|Montages")
	TObjectPtr<UAnimMontage> FireMontageStepOut;

	/** Реакция на попадание. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual|Montages")
	TObjectPtr<UAnimMontage> HitReactMontage;

	/** Смерть. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual|Montages")
	TObjectPtr<UAnimMontage> DeathMontage;

	/** Вход в наблюдение (вскинуть оружие). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Visual|Montages")
	TObjectPtr<UAnimMontage> OverwatchEnterMontage;
};
