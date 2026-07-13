# XRU1 — Turn-Based Tactics Prototype (UE 5.7)

> ## ⚠️ Язык общения — РУССКИЙ
> Отвечай пользователю и рассуждай (thinking) **на русском языке**. Технические
> идентификаторы (имена классов, файлов, API UE, команды) оставляй как есть на английском —
> переводить их не нужно. Комментарии в новом C++ коде — тоже на русском (как в перенесённом коде донора).

Курсовой прототип пошаговой тактики в духе XCOM 2 (укрытия, Action Points, Overwatch,
отряд из 4 классов). Собран из шаблона Epic **Top Down** + выборочный перенос систем из
учебного репозитория-донора.

## Ключевые факты для агента
- **Движок:** UE **5.7** — `D:/UE5/UE_5.7`. Собирать только на 5.7.
- **Игровой модуль:** `XRU1` (Runtime). Include-пути и зависимости — в
  `Source/XRU1/XRU1.Build.cs`.
- **Донор (только чтение!):** `D:/UE5/UnrealProjects/cst-3d-gubkin-2026-04`
  (`TopDownCST.uproject`). Не изменять — это чужой git-репозиторий, источник для копирования.
  Его полный аудит — в `cst-3d-gubkin-2026-04/AUDIT.md`.

## Команда сборки (редактор ДОЛЖЕН быть закрыт)
```
D:/UE5/UE_5.7/Engine/Build/BatchFiles/Build.bat XRU1Editor Win64 Development \
  -project="D:/UE5/UnrealProjects/XRU1/XRU1.uproject" -waitmutex
```
Если сборка падает с `Unable to build while Live Coding is active` — открыт редактор
Unreal. Закрыть его (или собирать через Live Coding: Ctrl+Alt+F11 в редакторе).

Удобная обёртка одной командой: `.\Build-XRU1.ps1` (предупредит, если открыт редактор) или
`.\Build-XRU1.ps1 -StopEditor` (закроет редактор и соберёт).

## Структура Source/XRU1
- `Characters/`, `UI/`, `Interaction/`, `PCG/` — **перенесено из донора** (иерархия юнита на
  GAS, HUD-виджеты, детектор интеракции, PCG-ноды). API-макрос переименован
  `TOPDOWNCST_API` → `XRU1_API`.
- `Tactics/` — **написано с нуля**: пошаговое ядро (TurnManager, ActionPoints, Cover,
  Overwatch, UnitBase+4 класса, AIController с sight, SaveGame, GameInstance, POI хаба).
- `UI/Menus/` — **написано с нуля**: каркас экранов меню на CommonUI.
- `Variant_Strategy/`, `Variant_TwinStick/`, `XRU1Character/GameMode/PlayerController` —
  из шаблона Top Down (не тактические; можно удалить при чистке).

## Плагины (в `Plugins/`, перенесены из донора)
`STQuestSystem` (туториал-подсказки, mission-select), `TeamManager` (фракции отряд/враги),
`GameplayMessageRouter` (обязателен: от него зависит STQuestSystem). Включены в
`XRU1.uproject` вместе с движковыми `GameplayAbilities`, `CommonUI`, `PCG`.

## Соглашения
- Новые C++ классы под тюнинг из BP: `UPROPERTY(EditDefaultsOnly/EditAnywhere)` для
  дизайнерских параметров, `UFUNCTION(BlueprintCallable/Pure)` для API.
- Финальная боевая логика (урон, AI-решения) НЕ пишется в каркасе — помечена `TODO(next phase)`.
- Перенос из донора: при конфликте имён — префиксовать, донор не патчить.

Подробности переноса — в `MIGRATION_NOTES.md`.

## Инструменты разработки
- **codebase-memory MCP** — семантический граф кодовой базы (`search_code`, `trace_path`,
  `get_architecture`, `search_graph`, `index_repository` и др.). Индекс локальный (SQLite в
  `~/.cache/codebase-memory-mcp/`), без внешних сервисов/ключей.
  - **Модель установки (важно):** сервер регистрируется **ГЛОБАЛЬНО, один на все проекты** —
    в `~/.claude.json` (ключ `mcpServers.codebase-memory-mcp` → `~/.local/bin/codebase-memory-mcp.exe`).
    Отдельного «проектного» MCP-конфига нет и не нужно. «Подключить проект» = **проиндексировать**
    его: `index_repository` (repo_path = корень проекта). Индексируется только код —
    `.cbmignore` исключает `Content/`, `Binaries/`, `Intermediate/` и пр.
  - **Проверка:** `/mcp` в Claude Code должен показывать `codebase-memory-mcp`. Индекс проекта
    `D-UE5-UnrealProjects-XRU1` уже построен. Если правил C++ — переиндексируй (`index_repository`).
  - **Как использовать:** вместо чтения множества файлов сначала спрашивай граф — экономит токены.
  - **CLI-фолбэк** (без MCP): `& "$env:USERPROFILE\.local\bin\codebase-memory-mcp.exe" cli index_repository '{"repo_path":"D:/UE5/UnrealProjects/XRU1","mode":"full"}'`
- **Разрешения** (`.claude/settings.json`): allowlist на build/copy/MCP; запись в донора
  запрещена (`deny`).

