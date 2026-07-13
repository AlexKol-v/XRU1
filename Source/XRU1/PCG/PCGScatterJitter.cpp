#include "PCGScatterJitter.h"

#include "Data/PCGSpatialData.h"
#include "Data/PCGBasePointData.h"
#include "Helpers/PCGHelpers.h"
#include "PCGContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGScatterJitter)

#define LOCTEXT_NAMESPACE "PCGScatterJitterElement"

#if WITH_EDITOR
FText UPCGScatterJitterSettings::GetNodeTooltipText() const
{
    return LOCTEXT("NodeTooltip", "Детерминированно расшатывает трансформ каждой точки (поворот по Yaw, наклон, масштаб, смещение по высоте), чтобы размещённая растительность не выглядела как регулярная сетка. Число точек не меняется.");
}
#endif

FPCGElementPtr UPCGScatterJitterSettings::CreateElement() const
{
    return MakeShared<FPCGScatterJitterElement>();
}

bool FPCGScatterJitterElement::ExecuteInternal(FPCGContext* Context) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FPCGScatterJitterElement::Execute);

    const UPCGScatterJitterSettings* Settings = Context->GetInputSettings<UPCGScatterJitterSettings>();
    check(Settings);

    TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
    TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

    const bool bRandomYaw = Settings->bRandomYaw;
    const float MaxTiltAngle = FMath::Max(0.f, Settings->MaxTiltAngle);
    const float MinScale = FMath::Min(Settings->MinScale, Settings->MaxScale);
    const float MaxScale = FMath::Max(Settings->MinScale, Settings->MaxScale);
    const float MaxZOffset = FMath::Max(0.f, Settings->MaxZOffset);
    const int32 SettingsSeed = Settings->Seed;

    for (const FPCGTaggedData& Input : Inputs)
    {
        FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

        if (!Input.Data || Cast<UPCGSpatialData>(Input.Data) == nullptr)
        {
            PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidInputData", "Invalid input data"));
            continue;
        }

        const UPCGBasePointData* OriginalData = Cast<UPCGSpatialData>(Input.Data)->ToBasePointData(Context);
        if (!OriginalData)
        {
            PCGE_LOG(Error, GraphAndLog, LOCTEXT("NoPointDataInInput", "Unable to get point data from input"));
            continue;
        }

        const int32 NumPoints = OriginalData->GetNumPoints();

        UPCGBasePointData* OutputData = FPCGContext::NewPointData_AnyThread(Context);

        FPCGInitializeFromDataParams InitializeFromDataParams(OriginalData);
        InitializeFromDataParams.bInheritSpatialData = false;
        OutputData->InitializeFromDataWithParams(InitializeFromDataParams);

        // Операция 1:1 — структуру и количество точек копируем как есть, меняем
        // только Transform, поэтому именно его обязательно выделяем под запись.
        OutputData->SetNumPoints(NumPoints, /*bInitializeValues=*/false);
        OutputData->AllocateProperties(OriginalData->GetAllocatedProperties() | EPCGPointNativeProperties::Transform);
        OutputData->CopyUnallocatedPropertiesFrom(OriginalData);

        Output.Data = OutputData;

        const FConstPCGPointValueRanges ReadRanges(OriginalData);
        FPCGPointValueRanges WriteRanges(OutputData, /*bAllocate=*/false);

        for (int32 Index = 0; Index < NumPoints; ++Index)
        {
            // Скопировать все свойства точки, затем переопределить только трансформ.
            WriteRanges.SetFromValueRanges(Index, ReadRanges, Index);

            FTransform PointTransform = ReadRanges.TransformRange[Index];

            // Детерминированный сид: настройки ноды + сид точки + её позиция.
            const int32 FinalSeed = PCGHelpers::ComputeSeed(
                SettingsSeed,
                ReadRanges.SeedRange[Index],
                PCGHelpers::ComputeSeedFromPosition(PointTransform.GetLocation()));
            FRandomStream Rng(FinalSeed);

            FQuat Rotation = PointTransform.GetRotation();

            if (bRandomYaw)
            {
                const float YawRad = FMath::DegreesToRadians(Rng.FRandRange(0.f, 360.f));
                Rotation = FQuat(FVector::UpVector, YawRad) * Rotation;
            }

            if (MaxTiltAngle > 0.f)
            {
                const float TiltAzimuthRad = FMath::DegreesToRadians(Rng.FRandRange(0.f, 360.f));
                const FVector TiltAxis(FMath::Cos(TiltAzimuthRad), FMath::Sin(TiltAzimuthRad), 0.0);
                const float TiltRad = FMath::DegreesToRadians(Rng.FRandRange(0.f, MaxTiltAngle));
                Rotation = FQuat(TiltAxis, TiltRad) * Rotation;
            }

            const float UniformScale = Rng.FRandRange(MinScale, MaxScale);

            FVector Location = PointTransform.GetLocation();
            if (MaxZOffset > 0.f)
            {
                Location.Z += Rng.FRandRange(-MaxZOffset, MaxZOffset);
            }

            PointTransform.SetRotation(Rotation.GetNormalized());
            PointTransform.SetScale3D(PointTransform.GetScale3D() * UniformScale);
            PointTransform.SetLocation(Location);

            WriteRanges.TransformRange[Index] = PointTransform;
        }

        PCGE_LOG(Verbose, LogOnly, FText::Format(LOCTEXT("GenerationInfo", "Jittered {0} points"), NumPoints));
    }

    return true;
}

#undef LOCTEXT_NAMESPACE
