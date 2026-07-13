// Copyright Epic Games, Inc. All Rights Reserved.

#include "SQuestWaypointTool.h"

#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "Modules/ModuleManager.h"
#include "QuestWaypoint.h"
#include "ScopedTransaction.h"
#include "SGameplayTagCombo.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SQuestWaypointTool"

namespace QuestWaypointTool_Internal
{
    /** Пропускает только наследников AQuestWaypoint (нативные и Blueprint). */
    class FWaypointClassFilter : public IClassViewerFilter
    {
    public:
        virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass,
            TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
        {
            return InClass
                && InClass->IsChildOf(AQuestWaypoint::StaticClass())
                && !InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
        }

        virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions,
            const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData,
            TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
        {
            return InUnloadedClassData->IsChildOf(AQuestWaypoint::StaticClass())
                && !InUnloadedClassData->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
        }
    };
} // namespace QuestWaypointTool_Internal

UWorld* SQuestWaypointTool::GetEditorWorld()
{
    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void SQuestWaypointTool::Construct(const FArguments& InArgs)
{
    SelectedClass = AQuestWaypoint::StaticClass();
    RebuildWaypointList();

    ChildSlot
    [
        SNew(SVerticalBox)

        // Панель: выбор класса, добавление, обновление.
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.f, 0.f, 4.f, 0.f)
            [
                SAssignNew(ClassPickerButton, SComboButton)
                .OnGetMenuContent(this, &SQuestWaypointTool::MakeClassPickerMenu)
                .ButtonContent()
                [
                    SNew(STextBlock)
                    .Text(this, &SQuestWaypointTool::GetSelectedClassText)
                ]
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.f, 0.f, 4.f, 0.f)
            [
                SNew(SButton)
                .Text(LOCTEXT("AddButton", "+ Добавить"))
                .ToolTipText(LOCTEXT("AddTip", "Создать точку выбранного класса в фокусе вьюпорта."))
                .OnClicked(this, &SQuestWaypointTool::OnAddClicked)
            ]

            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            [
                SNullWidget::NullWidget
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SButton)
                .Text(LOCTEXT("RefreshButton", "Обновить"))
                .ToolTipText(LOCTEXT("RefreshTip", "Перечитать точки из текущего уровня."))
                .OnClicked(this, &SQuestWaypointTool::OnRefreshClicked)
            ]
        ]

        // Список точек.
        + SVerticalBox::Slot()
        .FillHeight(1.f)
        .Padding(4.f)
        [
            SAssignNew(ListView, SListView<TWeakObjectPtr<AQuestWaypoint>>)
            .ListItemsSource(&Waypoints)
            .OnGenerateRow(this, &SQuestWaypointTool::OnGenerateRow)
            .SelectionMode(ESelectionMode::Single)
        ]
    ];
}

TSharedRef<SWidget> SQuestWaypointTool::MakeClassPickerMenu()
{
    FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

    FClassViewerInitializationOptions Options;
    Options.Mode = EClassViewerMode::ClassPicker;
    Options.bShowNoneOption = false;
    Options.ClassFilters.Add(MakeShared<QuestWaypointTool_Internal::FWaypointClassFilter>());

    return SNew(SBox)
        .WidthOverride(320.f)
        .HeightOverride(400.f)
        [
            ClassViewerModule.CreateClassViewer(Options,
                FOnClassPicked::CreateSP(this, &SQuestWaypointTool::OnClassPicked))
        ];
}

void SQuestWaypointTool::OnClassPicked(UClass* PickedClass)
{
    if (PickedClass)
    {
        SelectedClass = PickedClass;
    }
    if (ClassPickerButton.IsValid())
    {
        ClassPickerButton->SetIsOpen(false);
    }
}

FText SQuestWaypointTool::GetSelectedClassText() const
{
    const UClass* Class = SelectedClass.Get();
    return FText::Format(LOCTEXT("ClassFmt", "Класс: {0}"),
        FText::FromString(Class ? Class->GetName() : AQuestWaypoint::StaticClass()->GetName()));
}

FReply SQuestWaypointTool::OnAddClicked()
{
    UWorld* World = GetEditorWorld();
    UClass* Class = SelectedClass.Get();
    if (!World || !Class || !GEditor)
    {
        return FReply::Handled();
    }

    // Точка создаётся в фокусе вьюпорта: трасса вперёд от камеры до геометрии,
    // при промахе — на фиксированном расстоянии перед камерой.
    FVector Location = FVector::ZeroVector;
    if (const FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient)
    {
        const FVector ViewLocation = ViewportClient->GetViewLocation();
        const FVector Forward = ViewportClient->GetViewRotation().Vector();

        FHitResult Hit;
        FCollisionQueryParams Params(TEXT("QuestWaypointPlace"), /*bTraceComplex*/ true);
        if (World->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + Forward * 100000.f, ECC_WorldStatic, Params))
        {
            Location = Hit.Location;
        }
        else
        {
            Location = ViewLocation + Forward * 1000.f;
        }
    }

    const FScopedTransaction Transaction(LOCTEXT("AddWaypoint", "Добавить точку квеста"));
    AActor* NewActor = GEditor->AddActor(World->GetCurrentLevel(), Class, FTransform(Location));
    if (NewActor)
    {
        GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
        GEditor->SelectActor(NewActor, /*bSelected*/ true, /*bNotify*/ true);
    }

    RebuildWaypointList();
    return FReply::Handled();
}

FReply SQuestWaypointTool::OnRefreshClicked()
{
    RebuildWaypointList();
    return FReply::Handled();
}

void SQuestWaypointTool::RebuildWaypointList()
{
    Waypoints.Reset();
    if (UWorld* World = GetEditorWorld())
    {
        for (TActorIterator<AQuestWaypoint> It(World); It; ++It)
        {
            Waypoints.Add(*It);
        }
    }
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

TSharedRef<ITableRow> SQuestWaypointTool::OnGenerateRow(TWeakObjectPtr<AQuestWaypoint> Waypoint,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FString Label = Waypoint.IsValid() ? Waypoint->GetActorLabel() : TEXT("<невалидно>");

    return SNew(STableRow<TWeakObjectPtr<AQuestWaypoint>>, OwnerTable)
    [
        SNew(SHorizontalBox)

        // Метка актора.
        + SHorizontalBox::Slot()
        .FillWidth(0.4f)
        .VAlign(VAlign_Center)
        .Padding(4.f, 2.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Label))
        ]

        // Тег точки — инлайн-редактор.
        + SHorizontalBox::Slot()
        .FillWidth(0.6f)
        .VAlign(VAlign_Center)
        .Padding(4.f, 2.f)
        [
            SNew(SGameplayTagCombo)
            .Tag_Lambda([Waypoint]()
            {
                return Waypoint.IsValid() ? Waypoint->WaypointTag : FGameplayTag();
            })
            .OnTagChanged(SGameplayTagCombo::FOnTagChanged::CreateSP(this, &SQuestWaypointTool::SetWaypointTag, Waypoint))
        ]

        // Фокус камеры.
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(2.f)
        [
            SNew(SButton)
            .Text(LOCTEXT("FocusButton", "Фокус"))
            .ToolTipText(LOCTEXT("FocusTip", "Выделить точку и навести на неё камеру вьюпорта."))
            .OnClicked(FOnClicked::CreateSP(this, &SQuestWaypointTool::OnFocusClicked, Waypoint))
        ]

        // Удаление.
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(2.f)
        [
            SNew(SButton)
            .Text(LOCTEXT("DeleteButton", "Удалить"))
            .ToolTipText(LOCTEXT("DeleteTip", "Удалить точку с уровня."))
            .OnClicked(FOnClicked::CreateSP(this, &SQuestWaypointTool::OnDeleteClicked, Waypoint))
        ]
    ];
}

void SQuestWaypointTool::SetWaypointTag(const FGameplayTag NewTag, TWeakObjectPtr<AQuestWaypoint> Waypoint)
{
    AQuestWaypoint* Actor = Waypoint.Get();
    if (!Actor)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("SetWaypointTag", "Изменить тег точки квеста"));
    Actor->Modify();
    Actor->WaypointTag = NewTag;
    Actor->MarkPackageDirty();
}

FReply SQuestWaypointTool::OnFocusClicked(TWeakObjectPtr<AQuestWaypoint> Waypoint)
{
    AQuestWaypoint* Actor = Waypoint.Get();
    if (Actor && GEditor)
    {
        GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
        GEditor->SelectActor(Actor, /*bSelected*/ true, /*bNotify*/ true);
        GEditor->MoveViewportCamerasToActor(*Actor, /*bActiveViewportOnly*/ false);
    }
    return FReply::Handled();
}

FReply SQuestWaypointTool::OnDeleteClicked(TWeakObjectPtr<AQuestWaypoint> Waypoint)
{
    AQuestWaypoint* Actor = Waypoint.Get();
    UWorld* World = GetEditorWorld();
    if (Actor && World && GEditor)
    {
        const FScopedTransaction Transaction(LOCTEXT("DeleteWaypoint", "Удалить точку квеста"));
        GEditor->SelectActor(Actor, /*bSelected*/ false, /*bNotify*/ true);
        World->EditorDestroyActor(Actor, /*bShouldModifyLevel*/ true);
    }

    RebuildWaypointList();
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
