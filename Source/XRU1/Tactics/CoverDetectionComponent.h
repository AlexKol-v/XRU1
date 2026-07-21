#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "CoverTypes.h"
#include "CoverDetectionComponent.generated.h"

class UGameplayEffect;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCoverStateChanged, ECoverType, NewBestCover);

/**
 * Детекция укрытий вокруг юнита. Трейсит окружение по кардинальным направлениям
 * и определяет наличие стены (half/full cover) рядом. Отдельно умеет посчитать
 * тип укрытия ОТНОСИТЕЛЬНО конкретного врага (укрытие работает, только если стена
 * стоит между юнитом и источником огня).
 *
 * Статус укрытия отражается на ASC юнита бессрочным GameplayEffect'ом с тегом
 * Cover.Half / Cover.Full (для UI и способностей), а численный бонус к защите
 * вычитается из шанса попадания стрелка в момент выстрела — см.
 * UTacticsCombatStatics::ComputeHitChance (укрытие считается против конкретного
 * стрелка, как в XCOM: фланкирование его обнуляет).
 */
UCLASS(ClassGroup = (Tactics), meta = (BlueprintSpawnableComponent))
class XRU1_API UCoverDetectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCoverDetectionComponent();

	/** Дистанция трейса до стены, за которой считаем, что юнит «в укрытии». */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Cover")
	float CoverTraceDistance = 120.f;

	/** Высота трейса «половинчатого» укрытия (низкая стена) от пола юнита. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Cover")
	float HalfCoverHeight = 60.f;

	/** Высота трейса «полного» укрытия (высокая стена) от пола юнита. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Cover")
	float FullCoverHeight = 150.f;

	/** Канал трейса для поиска стен-укрытий. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Cover")
	TEnumAsByte<ECollisionChannel> CoverTraceChannel = ECC_WorldStatic;

	/** Бонус защиты половинчатого укрытия: вычитается из шанса попадания стрелка (XCOM: 20). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Cover", meta = (ClampMin = "0", ClampMax = "100"))
	float HalfCoverDefenseBonus = 20.f;

	/** Бонус защиты полного укрытия (XCOM: 40). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Cover", meta = (ClampMin = "0", ClampMax = "100"))
	float FullCoverDefenseBonus = 40.f;

	/** GE, навешиваемый при половинчатом укрытии (выдаёт тег Cover.Half). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	TSubclassOf<UGameplayEffect> HalfCoverEffect;

	/** GE, навешиваемый при полном укрытии (выдаёт тег Cover.Full). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	TSubclassOf<UGameplayEffect> FullCoverEffect;

	/** Лучшее укрытие из всех направлений (кэш последнего EvaluateSurroundings). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	ECoverType BestCoverAround = ECoverType::None;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Cover")
	FOnCoverStateChanged OnCoverStateChanged;

	/**
	 * Пересчитывает укрытие по 4 кардинальным направлениям вокруг юнита, обновляет
	 * BestCoverAround и синхронизирует GE/тег укрытия на ASC владельца.
	 * Вызывать после каждого перемещения юнита.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cover")
	ECoverType EvaluateSurroundings();

	/** Тип укрытия, эффективный против конкретной угрозы (стена должна быть между юнитом и врагом). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cover")
	ECoverType GetCoverAgainst(const AActor* Threat) const;

	/**
	 * Укрытие В ПРОИЗВОЛЬНОЙ ТОЧКЕ против угрозы — той же математикой, что у
	 * стоящего юнита (Base = где будет ActorLocation, т.е. точка пола + половина
	 * капсулы). Нужна AI: «какое укрытие я получу, если встану сюда» — план и
	 * факт обязаны считаться одинаково, иначе враг бежит в «укрытие», которого
	 * по прибытии не окажется. Настройки (высоты/дистанция/канал) — с ЭТОГО
	 * компонента: у кого спрашиваем, тем и мерим.
	 */
	ECoverType EvaluateCoverAtLocation(const FVector& Base, const FVector& ThreatLocation) const;

	/** Общее ядро трейса укрытия (см. EvaluateCoverAtLocation). */
	static ECoverType TraceCoverAtLocation(const UWorld* World, const FVector& Base, const FVector& Direction,
		float TraceDistance, float HalfHeight, float FullHeight, ECollisionChannel Channel, const AActor* Ignored);

	/** Численный бонус защиты против конкретного стрелка (0 / Half / Full). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Cover")
	float GetDefenseBonusAgainst(const AActor* Threat) const;

protected:
	virtual void BeginPlay() override;

	/** Трейс в одном направлении: возвращает тип укрытия у стены в этом направлении. */
	ECoverType TraceCoverInDirection(const FVector& Direction) const;

	/** Снимает старый GE укрытия и навешивает соответствующий новому состоянию. */
	void ApplyCoverEffect(ECoverType CoverType);

	/** Хэндл активного GE укрытия на ASC владельца (для снятия при смене состояния). */
	FActiveGameplayEffectHandle ActiveCoverEffectHandle;
};
