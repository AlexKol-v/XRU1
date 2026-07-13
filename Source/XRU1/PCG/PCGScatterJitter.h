#pragma once

#include "PCGSettings.h"

#include "PCGScatterJitter.generated.h"

/**
 * Кастомная PCG-нода: естественный разброс точек размещения растительности.
 *
 * Назначение в связке Landscape → Foliage → PCG: Surface Sampler выдаёт точки,
 * выровненные по сетке и с одинаковым масштабом — расставленные по ним меши
 * выглядят искусственно. Эта нода детерминированно «расшатывает» каждую точку:
 *   - случайный поворот по Yaw (трава/деревья смотрят в разные стороны);
 *   - случайный равномерный масштаб в диапазоне `[MinScale, MaxScale]`;
 *   - случайный наклон до `MaxTiltAngle` (лёгкий завал ствола);
 *   - случайное смещение по высоте `±MaxZOffset` (утопить/приподнять меш).
 *
 * Число точек не меняется (операция 1:1), поэтому реализация — простой
 * синхронный проход. Случайность детерминирована: сид собирается из сида
 * настроек ноды, сида точки и её позиции через `PCGHelpers::ComputeSeed`,
 * поэтому повторная генерация графа даёт идентичный результат.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), meta = (DisplayName = "Scatter Jitter"))
class XRU1_API UPCGScatterJitterSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    //~Begin UPCGSettings interface
#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override { return FName(TEXT("ScatterJitter")); }
    virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGScatterJitter", "NodeTitle", "Scatter Jitter"); }
    virtual FText GetNodeTooltipText() const override;
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::PointOps; }
#endif

protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override { return Super::DefaultPointInputPinProperties(); }
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }
    virtual FPCGElementPtr CreateElement() const override;
    //~End UPCGSettings interface

public:
    /** Применять случайный поворот по Yaw в диапазоне [0, 360). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation", meta = (PCG_Overridable))
    bool bRandomYaw = true;

    /** Максимальный случайный наклон (градусы) вокруг произвольной горизонтальной оси. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation", meta = (ClampMin = "0", ClampMax = "45", Units = "deg", PCG_Overridable))
    float MaxTiltAngle = 0.f;

    /** Нижняя граница случайного равномерного масштаба. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (ClampMin = "0.01", PCG_Overridable))
    float MinScale = 0.8f;

    /** Верхняя граница случайного равномерного масштаба. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (ClampMin = "0.01", PCG_Overridable))
    float MaxScale = 1.2f;

    /** Максимальное случайное смещение по высоте (±, см). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset", meta = (ClampMin = "0", Units = "cm", PCG_Overridable))
    float MaxZOffset = 0.f;
};

/** Элемент исполнения ноды джиттера разброса. */
class FPCGScatterJitterElement : public IPCGElement
{
protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override;
    virtual EPCGElementExecutionLoopMode ExecutionLoopMode(const UPCGSettings* InSettings) const override { return EPCGElementExecutionLoopMode::SinglePrimaryPin; }
    virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }
};
