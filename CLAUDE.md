# XRU1 — Turn-Based Tactics Prototype (UE 5.7)

> ## ⚠️ Язык общения — РУССКИЙ
> Отвечай пользователю и рассуждай (thinking) **на русском языке**. Технические
> идентификаторы (имена классов, файлов, API UE, команды) оставляй как есть на английском —
> переводить их не нужно. Комментарии в новом C++ коде — тоже на русском (как в перенесённом коде донора).

Курсовой прототип пошаговой тактики в духе XCOM 2 (укрытия, Action Points, Overwatch,
отряд из 4 классов). Собран из шаблона Epic **Top Down** + выборочный перенос систем из
учебного репозитория-донора.

## ⚑ Документация — источник истины: `docs/`
Дизайн, план и правила проекта живут в **`docs/`** (начни с `docs/README.md`):
GDD с правилами боя и числами (`01_GDD.md`), лор и сценарии (`02_LORE_SCRIPT.md`),
карта кода и беклог C++ (`03_CODE_OVERVIEW.md`), поэтапный план (`04_ROADMAP.md`),
гайд по редактору (`05_EDITOR_GUIDE.md`), конвенции (`06_CONVENTIONS.md`).
**Перед любой задачей** сверяйся с ROADMAP (какой этап) и GDD (какие правила);
после выполнения — проставь чекбоксы и синхронизируй доки. Отклонение от GDD
сначала фиксируется в GDD.

## Ключевые факты для агента
- **Разработка идёт на ДВУХ машинах** (общий git-репозиторий) — пути НЕ хардкодить,
  всегда определять относительно корня проекта / через реестр:
  | | Машина 1 | Машина 2 |
  |---|---|---|
  | Проект | `D:/UE5/UnrealProjects/XRU1` | `D:/Unrial_Projects/XRU1` |
  | Движок UE 5.7 | `D:/UE5/UE_5.7` | `D:/Program Files/Epic Games/UE_5.7` |
  | Донор | `D:/UE5/UnrealProjects/cst-3d-gubkin-2026-04` | `D:/Unrial_Projects/cst-3d-gubkin-2026-04` |

  Движок ищется по реестру `HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.7`
  (`InstalledDirectory`) — так делает `Build-XRU1.ps1`. Собирать только на 5.7.
- **Игровой модуль:** `XRU1` (Runtime). Include-пути и зависимости — в
  `Source/XRU1/XRU1.Build.cs`.
- **Донор (только чтение!):** `cst-3d-gubkin-2026-04` (`TopDownCST.uproject`), лежит
  рядом с проектом (см. таблицу). Не изменять — это чужой git-репозиторий, источник
  для копирования. Его полный аудит — в `cst-3d-gubkin-2026-04/AUDIT.md`.

## Команда сборки (редактор ДОЛЖЕН быть закрыт)
```
.\Build-XRU1.ps1              # из корня проекта; сам найдёт движок и проект
.\Build-XRU1.ps1 -StopEditor  # закроет редактор и соберёт
```
Фолбэк, если обёртка не сработала (подставить путь движка своей машины):
```
& "<ENGINE>/Engine/Build/BatchFiles/Build.bat" XRU1Editor Win64 Development `
  -project="<PROJECT>/XRU1.uproject" -waitmutex
```
Если сборка падает с `Unable to build while Live Coding is active` — открыт редактор
Unreal. Закрыть его (или собирать через Live Coding: Ctrl+Alt+F11 в редакторе).

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

Отдельно: **UnrealClaude** (Editor-плагин, https://github.com/Natfii/UnrealClaude,
установлен 2026-07-18) — MCP-мост к редактору для агентов: чтение/правка BP и WBP
графов (`unreal_blueprint_query`/`unreal_blueprint_modify`), ассеты, акторы,
Enhanced Input, скриншот вьюпорта. Работает ТОЛЬКО при открытом редакторе:
плагин поднимает HTTP-сервер `localhost:3000`, stdio-мост — `Plugins/UnrealClaude/
Resources/mcp-bridge/index.js` (нужен `npm install` там после клона; конфиг —
`.mcp.json` в корне, общий для обеих машин). Новая сессия Claude Code подхватит
MCP сама; в текущей (без перезапуска) сервер можно дёргать напрямую:
`curl http://localhost:3000/mcp/status`. Blueprint-правки плагина местами сырые
(«don't rely on fully») — после каждой правки графа верифицировать чтением.

## Соглашения
Полные конвенции — `docs/06_CONVENTIONS.md`. Кратко:
- Новые C++ классы под тюнинг из BP: `UPROPERTY(EditDefaultsOnly/EditAnywhere)` для
  дизайнерских параметров, `UFUNCTION(BlueprintCallable/Pure)` для API; комментарии на русском.
- Боевые способности — наследники `UTacticalAbility`; урон — через
  `UTacticsCombatStatics::ResolveShot`; теги — нативные в `TacticsGameplayTags`.
- GE-компоненты в конструкторе CDO — `CreateDefaultSubobject` + `GEComponents.Add`
  (НЕ `FindOrAddComponent` — фатал при старте редактора).
- Перенос из донора: при конфликте имён — префиксовать, донор не патчить.

Подробности переноса — в `MIGRATION_NOTES.md` (история); текущее состояние кода —
`docs/03_CODE_OVERVIEW.md`.

## Долгие команды — не выполнять самому, просить пользователя
Если команда потенциально долгая (неопределённо долгая или дольше пары минут —
сборка UE, `git push`/`git lfs push` крупных бинарников, `npm install`, полная
переиндексация большого репозитория и т.п.) — **не запускать её самостоятельно
в фоне и не ждать**. Вместо этого дать пользователю готовую команду и попросить
запустить её самому в своём терминале, объяснив, зачем она нужна. Причина:
такие операции (например, LFS-push на медленном канале) могут висеть очень
долго, агент не должен занимать сессию/бюджет ожиданием — пользователю проще
запустить и заняться своими делами, чем ждать, пока агент несколько раз
перепроверяет прогресс.
Короткие фоновые операции (обычная сборка при закрытом редакторе, если раньше
уже собиралось быстро) — по-прежнему можно и нужно выполнять самому.

## Инструменты разработки
- **codebase-memory MCP** — семантический граф кодовой базы (`search_code`, `trace_path`,
  `get_architecture`, `search_graph`, `index_repository` и др.). Индекс локальный (SQLite в
  `~/.cache/codebase-memory-mcp/`), без внешних сервисов/ключей.
  - **Модель установки (важно):** сервер регистрируется **ГЛОБАЛЬНО, один на все проекты** —
    в `~/.claude.json` (ключ `mcpServers.codebase-memory-mcp` → `~/.local/bin/codebase-memory-mcp.exe`).
    Отдельного «проектного» MCP-конфига нет и не нужно. «Подключить проект» = **проиндексировать**
    его: `index_repository` (repo_path = корень проекта). Индексируется только код —
    `.cbmignore` исключает `Content/`, `Binaries/`, `Intermediate/` и пр.
  - **Проверка:** `/mcp` в Claude Code должен показывать `codebase-memory-mcp`. Индекс
    локальный и привязан к пути проекта, т.е. **на каждой машине свой** (машина 1:
    `D-UE5-UnrealProjects-XRU1`). Если MCP/CLI на машине не установлен — работать
    обычными Grep/Glob/Read, это не блокер. Если правил C++ — переиндексируй (`index_repository`).
  - **Как использовать:** вместо чтения множества файлов сначала спрашивай граф — экономит токены.
  - **CLI-фолбэк** (без MCP): `& "$env:USERPROFILE\.local\bin\codebase-memory-mcp.exe" cli index_repository '{"repo_path":"D:/UE5/UnrealProjects/XRU1","mode":"full"}'`
- **Разрешения** (`.claude/settings.json`): allowlist на build/copy/MCP; запись в донора
  запрещена (`deny`).

