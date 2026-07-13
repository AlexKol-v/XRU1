// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SDockTab;
class FSpawnTabArgs;

/**
 * Класс редакторского модуля плагина STQuestSystem.
 * Регистрирует утилиты авторинга и отладки (Лекция 3): отладчик квестов
 * (nomad-вкладка), пункты меню Tools и инструмент расстановки точек.
 * Валидатор и фабрика ассетов обнаруживаются движком автоматически.
 */
class FSTQuestSystemEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    /** Создаёт содержимое вкладки отладчика квестов. */
    static TSharedRef<SDockTab> SpawnQuestDebuggerTab(const FSpawnTabArgs& Args);

    /** Создаёт содержимое вкладки тула управления точками квеста. */
    static TSharedRef<SDockTab> SpawnQuestWaypointToolTab(const FSpawnTabArgs& Args);

    /** Регистрирует пункты меню Tools (вызывается через ToolMenus). */
    void RegisterMenus();
};
