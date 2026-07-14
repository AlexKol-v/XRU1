#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TacticalCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;

/**
 * Камера тактического боя (XCOM-стиль): панорамирование по земле, поворот
 * шагами по 45°, зум пружиной. Пешка игрока в боевых уровнях; ввод шлёт
 * ATacticalPlayerController.
 */
UCLASS(Blueprintable)
class XRU1_API ATacticalCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ATacticalCameraPawn();

	/** Панорамирование в плоскости земли (ось X — вправо, Y — вперёд относительно камеры). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddPanInput(const FVector2D& Input);

	/** Поворот на шаг: Direction > 0 — по часовой (E), < 0 — против (Q). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddRotationStep(float Direction);

	/** Зум: положительное значение — приблизить. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddZoomInput(float Input);

	/** Мгновенно перелететь к актору (фокус на юните при выборе). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FocusOnActor(const AActor* Target);

	// --- Дизайнерские параметры ----------------------------------------------

	/** Скорость панорамирования (см/сек на единицу ввода). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float PanSpeed = 2000.f;

	/** Шаг поворота (град). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float RotationStep = 45.f;

	/** Скорость интерполяции поворота/зума. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float InterpSpeed = 8.f;

	/** Пределы длины пружины (зум), см. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float MinZoom = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float MaxZoom = 2600.f;

	/** Шаг зума за тик колеса, см. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float ZoomStep = 300.f;

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Целевые значения для плавной интерполяции. */
	float TargetYaw = 45.f;
	float TargetZoom = 1800.f;
};
