// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class AQuestWaypoint;
class ITableRow;
class SComboButton;
class STableViewBase;
class UWorld;

/**
 * Окно управления точками квеста в мире редактора: создание точек выбранного
 * класса (нативный AQuestWaypoint и его Blueprint-наследники), список
 * существующих точек с правкой WaypointTag и удалением. Регистрируется как
 * nomad-вкладка редактора (как SQuestDebugger).
 */
class SQuestWaypointTool : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SQuestWaypointTool) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    /** Содержимое выпадающего class-picker'а (наследники AQuestWaypoint). */
    TSharedRef<SWidget> MakeClassPickerMenu();

    /** Колбэк выбора класса в class-picker'е. */
    void OnClassPicked(UClass* PickedClass);

    /** Подпись кнопки выбора класса. */
    FText GetSelectedClassText() const;

    /** Спаунит точку выбранного класса в фокусе вьюпорта. */
    FReply OnAddClicked();

    /** Пересобирает список точек по кнопке «Обновить». */
    FReply OnRefreshClicked();

    /** Перечитывает точки из мира редактора в источник списка. */
    void RebuildWaypointList();

    /** Создаёт строку списка для одной точки. */
    TSharedRef<ITableRow> OnGenerateRow(TWeakObjectPtr<AQuestWaypoint> Waypoint,
        const TSharedRef<STableViewBase>& OwnerTable);

    /**
     * Назначает тег точке (через транзакцию). Порядок параметров — под
     * payload-привязку CreateSP: параметр делегата (NewTag) идёт первым,
     * связанный payload (Waypoint) — последним.
     */
    void SetWaypointTag(const FGameplayTag NewTag, TWeakObjectPtr<AQuestWaypoint> Waypoint);

    /** Выделяет точку и наводит на неё камеру вьюпорта. */
    FReply OnFocusClicked(TWeakObjectPtr<AQuestWaypoint> Waypoint);

    /** Удаляет точку из мира редактора (через транзакцию). */
    FReply OnDeleteClicked(TWeakObjectPtr<AQuestWaypoint> Waypoint);

    /** Мир редактора (или nullptr). */
    static UWorld* GetEditorWorld();

    /** Класс, выбранный для создания (по умолчанию AQuestWaypoint). */
    TSubclassOf<AQuestWaypoint> SelectedClass;

    /** Источник списка и сам список. */
    TArray<TWeakObjectPtr<AQuestWaypoint>> Waypoints;
    TSharedPtr<SListView<TWeakObjectPtr<AQuestWaypoint>>> ListView;
    TSharedPtr<SComboButton> ClassPickerButton;
};
