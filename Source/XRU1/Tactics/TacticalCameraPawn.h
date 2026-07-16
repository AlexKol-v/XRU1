#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TacticalCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UPostProcessComponent;
class UMaterialInterface;

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

	/** Перелететь к актору: плавный полёт (XCOM) или мгновенно (bInstant). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FocusOnActor(const AActor* Target, bool bInstant = false);

	/** Перелететь к точке в плоскости земли (плавно; bInstant — телепорт). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FocusOnLocation(const FVector& Location, bool bInstant = false);

	/**
	 * Следовать за актором (бегущий свой юнит / действующий враг), пока не
	 * вызван ClearFollowTarget или игрок не двинул камеру сам (XCOM: ручная
	 * панорама разрывает follow).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void SetFollowTarget(const AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void ClearFollowTarget();

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

	/** Скорость полёта камеры к цели фокуса/следования (VInterpTo). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float FocusInterpSpeed = 8.f;

	/** Пределы длины пружины (зум), см. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float MinZoom = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float MaxZoom = 2600.f;

	/** Шаг зума за тик колеса, см. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float ZoomStep = 300.f;

	/**
	 * Post-process материал обводки юнитов (M_OutlinePP: edge-detect по Custom
	 * Stencil). Вешается блендаблом на unbound-PostProcessComponent пешки —
	 * PostProcessVolume на карте не нужен.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Camera")
	TObjectPtr<UMaterialInterface> OutlineMaterial;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Глобальный (unbound) пост-процесс пешки: несёт обводку юнитов. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Целевые значения для плавной интерполяции. */
	float TargetYaw = 45.f;
	float TargetZoom = 1800.f;

	// --- Фокус/следование (XCOM-полёт камеры) ---------------------------------

	/** Актор, за которым камера следует каждый тик (невалиден = не следуем). */
	TWeakObjectPtr<const AActor> FollowTarget;

	/** Точка, к которой камера летит (валидна при bHasFocusGoal). */
	FVector FocusGoal = FVector::ZeroVector;
	bool bHasFocusGoal = false;
};
