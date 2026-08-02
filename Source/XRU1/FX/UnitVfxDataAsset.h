#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/DataAsset.h"
#include "UnitVfxDataAsset.generated.h"

class UNiagaraSystem;

/**
 * Визуальный профиль стрельбы юнита: вспышка дула, трассер и попадание.
 *
 * Профиль отделён от Blueprint юнита ровно по той же причине, что и звуковой
 * (`UUnitAudioDataAsset`): «когда рисуем» решает C++ на подтверждённых точках
 * боя, «что рисуем» — дизайнер в ассете. Пустое поле = эффект намеренно
 * отсутствует, а не ошибка.
 */
UCLASS(BlueprintType)
class XRU1_API UUnitVfxDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Вспышка у дула; спавнится в точке выстрела с поворотом на цель. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	TObjectPtr<UNiagaraSystem> MuzzleFlash;

	/**
	 * Трассер. По умолчанию эффект НЕСЁТСЯ актором-снарядом от дула к цели
	 * (`bTracerFlies`), потому что trail-системы рисуют шлейф за движущимся
	 * компонентом и на месте выглядят как вспышка вбок.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	TObjectPtr<UNiagaraSystem> Tracer;

	/**
	 * true — эффект везёт актор-снаряд (правильно для trail/ribbon-систем).
	 * false — система просто спавнится в дуле с поворотом на цель (для beam-
	 * систем, которым нужна только конечная точка).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	bool bTracerFlies = true;

	/**
	 * Имя пользовательского float-параметра скорости у системы трассера.
	 * Пусто — систему не трогаем и она летит на своей дефолтной скорости.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FName TracerSpeedParameter = TEXT("User.InitialSpeed");

	/**
	 * Скорость трассера, см/с. На нашей дальности (AttackRange 3000) 12000 см/с
	 * даёт ~0.25 с полёта: глазом читается как выстрел, а не как ракета.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot", meta = (ClampMin = "100"))
	float TracerSpeed = 12000.f;

	/**
	 * Имя точки дула. Ищется по порядку: Scene Component с таким ИМЕНЕМ или
	 * ТЕГОМ (в юните и во всех вложенных акторах оружия) → сокет с таким именем
	 * на меше → фолбэк (точка LOS стрелка).
	 *
	 * Практика для составного BP оружия: добавить в него пустой Scene Component
	 * `Muzzle`, поставить на срез ствола — двигается мышью и видно в вьюпорте.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FName MuzzleSocketName = TEXT("Muzzle");

	/**
	 * Доворот эффектов относительно направления «дуло → цель». Нужен, если
	 * система Niagara летит не по своей оси +X: тогда трассер уходит вбок, и
	 * это лечится здесь, а не пересборкой чужого ассета.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FRotator ShotRotationOffset = FRotator::ZeroRotator;

	/**
	 * Имя векторного user-параметра «конечная точка» у beam-систем
	 * (например `User.BeamEnd`). Пусто — система считается «летящей» и
	 * управляется только поворотом и скоростью.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FName TracerEndParameter;

	/** Попадание по бойцу (кровь/пыль экипировки). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Impact")
	TObjectPtr<UNiagaraSystem> ImpactFlesh;

	/** Попадание по геометрии — по типу поверхности из физматериала. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Impact")
	TMap<TEnumAsByte<EPhysicalSurface>, TObjectPtr<UNiagaraSystem>> ImpactBySurface;

	/** Фолбэк, когда поверхность не распознана или не перечислена. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Impact")
	TObjectPtr<UNiagaraSystem> DefaultImpact;

	/**
	 * Насколько промах уводится в сторону от цели (см). XCOM рисует промах
	 * мимо цели, а не в неё: без этого игрок не отличает «не попал» от
	 * «попал, но урона нет».
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot", meta = (ClampMin = "0"))
	float MissSpread = 120.f;

	/** Эффект попадания для поверхности; при отсутствии — DefaultImpact. */
	UNiagaraSystem* FindImpact(EPhysicalSurface Surface) const;
};
