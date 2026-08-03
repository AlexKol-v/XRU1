# Справочник агента: чем управлять Unreal из этого проекта

Что агент реально может делать с редактором и проектом, каким каналом, и где
границы. Проверено экспериментально на UE 5.7.4, машина 1, 2026-07-30.

**Читать до того, как решать «это невозможно»** — половина возможностей спрятана
за каналом 2 и в списке инструментов не показывается.

---

## 0. Три канала взаимодействия

| # | Канал | Чем ходить | Когда нужен |
|---|---|---|---|
| 1 | **MCP-мост UnrealClaude** (stdio) | инструменты `mcp__unrealclaude__*` | 90% задач по редактору; удобно, схемы подсказываются |
| 2 | **HTTP напрямую** `localhost:3000` | `curl` / любой HTTP-клиент | всё, что мост не показывает: **Python/C++ в редакторе**, консоль, очередь задач |
| 3 | **Файлы проекта** | `Read`/`Grep`/`Glob`/`Edit`, `git`, `Build-XRU1.ps1`, codebase-memory MCP | C++ исходники, доки, история; чтение `.uasset` в обход редактора |

Каналы 1 и 2 требуют **открытого редактора** (плагин поднимает сервер в процессе
редактора). Канал 3 работает всегда.

---

## 1. Канал 1 — MCP-мост

Конфиг — `.mcp.json` в корне (общий для обеих машин), мост —
`Plugins/UnrealClaude/Resources/mcp-bridge/index.js`.

Мост намеренно ужимает **28 инструментов движка до 16**, чтобы экономить контекст
(«28 tools / ~30K tokens → 16 tools / ~12K tokens»). Классификация — в
`Resources/mcp-bridge/tool-router.js`.

### 1.1 Simple — проходят как есть (12)

`spawn_actor`, `move_actor`, `delete_actors`, `set_property`, `get_level_actors`,
`open_level`, `asset_search`, `asset_dependencies`, `asset_referencers`,
`capture_viewport`, `get_output_log`, `blueprint_query`

### 1.2 Mega — схлопнуты в роутер `unreal_ue` (7)

Вызов: `unreal_ue(domain, operation, params)`. Домены:

| domain | Что умеет |
|---|---|
| `blueprint` | `create`, `add_variable`, `add_function`, `add_node`, `add_nodes`, `connect_pins`, `set_pin_value`, `delete_node`… + все query-операции (`inspect`, `get_nodes`, `search_nodes`, `find_references`…) |
| `anim` | стейт-машины: состояния, переходы, условия, привязка анимаций, валидация |
| `character` | персонажи + `character_data` (DataTable статов) |
| `enhanced_input` | Input Actions, Mapping Contexts |
| `material` | материалы и инстансы |
| `asset` | `set_asset_property`, `save_asset`, `get_asset_info`, `list_assets`, `duplicate`, `rename`, `move`, `delete`, `reimport` |

Query-операции `blueprint` автоматически уходят в `blueprint_query`, остальные —
в `blueprint_modify` (с авто-компиляцией).

**Вставка узла в существующую exec-цепочку — проверенный рецепт (2026-08-03).**
Так были добавлены latent-узлы доворота в `BP_GA_Attack`/`BP_GA_Overwatch`:

1. `add_node` берёт параметры узла ТОЛЬКО из `node_params` и ТОЛЬКО под этими
   ключами: `{"function": "...", "target_class": "/Script/XRU1.TacticalAbility"}`
   для `CallFunction`, `{"variable": "..."}` для `VariableGet/VariableSet`.
   `function_name` игнорируется — ответ «Function name is required».
2. Узлы адресуются либо своим `node_id` (его получают только созданные мостом),
   либо `node_guid` из `blueprint_query` — `FindNodeById` понимает оба.
3. ⚠️ `connect_pins` **не разрывает** прежнюю связь exec-выхода: он делает
   `MakeLinkTo`, и компиляция падает с «У исполнительного выходного контакта не
   может быть несколько подключений». Порядок обязателен: сначала
   `disconnect_pins` старой пары, потом два `connect_pins` (источник → новый,
   новый → прежний приёмник).
4. `get_node_pins` показывает `connected_to` с guid'ами — им и проверять
   результат: правки моста верифицируются чтением, а не доверием к `success`.

### 1.3 Собственные инструменты моста (3)

`unreal_status`, `unreal_get_project_context`, `unreal_get_ue_context`
(последний — встроенная документация UE 5.7 по категориям: `blueprint`, `assets`,
`animation`, `slate`, `actor`, `enhanced_input`, `material`, `replication`,
`ue_core`, `parallel_workflows`).

---

## 2. Канал 2 — HTTP напрямую (важное!)

```
GET  http://localhost:3000/mcp/status        # живость + список инструментов
GET  http://localhost:3000/mcp/tools         # полные схемы параметров
POST http://localhost:3000/mcp/tool/<имя>    # вызов, JSON-параметры в теле
```

Имена и параметры — те же, что у `mcp__unrealclaude__*` (без префикса `unreal_`).

### 2.1 Что спрятано от моста (9 инструментов)

Помечены `HIDDEN_TOOL_NAMES` — **вызываются, но не показываются** в списке:

`execute_script`, `run_console_command`, `cleanup_scripts`, `get_script_history`,
`task_submit`, `task_status`, `task_result`, `task_list`, `task_cancel`

Спрятаны сознательно, чтобы модель тянулась к структурным инструментам, а не к
произвольным скриптам. Это **fallback, а не запрет**.

### 2.2 `execute_script` — главный обходной путь

Выполняет **Python** (`script_type: "python"`) или **C++ через Live Coding**
(`"cpp"`) прямо в редакторе. Через Python доступно всё `unreal` API — то, что не
отдаёт ни один специализированный инструмент.

Работает асинхронно:

```
POST /mcp/tool/execute_script
{"script_type":"python","description":"...","script_content":"import unreal\n..."}
   -> data.task_id

POST /mcp/tool/task_status  {"task_id":"..."}   # ждать completed/failed
POST /mcp/tool/task_result  {"task_id":"..."}   # data.data.output == stdout
```

Подводные камни:

- `task_result` **до** завершения задачи отдаёт **HTTP 400** — сначала опрашивать
  `task_status`.
- Скрипт обязан иметь `@Description` в шапке-комментарии **или** параметр
  `description` — иначе отказ.
- `print()` возвращается в `output`. В хвост приписывается шумовое
  `[WARNING: ... no new actors were created ...]` — это **не ошибка**, фильтровать.
- Скрипты складываются в `Content/UnrealClaude/Scripts/` и копятся в истории
  (`get_script_history`); почистить — `cleanup_scripts`.

**Разрешения.** Каждый скрипт по умолчанию требует подтверждения модальным
диалогом в редакторе (`ScriptPermissionDialog`) — иначе `execute_script` висит,
пока человек не нажмёт кнопку.

В этом проекте **авто-одобрение включено** (по явной просьбе пользователя,
2026-07-30): `Config/DefaultEditor.ini` →
`[/Script/UnrealClaude.UnrealClaudeSettings]` → `bAutoApproveScripts=True`.
Файл в git, значит на второй машине настройка та же. Проверить, что работает:
в логе после запуска скрипта должна быть строка
`Script auto-approved (bAutoApproveScripts=true)`.

Переключить можно в Project Settings → Plugins → Unreal Claude → «Auto-approve
script execution», либо правкой того же ini.

> ⚠️ Безопасность: включённое авто-одобрение означает, что агент исполняет
> произвольный код в редакторе без подтверждения человеком. `run_console_command`
> умеет `py ...`, то есть даёт то же самое в обход `execute_script`. Это
> осознанный компромисс ради потока работы, а не случайная настройка.

> Свойство читается из Python **только под точным C++-именем**:
> `cdo.get_editor_property('bAutoApproveScripts')`. Змеиные варианты
> (`auto_approve_scripts`) не работают — класс не экспонирован в Python, поэтому
> имя не преобразуется. Это общее правило для всех неэкспонированных классов.

---

## 3. Канал 3 — файлы и код

- C++ исходники, доки, конфиги — обычные `Read`/`Grep`/`Glob`/`Edit`.
- **codebase-memory MCP** — семантический граф. Проект проиндексирован как
  `D-UE5-UnrealProjects-XRU1` (4383 узла, 15735 рёбер). Донор — тоже
  (`D-UE5-UnrealProjects-cst-3d-gubkin-2026-04`). Спрашивать граф дешевле, чем
  читать десяток файлов.
- **Чтение `.uasset` в обход редактора.** Имена объектов лежат в таблице имён
  открытым текстом — быстрый способ узнать состав виджета/ассета, не поднимая
  редактор:
  ```bash
  python -c "import re;d=open('WBP_MainMenu.uasset','rb').read();print(sorted({s.decode() for s in re.findall(rb'[ -~]{4,}',d)}))"
  ```
- Сборка — `.\Build-XRU1.ps1 -StopEditor` (редактор должен быть закрыт).

---

## 4. Что агент НЕ может (проверено)

| Ограничение | Детали и обход |
|---|---|
| **Верстать UMG** (WidgetTree) | `blueprint` домен видит только `EventGraph`; `graph_name:"WidgetTree"` → `Graph not found`. Python: `unreal.WidgetTree` **не экспонирован**, `get_editor_property('root_widget')` падает. Чтение: `unreal.find_object(bp,'WidgetTree')`, затем `unreal.find_object(tree,'Btn_Continue')` по известному имени. **Запись СНЯТА 2026-08-01**: собственная editor-библиотека `UXRU1WidgetAuthoringLibrary` строит вёрстку из C++ и вызывается из Python — рецепт §5.2.3 |
| **Bound-события виджетов** (`OnClicked`) | `blueprint add_node` знает только `CallFunction`, `Branch`, `Event(BeginPlay/Tick/EndPlay)`, `VariableGet/Set`, `Sequence`, `Add/Subtract/Multiply/Divide`, `PrintString`. `K2Node_ComponentBoundEvent`, `Bind Event`, `Create Delegate` — нет. Python тоже нет: **классы `K2Node*` не экспонированы**. **Принятый в проекте обход — авто-биндинг в C++** (`NativeOnInitialized` + `BindWidgetOptional` по каноничным именам): так работают все экраны меню (`MenuWidgets.cpp`), графы WBP пустые |
| `set_asset_property` для `TSoftObjectPtr<>` и `FText` | `Unsupported property type` при любом формате. **Обход — Python на CDO** (§5.2.4): `set_editor_property` спокойно принимает и загруженный `UWorld` в `TSoftObjectPtr`, и `unreal.Text(...)` в `FText` |
| ~~Запуск PIE~~ | **СНЯТО 2026-08-02.** У плагина своего инструмента нет, но Python его и не требует: `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)` даёт `editor_request_begin_play()` / `editor_request_end_play()` / `is_in_play_in_editor()`. Запуск отложенный (мир меняется уже после скрипта), поэтому краш из §5.2.5 не срабатывает — проверено многократно. Рецепт прогона — §5.5 |
| `blueprint_query get_nodes` | максимум 100 нод, `offset` игнорируется. Длинный граф — только через `search_nodes` |
| **Сборка StateTree** (состояния, задачи, переходы) | Создать НОВЫЕ states из Python нельзя (`SubTrees`/`Children` protected). Но **правка существующего графа возможна** (проверено 2026-08-01, машина 2): states достаются `find_object` по имени объекта, `Tasks`/`Transitions`/`TasksCompletion`/`Name` читаются и ПИШУТСЯ целыми массивами; значения ВНУТРИ `FInstancedStruct` правятся через `struct.export_text()` → замена текста → `struct.import_text()` (обходит и `CPF_DisableEditOnInstance`). Задачи можно копировать между states целыми элементами `StateTreeEditorNode` (не забыть новый GUID ноды). Полное чтение графа — `AssetTools.export_assets` в T3D (UTF-16). Рецепт — §5.4 |
| Перенос акторов **в persistent** | `move_selected_actors_to_level`/`move_actors_to_level` принимают только `ULevelStreaming`, persistent им не является. Обход: сделать persistent текущим (`LevelEditorSubsystem.set_current_level_by_name`), пересоздать актора там и удалить исходный |

---

## 5. Рецепты

### 5.1 Правка Class Defaults (CDO)

Единственный рабочий путь — адресовать **CDO**, а не сам Blueprint:

```
unreal_ue(domain="asset", operation="set_asset_property", params={
  "asset_path": "/Game/.../BP_Foo.Default__BP_Foo_C",   # ← Default__..._C
  "property": "PlayerControllerClass",
  "value": "/Game/.../BP_Bar.BP_Bar_C"
})
```

`/Game/.../BP_Foo` → «property not found on Blueprint».
`/Game/.../BP_Foo_C` (без `Default__`) → `properties: []`.
После правки — `operation:"save_asset"` на **сам ассет** (без суффикса).

### 5.2 Что лежит в Widget Blueprint

```python
import unreal
bp = unreal.load_asset('/Game/XRU1Game/UI/Menus/WBP_MainMenu')
tree = unreal.find_object(bp, 'WidgetTree')          # root_widget НЕ прочитать
btn  = unreal.find_object(tree, 'Btn_Continue')      # но по имени — можно
print(type(btn).__name__)                            # Button
```
Имена берутся из `.uasset` (см. §3) либо из соглашений проекта.

### 5.2.1 Работа со streaming sublevel и компонентами на инстансах

Проверено 2026-07-30 при сборке сценарных sublevel.

```python
w  = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
sl = unreal.GameplayStatics.get_streaming_level(w, 'SL_Showreel_Tutorial')  # ULevelStreaming
unreal.EditorLevelUtils.make_level_current(sl)      # ждёт ULevelStreaming, НЕ ULevel
unreal.EditorLevelUtils.set_level_visibility(lvl, True, False)  # а тут наоборот ULevel
```

`get_levels(w)` отдаёт `ULevel`; связать с пакетом — `lvl.get_outer().get_name()`.
Флаги стриминга правятся напрямую: `sl.set_editor_property('bShouldBeLoaded', False)`.
Сменить класс стриминга (Always Loaded → Blueprint) можно только связкой
`remove_level_from_world(lvl)` + `add_level_to_world(w, path, unreal.LevelStreamingDynamic)`.

Компонент на **инстансе актора уровня** (сохраняется в `.umap`):

```python
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
handles = sds.k2_gather_subobject_data_for_instance(actor)
params = unreal.AddNewSubobjectParams()
params.set_editor_property('parent_handle', handles[0])
params.set_editor_property('new_class', unreal.ScenarioActorIdComponent)
handle, fail = sds.add_new_subobject(params)
sds.rename_subobject(handle, 'ScenarioId')
comp = actor.get_components_by_class(unreal.ScenarioActorIdComponent)[0]
```

Планирование позиций без глаз — навигация плюс трассировка:
`unreal.NavigationSystemV1.project_point_to_navigation(w, p, None, None, extent)`
(имя именно такое; `k2_project_point_to_navigation` не существует) и
`unreal.SystemLibrary.line_trace_single(...)` на высотах ~90 (грудь) и ~150–175
(голова) — так отличается half cover от full. `HitResult.hit_actor` в Python
нет: владелец берётся через `hit.get_editor_property('component').get_owner()`.
После перемещения `NavMeshBoundsVolume` навмеш пуст, пока не выполнить
`unreal.SystemLibrary.execute_console_command(w, "RebuildNavigation")`.

`unreal.Rotator(a, b, c)` — это `(roll, pitch, yaw)`; безопаснее задавать поля
по именам.

### 5.2.2 Чтение и правка StateTree из Python (2026-08-01)

Чтение всего графа — текстовый экспорт (T3D в UTF-16, читается `open(..., encoding='utf-16')`):

```python
unreal.AssetToolsHelpers.get_asset_tools().export_assets(
    ['/Game/XRU1Game/Quests/ST_Quest_Tutorial'], out_dir)
```

Правка значений (переходы, политика gate, каналы задач):

```python
st   = unreal.load_asset('/Game/XRU1Game/Quests/ST_Quest_Tutorial')
ed   = unreal.find_object(st, 'StateTreeEditorData_0')
root = unreal.find_object(ed, 'StateTreeState_0')      # имена объектов — из T3D
state = unreal.find_object(root, 'StateTreeState_20')
trs = state.get_editor_property('Transitions')
t0 = trs[0]
txt = t0.export_text()                                  # у ЛЮБОЙ StructBase
t0.import_text(txt.replace(...))                        # обходит edit-флаги
trs[0] = t0
state.modify(); state.set_editor_property('Transitions', trs)
```

Ловушки:

- `set_editor_property` на ПОЛЕ структуры (`link.set_editor_property('name',…)`)
  падает «cannot be edited on instances» — менять только через
  `export_text/import_text` и записывать массив целиком на state.
- Новую задачу собрать «из воздуха» нельзя, но можно скопировать готовый
  элемент `Tasks` из другого state (`el.copy()` → `import_text` с правками) —
  обязательно заменить последний `ID=<hex32>` на свежий GUID.
- **Компиляция**: `save_asset` дерево НЕ компилирует (компилирует тулкит по
  Compile/Save в своём окне). Рабочий цикл: `save_asset` →
  `EditorLoadingAndSavingUtils.reload_packages([pkg])` (PostLoad компилирует,
  в логе `Compile StateTree … succeeded`) → ещё раз `save_asset`. Проверять
  повторным T3D-экспортом: новые ID нод должны появиться в `IDToNodeMappings`.

### 5.2.2a Создание СОСТОЯНИЙ StateTree — только через editor-библиотеку (2026-08-02)

Значения правятся из Python (§5.2.2), но **новое состояние оттуда создать
нельзя**: `Children`/`SubTrees` объявлены `UPROPERTY(Instanced)` без
`EditDefaultsOnly`, и Python их не пишет. Пробел закрывает
`UXRU1StateTreeAuthoringLibrary`
([Source/XRU1/Tactics/Editor/](../../Source/XRU1/Tactics/Editor/XRU1StateTreeAuthoringLibrary.h)),
собираемая только в editor-конфигурации (deps `StateTreeEditorModule`,
UncookedOnly-модуль плагина):

```python
L = unreal.XRU1StateTreeAuthoringLibrary
ASSET = '/Game/XRU1Game/Quests/ST_Quest_Tutorial'
L.insert_pause_state_before(ASSET, 'B0_EnterSector', 'B0_Pause', 2.5)  # состояние с движковым Delay Task
L.move_tasks_between_states(ASSET, 'B0_EnterSector', 'B0_Pause', [0, 1])
for row in L.describe_states(ASSET):                                   # имена, задачи, переходы
    print(row)
```

`insert_pause_state_before` идемпотентна (по имени нового состояния),
перенаправляет НА паузу все переходы, которые вели в целевое состояние, и
добавляет свой `OnStateCompleted → Target`. Ассет не сохраняет — обычный цикл
`save_asset` → `reload_packages` → `save_asset` остаётся за вызывающим.

⚠️ Пауза между шагами делается ТОЛЬКО отдельным состоянием: `Transition Delay`
на completion-переходах движок молча сбрасывает (`StateTreeCompiler.cpp`,
«Completion transitions cannot have delay»), а задержка внутри задачи не
останавливает соседние задачи того же состояния.

### 5.2.3 Вёрстка Widget Blueprint через editor-библиотеку (2026-08-01)

MCP/Python не пишут в WidgetTree, но **C++ editor-код проекта — пишет**.
В модуле есть `UXRU1WidgetAuthoringLibrary`
([Source/XRU1/UI/Editor/](../../Source/XRU1/UI/Editor/XRU1WidgetAuthoringLibrary.h)):
функции `Build*Layout(AssetPath, bOverwriteExisting)` строят вёрстку экранов
прямо в WBP-ассете и компилируют его. Вызов из редактора:

```python
import unreal
ok = unreal.XRU1WidgetAuthoringLibrary.build_settings_menu_layout(
    '/Game/XRU1Game/UI/Menus/WBP_Settings', False)   # False = не трогать рукотворную вёрстку
unreal.EditorAssetLibrary.save_asset('/Game/XRU1Game/UI/Menus/WBP_Settings')
```

Ключевые API внутри (для новых функций): `Blueprint->WidgetTree->ConstructWidget<T>()`,
слоты через `Cast<UOverlaySlot/UVerticalBoxSlot/...>(Widget->Slot)`, затем
`FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified` +
`FKismetEditorUtilities::CompileBlueprint`. Зависимости `UnrealEd`/`UMGEditor`
подключены в `XRU1.Build.cs` под `Target.bBuildEditor`. Новый WBP создаётся из
Python фабрикой: `unreal.WidgetBlueprintFactory()` +
`factory.set_editor_property('parent_class', unreal.MissionResultWidget)` +
`AssetTools.create_asset(...)`.

⚠️ В комментариях UHT-заголовков не писать `Btn_*/Sld_*` слитно — «`*` + `/`»
закрывает блочный комментарий и ломает компиляцию (пойман 2026-08-01).

**Не только сборка с нуля.** `AddScreenBackground(AssetPath)` вставляет
`Img_Background` в УЖЕ собранную (в том числе рукотворную) вёрстку, не разрушая
её: Overlay-корню ребёнок сдвигается в индекс 0, Canvas-корню ставится
ZOrder −100, любой другой корень заворачивается в новый Overlay. Тем же приёмом
пишутся будущие «точечные» правки чужих экранов.

⚠️ Виджету, созданному кодом, надо **самому проставить GUID** (`Blueprint->
WidgetVariableNameToGuidMap.Add(Name, FGuid::NewGuid())` перед компиляцией):
иначе компилятор UMG 5.7 на каждый такой виджет пишет ensure «was added but did
not get a GUID» с полным стеком — десятки килобайт мусора в Output Log за один
вызов. Сделано в `FinalizeBlueprint`; перебор — `ForEachObjectWithOuter` по
WidgetTree, как у самого компилятора: обход по живой иерархии
(`WidgetTree->ForEachWidget`) не видит виджеты прежней вёрстки, оставшиеся под
деревом после пересборки.

Полностью шум это не снимает: при ПЕРЕСБОРКЕ экрана остаётся зеркальный ensure
«was deleted but still has a GUID» — служебные виджеты получают автоимена
(`Row_N`, `Box_N`) со сквозного счётчика, поэтому от прошлой сборки в карте GUID
остаются имена, которых больше нет. Движок эти записи чинит сам, на игру они не
влияют. Радикальное лечение — детерминированные имена служебных виджетов (сброс
счётчика на каждую сборку); пока не сделано.

### 5.2.4 `TSoftObjectPtr` и `FText` на CDO из Python (2026-08-01)

`set_asset_property` их не берёт, Python на CDO — берёт:

```python
cdo = unreal.load_object(None, '/Game/XRU1Game/Core/BP_TacticsGameInstance.Default__BP_TacticsGameInstance_C')
cdo.set_editor_property('MainMenuLevel', unreal.load_asset('/Game/XRU1Game/Maps/L_MainMenu'))  # TSoftObjectPtr<UWorld>
cdo.set_editor_property('AuthorName', unreal.Text('Aleksei Beer'))                             # FText
unreal.EditorAssetLibrary.save_asset('/Game/XRU1Game/Core/BP_TacticsGameInstance')
```

### 5.2.5 ⚠️ Смена уровня внутри `execute_script` = краш редактора (2026-08-02)

**Нельзя вызывать `new_level` / `load_level` / любую смену карты из Python в
`execute_script`.** Редактор гарантированно падает:

```
EXCEPTION_ACCESS_VIOLATION
  FScriptExecutionManager::ExecutePython  →  FActorIteratorState  →
  UWorld::AddOnActorSpawnedHandler
```

Причина: после скрипта плагин сам проходит `TActorIterator` по миру, который
закэшировал ДО запуска (ради шумного «no new actors were created»). Если скрипт
сменил уровень, тот мир уже уничтожен. Поймано дважды подряд.

Рабочий порядок создания уровня:

1. `open_level {action:"new", save_current:true}` — новый пустой мир;
2. проверить `get_level_actors` (`levelName` = `Untitled_*`, ~5 служебных акторов);
3. `open_level {action:"save_as", save_path:"/Game/.../L_X"}` — **параметр
   называется `save_path`, не `level_path`**;
4. заселение — уже `execute_script` (spawn/настройка/`save_current_level()`
   безопасны, пока мир не меняется);
5. вернуть пользователя: `open_level {action:"open", level_path:"..."}`.

Ещё две ловушки заселения:

- `EditorActorSubsystem.spawn_actor_from_class` ставит актор **в точку
  вьюпорта**, а не в переданную: позицию задавать `set_actor_location` после
  спавна.
- `WorldSettings` **не входит** в `get_all_level_actors()` — из Python его не
  найти. `DefaultGameMode` ставится инструментом
  `set_property {actor_name:"WorldSettings", property:"DefaultGameMode", value:"/Game/.../BP_X.BP_X_C"}`
  (в `get_level_actors` актор при этом виден).

### 5.2.6 PIE прямо из агента и как увидеть UI (2026-08-02)

Полный прогон фронтенда проходится без человека за мышью.

```python
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.editor_request_begin_play()      # отложенный запуск, безопасен в execute_script
gw  = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
les.editor_request_end_play()
```

- ⚠️ **`capture_viewport` НЕ показывает Slate-UI** — только 3D. Меню на нём
  выглядит чёрным экраном, и это ложная улика. Скриншот с интерфейсом:
  `unreal.SystemLibrary.execute_console_command(gw, 'shot showui')` →
  `Saved/Screenshots/WindowsEditor/ScreenShot*.png`, читается обычным `Read`.
  Мир обязателен **игровой** (`get_game_world()`): с редакторским миром команда
  снимет вьюпорт редактора, а `HighResShot` не рисует UI вовсе.
- **Нажимать кнопки не нужно — можно звать методы виджетов.** Живые экраны
  лежат в дереве корневого лейаута, а он — под GameInstance:
  `find_object(gi, 'WBP_PrimaryGameLayout_C_0')` → `find_object(layout,
  'WidgetTree_0')` → `find_object(tree, 'WBP_MainMenu_C_0')`, дальше любые
  `BlueprintCallable` (`request_new_game()`, `choose_difficulty(...)`,
  `finish_intro()`, `start_operation()`). Индекс лейаута растёт на каждую смену
  уровня — искать перебором `_C_0.._C_5`, иначе найдёшь мёртвый лейаут
  прошлого мира.
- Смена уровня внутри PIE (для проверки travel): консольное `open /Game/…`.
- Контроллер и акторы — как обычно, `unreal.GameplayStatics.get_player_controller(gw, 0)`
  и `get_all_actors_of_class(gw, …)`.

### 5.2.6a ⚠️ CDO НЕ показывает компоненты, добавленные в BP (2026-08-03)

`cdo.get_components_by_class(...)` возвращает **только компоненты из C++
конструктора**. Всё, что дизайнер добавил в редакторе Blueprint, живёт в SCS и на
CDO отсутствует — «компонента нет» по такому скану **ложный вывод** (поймано на
`Gun`: ChildActorComponent с оружием у всех юнитов был на месте).

Надёжный способ — посмотреть на живом акторе, временно заспавнив его в
редакторском мире (child actors там создаются, как в игре):

```python
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
a = eas.spawn_actor_from_class(unreal.load_class(None, '/Game/.../BP_Unit_Assault.BP_Unit_Assault_C'),
                               unreal.Vector(0, 0, -50000))   # подальше от чужой геометрии
for c in a.get_all_child_actors(True):
    print(c.get_name(), [x.get_name() for x in c.get_components_by_class(unreal.SceneComponent)])
eas.destroy_actor(a)
```

Дёшево и без редактора: имена SCS-компонентов лежат в `.uasset` как
`<Name>_GEN_VARIABLE` (§3), а класс child actor — рядом (`ChildActorClass`).

### 5.2.7 Ловушки канала 2 (HTTP), стоившие времени

- **PowerShell + `ConvertTo-Json` + CRLF = «Request body too large».** В PS 5.1
  строка с `\r\n` раздувается при сериализации до мегабайтов (784 символа → 2.2 МБ),
  и сервер отбивает запрос по лимиту 1 МБ. Лечится нормализацией:
  `(Get-Content -Raw $p) -replace "\`r\`n", "\`n"`.
- **`str()` unreal-структур мост режет.** `print("%s" % vector)` даёт пустую
  строку, а `print("%s" % uclass)` — иногда пустую, иногда полную. Значения
  форматировать вручную (`"%.1f" % v.x`, `obj.get_path_name()`), иначе
  «свойство пустое» окажется ложным выводом. Кириллица в `print` местами
  приходит мохнатой — в диагностике безопаснее английский.
- Имена Python-обёрток: `unreal.CollisionResponse` не enum (членов
  `BLOCK`/`ECR_BLOCK` нет) — переключать профилем
  `set_collision_profile_name('BlockAll'/'NoCollision')`. У `HitResult` нет ни
  `impact_point`, ни `get_editor_property('impact_point')`, и
  `SystemLibrary.break_hit_result` не существует: брать `hit.to_tuple()`,
  где `[4]` = Location, `[5]` = ImpactPoint, `[9]` = актор, `[10]` = компонент.

### 5.2.8 ⚠️ Перенос ассетов: две ловушки, стоившие сессии (2026-08-03)

**1. `delete_asset` по package_name УДАЛЯЕТ ЦЕЛЬ РЕДИРЕКТОРА, а не редиректор.**
После `rename_asset` в старом пакете остаётся `ObjectRedirector`. Кажется
логичным подчистить его так:

```python
for a in ar.get_assets(redirector_filter):
    eal.delete_asset(str(a.package_name))       # ❌ путь резолвится ПО редиректору
```

Путь `/Game/.../DA_X` в этот момент ведёт уже на **новый** ассет — и удаляется
он, а редиректор остаётся. Симптом: ассет исчезает из целевой папки, карты
грузятся с `LoadErrors: зависимый пакет ... был недоступен`, ссылки на акторах
молча становятся `None`. Правильно — адресовать объект и проверять класс:

```python
obj = unreal.load_object(None, "%s.%s" % (a.package_name, a.asset_name))
if isinstance(obj, unreal.ObjectRedirector) and not ar.get_referencers(...):
    eal.delete_loaded_asset(obj)
```

Лечение, если уже случилось: закрыть редактор **без сохранения** (в памяти
ссылки уже занулены, сохранение запишет порчу в `.umap`), вернуть ассет
`git checkout HEAD -- <файл>`, перенести заново, починить внутренние ссылки.

**2. Компиляция BP GameInstance рядом с PIE = краш редактора.**
`BlueprintEditorLibrary.compile_blueprint` на `BP_TacticsGameInstance` в одном
скрипте с `editor_request_end_play()` даёт
`EXCEPTION_ACCESS_VIOLATION` в `FSubsystemCollectionBase::Deinitialize`.
Правило: остановка PIE — **отдельный скрипт, в котором больше ничего нет**;
компиляция GameInstance — только при выключенном PIE. Для записи свойства в CDO
компиляция и не нужна: `set_editor_property` + `save_asset` достаточно (значение
переживает перезапуск редактора — проверено).

Ещё из того же захода: `AssetTools.fixup_referencers` **в Python не
существует**. Fix Up делается иначе — открыть каждую ссылающуюся карту
(`open_level`) и сохранить её: `rename_asset` пересохраняет только те пакеты,
что уже загружены, а карты обычно не загружены. `unreal.AssetManager.get()`
из Python тоже недоступен — проверять discovery квестов приходится живым PIE.

### 5.3 Уровни при живом пользователе

Редактор общий: пользователь может переключать уровни параллельно.

- Сверяться `get_level_actors` → поле `levelName` (не доверять `mapName` из ответа
  `open_level`).
- **Не делать повторный `save_as`** на тот же путь без проверки: однажды это
  сохранило чужой открытый уровень поверх свежесозданного (`L_MainMenu` получил
  96 акторов боевой карты).
- Порча ассета лечится быстрее всего через `asset delete` + пересоздание.
- После работы вернуть пользователя на его уровень.

---

## 6. Альтернативные тулинги (разведка 2026-07-30)

| Инструмент | Вердикт |
|---|---|
| [Официальный Unreal MCP от Epic](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?lang=en-US) | ~80 инструментов, полное авторство Blueprint (любая нода) + UMG + PIE. Снял бы оба блокера. **Но требует UE 5.8**, проект на 5.7 |
| [VibeUE, ветка `5-7`](https://github.com/kevinpbuckley/VibeUE) | MIT. `add_component` кладёт виджеты в WidgetTree, `bind_event` вешает `OnClicked` — снимает оба блокера. **Цена:** аккаунт и API-ключ на `vibeue.com`, сборка плагина, установка на обеих машинах |
| [UnrealMotionGraphicsMCP](https://github.com/winyunq/UnrealMotionGraphicsMCP) | заточен под UMG. Не проверялся |
| [unreal-mcp (GenOrca)](https://github.com/GenOrca/unreal-mcp) | 253 действия, расширяется Python без пересборки. Не проверялся |

---

## 7. Как выбирать канал

1. Есть специализированный инструмент (канал 1) — брать его.
2. Нет — проверить, не спрятан ли он (канал 2, §2.1).
3. Всё ещё нет — **Python через `execute_script`**; сперва прочитать, потом писать.
4. Не лезет в Python (`K2Node*`, WidgetTree на запись) — это ручная работа
   человека либо C++.
5. Вопрос про код, а не про редактор — канал 3, начиная с codebase-memory.
