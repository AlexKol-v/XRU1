#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "CoverTuningDataAsset.generated.h"

/**
 * Единый ассет тюнинга укрытий, линии видимости, выглядывания (peek) и высоты.
 * Собирает воедино параметры, раньше размазанные между UCoverDetectionComponent
 * (UPROPERTY), UTacticsCombatStatics (static constexpr) и захардкоженным
 * множителем глухой обороны (литерал 2.f). Теперь всё это тюнит дизайнер.
 *
 * По образцу UTacticalHUDStyleData: ассет назначается один раз в
 * BP-наследнике UTacticsGameInstance (поле CoverTuning) и достаётся через
 * UTacticsCombatStatics::GetCoverTuning(World). Пер-юнитное переопределение —
 * UCoverDetectionComponent::TuningOverride.
 *
 * ВАЖНО: дефолты класса РАВНЫ прежним числам кода, поэтому пока ассет никуда не
 * назначен, резолвер отдаёт CDO и поведение игры не меняется ни на йоту.
 */
UCLASS(BlueprintType)
class XRU1_API UCoverTuningDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Геометрия детекта укрытия ---
	/** Дистанция трейса до стены, чтобы считаться «в укрытии» (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Detect", meta = (ClampMin = "10"))
	float CoverTraceDistance = 120.f;

	/** Низкое укрытие: высота трейса ОТ ПОЛА (после Ф2). Ящик 60 см = Half. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Detect", meta = (ClampMin = "10"))
	float HalfCoverHeight = 60.f;

	/** Высокое укрытие: ПОРОГ высоты трейса ОТ ПОЛА (после Ф2). Стена 160 см = Full. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Detect", meta = (ClampMin = "10"))
	float FullCoverHeight = 150.f;

	/** Канал трейса для поиска стен-укрытий. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Detect")
	TEnumAsByte<ECollisionChannel> CoverTraceChannel = ECC_WorldStatic;

	/** 4 оси или 8 (с диагоналями). Пока не используется — задел под Ф8. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Detect", meta = (ClampMin = "4", ClampMax = "16"))
	int32 SurroundingDirections = 8;

	// --- Защита (числа XCOM) ---
	/** Бонус защиты половинчатого укрытия (XCOM: 20). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Defense", meta = (ClampMin = "0", ClampMax = "100"))
	float HalfCoverDefenseBonus = 20.f;

	/** Бонус защиты полного укрытия (XCOM: 40). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Defense", meta = (ClampMin = "0", ClampMax = "100"))
	float FullCoverDefenseBonus = 40.f;

	/** Множитель глухой обороны: модель XCOM 1 — удвоение укрытия. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Defense", meta = (ClampMin = "1", ClampMax = "4"))
	float HunkerDownMultiplier = 2.f;

	// --- LOS и выглядывание ---
	/** Высота глаз над ActorLocation (центр капсулы) ≈148 см над полом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|LOS")
	float EyeHeightOffset = 60.f;

	/** Толщина луча LOS (см): гасит «выстрел в щель» на стыке мешей. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|LOS", meta = (ClampMin = "1"))
	float LosSphereRadius = 15.f;

	/** Быстрый step-out вбок от центра (см) — грубое выглядывание из-за угла. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|LOS", meta = (ClampMin = "0"))
	float LosPeekOffset = 70.f;

	/** Шаг поиска края собственного укрытия (см). Меньше шаг — точнее найденный край (меньше «перелёта» мимо истинной границы). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Peek", meta = (ClampMin = "5"))
	float PeekEdgeStep = 15.f;

	/**
	 * Насколько далеко юнит выглядывает вдоль своего укрытия (см). Прямой аналог
	 * «одной клетки» XCOM (2026-07: сверено с реальным масштабом XCOM 2 — тайл
	 * там 96 юнитов Unreal ≈ 96 см, а не «полтора-два метра», как казалось на
	 * глаз при 150–250; отсюда и ужат до значения ниже). Управляет И стрелком,
	 * И целью. За длинной глухой стеной края нет в этом радиусе → LOS честно
	 * рвётся (механика XCOM). ⚠️ Это ВЕРХНИЙ предел поиска, а не гарантированная
	 * дистанция: итоговый вынос точки peek = найденный край (≤ этого числа) +
	 * радиус капсулы юнита + PeekOutwardOffset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Peek", meta = (ClampMin = "50"))
	float PeekEdgeMaxDistance = 60.f;

	/**
	 * Вынос точки выглядывания за угол СВЕРХ радиуса капсулы (см) — только
	 * «продышаться» после найденного края, не отдельная дальность. Радиус
	 * капсулы (~34 см) уже даёт физический зазор от стены; это добавка поверх
	 * него. Игрок хотел «край укрытия + 20–30 см» — при капсуле ~34 см это
	 * значение держим маленьким, чтобы сумма не улетала за полметра.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Peek", meta = (ClampMin = "0"))
	float PeekOutwardOffset = 10.f;

	// --- Высота ---
	/** Порог «выше цели» для преимущества высоты (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Height")
	float HeightAdvantageZ = 150.f;

	/** Бонус к точности стрелку сверху (XCOM: +20). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Height")
	float HeightAdvantageAimBonus = 20.f;

	/** У нас ±20 симметрично; в XCOM штрафа снизу нет. При false штраф снизу отключён. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Height")
	bool bSymmetricHeightPenalty = true;
};
