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
| **Верстать UMG** (WidgetTree) | `blueprint` домен видит только `EventGraph`; `graph_name:"WidgetTree"` → `Graph not found`. Python: `unreal.WidgetTree` **не экспонирован**, `get_editor_property('root_widget')` падает. **Обход только для чтения:** `unreal.find_object(bp,'WidgetTree')`, затем `unreal.find_object(tree,'Btn_Continue')` по известному имени. Создание виджетов — руками в Designer |
| **Bound-события виджетов** (`OnClicked`) | `blueprint add_node` знает только `CallFunction`, `Branch`, `Event(BeginPlay/Tick/EndPlay)`, `VariableGet/Set`, `Sequence`, `Add/Subtract/Multiply/Divide`, `PrintString`. `K2Node_ComponentBoundEvent`, `Bind Event`, `Create Delegate` — нет. Python тоже нет: **классы `K2Node*` не экспонированы**. Обход: руками, либо авто-биндинг в C++ (`GetWidgetFromName` + `NativeOnInitialized`) |
| `set_asset_property` для `TSoftObjectPtr<>` и `FText` | `Unsupported property type` при любом формате. Только руками в Class Defaults |
| Запуск PIE | в этом плагине нет управления Play-in-Editor (в официальном MCP от Epic для 5.8 — есть) |
| `blueprint_query get_nodes` | максимум 100 нод, `offset` игнорируется. Длинный граф — только через `search_nodes` |
| **Сборка StateTree** (состояния, задачи, переходы) | `UStateTree::EditorData` не читается через `get_editor_property`, но объект достаётся `unreal.find_object(st, 'StateTreeEditorData_0')`. Дальше тупик: `SubTrees` тоже protected, а задачи лежат во `FInstancedStruct`, который Python не собирает. `Schema` прочитать можно. **Графы StateTree — только руками** |
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
