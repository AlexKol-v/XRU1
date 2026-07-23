#include "UnitHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#include "UnitAttributeWidget.h"
#include "UnitHUDLayoutData.h"
#include "UnitStatusIconWidget.h"

void UUnitHUDWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (!RootVBox && WidgetTree)
    {
        RootVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootVBox"));
        WidgetTree->RootWidget = RootVBox;
    }
}

void UUnitHUDWidget::InitFromLayout(UUnitHUDLayoutData* Layout, UAbilitySystemComponent* InASC)
{
    if (!RootVBox || !Layout)
    {
        return;
    }

    // Полная пересборка: чистим предыдущее состояние.
    RootVBox->ClearChildren();
    ChildWidgets.Reset();

    // Сортируем стабильно: сначала по строке (V), затем по колонке (H).
    TArray<FUnitHUDWidgetSlot> Slots = Layout->WidgetSlots;

    // Tactical status — системный consumer того же статуса, что показывается
    // в Unit Flag. По умолчанию встаёт справа от последнего слота последней строки,
    // но конкретный DataAsset может задать явные V/H, размер и padding.
    const bool bHasExplicitStatusSlot = Slots.ContainsByPredicate(
        [](const FUnitHUDWidgetSlot& SlotDef)
        {
            return SlotDef.WidgetClass &&
                SlotDef.WidgetClass->IsChildOf(UUnitStatusIconWidget::StaticClass());
        });
    if (Layout->bShowTacticalStatusIcon &&
        Layout->TacticalStatusWidgetClass &&
        !bHasExplicitStatusSlot)
    {
        int32 LastRow = 0;
        for (const FUnitHUDWidgetSlot& Existing : Slots)
        {
            LastRow = FMath::Max(LastRow, Existing.VerticalIndex);
        }

        const int32 StatusRow = Layout->TacticalStatusVerticalIndex >= 0
            ? Layout->TacticalStatusVerticalIndex
            : LastRow;
        int32 AppendColumn = 0;
        for (const FUnitHUDWidgetSlot& Existing : Slots)
        {
            if (Existing.VerticalIndex == StatusRow)
            {
                AppendColumn =
                    FMath::Max(AppendColumn, Existing.HorizontalIndex + 1);
            }
        }

        FUnitHUDWidgetSlot StatusSlot;
        StatusSlot.WidgetClass = Layout->TacticalStatusWidgetClass.Get();
        StatusSlot.Size = Layout->TacticalStatusSlotSize;
        StatusSlot.Color = Layout->TacticalStatusColor;
        StatusSlot.Padding = Layout->TacticalStatusSlotPadding;
        StatusSlot.VerticalIndex = StatusRow;
        StatusSlot.HorizontalIndex =
            Layout->TacticalStatusHorizontalIndex >= 0
                ? Layout->TacticalStatusHorizontalIndex
                : AppendColumn;
        Slots.Add(StatusSlot);
    }

    Slots.StableSort([](const FUnitHUDWidgetSlot& A, const FUnitHUDWidgetSlot& B)
    {
        if (A.VerticalIndex != B.VerticalIndex) return A.VerticalIndex < B.VerticalIndex;
        return A.HorizontalIndex < B.HorizontalIndex;
    });

    UHorizontalBox* CurrentRow = nullptr;
    int32 CurrentRowIndex = INT32_MIN;

    // Имя SlotDef — чтобы не затенять UWidget::Slot (UPanelSlot* в базе UWidget).
    for (const FUnitHUDWidgetSlot& SlotDef : Slots)
    {
        if (!SlotDef.WidgetClass)
        {
            continue;
        }

        // На каждую новую V-строку — новый HBox.
        if (SlotDef.VerticalIndex != CurrentRowIndex)
        {
            CurrentRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
            RootVBox->AddChildToVerticalBox(CurrentRow);
            CurrentRowIndex = SlotDef.VerticalIndex;
        }

        UUnitAttributeWidget* Child =
            CreateWidget<UUnitAttributeWidget>(GetOwningPlayer(), SlotDef.WidgetClass);
        if (!Child)
        {
            continue;
        }

        Child->ApplyStyle(SlotDef.Size, SlotDef.Color);
        Child->InitFromASC(InASC);

        // Принудительный размер слота — через SizeBox-обёртку.
        USizeBox* SizeWrap = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        SizeWrap->SetWidthOverride (SlotDef.Size.X);
        SizeWrap->SetHeightOverride(SlotDef.Size.Y);
        SizeWrap->AddChild(Child);

        if (UHorizontalBoxSlot* RowSlot =
            CurrentRow->AddChildToHorizontalBox(SizeWrap))
        {
            RowSlot->SetPadding(SlotDef.Padding);
            RowSlot->SetVerticalAlignment(VAlign_Center);
        }
        ChildWidgets.Add(Child);
    }
}
