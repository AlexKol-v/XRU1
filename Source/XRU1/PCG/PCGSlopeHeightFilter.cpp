#include "PCGSlopeHeightFilter.h"

#include "Data/PCGSpatialData.h"
#include "Data/PCGBasePointData.h"
#include "Helpers/PCGAsync.h"
#include "PCGContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSlopeHeightFilter)

#define LOCTEXT_NAMESPACE "PCGSlopeHeightFilterElement"

#if WITH_EDITOR
FText UPCGSlopeHeightFilterSettings::GetNodeTooltipText() const
{
    return LOCTEXT("NodeTooltip", "Отбирает точки по углу склона (нормаль поверхности относительно мировой вертикали) и по диапазону высоты Z. Применяется после Surface Sampler для размещения растительности только на пригодных участках ландшафта.");
}
#endif

FPCGElementPtr UPCGSlopeHeightFilterSettings::CreateElement() const
{
    return MakeShared<FPCGSlopeHeightFilterElement>();
}

bool FPCGSlopeHeightFilterElement::ExecuteInternal(FPCGContext* Context) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSlopeHeightFilterElement::Execute);

    const UPCGSlopeHeightFilterSettings* Settings = Context->GetInputSettings<UPCGSlopeHeightFilterSettings>();
    check(Settings);

    TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
    TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

    const float MinSlope = FMath::Min(Settings->MinSlopeAngle, Settings->MaxSlopeAngle);
    const float MaxSlope = FMath::Max(Settings->MinSlopeAngle, Settings->MaxSlopeAngle);
    const float MinHeight = FMath::Min(Settings->MinHeight, Settings->MaxHeight);
    const float MaxHeight = FMath::Max(Settings->MinHeight, Settings->MaxHeight);
    const bool bInvertFilter = Settings->bInvertFilter;

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

        UPCGBasePointData* FilteredData = FPCGContext::NewPointData_AnyThread(Context);

        FPCGInitializeFromDataParams InitializeFromDataParams(OriginalData);
        // Точки будут отфильтрованы, поэтому пространственные данные не наследуем.
        InitializeFromDataParams.bInheritSpatialData = false;
        FilteredData->InitializeFromDataWithParams(InitializeFromDataParams);

        Output.Data = FilteredData;

        auto InitializeFunc = [FilteredData, OriginalData]()
        {
            FilteredData->SetNumPoints(OriginalData->GetNumPoints(), /*bInitializeValues=*/false);
            FilteredData->AllocateProperties(OriginalData->GetAllocatedProperties());
            FilteredData->CopyUnallocatedPropertiesFrom(OriginalData);
        };

        auto AsyncProcessRangeFunc = [OriginalData, FilteredData, MinSlope, MaxSlope, MinHeight, MaxHeight, bInvertFilter](int32 StartReadIndex, int32 StartWriteIndex, int32 Count)
        {
            const FConstPCGPointValueRanges ReadRanges(OriginalData);
            FPCGPointValueRanges WriteRanges(FilteredData, /*bAllocate=*/false);

            int32 NumWritten = 0;

            for (int32 ReadIndex = StartReadIndex; ReadIndex < StartReadIndex + Count; ++ReadIndex)
            {
                const FTransform& PointTransform = ReadRanges.TransformRange[ReadIndex];

                // Ось Z трансформа точки — нормаль поверхности под этой точкой.
                const FVector SurfaceNormal = PointTransform.GetUnitAxis(EAxis::Z);
                const double CosAngle = FMath::Clamp(FVector::DotProduct(SurfaceNormal, FVector::UpVector), -1.0, 1.0);
                const double SlopeAngleDeg = FMath::RadiansToDegrees(FMath::Acos(CosAngle));
                const double HeightZ = PointTransform.GetLocation().Z;

                const bool bSlopeInRange = (SlopeAngleDeg >= MinSlope && SlopeAngleDeg <= MaxSlope);
                const bool bHeightInRange = (HeightZ >= MinHeight && HeightZ <= MaxHeight);
                const bool bAccepted = bSlopeInRange && bHeightInRange;

                if (bAccepted != bInvertFilter)
                {
                    const int32 WriteIndex = StartWriteIndex + NumWritten;
                    WriteRanges.SetFromValueRanges(WriteIndex, ReadRanges, ReadIndex);
                    ++NumWritten;
                }
            }

            return NumWritten;
        };

        auto MoveDataRangeFunc = [FilteredData](int32 RangeStartIndex, int32 MoveToIndex, int32 NumElements)
        {
            FilteredData->MoveRange(RangeStartIndex, MoveToIndex, NumElements);
        };

        auto FinishedFunc = [FilteredData](int32 NumWritten)
        {
            FilteredData->SetNumPoints(NumWritten);
        };

        FPCGAsyncState* AsyncState = Context ? &Context->AsyncState : nullptr;
        FPCGAsync::AsyncProcessingRangeEx(
            AsyncState,
            OriginalData->GetNumPoints(),
            InitializeFunc,
            AsyncProcessRangeFunc,
            MoveDataRangeFunc,
            FinishedFunc,
            /*bEnableTimeSlicing=*/false);

        PCGE_LOG(Verbose, LogOnly, FText::Format(LOCTEXT("GenerationInfo", "Generated {0} points out of {1} source points"), FilteredData->GetNumPoints(), OriginalData->GetNumPoints()));
    }

    return true;
}

#undef LOCTEXT_NAMESPACE
