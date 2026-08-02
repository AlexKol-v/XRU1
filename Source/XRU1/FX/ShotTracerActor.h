#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShotTracerActor.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/**
 * Летящий трассер: несёт Niagara-эффект от дула к точке попадания.
 *
 * Зачем актор, а не «спавн системы с поворотом»: трассерные эффекты (в том числе
 * `NS_BulletTracer` из Niagara Examples) рисуют шлейф ЗА движущимся компонентом.
 * Если систему просто заспавнить на месте, шлейфу не за чем тянуться, и выстрел
 * читается как вспышка «вбок». Здесь эффект прикреплён к актору, который честно
 * летит по прямой и самоуничтожается по прибытии.
 */
UCLASS()
class XRU1_API AShotTracerActor : public AActor
{
	GENERATED_BODY()

public:
	AShotTracerActor();

	/**
	 * Запускает трассер. Возвращает время полёта, с; 0 — если эффект не задан
	 * или расстояние вырождено.
	 */
	static float Launch(const UObject* WorldContext, UNiagaraSystem* System,
		const FVector& Start, const FVector& End, float Speed);

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "VFX")
	TObjectPtr<UNiagaraComponent> Effect;

private:
	FVector TargetLocation = FVector::ZeroVector;
	float FlightSpeed = 10000.f;
	/** Страховка от «вечного» трассера, если цель окажется недостижимой. */
	float LifeLeft = 2.f;
};
