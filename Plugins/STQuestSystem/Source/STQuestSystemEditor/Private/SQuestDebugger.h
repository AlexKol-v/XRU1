// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/SListView.h"

class APawn;
class ITableRow;
class STableViewBase;
class SWidget;
class UQuestDefinition;
class UQuestSubsystem;

/**
 * Панель отладчика квестов PIE-мира. Два списка: слева — все квесты, известные
 * системе (реестр определений, с живым состоянием/прогрессом), справа — квесты
 * выбранного персонажа (комбо со всеми пешками — игрок и NPC). Данные
 * обновляются автоматически по таймеру; прогресс в строках — через привязки.
 * Регистрируется как nomad-вкладка редактора.
 */
class SQuestDebugger : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SQuestDebugger) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    /** Периодический колбэк авто-обновления. */
    EActiveTimerReturnType OnRefreshTimer(double InCurrentTime, float InDeltaTime);

    /** Перечитывает реестр квестов, пешки и квесты выбранного персонажа. */
    void RefreshAll();

    /** Пересобирает список пешек PIE-мира, поддерживая корректный выбор. */
    void RebuildPawnOptions();

    /** Строка списка квестов (текст обновляется через Text_Lambda). */
    TSharedRef<ITableRow> OnGenerateRow(TWeakObjectPtr<UQuestDefinition> Quest,
        const TSharedRef<STableViewBase>& OwnerTable);

    /** Виджет элемента комбо персонажей. */
    TSharedRef<SWidget> OnGenerateCharacterWidget(TWeakObjectPtr<APawn> Pawn);

    /** Колбэк выбора персонажа в комбо. */
    void OnCharacterSelected(TWeakObjectPtr<APawn> Pawn, ESelectInfo::Type SelectInfo);

    /** Подпись текущего выбранного персонажа на кнопке комбо. */
    FText GetSelectedCharacterText() const;

    /** Статус-строка вверху (заглушка, если нет PIE). */
    FText GetStatusText() const;

    /** Подсистема квестов PIE-мира (или nullptr). */
    static UQuestSubsystem* GetQuestSubsystem();

    /** Текст строки по определению квеста: QuestId, живое состояние, % и цели. */
    static FString FormatQuest(const UQuestDefinition* Quest);

    /** Читаемая подпись пешки. */
    static FString GetPawnLabel(const APawn* Pawn);

    /** Источники списков и комбо. */
    TArray<TWeakObjectPtr<UQuestDefinition>> AllQuests;
    TArray<TWeakObjectPtr<UQuestDefinition>> OwnerQuests;
    TArray<TWeakObjectPtr<APawn>> PawnOptions;
    TWeakObjectPtr<APawn> SelectedPawn;

    TSharedPtr<SListView<TWeakObjectPtr<UQuestDefinition>>> AllListView;
    TSharedPtr<SListView<TWeakObjectPtr<UQuestDefinition>>> OwnerListView;
    TSharedPtr<SComboBox<TWeakObjectPtr<APawn>>> CharacterCombo;
};
