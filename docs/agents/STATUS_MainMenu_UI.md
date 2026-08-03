# Статус: главное меню и экраны настроек

Рабочий трекер по [BRIEF_MainMenu.md](BRIEF_MainMenu.md). Обновляется по ходу
работы: что уже собрано, что осталось руками, и **что физически может/не может
агент** через MCP-мост UnrealClaude (проверено экспериментально 2026-07-30).

> **2026-08-02: ЗАДАЧА ЗАКРЫТА.** Цикл экранов собран и прогнан в PIE
> (меню → сложность → интро с реальным роликом → хаб → брифинг → бой → результат
> → возврат), поля CDO проставлены, авто-биндинг из §4 принят как штатная схема.
> Разделы 2 и 4 ниже оставлены как история решения; актуальные возможности и
> ограничения тулинга живут в [AGENT_UNREAL_TOOLING.md](AGENT_UNREAL_TOOLING.md)
> (там же снят блокер вёрстки UMG). Остаток по фронтенду — в
> [../09_UI_HUD.md](../09_UI_HUD.md) §4–5.
>
> **2026-08-01: оба блокера сняты, экраны собраны.** Вёрстка всех пустых WBP
> построена программно собственной editor-библиотекой
> `UXRU1WidgetAuthoringLibrary` (вызов из Python через `execute_script`), а вся
> привязка §2.3 реализована C++ авто-биндингом в `NativeOnInitialized`
> (вариант §4 принят). Созданы `WBP_MissionResult` и `WBP_POIPopup`; CDO-поля
> §2.1 проставлены Python-скриптом (`set_editor_property` на CDO умеет и
> `TSoftObjectPtr`, и `FText` — ограничение касалось только `set_asset_property`).
> Осталось: PIE-прогон чек-листа §4 брифа, затем `GameDefaultMap = L_MainMenu`.
> Рецепты — [AGENT_UNREAL_TOOLING.md](AGENT_UNREAL_TOOLING.md) §5.2.3–5.2.4.

---

## 1. Что готово

### Ассеты и связи (сделано агентом через MCP)

| Ассет | Путь | Состояние |
|---|---|---|
| `WBP_IntroPlayer` | `/Game/XRU1Game/UI/Menus/` | создан (родитель `UIntroPlayerWidget`), **вёрстка пустая** |
| `BP_MenuPlayerController` | `/Game/XRU1Game/Core/` | создан, `RootLayoutClass`=`WBP_PrimaryGameLayout`, `InitialScreenClass`=`WBP_MainMenu` |
| `GM_MainMenu` | `/Game/XRU1Game/Core/` | создан, `PlayerControllerClass`=`BP_MenuPlayerController`, `DefaultPawnClass`=None |
| `L_MainMenu` | `/Game/XRU1Game/Maps/` | создан, пустой, World Settings → `GameMode Override`=`GM_MainMenu` |

Проставленные class-default ссылки:

- `WBP_MainMenu` → `SettingsScreenClass`=`WBP_Settings`, `AboutScreenClass`=`WBP_AboutMenuWidget`, `DifficultyScreenClass`=`WBP_DifficultySelect`
- `WBP_DifficultySelect` → `IntroScreenClass`=`WBP_IntroPlayer`
- `BP_TacticalPlayerController` → `PauseMenuClass`=`WBP_PauseMenuWidget`

### Вёрстка (сделано пользователем)

**`WBP_MainMenu`** — готова. В дереве: `CanvasPanel` → `Overlay`/`Border`/`SizeBox`
/`VerticalBox`, тексты (`CommonTextBlock`/`TextBlock`) и все пять кнопок:

```
Btn_Continue   Btn_NewGame   Btn_Settings   Btn_About   Btn_Quit
```

**`WBP_DifficultySelect`** — готова. В дереве: `Border`/`Overlay`/`SizeBox`
/`HorizontalBox`, `Image`, текст `Back_Text_Title` и четыре кнопки:

```
Btn_Easy   Btn_Medium   Btn_Hard   Btn_Back
```

Имена совпадают с планом — логику можно вешать на них как есть.

---

## 2. Что осталось

### 2.1 Два поля, недоступные MCP (правятся руками, 2 минуты)

| Где | Поле | Значение |
|---|---|---|
| `BP_TacticsGameInstance` → Class Defaults | `MainMenuLevel` | `L_MainMenu` |
| `WBP_AboutMenuWidget` → Class Defaults | `ProjectInfo` | «Проектная работа: прототип пошаговой тактики на UE 5.7 (референс — XCOM 2).» |
| `WBP_AboutMenuWidget` → Class Defaults | `AuthorName` | ФИО, курс/группа |

Причина — см. §3: `set_asset_property` не умеет `TSoftObjectPtr<UWorld>` и `FText`.

### 2.2 Вёрстка оставшихся экранов

Пустые (в `.uasset` нет ни одного именованного виджета): `WBP_Settings`,
`WBP_AboutMenuWidget`, `WBP_PauseMenuWidget`, `WBP_IntroPlayer`.

**Соглашение по именам обязательно** — по ним вешается логика.

#### `WBP_Settings` — главное содержательное место (§3.2 брифа)

Секция «Звук» — пять `Slider`, диапазон **0..1**:

| Виджет | Подпись | Поле структуры |
|---|---|---|
| `Sld_Master` | Общая | `MasterVolume` |
| `Sld_Music` | Музыка | `MusicVolume` |
| `Sld_Sfx` | Эффекты | `SfxVolume` |
| `Sld_UI` | Интерфейс | `UIVolume` |
| `Sld_Voice` | Голос | `VoiceVolume` |

Секция «Изображение»:

| Виджет | Тип | Поле структуры |
|---|---|---|
| `Cmb_Quality` | `ComboBoxString`, опции Низкое/Среднее/Высокое/Эпическое → 0..3 | `ScalabilityLevel` |
| `Sld_ResolutionScale` | `Slider`, диапазон 0.25..1 | `ResolutionScale` |
| `Chk_Fullscreen` | `CheckBox` | `bFullscreen` |
| `Chk_VSync` | `CheckBox` | `bVSync` |
| `Btn_Apply` | `Button` «Применить» | — |

Общие: `Btn_Reset` («Сбросить»), `Btn_Back` («Назад»).

#### `WBP_AboutMenuWidget`

`Txt_Author` (Bind → `AuthorName`), `Txt_ProjectInfo` (multiline, Bind →
`ProjectInfo`), `Btn_Back`.

#### `WBP_PauseMenuWidget`

Тёмная подложка на весь экран, по центру: `Btn_Resume`, `Btn_Settings`,
`Btn_ReturnToMenu`.

#### `WBP_IntroPlayer`

Минимум: фон + `Btn_Skip` (можно прозрачную на весь экран) + подсказка
«Пропустить — клик». Полноценный `MediaPlayer` с `TU_Intro.mp4` — отдельной
задачей, цикл меню он не блокирует.

### 2.3 Привязка логики (графы)

**Это единственное место, где нужен ручной труд помимо вёрстки** — см. §3,
пункт про `OnClicked`. Что к чему подключать:

| Экран | Событие | Вызов |
|---|---|---|
| MainMenu | `Btn_Continue.OnClicked` | `RequestContinue()` |
| MainMenu | `Btn_NewGame.OnClicked` | `RequestNewGame()` |
| MainMenu | `Btn_Settings.OnClicked` | `RequestSettings()` |
| MainMenu | `Btn_About.OnClicked` | `RequestAbout()` |
| MainMenu | `Btn_Quit.OnClicked` | `RequestQuit()` |
| MainMenu | `Btn_Continue.IsEnabled` (Bind) | `CanContinue()` |
| Difficulty | `Btn_Easy/Medium/Hard.OnClicked` | `ChooseDifficulty(Easy/Medium/Hard)` |
| Difficulty | `Btn_Back.OnClicked` | `RequestBack()` |
| Settings | каждый `Sld_*.OnValueChanged` | собрать `FTacticsAudioSettings` целиком → `ApplyAudioSettings(New, bSaveToSlot=false)` |
| Settings | каждый `Sld_*.OnMouseCaptureEnd` | то же, но `bSaveToSlot=true` |
| Settings | `OnInitialized`/`OnActivated` | `GetAudioSettings()`/`GetVideoSettings()` → расставить контролы |
| Settings | `Btn_Apply.OnClicked` | собрать `FTacticsVideoSettings` → `ApplyVideoSettings(New, true)` |
| Settings | `Btn_Reset.OnClicked` | `ResetToDefaults()` → перечитать значения в контролы |
| Settings | `Btn_Back.OnClicked` | `RequestBack()` |
| About | `Btn_Back.OnClicked` | `RequestBack()` |
| Pause | `Btn_Resume.OnClicked` | `RequestResume()` |
| Pause | `Btn_Settings.OnClicked` | `PushScreen(WBP_Settings)` |
| Pause | `Btn_ReturnToMenu.OnClicked` | `RequestReturnToMenu()` |
| Intro | `Btn_Skip.OnClicked` | `FinishIntro()` |

### 2.4 Финал

- Прогнать чек-лист §4 брифа (в т.ч. 1920×1080 и 1920×1200).
- **Только после этого** — `Project Settings → Maps & Modes → Game Default Map = L_MainMenu`.

---

## 3. Возможности MCP-моста: что агент может и не может

Проверено экспериментально на этом проекте (UnrealClaude, редактор открыт,
`http://localhost:3000`). **28 инструментов**, часть из них не проброшена в
stdio-мост и доступна только прямым HTTP-вызовом.

### Может

| Возможность | Как |
|---|---|
| Создавать Blueprint/Widget Blueprint любого родителя | `blueprint` → `create` |
| Править **Class Defaults** (CDO) | `asset` → `set_asset_property`, `asset_path` = `/Game/.../BP_Foo.Default__BP_Foo_C` (именно CDO, не сам ассет) |
| Создавать/открывать/сохранять уровни, править World Settings | `open_level`, `set_property` на акторе `WorldSettings` |
| Спавнить/двигать/удалять акторы, читать состав уровня | `spawn_actor`, `move_actor`, `delete_actors`, `get_level_actors` |
| Искать ассеты, зависимости, референсы | `asset_search`, `asset_dependencies`, `asset_referencers` |
| Читать граф Blueprint (ноды, пины, переменные, функции) | `blueprint_query` |
| Добавлять в граф ноды `CallFunction`, `Branch`, `VariableGet/Set`, `Sequence`, math, `PrintString`, события `BeginPlay/Tick/EndPlay` | `blueprint` → `add_node`/`add_nodes`/`connect_pins` |
| **Выполнять произвольный Python в редакторе** | `execute_script` (`script_type: "python"`) — асинхронно, результат через `task_status`/`task_result` |
| Выполнять C++ через Live Coding | `execute_script` (`script_type: "cpp"`) |
| Консольные команды | `run_console_command` |
| Скриншот вьюпорта | `capture_viewport` |
| Читать Output Log | `get_output_log` |
| Anim Blueprint: стейт-машины, состояния, переходы | `anim` домен |
| Enhanced Input, материалы, персонажи | `enhanced_input`, `material`, `character` домены |

**Python через `execute_script` — ключевая возможность**, о которой легко забыть:
через неё доступно всё, что есть в `unreal` Python API, включая чтение объектов,
которые не отдаёт ни один специализированный инструмент.

> Вызов напрямую (мост его не пробрасывает):
> ```
> POST http://localhost:3000/mcp/tool/execute_script
> {"script_type":"python","description":"...","script_content":"import unreal\n..."}
> ```
> затем `POST /mcp/tool/task_status` и `/mcp/tool/task_result` с `task_id`.
> Скрипт обязан содержать `@Description` в шапке либо параметр `description`.

### Не может (состояние на 2026-07-30; часть снята позже — см.
[AGENT_UNREAL_TOOLING.md](AGENT_UNREAL_TOOLING.md) §4)

| Ограничение | Детали |
|---|---|
| **Редактировать WidgetTree (вёрстку UMG)** | `blueprint` домен видит только `EventGraph`; `graph_name: "WidgetTree"` → `Graph not found`. Кнопки/слайдеры/чекбоксы добавляются **только руками** в Designer |
| **Создавать событие `OnClicked` кнопки в графе** | поддерживаемые типы нод: `CallFunction, Branch, Event(BeginPlay/Tick/EndPlay), VariableGet, VariableSet, Sequence, Add/Subtract/Multiply/Divide, PrintString`. Bound-события виджетов (`K2Node_ComponentBoundEvent`) и `Bind Event`/`Create Delegate` — не поддерживаются. Через Python тоже нельзя: **классы `K2Node*` не экспонированы в Python API** |
| `set_asset_property` для `TSoftObjectPtr<...>` и `FText` | `Unsupported property type` при любом формате значения |
| Python: `unreal.WidgetTree` не экспонирован | `get_editor_property('root_widget')` падает. Обход для **чтения**: `unreal.find_object(bp, 'WidgetTree')` даёт объект дерева, а конкретный виджет — `unreal.find_object(tree, 'Btn_Continue')` (нужно знать имя). Имена можно вытащить из `.uasset` поиском ASCII-строк |
| `open_level` небезопасен при живом пользователе | инструмент общий на весь редактор; повторный `save_as` может сохранить чужой открытый уровень. Сверяться через `get_level_actors` (поле `levelName`) |
| `blueprint_query get_nodes` | отдаёт максимум 100 нод, `offset` игнорируется; длинный граф — только через `search_nodes` |

---

## 4. Открытый вопрос: авто-биндинг кнопок в C++

Ручная привязка по таблице §2.3 — это ~30 связей в графах, из которых
самые муторные — 10 обработчиков слайдеров настроек (`OnValueChanged` +
`OnMouseCaptureEnd`, каждый собирает структуру целиком).

**Альтернатива:** добавить в `UMenuScreenBase`/наследников `NativeOnInitialized()`,
который сам находит виджеты по имени (`GetWidgetFromName("Btn_Continue")`) и
привязывает делегаты к уже существующим `Request*`-методам. Тогда от дизайнера
требуется только вёрстка с правильными именами, графы остаются пустыми.

- **За:** убирает весь ручной труд §2.3, устойчиво к переделкам вёрстки,
  исключает класс ошибок «забыл привязать слайдер».
- **Против:** это C++ (нужна сборка при закрытом редакторе), и бриф §5 просил
  агента ограничиться BP-наследниками и вёрсткой (правда, запрет там сформулирован
  про «новые функции настроек», а не про инфраструктуру привязки).

Решение за пользователем.

---

## 5. Обзор инструментов: чем можно снять текущие ограничения

Разведка 2026-07-30. Два наших блокера — вёрстка UMG и bound-события (`OnClicked`).

### Официальный Unreal MCP от Epic — **не подходит, требует UE 5.8**

Epic встроила [Unreal MCP](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?lang=en-US)
в движок: MCP-сервер внутри процесса редактора, ~80 инструментов, включая полное
авторство Blueprint (любая нода из Blueprint Action Database, разводка пинов),
UMG-виджеты, Behavior Tree, DataTable, материалы, управление PIE. Это закрыло бы
оба блокера сразу.

**Но документация — по UE 5.8**, а проект зафиксирован на 5.7 (`CLAUDE.md`:
«Собирать только на 5.7»). Путь на будущее, если проект когда-нибудь поедет на 5.8.

### VibeUE, ветка `5-7` — **подходит технически**

[github.com/kevinpbuckley/VibeUE](https://github.com/kevinpbuckley/VibeUE),
ветка `5-7` (есть также `5-6`, `5-8`, `master`). MIT. Заявлено:

- `WidgetService.add_component(path, type, name, parent)` — **добавление виджетов
  (Button/Slider/CheckBox/ComboBox) прямо в WidgetTree** → снимает блокер вёрстки;
- `WidgetService.bind_event(path, widget, event, function)` — **привязка `OnClicked`
  и прочих событий виджета к функции** → снимает блокер биндинга;
- `BlueprintService` (119 методов): event dispatchers, `add_delegate_bind_node()`,
  timelines, batch-построение графа с авто-раскладкой;
- MVVM-биндинги, UMG-анимации, PIE-валидация с чтением свойств вживую.

**Цена вопроса, о которой надо знать заранее:**

1. Требуется **аккаунт и API-ключ на `vibeue.com/login`** — внешний сервис и
   регистрация. Агент аккаунты не создаёт, это делает пользователь.
2. Плагин кладётся в `Plugins/` и собирается (`BuildPlugin.bat`) — а разработка идёт
   на **двух машинах с общим git**, значит установка/сборка нужна на обеих.
3. Стабильность на нашей конфигурации не проверена.

### Прочие варианты (не проверялись вживую)

- [UnrealMotionGraphicsMCP](https://github.com/winyunq/UnrealMotionGraphicsMCP) —
  MCP, сфокусированный именно на UMG (структура UI, анимации, интеграция с BP).
- [UMGBridge](https://lobehub.com/mcp/977908569-umgbridgeplugin) — TCP-мост для
  создания/правки UMG-виджетов через MCP.
- [unreal-mcp (GenOrca)](https://github.com/GenOrca/unreal-mcp) — 253 действия
  в 21 домене, расширяется Python-функциями без пересборки редактора.

### Вывод

Полный отказ от ручной работы возможен только через сторонний плагин с внешней
регистрацией (VibeUE) либо через переход на UE 5.8 (официальный MCP). Если этого
не хочется — остаётся выбор из §4: авто-биндинг в C++ (снимает 100% ручной
привязки, вёрстка остаётся за дизайнером) или полностью ручная разводка графов.
