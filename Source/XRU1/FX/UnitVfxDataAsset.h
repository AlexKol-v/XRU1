#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/DataAsset.h"
#include "UnitVfxDataAsset.generated.h"

class UNiagaraComponent;
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
	 * Трассер. Системы такого рода (в том числе `NS_BulletTracer` из Niagara
	 * Examples) САМИ считают полёт пули по своим user-параметрам: начало, конец,
	 * скорость, время жизни шлейфа. Без этих параметров система рисует трассер по
	 * своим дефолтным точкам — то есть в стороне от выстрела или вообще вне кадра.
	 * Имена параметров ниже; пустое имя = параметр у системы не трогаем.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	TObjectPtr<UNiagaraSystem> Tracer;

	/**
	 * true — эффект дополнительно везёт актор-снаряд, летящий с той же скоростью
	 * (нужно системам, которые рисуют шлейф относительно своего компонента).
	 * false — система спавнится в дуле и летит только своими параметрами.
	 * Точки и скорость передаются в обоих режимах, поэтому переключатель влияет
	 * лишь на то, едет ли компонент вместе с пулей.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	bool bTracerFlies = true;

	/** Имя float-параметра скорости у системы трассера. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FName TracerSpeedParameter = TEXT("User.InitialSpeed");

	/** Имя параметра «точка вылета» (мировая точка дула). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FName TracerStartParameter = TEXT("User.SpawnPosition");

	/** Имя float-параметра «сколько держится шлейф после прилёта», с. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FName TracerTrailDurationParameter = TEXT("User.TrailDuration");

	/**
	 * Скорость трассера, см/с. 7000 на дальности 3000 даёт ~0.43 с полёта —
	 * выстрел успевает прочитаться глазом, но темп боя не проседает.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot", meta = (ClampMin = "100"))
	float TracerSpeed = 7000.f;

	/**
	 * Сколько шлейф держится после прилёта, с. Пуля уже попала, а след ещё виден —
	 * именно это делает направление выстрела читаемым в пошаговом бою.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot", meta = (ClampMin = "0"))
	float TracerTrailDuration = 0.35f;

	/**
	 * Имя точки дула. Ищется по порядку: Scene Component с таким ИМЕНЕМ или
	 * ТЕГОМ (в юните и во всех вложенных акторах оружия) → сокет с таким именем
	 * на меше → фолбэк (точка LOS стрелка).
	 *
	 * Практика для составного BP оружия: добавить в него пустой Scene Component
	 * `Muzzle`, поставить на срез ствола — двигается мышью и видно в вьюпорте.
	 * Так и сделано во всех четырёх BP оружия (`BP_AssaultRifle_Default` и др.),
	 * которые юниты носят Child Actor компонентом `Gun`.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FName MuzzleSocketName = TEXT("Muzzle");

	/**
	 * Доворот ВСПЫШКИ относительно направления «дуло → цель». Нужен, если
	 * система Niagara ориентирована не по своей оси +X. Трассер сюда не смотрит:
	 * он летит по точкам (`TracerStartParameter`/`TracerEndParameter`), а не по
	 * повороту компонента.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FRotator ShotRotationOffset = FRotator::ZeroRotator;

	/**
	 * Имя параметра «конечная точка» — мировая точка, где пуля упирается в цель
	 * или в геометрию. Тип (Position или Vector) определяется у самой системы.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot")
	FName TracerEndParameter = TEXT("User.Hit");

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
	 * Имя параметра «нормаль поверхности» у эффекта попадания. Крошка и искры
	 * разлетаются по нему, а не по повороту компонента: поворот системы такие
	 * эффекты игнорируют.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Impact")
	FName ImpactNormalParameter = TEXT("User.Hit Normal");

	/** Имя параметра «направление пули» у эффекта попадания. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Impact")
	FName ImpactDirectionParameter = TEXT("User.Hit Direction");

	/**
	 * Насколько промах уводится в сторону от цели (см). XCOM рисует промах
	 * мимо цели, а не в неё: без этого игрок не отличает «не попал» от
	 * «попал, но урона нет».
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Shot", meta = (ClampMin = "0"))
	float MissSpread = 120.f;

	/** Эффект попадания для поверхности; при отсутствии — DefaultImpact. */
	UNiagaraSystem* FindImpact(EPhysicalSurface Surface) const;

	/**
	 * Отдаёт трассеру геометрию выстрела: откуда, куда, с какой скоростью и
	 * сколько держать шлейф. Вызывать ДО активации компонента — user-параметры
	 * читаются на спавне системы.
	 */
	void ApplyTracerParameters(UNiagaraComponent* Component,
		const FVector& Start, const FVector& End) const;

	/** Отдаёт эффекту попадания нормаль поверхности и направление пули. */
	void ApplyImpactParameters(UNiagaraComponent* Component,
		const FVector& Normal, const FVector& Direction) const;
};
