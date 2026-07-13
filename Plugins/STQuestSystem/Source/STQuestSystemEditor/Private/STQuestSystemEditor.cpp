// Copyright Epic Games, Inc. All Rights Reserved.

#include "STQuestSystemEditor.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "SQuestDebugger.h"
#include "SQuestWaypointTool.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FSTQuestSystemEditorModule"

namespace STQuestSystemEditor_Internal
{
    /** Имя nomad-вкладки отладчика квестов. */
    static const FName QuestDebuggerTabName(TEXT("STQuestDebugger"));

    /** Имя nomad-вкладки тула управления точками квеста. */
    static const FName QuestWaypointToolTabName(TEXT("STQuestWaypointTool"));
} // namespace STQuestSystemEditor_Internal

void FSTQuestSystemEditorModule::StartupModule()
{
    // Вкладка отладчика квестов (открывается из меню Tools).
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        STQuestSystemEditor_Internal::QuestDebuggerTabName,
        FOnSpawnTab::CreateStatic(&FSTQuestSystemEditorModule::SpawnQuestDebuggerTab))
        .SetDisplayName(LOCTEXT("QuestDebuggerTabTitle", "Отладчик квестов"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    // Вкладка тула управления точками квеста.
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        STQuestSystemEditor_Internal::QuestWaypointToolTabName,
        FOnSpawnTab::CreateStatic(&FSTQuestSystemEditorModule::SpawnQuestWaypointToolTab))
        .SetDisplayName(LOCTEXT("QuestWaypointToolTabTitle", "Точки квеста"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    // Пункты меню регистрируем, когда ToolMenus готов.
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSTQuestSystemEditorModule::RegisterMenus));
}

void FSTQuestSystemEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    if (FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(STQuestSystemEditor_Internal::QuestDebuggerTabName);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(STQuestSystemEditor_Internal::QuestWaypointToolTabName);
    }
}

TSharedRef<SDockTab> FSTQuestSystemEditorModule::SpawnQuestDebuggerTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SQuestDebugger)
        ];
}

TSharedRef<SDockTab> FSTQuestSystemEditorModule::SpawnQuestWaypointToolTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SQuestWaypointTool)
        ];
}

void FSTQuestSystemEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
    if (!ToolsMenu)
    {
        return;
    }

    FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
        TEXT("STQuestSystem"), LOCTEXT("QuestSection", "Система квестов"));

    Section.AddMenuEntry(
        TEXT("OpenQuestDebugger"),
        LOCTEXT("OpenQuestDebugger", "Отладчик квестов"),
        LOCTEXT("OpenQuestDebuggerTip", "Открыть панель активных квестов PIE-мира."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(STQuestSystemEditor_Internal::QuestDebuggerTabName);
        })));

    Section.AddMenuEntry(
        TEXT("OpenQuestWaypointTool"),
        LOCTEXT("OpenQuestWaypointTool", "Точки квеста"),
        LOCTEXT("OpenQuestWaypointToolTip", "Открыть окно управления точками квеста на карте."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(STQuestSystemEditor_Internal::QuestWaypointToolTabName);
        })));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSTQuestSystemEditorModule, STQuestSystemEditor)
