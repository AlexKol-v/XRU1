#include "UnitHUDLayoutData.h"

#include "UnitAttributeWidget.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UUnitHUDLayoutData::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    TSet<FIntPoint> Seen;
    for (int32 Index = 0; Index < WidgetSlots.Num(); ++Index)
    {
        const FUnitHUDWidgetSlot& Slot = WidgetSlots[Index];
        const FIntPoint Key(Slot.VerticalIndex, Slot.HorizontalIndex);

        bool bAlready = false;
        Seen.Add(Key, &bAlready);
        if (bAlready)
        {
            Context.AddError(FText::FromString(FString::Printf(
                TEXT("UnitHUDLayoutData: дубликат пары (VerticalIndex=%d, HorizontalIndex=%d) в слоте #%d"),
                Slot.VerticalIndex, Slot.HorizontalIndex, Index)));
            Result = EDataValidationResult::Invalid;
        }

        if (!Slot.WidgetClass)
        {
            Context.AddWarning(FText::FromString(FString::Printf(
                TEXT("UnitHUDLayoutData: слот #%d не имеет назначенного WidgetClass"), Index)));
        }
    }
    return Result;
}
#endif
