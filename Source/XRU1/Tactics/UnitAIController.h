#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "UnitAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;

/**
 * AI-контроллер тактического юнита. Несёт UAIPerceptionComponent с чувством
 * зрения (UAISenseConfig_Sight) — общий источник «линии видимости» и для
 * ходов вражеского AI, и для реакций Overwatch у юнитов игрока.
 *
 * Каркас перенесён по паттерну донорского NPCStateTreeAIController (только часть
 * с perception/sight, без StateTree-веток).
 */
UCLASS()
class XRU1_API AUnitAIController : public AAIController
{
	GENERATED_BODY()

public:
	AUnitAIController();

	UAIPerceptionComponent* GetUnitPerception() const { return Perception; }

	/** Радиус зрения (см). Дизайнерский параметр, влияет на дальность обнаружения. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception")
	float SightRadius = 1400.f;

	/** Радиус, на котором цель теряется после обнаружения. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception")
	float LoseSightRadius = 1600.f;

	/** Половина угла конуса зрения (град). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Perception")
	float PeripheralVisionHalfAngle = 60.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Perception")
	TObjectPtr<UAIPerceptionComponent> Perception;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;
};
