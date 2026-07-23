#include "UnitHUDLayoutData.h"

#include "UnitAttributeWidget.h"
#include "UnitStatusIconWidget.h"

UUnitHUDLayoutData::UUnitHUDLayoutData()
{
	TacticalStatusWidgetClass = UUnitStatusIconWidget::StaticClass();
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UUnitHUDLayoutData::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = CombineDataValidationResults(
        Super::IsDataValid(Context), EDataValidationResult::Valid);

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

        if (Slot.Size.X <= 0.f || Slot.Size.Y <= 0.f)
        {
            Context.AddError(FText::FromString(FString::Printf(
                TEXT("UnitHUDLayoutData: Size слота #%d должен быть положительным"), Index)));
            Result = EDataValidationResult::Invalid;
        }
        if (Slot.Padding.Left < 0.f || Slot.Padding.Top < 0.f ||
            Slot.Padding.Right < 0.f || Slot.Padding.Bottom < 0.f)
        {
            Context.AddError(FText::FromString(FString::Printf(
                TEXT("UnitHUDLayoutData: Padding слота #%d не может быть отрицательным"), Index)));
            Result = EDataValidationResult::Invalid;
        }
        if (Slot.VerticalIndex < 0 || Slot.HorizontalIndex < 0)
        {
            Context.AddError(FText::FromString(FString::Printf(
                TEXT("UnitHUDLayoutData: V/H индексы слота #%d не могут быть отрицательными"), Index)));
            Result = EDataValidationResult::Invalid;
        }
    }

    const bool bHasExplicitStatusSlot = WidgetSlots.ContainsByPredicate(
        [](const FUnitHUDWidgetSlot& Slot)
        {
            return Slot.WidgetClass &&
                Slot.WidgetClass->IsChildOf(
                    UUnitStatusIconWidget::StaticClass());
        });

    if (bShowTacticalStatusIcon && !bHasExplicitStatusSlot)
    {
        if (!TacticalStatusWidgetClass)
        {
            Context.AddError(INVTEXT(
                "UnitHUDLayoutData: включён tactical status, но WidgetClass не задан"));
            Result = EDataValidationResult::Invalid;
        }
        if (TacticalStatusSlotSize.X <= 0.f || TacticalStatusSlotSize.Y <= 0.f)
        {
            Context.AddError(INVTEXT(
                "UnitHUDLayoutData: TacticalStatusSlotSize должен быть положительным"));
            Result = EDataValidationResult::Invalid;
        }
        if (TacticalStatusSlotPadding.Left < 0.f ||
            TacticalStatusSlotPadding.Top < 0.f ||
            TacticalStatusSlotPadding.Right < 0.f ||
            TacticalStatusSlotPadding.Bottom < 0.f)
        {
            Context.AddError(INVTEXT(
                "UnitHUDLayoutData: TacticalStatusSlotPadding не может быть отрицательным"));
            Result = EDataValidationResult::Invalid;
        }

        // Для auto-колонки конфликт невозможен: runtime добавит её справа от
        // последнего слота. Явный H обязан быть свободен в выбранной строке.
        if (TacticalStatusHorizontalIndex >= 0)
        {
            int32 ResolvedRow = TacticalStatusVerticalIndex;
            if (ResolvedRow < 0)
            {
                ResolvedRow = 0;
                for (const FUnitHUDWidgetSlot& Slot : WidgetSlots)
                {
                    ResolvedRow = FMath::Max(ResolvedRow, Slot.VerticalIndex);
                }
            }

            const FIntPoint StatusKey(
                ResolvedRow, TacticalStatusHorizontalIndex);
            if (Seen.Contains(StatusKey))
            {
                Context.AddError(FText::FromString(FString::Printf(
                    TEXT("UnitHUDLayoutData: tactical status конфликтует со слотом (VerticalIndex=%d, HorizontalIndex=%d)"),
                    StatusKey.X, StatusKey.Y)));
                Result = EDataValidationResult::Invalid;
            }
        }
    }
    return Result;
}
#endif
