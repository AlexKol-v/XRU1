#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TacticalScenarioDataAsset.generated.h"

class UQuestDefinition;
class UWorld;

UENUM(BlueprintType)
enum class ETacticalScenarioKind : uint8
{
	Tutorial UMETA(DisplayName = "Tutorial"),
	Mission  UMETA(DisplayName = "Mission")
};

/**
 * Конфигурация логического сценария на общей физической карте.
 * Tutorial и Mission01 отличаются данными/наборами акторов, а не копиями World.
 */
UCLASS(BlueprintType)
class XRU1_API UTacticalScenarioDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Стабильный ID для save/result routing: Tutorial, Mission01 и т.п. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	FName ScenarioId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	ETacticalScenarioKind Kind = ETacticalScenarioKind::Tutorial;

	/** StateTree-квест, который управляет целями этого запуска общей карты. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	TSoftObjectPtr<UQuestDefinition> QuestDefinition;

	/**
	 * Единственный scenario-specific streaming sublevel. Persistent-карта хранит
	 * общий арт/nav/light; Tutorial и Mission01 не смешиваются и не дублируют его.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	TSoftObjectPtr<UWorld> ScenarioSublevel;

	/** -1 — взять правило GameMode/сложности; 0 — без таймера; >0 — явный лимит. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario", meta = (ClampMin = "-1"))
	int32 TurnLimit = -1;

	/**
	 * AnchorId якоря стартовой позиции камеры в scenario sublevel
	 * (AScenarioAnchorPoint). Камера ставится на него при старте сценария —
	 * PlayerStart persistent-карты общий и не знает, где начинается конкретный
	 * сценарий. None — камера остаётся где заспавнилась.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	FName InitialCameraAnchorId;

	/** Профиль начального reveal для будущей системы тумана войны. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Fog")
	FName FogProfileId;

	/** Новый запуск всегда начинает с чистого runtime-состояния тумана. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Fog")
	bool bResetFogOnStart = true;
};
