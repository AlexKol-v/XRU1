#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AIActionEvaluator.h"
#include "AIBehaviorProfileDataAsset.generated.h"

/** Настройка источников знаний AI. Эти значения применяются к AI Perception в BeginPlay. */
USTRUCT(BlueprintType)
struct XRU1_API FAIPerceptionTuning
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0"))
	float SightRadius = 1400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0"))
	float LoseSightRadius = 1600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "1", ClampMax = "180"))
	float PeripheralVisionHalfAngle = 180.f;
};

/** Настройка перемещения и локального обхода других агентов. */
USTRUCT(BlueprintType)
struct XRU1_API FAINavigationTuning
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Navigation", meta = (ClampMin = "0", ClampMax = "10"))
	float CrowdSeparationWeight = 2.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Navigation", meta = (ClampMin = "5", ClampMax = "25"))
	float RouteCornerAcceptance = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Navigation", meta = (ClampMin = "10"))
	float ManeuverArrivalTolerance = 120.f;
};

/** Настройка FSM тревоги и читаемости пошагового исполнения. */
USTRUCT(BlueprintType)
struct XRU1_API FAIAlertTuning
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alert", meta = (ClampMin = "0"))
	float InvestigateAcceptanceRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alert", meta = (ClampMin = "0", ClampMax = "1"))
	float InvestigateOverwatchChance = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alert", meta = (ClampMin = "0.01"))
	float ActionInterval = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alert", meta = (ClampMin = "0"))
	float TauntPriorityRadius = 2500.f;
};

/** Все коэффициенты поиска и оценки позиции. */
USTRUCT(BlueprintType)
struct XRU1_API FAIPositionScoringTuning
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float CoverDefenseWeight = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float OpenCoverFactor = -4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float HalfCoverFactor = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float FullCoverFactor = 1.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float FlankPositionBonus = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float HeightPositionBonus = 15.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0"))
	float MinSpreadDistance = 576.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0", ClampMax = "1"))
	float SpreadPenaltyMultiplier = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float AllyVisibilityWeight = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0"))
	float OverwatchExposurePenalty = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float LineOfFireBonus = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float LoseLineOfFirePenalty = 45.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0"))
	float TravelCostPerCm = 0.015f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0"))
	float IdealRangeWeight = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "100"))
	float IdealRangeFalloff = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0"))
	float RelocateBias = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0", ClampMax = "1"))
	float RetreatHealthFraction = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0"))
	float RetreatRewardPerCm = 0.01f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "0"))
	float CoverSnapDistance = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxScoredThreats = 4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Position")
	float EnemyVisibilityWeight = 20.f;
};

/** Коэффициенты выбора основной цели. */
USTRUCT(BlueprintType)
struct XRU1_API FAITargetScoringTuning
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target", meta = (ClampMin = "0", ClampMax = "100"))
	float HitChanceHighThreshold = 65.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target", meta = (ClampMin = "0", ClampMax = "100"))
	float HitChanceLowThreshold = 35.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float HitChanceHighScore = 70.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float HitChanceMediumScore = 40.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float HitChanceLowScore = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float TauntingScore = 1000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float FlankedScore = 50.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float KillShotScore = 15.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float WoundedScore = 5.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float DownedScore = -1000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	float NoLineOfFireScore = -200.f;
};

/**
 * Единый переиспользуемый профиль тактического AI.
 *
 * Профиль имеет приоритет над полями BP-контроллера и применяется один раз в BeginPlay.
 * Это позволяет хранить Normal/Aggressive/Defensive и профили сложности отдельными ассетами,
 * не размножая несогласованные значения по Blueprint-наследникам контроллера.
 */
UCLASS(BlueprintType)
class XRU1_API UAIBehaviorProfileDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Tuning")
	FAIPerceptionTuning Perception;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Tuning")
	FAINavigationTuning Navigation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Tuning")
	FAIAlertTuning Alert;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Tuning")
	FAIPositionScoringTuning Position;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Tuning")
	FAITargetScoringTuning Target;

	/**
	 * Если включено, нативный набор оценщиков контроллера полностью заменяется этим массивом.
	 * Пустой массив тогда намеренно оставляет AI только с терминальным Skip-фолбэком.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Actions")
	bool bOverrideActionEvaluators = false;

	/** Шаблоны клонируются в конкретный контроллер: runtime-состояние между юнитами не разделяется. */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "AI|Actions",
		meta = (EditCondition = "bOverrideActionEvaluators"))
	TArray<TObjectPtr<UAIActionEvaluator>> ActionEvaluators;
};
