#pragma once

#include "PCGSettings.h"

#include "PCGSlopeHeightFilter.generated.h"

/**
 * Кастомная PCG-нода: отбор точек по углу склона и диапазону высоты.
 *
 * Назначение в связке Landscape → Foliage → PCG: после семплирования
 * поверхности ландшафта (Surface Sampler) каждая точка несёт в своём
 * `Transform` ориентацию по нормали поверхности. Эта нода отбрасывает точки,
 * чей склон круче `MaxSlopeAngle` (или положе `MinSlopeAngle`), а также те,
 * чья высота `Z` выходит за пределы `[MinHeight, MaxHeight]`. Так растительность
 * не появляется на отвесных скалах и за границами биома.
 *
 * Угол склона вычисляется как угол между осью Z трансформа точки (нормаль
 * поверхности) и мировой вертикалью: 0° — идеально ровная площадка, 90° —
 * вертикальная стена.
 *
 * Реализация повторяет канонический паттерн движкового `UPCGDensityFilterSettings`:
 * число выходных точек меньше входного, поэтому используется
 * `FPCGAsync::AsyncProcessingRangeEx` с компактизацией результата.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), meta = (DisplayName = "Filter Points by Slope and Height"))
class XRU1_API UPCGSlopeHeightFilterSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    //~Begin UPCGSettings interface
#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override { return FName(TEXT("SlopeHeightFilter")); }
    virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGSlopeHeightFilter", "NodeTitle", "Filter Points by Slope and Height"); }
    virtual FText GetNodeTooltipText() const override;
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Filter; }
#endif

protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override { return Super::DefaultPointInputPinProperties(); }
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }
    virtual FPCGElementPtr CreateElement() const override;
    //~End UPCGSettings interface

public:
    /** Минимальный допустимый угол склона (градусы). Точки положе отбрасываются. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slope", meta = (ClampMin = "0", ClampMax = "90", Units = "deg", PCG_Overridable))
    float MinSlopeAngle = 0.f;

    /** Максимальный допустимый угол склона (градусы). Точки круче отбрасываются. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slope", meta = (ClampMin = "0", ClampMax = "90", Units = "deg", PCG_Overridable))
    float MaxSlopeAngle = 35.f;

    /** Нижняя граница высоты (мировой Z, см). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height", meta = (Units = "cm", PCG_Overridable))
    float MinHeight = -1000000.f;

    /** Верхняя граница высоты (мировой Z, см). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height", meta = (Units = "cm", PCG_Overridable))
    float MaxHeight = 1000000.f;

    /** Инвертировать результат: оставить именно отбракованные точки. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
    bool bInvertFilter = false;
};

/** Элемент исполнения ноды отбора по склону и высоте. */
class FPCGSlopeHeightFilterElement : public IPCGElement
{
protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override;
    virtual EPCGElementExecutionLoopMode ExecutionLoopMode(const UPCGSettings* InSettings) const override { return EPCGElementExecutionLoopMode::SinglePrimaryPin; }
    virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }
};
