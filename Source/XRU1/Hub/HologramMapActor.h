#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HologramMapActor.generated.h"

class AMissionPointOfInterest;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Голографическая карта хаба: проекция местности, которую игрок вращает мышью
 * и на которой стоят маркеры миссий (BRIEF_HubHologram §3).
 *
 * Иерархия:
 *   Root
 *     ├── RotationRoot   — вращается по yaw; ВСЕ маркеры крепятся сюда, иначе
 *     │     └── TerrainMesh   маркер уедет с горы на степь при первом же повороте
 *     └── GlowLight      — подсветка снизу, вращаться не должна
 *
 * Наклон (pitch) принадлежит камере (`AHubCameraPawn`), а не карте: вертикальный
 * drag должен ощущаться как «смотрю под другим углом», а не «роняю карту набок».
 * Карта отвечает только за yaw и медленное автовращение в простое.
 */
UCLASS(Blueprintable)
class XRU1_API AHologramMapActor : public AActor
{
	GENERATED_BODY()

public:
	AHologramMapActor();

	/** Поворот от контроллера, град (+ по часовой). Сбрасывает таймер автовращения. */
	UFUNCTION(BlueprintCallable, Category = "Hub|Hologram")
	void AddYawInput(float DeltaDegrees);

	/** Маркеры миссий, прикреплённые к карте. */
	UFUNCTION(BlueprintPure, Category = "Hub|Hologram")
	TArray<AMissionPointOfInterest*> GetPointsOfInterest() const;

	/** Вернуть исходный ракурс карты. */
	UFUNCTION(BlueprintCallable, Category = "Hub|Hologram")
	void ResetView();

	/** Узел, к которому обязаны крепиться маркеры (вращается вместе с рельефом). */
	UFUNCTION(BlueprintPure, Category = "Hub|Hologram")
	USceneComponent* GetRotationRoot() const { return RotationRoot; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hub|Hologram")
	TObjectPtr<USceneComponent> Root;

	/** Вращающийся узел: рельеф и все маркеры. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hub|Hologram")
	TObjectPtr<USceneComponent> RotationRoot;

	/** Сюда дизайнер кладёт меш рельефа и голографический материал. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hub|Hologram")
	TObjectPtr<UStaticMeshComponent> TerrainMesh;

	/** Мягкая подсветка проекции снизу. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hub|Hologram")
	TObjectPtr<UPointLightComponent> GlowLight;

	/** Начальный поворот карты, град. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Hologram")
	float DefaultYaw = 0.f;

	/** Медленное автовращение в простое, град/сек (0 — выключено). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Hologram")
	float IdleSpinSpeed = 3.f;

	/**
	 * Пауза без ввода, после которой возобновляется автовращение, сек.
	 * Вращение, продолжающееся во время перетаскивания, читается как рассинхрон.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Hologram", meta = (ClampMin = "0"))
	float IdleSpinDelay = 4.f;

private:
	/** Применяет CurrentYaw к RotationRoot (единственная точка записи поворота). */
	void ApplyRotation();

	/** Текущий yaw карты, град (нормализован). */
	float CurrentYaw = 0.f;

	/** Время последнего ввода игрока (World time), для паузы автовращения. */
	double LastInputTime = 0.0;
};
