#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MoveRangeVisualizer.generated.h"

class AUnitBase;
class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Визуализация зоны хода выбранного юнита (GDD §5.3): область достижимости
 * по ДЛИНЕ ПУТИ навмеша (не радиус!) — полигонная заливка по полигонам
 * Recast-навмеша. Секция 0 — зона 1 AP (синяя), секция 1 — кольцо 2 AP
 * (жёлтая). Спавнится тактическим контроллером, обновляется при выборе
 * юнита и трате AP.
 */
UCLASS(Blueprintable)
class XRU1_API AMoveRangeVisualizer : public AActor
{
	GENERATED_BODY()

public:
	AMoveRangeVisualizer();

	/** Материал зоны 1 AP (полупрозрачный синий; задаётся в BP). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UMaterialInterface> ZoneOneMaterial;

	/** Материал зоны 2 AP (полупрозрачный жёлтый). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UMaterialInterface> ZoneTwoMaterial;

	/** Подъём заливки над полом против z-fighting (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange")
	float SurfaceOffset = 8.f;

	/** Перестраивает зону под юнита: 1 AP = MoveRange, 2 AP = 2×MoveRange по пути. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|MoveRange")
	void ShowForUnit(AUnitBase* Unit);

	/** Прячет зону (юнит снят с выбора / чужая фаза). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|MoveRange")
	void Hide();

protected:
	/**
	 * Строит секцию меша из полигонов навмеша, достижимых по пути в пределах
	 * [MinPathDistance..MaxPathDistance] от Origin (перекрытие исключается
	 * набором полигонов ближней зоны).
	 */
	void BuildZoneSection(int32 SectionIndex, const FVector& Origin,
		float MaxPathDistance, TSet<uint64>& InOutUsedPolys, UMaterialInterface* Material);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UProceduralMeshComponent> ZoneMesh;
};
