#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShotTracerActor.generated.h"

class UNiagaraComponent;
class UUnitVfxDataAsset;

/**
 * Летящий трассер: несёт Niagara-эффект от дула к точке попадания.
 *
 * Зачем актор, а не «спавн системы с поворотом»: часть трассерных систем рисует
 * шлейф относительно СВОЕГО компонента, и на месте такой эффект читается как
 * вспышка вбок. Здесь компонент честно летит по прямой с той же скоростью,
 * которую получает сама система (`ApplyTracerParameters`), поэтому обе трактовки
 * совпадают и трассер идёт синхронно с пулей.
 *
 * По прилёте актор НЕ исчезает мгновенно: эмиссия гасится, а шлейф доигрывает
 * `TracerTrailDuration` — иначе `Destroy()` срезал бы след ровно в момент удара.
 */
UCLASS()
class XRU1_API AShotTracerActor : public AActor
{
	GENERATED_BODY()

public:
	AShotTracerActor();

	/**
	 * Запускает трассер по профилю юнита. Возвращает время полёта, с; 0 — если
	 * эффект не задан или расстояние вырождено.
	 */
	static float Launch(const UObject* WorldContext, const UUnitVfxDataAsset* Profile,
		const FVector& Start, const FVector& End);

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "VFX")
	TObjectPtr<UNiagaraComponent> Effect;

private:
	FVector TargetLocation = FVector::ZeroVector;
	float FlightSpeed = 7000.f;
	/** Сколько шлейфу дать доиграть после прилёта, с. */
	float TrailLinger = 0.35f;
	/** Страховка от «вечного» трассера, если цель окажется недостижимой. */
	float LifeLeft = 2.f;
};
