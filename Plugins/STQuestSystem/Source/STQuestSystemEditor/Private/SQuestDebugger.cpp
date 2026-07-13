// Copyright Epic Games, Inc. All Rights Reserved.

#include "SQuestDebugger.h"

#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "QuestDefinition.h"
#include "QuestInstance.h"
#include "QuestSubsystem.h"
#include "QuestTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SQuestDebugger"

namespace SQuestDebugger_Internal
{
    /** Человекочитаемое имя состояния квеста. */
    FString StateToString(EQuestState State)
    {
        switch (State)
        {
        case EQuestState::Inactive:  return TEXT("Inactive");
        case EQuestState::Available: return TEXT("Available");
        case EQuestState::Active:    return TEXT("Active");
        case EQuestState::Completed: return TEXT("Completed");
        case EQuestState::Failed:    return TEXT("Failed");
        default:                     return TEXT("?");
        }
    }
} // namespace SQuestDebugger_Internal

UQuestSubsystem* SQuestDebugger::GetQuestSubsystem()
{
    UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
}

FString SQuestDebugger::GetPawnLabel(const APawn* Pawn)
{
    return Pawn ? Pawn->GetActorLabel() : TEXT("<нет>");
}

FString SQuestDebugger::FormatQuest(const UQuestDefinition* Quest)
{
    if (!Quest)
    {
        return TEXT("<невалидно>");
    }

    const FGameplayTag QuestId = Quest->QuestId;
    const UQuestSubsystem* Subsystem = GetQuestSubsystem();
    const EQuestState State = Subsystem ? Subsystem->GetQuestState(QuestId) : EQuestState::Inactive;
    const UQuestInstance* Instance = Subsystem ? Subsystem->GetQuestInstance(QuestId) : nullptr;
    const int32 Percent = Instance ? FMath::RoundToInt(Instance->Progress.CompletionPercent * 100.f) : 0;

    FString Out = FString::Printf(TEXT("%s  [%s %d%%]"),
        *QuestId.ToString(), *SQuestDebugger_Internal::StateToString(State), Percent);

    if (Instance)
    {
        for (const FObjectiveProgress& Objective : Instance->Progress.Objectives)
        {
            Out += FString::Printf(TEXT("\n   - %s: %d/%d"),
                *Objective.ObjectiveId.ToString(), Objective.Current, Objective.Required);
        }
    }
    return Out;
}

void SQuestDebugger::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SVerticalBox)

        // Статус (заглушка, если нет PIE).
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4.f)
        [
            SNew(STextBlock)
            .Text(this, &SQuestDebugger::GetStatusText)
            .AutoWrapText(true)
        ]

        // Две колонки.
        + SVerticalBox::Slot()
        .FillHeight(1.f)
        .Padding(4.f)
        [
            SNew(SHorizontalBox)

            // Левая: все квесты, известные системе (реестр).
            + SHorizontalBox::Slot()
            .FillWidth(0.5f)
            .Padding(0.f, 0.f, 4.f, 0.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(2.f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("AllHeader", "Все квесты системы"))
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.f)
                [
                    SAssignNew(AllListView, SListView<TWeakObjectPtr<UQuestDefinition>>)
                    .ListItemsSource(&AllQuests)
                    .OnGenerateRow(this, &SQuestDebugger::OnGenerateRow)
                    .SelectionMode(ESelectionMode::None)
                ]
            ]

            // Правая: персонаж + его квесты.
            + SHorizontalBox::Slot()
            .FillWidth(0.5f)
            .Padding(4.f, 0.f, 0.f, 0.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(2.f)
                [
                    SAssignNew(CharacterCombo, SComboBox<TWeakObjectPtr<APawn>>)
                    .OptionsSource(&PawnOptions)
                    .OnGenerateWidget(this, &SQuestDebugger::OnGenerateCharacterWidget)
                    .OnSelectionChanged(this, &SQuestDebugger::OnCharacterSelected)
                    [
                        SNew(STextBlock)
                        .Text(this, &SQuestDebugger::GetSelectedCharacterText)
                    ]
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.f)
                [
                    SAssignNew(OwnerListView, SListView<TWeakObjectPtr<UQuestDefinition>>)
                    .ListItemsSource(&OwnerQuests)
                    .OnGenerateRow(this, &SQuestDebugger::OnGenerateRow)
                    .SelectionMode(ESelectionMode::None)
                ]
            ]
        ]
    ];

    // Первичное наполнение + авто-обновление.
    RefreshAll();
    RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SQuestDebugger::OnRefreshTimer));
}

EActiveTimerReturnType SQuestDebugger::OnRefreshTimer(double InCurrentTime, float InDeltaTime)
{
    RefreshAll();
    return EActiveTimerReturnType::Continue;
}

void SQuestDebugger::RefreshAll()
{
    const UQuestSubsystem* Quests = GetQuestSubsystem();

    // Левая колонка — все известные системе квесты (реестр определений).
    AllQuests.Reset();
    if (Quests)
    {
        for (UQuestDefinition* Definition : Quests->GetAllKnownQuests())
        {
            if (Definition)
            {
                AllQuests.Add(Definition);
            }
        }
    }

    RebuildPawnOptions();

    // Правая колонка — определения квестов, которыми владеет выбранный персонаж.
    OwnerQuests.Reset();
    if (Quests && SelectedPawn.IsValid())
    {
        for (UQuestInstance* Instance : Quests->GetQuestsForOwner(SelectedPawn.Get()))
        {
            if (Instance && Instance->Definition)
            {
                OwnerQuests.Add(Instance->Definition);
            }
        }
    }

    if (AllListView.IsValid())
    {
        AllListView->RequestListRefresh();
    }
    if (OwnerListView.IsValid())
    {
        OwnerListView->RequestListRefresh();
    }
}

void SQuestDebugger::RebuildPawnOptions()
{
    UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;

    TArray<TWeakObjectPtr<APawn>> NewOptions;
    if (World)
    {
        for (TActorIterator<APawn> It(World); It; ++It)
        {
            NewOptions.Add(*It);
        }
    }

    // Обновляем комбо только при изменении набора, чтобы не дёргать открытый список.
    bool bChanged = NewOptions.Num() != PawnOptions.Num();
    for (int32 Index = 0; !bChanged && Index < NewOptions.Num(); ++Index)
    {
        bChanged = NewOptions[Index] != PawnOptions[Index];
    }
    if (bChanged)
    {
        PawnOptions = MoveTemp(NewOptions);
        if (CharacterCombo.IsValid())
        {
            CharacterCombo->RefreshOptions();
        }
    }

    // Поддерживаем валидный выбор: по умолчанию — пешка игрока, иначе первая.
    if (!SelectedPawn.IsValid() && PawnOptions.Num() > 0)
    {
        APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
        APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
        SelectedPawn = PlayerPawn ? PlayerPawn : PawnOptions[0].Get();
        if (CharacterCombo.IsValid() && SelectedPawn.IsValid())
        {
            CharacterCombo->SetSelectedItem(SelectedPawn);
        }
    }
}

TSharedRef<ITableRow> SQuestDebugger::OnGenerateRow(TWeakObjectPtr<UQuestDefinition> Quest,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<TWeakObjectPtr<UQuestDefinition>>, OwnerTable)
    [
        SNew(STextBlock)
        .AutoWrapText(true)
        .Text_Lambda([Quest]()
        {
            return FText::FromString(FormatQuest(Quest.Get()));
        })
    ];
}

TSharedRef<SWidget> SQuestDebugger::OnGenerateCharacterWidget(TWeakObjectPtr<APawn> Pawn)
{
    return SNew(STextBlock)
        .Text(FText::FromString(GetPawnLabel(Pawn.Get())));
}

void SQuestDebugger::OnCharacterSelected(TWeakObjectPtr<APawn> Pawn, ESelectInfo::Type SelectInfo)
{
    SelectedPawn = Pawn;
    RefreshAll();
}

FText SQuestDebugger::GetSelectedCharacterText() const
{
    const APawn* Pawn = SelectedPawn.Get();
    return Pawn
        ? FText::FromString(GetPawnLabel(Pawn))
        : LOCTEXT("PickCharacter", "(выберите персонажа)");
}

FText SQuestDebugger::GetStatusText() const
{
    return GetQuestSubsystem()
        ? FText::GetEmpty()
        : LOCTEXT("NoPIE", "Запустите игру (PIE), чтобы увидеть квесты.");
}

#undef LOCTEXT_NAMESPACE
