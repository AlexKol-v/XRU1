#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CoverTypes.h"
#include "CoverDetectionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCoverStateChanged, ECoverType, NewBestCover);

/**
 * Детекция укрытий вокруг юнита. Трейсит окружение по кардинальным направлениям
 * и определяет наличие стены (half/full cover) рядом. Отдельно умеет посчитать
 * тип укрытия ОТНОСИТЕЛЬНО конкретного врага (укрытие работает, только если стена
 * стоит между юнитом и источником огня).
 *
 * Каркас: трейс-логика и пороги высот заложены; выдача бонуса к защите через GAS
 * (GameplayEffect с тегами Cover.Half / Cover.Full) — точка расширения ApplyCoverTagsFor().
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

	/** Тег GAS для половинчатого укрытия (напр. Cover.Half). Навешивается при подсчёте боя. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	FGameplayTag HalfCoverTag;

	/** Тег GAS для полного укрытия (напр. Cover.Full). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	FGameplayTag FullCoverTag;

	/** Лучшее укрытие из всех направлений (кэш последнего EvaluateSurroundings). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	ECoverType BestCoverAround = ECoverType::None;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Cover")
	FOnCoverStateChanged OnCoverStateChanged;

	/** Пересчитывает укрытие по 4 кардинальным направлениям вокруг юнита, обновляет BestCoverAround. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cover")
	ECoverType EvaluateSurroundings();

	/** Тип укрытия, эффективный против конкретной угрозы (стена должна быть между юнитом и врагом). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cover")
	ECoverType GetCoverAgainst(const AActor* Threat) const;

protected:
	/** Трейс в одном направлении: возвращает тип укрытия у стены в этом направлении. */
	ECoverType TraceCoverInDirection(const FVector& Direction) const;
};
