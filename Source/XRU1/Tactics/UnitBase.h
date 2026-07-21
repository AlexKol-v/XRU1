#pragma once

#include "CoreMinimal.h"
#include "TDCombatant.h"
#include "TacticsTypes.h"
#include "UnitBase.generated.h"

class UActionPointsComponent;
class UCoverDetectionComponent;
class UCurveFloat;
class UDecalComponent;
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
	virtual void BeginPlay() override;

	/** Выдаёт общий набор + классовые способности через ASC. */
	void GrantClassAbilities();

	/** Реакция на изменение Health: 0 → смерть или тяжёлое ранение. */
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

	/** Кольцо-декаль выбранного юнита (скрыто по умолчанию; материал — в BP). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Highlight")
	TObjectPtr<UDecalComponent> SelectionDecal;

	/** Stencil-значения обводки (совпадают с ветками в M_OutlinePP). */
	static constexpr int32 HoverStencilAlly = 1;
	static constexpr int32 HoverStencilEnemy = 2;

	bool bIsDead = false;
	bool bIsDowned = false;
	bool bIsEvacuated = false;
};
