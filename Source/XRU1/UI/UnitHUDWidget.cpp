#include "UnitHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#include "UnitAttributeWidget.h"
#include "UnitHUDLayoutData.h"

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

        CurrentRow->AddChildToHorizontalBox(SizeWrap);
        ChildWidgets.Add(Child);
    }
}
