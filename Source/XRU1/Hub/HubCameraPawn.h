#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HubCameraPawn.generated.h"

class UCameraComponent;
class USceneComponent;
class USpringArmComponent;

/**
 * Камера хаба: статичная точка обзора вокруг голографической карты.
 * Крутится и зумится сам boom (SpringArm), а не pawn — это стандартная схема
 * орбитальной камеры, при которой центр обзора не «уплывает».
 *
 * Pawn ставится в центр карты; наклон ограничен, чтобы камера не ушла под пол
 * и не перевернулась через зенит.
 */
UCLASS(Blueprintable)
class XRU1_API AHubCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AHubCameraPawn();

	/** Наклон камеры (вертикальный drag), град. Клампится MinPitch/MaxPitch. */
	UFUNCTION(BlueprintCallable, Category = "Hub|Camera")
	void AddPitchInput(float DeltaDegrees);

	/** Зум колесом: +1 — приблизить на один шаг, -1 — отдалить. */
	UFUNCTION(BlueprintCallable, Category = "Hub|Camera")
	void AddZoomStep(float Steps);

	/** Вернуть исходные наклон и дистанцию. */
	UFUNCTION(BlueprintCallable, Category = "Hub|Camera")
	void ResetView();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hub|Camera")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hub|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hub|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Стартовый наклон, град (отрицательный — смотрим сверху вниз). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Camera")
	float DefaultPitch = -40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Camera")
	float MinPitch = -75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Camera")
	float MaxPitch = -12.f;

	/** Стартовая дистанция до карты, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Camera", meta = (ClampMin = "1"))
	float DefaultArmLength = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Camera", meta = (ClampMin = "1"))
	float MinArmLength = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Camera", meta = (ClampMin = "1"))
	float MaxArmLength = 2600.f;

	/** Шаг зума на одно движение колеса, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Camera", meta = (ClampMin = "1"))
	float ZoomStep = 180.f;

	/**
	 * Скорость подтягивания к целевой дистанции. Мгновенный зум по колесу
	 * выглядит рывками, поэтому дистанция интерполируется.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Camera", meta = (ClampMin = "0.1"))
	float ZoomInterpSpeed = 9.f;

private:
	/** Целевая длина boom; фактическая догоняет её в Tick. */
	float DesiredArmLength = 1400.f;
};
