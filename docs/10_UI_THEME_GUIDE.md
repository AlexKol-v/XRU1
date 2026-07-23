# Единая UI-тема: заполнение и подключение интерфейсов

Актуально на **2026-07-23**. Документ закрывает ручную часть этапов 2C–3 и
полировку боевого HUD этапа 6/B.9. Источник визуальных данных проекта — один
ассет:

`/Game/XRU1Game/Data/DA_TacticalHUDStyle`

Класс ассета исторически называется `UTacticalHUDStyleData`, но теперь хранит
тему **всего** UI: иконки действий и статусов, портреты, классовые иконки,
экранный арт, интро, палитру, размеры, независимые отступы блоков и оформление
контрастных подложек world-space status badge и счётчика врагов.

## 0. Что уже есть в проекте (аудит через UnrealClaude MCP)

Проверено в открытом Editor **2026-07-23**. Этот список важнее старых
инструкций «создать с нуля»:

| Уже есть | Фактическое состояние |
|---|---|
| `DA_TacticalHUDStyle` | существует в `/Game/XRU1Game/Data`; ссылается на все 35 текущих `Texture2D` UI и `FMS_TU_Intro` |
| `BP_TacticsGameInstance` | существует, наследник `UTacticsGameInstance`, `UITheme` указывает на `DA_TacticalHUDStyle`; класс уже прописан в `DefaultEngine.ini`, там же назначен `CommonGameViewportClient`; после создания карт останется заполнить только `HubLevel` и `MainMenuLevel` |
| `WBP_TacticalHUD` | существует на `UTacticalHUDWidget`; кнопки, размеры glyph и палитры уже применяет C++ `ApplyStyle`; `Create WBP Unit Portrait Widget.ReturnValue` подключён и к `SquadPanel`, и к массиву `Portraits`. Счётчик уже использует `EnemyCountIcon` и внутренний `EnemyCounterBackground`; visual properties читает C++ из темы |
| `WBP_UnitPortrait` | горизонтальный `Unit Flag` собран; `Refresh` получает общую тему, применяет portrait/class/status/frame/layout; четыре класса и матрица статусов приняты вручную. C++ создаёт независимый `CoverSizeBox → CoverIcon` в `HeaderRow`, если ветки ещё нет |
| HUD над головой | существующие HP/AP/cover слоты сохранены; C++ добавляет независимый `UUnitStatusIconWidget` через настройки `UUnitHUDLayoutData`. Полный build/restart пройден; одновременный cover + status и тёмная подложка подтверждены пользователем |
| BP-классы отряда | созданы `BP_Unit_Assault`, `BP_Unit_Sniper`, `BP_Unit_Medic` (`Unit_Healer`) и `BP_Unit_Tank`; на `Lvl_TopDown` уже стоит по одному бойцу каждого класса |
| `WBP_PrimaryGameLayout` | существует на `UPrimaryGameLayout`; четыре стека надо только сверить по именам из §3.2 Editor Guide |
| Базовые меню | существуют `WBP_MainMenu`, `WBP_DifficultySelect`, `WBP_Settings`, `WBP_AboutMenuWidget`, `WBP_PauseMenuWidget` |
| Пока отсутствует | `WBP_IntroPlayer`, `MP/MT/M_IntroVideo`, `CBS_*`/`CTS_*`, брифинги, результат, DemoComplete, `BP_MenuPlayerController`, `GM_MainMenu`, `L_MainMenu` |

У `WBP_UnitPortrait` удалены четыре прямые hard-reference на старые status-
текстуры. Осталась только `T_UI_SelectionFrame_White` в Designer как аварийный
preview-fallback; в runtime рамку всё равно задаёт `DA_TacticalHUDStyle`.
У `WBP_MainMenu` и `WBP_DifficultySelect` пока остаются прямые background-
текстуры — их очищать только после подключения экранного арта темы.

## 1. Почему схема устроена так

- `BP_TacticsGameInstance.UITheme` — единственная глобальная ссылка на тему.
  Любой WBP получает её через `GetUITheme`; у `WBP_TacticalHUD.Style` остаётся
  ссылка на тот же ассет только для превью Designer и как fallback.
- Маленькие HUD-иконки/портреты — обычные ссылки: в бою они нужны вместе и без
  задержки. Крупный экранный арт примерно 16:9 и интро — soft-reference:
  открытие HUD не загружает в память все экраны сразу.
- Обычные `UButton` боевого HUD используют палитры `ActionButtonPalette` и
  `EndTurnButtonPalette`. Форма brush остаётся из Designer, а тема задаёт tint
  Normal/Hovered/Pressed/Disabled и foreground иконки.
- Меню на `CommonButtonBase` используют отдельные Common Style классы
  (`Primary/Secondary/DangerMenuButtonStyle`, `Title/Body/CaptionTextStyle`).
  Тема хранит ссылки на них, поэтому место выбора стиля всё равно одно.
- Все большие блоки имеют собственный `FXRU1UIBlockLayout`: `DesiredSize`,
  `Padding`, `ItemSpacing`. Обычно нулевой `DesiredSize` означает «определяет
  UMG». Исключение — `PortraitCardLayout`: карточка намеренно фиксируется через
  `RootSizeBox`, поэтому оба компонента обязательны и должны быть больше нуля;
  валидатор темы считает `0` ошибкой.

Это соответствует рекомендациям Epic: данные системы хранятся в Data Asset,
CommonUI отделяет style assets от виджетов, сложный UI обновляется событиями,
а не per-frame bindings. При проектировании работаем при DPI Scale 1.0 и
проверяем разные разрешения/Safe Zone:

- [Epic: Data Assets](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-assets-in-unreal-engine)
- [Epic: Common UI Quickstart — Style Assets](https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-quickstart-guide-for-unreal-engine)
- [Epic: Optimization Guidelines for UMG](https://dev.epicgames.com/documentation/en-us/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine)
- [Epic: DPI Scaling](https://dev.epicgames.com/documentation/en-us/unreal-engine/dpi-scaling-in-unreal-engine)
- [Epic: Safe Zones](https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-safe-zones-in-unreal-engine)

`UDataAsset` здесь достаточен: тема одна и на неё постоянно ссылается
`GameInstance`. Переход на `UPrimaryDataAsset`/Asset Manager нужен только если
появятся несколько тем, ручная загрузка/выгрузка, asset bundles или chunking.

## 2. Подготовка после обновления C++

1. Сохранить все открытые BP/карты.
2. Закрыть Editor и из корня проекта запустить `.\Build-XRU1.ps1`. Для правок
   2026-07-23 это обязательно: добавлены отражаемый `UUnitStatusIconWidget`,
   targeted-ability API и новые `UPROPERTY` обеих подложек. Одного Live Coding
   недостаточно.
3. После успешной компиляции перезапустить Editor: UHT должен обновить
   отражаемые `UCLASS/UPROPERTY`, горячая перезагрузка не всегда обновляет
   Details корректно.
4. В Content Browser открыть `/Game/XRU1Game/UI/Icons`, ПКМ по папке →
   **Fix Up Redirectors in Folder**. Сейчас там восемь redirector после
   переименований; удалять их вручную нельзя.
5. Открыть `DA_TacticalHUDStyle`, убедиться, что видны группы `01`–`07`.
6. ПКМ по ассету → **Asset Actions → Validate Assets**. До заполнения будут
   warning о пустых полях; отрицательные размеры/отступы считаются error.
   В текущем состоянии ожидаются warnings для ещё не созданных `M_IntroVideo`
   и шести `CBS_*`/`CTS_*` style classes — это точный остаток, а не поломка DA.
   `UnitOverheadStatusBackgroundTexture = None` валиден и warning не создаёт;
   Alpha background color ниже `0.5` создаёт warning о возможной потере
   контраста.

## 3. Что назначить в DA_TacticalHUDStyle

### 3.1 Действия, счётчики и укрытия

| Поле темы | Ассет |
|---|---|
| `AttackIcon` | `T_Icon_Attack` |
| `OverwatchIcon` | `T_Icon_Overwatch` |
| `HunkerIcon` | `T_Icon_HunkerDown` |
| `AbilityIcon` | `T_Icon_FieldMedicine` как fallback |
| `InteractDefuseIcon` | `T_Icon_Defuse` |
| `InteractEvacIcon` | `T_Icon_Evacuation` |
| `SkipIcon` | `T_Icon_SkipTurn` |
| `EndTurnIcon` | `T_Icon_EndTurn` |
| `MoveIcon` | `T_Icon_Move` |
| `EnemyCountIcon` | `T_Icon_EnemyCount` |
| `EnemyCounterBackgroundTexture` | `None` — однотонный Border; либо белая/светло-серая растягиваемая alpha-mask |
| `HalfCoverIcon` | `T_Icon_HalfCover` |
| `FullCoverIcon` | `T_Icon_FullCover` |

`T_Icon_Exposed` на текущем этапе **не назначать**: действующий GDD говорит, что
фланкированная/открытая цель не показывает щит. Фаза Ф7 в
`11_COVER_AND_ENEMY_PLAN.md` предлагает в будущем жёлтый щит; перед её
реализацией сначала надо изменить GDD, затем добавить отдельное поле темы.

Для счётчика оставить стартовые значения:

- `EnemyCounterIconSize = 32×32`;
- `EnemyCounterBackgroundTexture = None`;
- `EnemyCounterBackgroundColor = (0.01, 0.025, 0.04, 0.88)`;
- `EnemyCounterBackgroundPadding = (8, 4, 8, 4)`.

`EnemyCounterLayout.Padding` — внешний отступ всего блока, а
`EnemyCounterBackgroundPadding` — внутренний от края тёмной плашки до PNG и
числа. Это разные параметры, их нельзя писать в один и тот же Border.

### 3.2 Статусы и выделение

| Поле темы | Ассет |
|---|---|
| `StatusOverwatchIcon` | `T_Icon_Overwatch` |
| `StatusHunkerIcon` | `T_Icon_StatusHunkerDown` |
| `StatusTauntIcon` | `T_Icon_StatusTaunt` |
| `StatusDownedIcon` | `T_Icon_StatusDowned` |
| `StatusEvacuatedIcon` | `T_Icon_Evacuation` |
| `StatusMovingIcon` | `T_Icon_Move` |
| `UnitOverheadStatusBackgroundTexture` | `None` — используется встроенная однотонная подложка; при необходимости назначить только белую/светло-серую растягиваемую PNG alpha-mask, которую код окрасит `BackgroundColor` |
| `SelectionFrameTexture` | `T_UI_SelectionFrame_White` |

Если специальная статусная текстура не задана, C++ использует подходящую
иконку действия как fallback. `GetStatusForUnit(Unit)` централизует приоритет
состояний, `GetStatusIconForUnit(Unit)` сразу возвращает итоговую текстуру, а
`GetStatusIcon(Status)` остаётся резолвером для экранов с уже известным enum.

Для underlay оставить значения по умолчанию:

- `UnitOverheadStatusBackgroundColor = (0.01, 0.025, 0.04, 0.92)`;
- `UnitOverheadStatusBackgroundPadding = 2` со всех сторон;
- `UnitOverheadStatusIconSize = 24×24`.

Итоговый badge занимает `24 + 2 + 2 = 28` Slate units и совпадает со стандартным
`TacticalStatusSlotSize = 28×28`. Не запекать подложку отдельно в каждую status
PNG: общий `UBorder` сохраняет один источник стиля и позволяет менять контраст
без повторного импорта шести иконок. Epic прямо определяет `UBorder` как
одно-дочерний контейнер для background image и регулируемого padding:
[Epic UBorder](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/UMG/UBorder).

### 3.3 Портреты, классы и классовые способности

| Поле темы | Ассет |
|---|---|
| `AssaultPortrait` | `T_Portrait_Assault` |
| `SniperPortrait` | `T_Portrait_Sniper` |
| `MedicPortrait` | `T_Portrait_Medic` |
| `TankPortrait` | `T_Portrait_Tank` |
| `MarauderPortrait` | `T_Portrait_Marauder` |
| `AssaultClassIcon` | `T_Icon_ClassAssault` |
| `SniperClassIcon` | `T_Icon_ClassSniper` |
| `MedicClassIcon` | `T_Icon_ClassMedic` |
| `TankClassIcon` | `T_Icon_ClassHeavy` |
| `MarauderClassIcon` | оставить `None`, пока отдельной картинки нет |
| `AssaultAbilityIcon` | `T_Icon_DashStrike` |
| `SniperAbilityIcon` | `T_Icon_SquadAim` |
| `MedicAbilityIcon` | `T_Icon_FieldMedicine` |
| `TankAbilityIcon` | `T_Icon_Taunt` |

`GetPortraitForUnit`, `GetClassIconForUnit`, `GetAbilityIconForUnit` сами
различают `AUnit_Assault/Sniper/Healer/Tank`; прочий `AUnitBase` считается
единственным врагом GDD — Marauder. Кнопка способности HUD меняет glyph при
смене выбранного бойца уже в C++.

### 3.4 Фоны, брифинги, результаты и интро

У каждого поля раскрыть структуру `FXRU1UIScreenArtwork` и заполнить `Texture`.
`Tint = White`, `DesiredSize = (0,0)` оставляют исходные цвета и Fill-размер.

| Поле темы | Ассет |
|---|---|
| `MainMenuArt.Texture` | `T_MainMenu_BG` |
| `DifficultyArt.Texture` | `T_Difficulty_BG` |
| `SettingsArt.Texture` | `T_MainMenu_BG` (пока общий фон) |
| `AboutArt.Texture` | `T_MainMenu_BG` (пока общий фон) |
| `PauseArt.Texture` | `T_MainMenu_BG` (затем можно заменить) |
| `TutorialBriefingArt.Texture` | `T_Brief_Tutorial` |
| `MissionBriefingArt.Texture` | `T_Brief_Mission01` |
| `VictoryResultArt.Texture` | `T_Result_Victory` |
| `DefeatResultArt.Texture` | `T_Result_Defeat` |
| `DemoCompleteArt.Texture` | `T_DemoComplete` |
| `IntroFallbackArt.Texture` | `T_MainMenu_BG` до появления кадров fallback |
| `IntroMediaSource` | `/Game/Movies/FMS_TU_Intro` |
| `IntroVideoMaterial` | заполнить после создания `M_IntroVideo` |

### 3.5 Размеры, блоки и палитра

Стартовые значения уже заданы C++ и появятся у новых полей:

- action glyph `48×48`, padding `10`;
- End Turn glyph `40×40`, padding `14×10`;
- портрет `104×104`, class/status/cover `28/24/26`;
- target cover и enemy counter `32×32`;
- overhead cover/status `24×24`;
- overhead status background: внутренний padding `2`, тёмный цвет
  `(0.01, 0.025, 0.04, 0.92)`, texture `None` (solid fallback).
- enemy counter background: внутренний padding `(8,4,8,4)`, цвет
  `(0.01,0.025,0.04,0.88)`, texture `None`.

Для каждого блока (`ActionsPanel`, `EndTurn`, `SquadPanel`, `PortraitCard`,
`TargetPanel`, `TurnPhase`, `EnemyCounter`, `TargetingBanner`, `UnitOverhead`
и каждого меню) отступы независимы. Рекомендуемый старт для 1920×1080:

| Блок | `Padding` | `ItemSpacing` |
|---|---:|---:|
| `ActionsPanelLayout` | `12,8,12,8` | `4,0,4,0` |
| `EndTurnLayout` | `16,10,16,10` | `8,0,0,0` |
| `SquadPanelLayout` | `16,12,16,12` | `6,0,6,0` |
| `PortraitCardLayout` | `4` | `8,0,0,0` |
| `TargetPanelLayout` | `16,12,16,12` | `6` |
| `TurnPhaseLayout` | `18,10,18,10` | `4` |
| `EnemyCounterLayout` | `12,8,12,8` | `8,0,0,0` |
| `TargetingBannerLayout` | `18,8,18,8` | `4` |
| меню/брифинг/результат | `64,48,64,48` | `12` |

Палитры кнопок уже дают тёмную подложку и светлый foreground. Не понижать
`NormalForeground` вместе с Normal background: информативные glyph должны
иметь хотя бы 3:1 к фону; тонкие линии лучше держать заметно выше минимума.
См. [WCAG 2.2 Non-text Contrast](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast)
и [технику G207 для иконок](https://www.w3.org/WAI/WCAG22/Techniques/general/G207).

## 4. Единственная глобальная ссылка

1. Открыть `/Game/XRU1Game/Core/BP_TacticsGameInstance`.
2. Class Defaults → `Tactics | UI` → `UITheme` = `DA_TacticalHUDStyle`.
3. Save.
4. В `WBP_TacticalHUD` можно оставить `Style = DA_TacticalHUDStyle`: это та
   же истина, но Designer увидит тему без игрового GameInstance. Если очистить,
   рантайм возьмёт глобальную тему автоматически.

## 5. Подключение боевого HUD

### 5.1 Нижняя панель действий

Ничего в графе делать не нужно. `UTacticalHUDWidget::ApplyStyle` автоматически:

- назначает все action/end-turn textures и размеры;
- применяет независимые padding Action и End Turn;
- оставляет brush-форму кнопки из Designer, но меняет tint четырёх состояний;
- переводит PNG-glyph в `UseForeground`, поэтому он яркий без hover и явно
  меняется при наведении/нажатии/disabled;
- подставляет способность выбранного класса и контекст Defuse/Evac.

После заполнения темы очистить прямой Brush у внутренних Image — необязательно,
но желательно: fallback останется только на случай незаполненного поля.

### 5.2 WBP_UnitPortrait

> **Актуальный статус 2026-07-23:** основная сборка этого раздела уже выполнена
> и проверена через UnrealClaude MCP. Повторно создавать или переставлять узлы
> из §5.2.1–5.2.8 не нужно. Реальные оставшиеся действия перечислены только в
> §5.2.9. Блок ниже свёрнут и сохранён как справка по устройству графа.

Инструкция ниже рассчитана на фактическую текущую иерархию этого Blueprint.
Итоговый вариант — компактная горизонтальная карточка типа XCOM `Unit Flag`:
портрет является отдельным блоком слева, а имя, HP, AP и статус остаются в
читаемой информационной колонке справа.

Такое разделение соответствует составу `Unit Flag` в
[XCOM 2 manual](https://www.feralinteractive.com/en/manuals/xcom2/latest/steam/)
и визуальной компоновке
[XCOM 2 Tactical Combat](https://interfaceingame.com/screenshots/xcom-2-tactical-combat/).
`Overlay` используется только для трёх настоящих слоёв карточки и двух слоёв
портрета. Это также не создаёт лишних layout-контейнеров, что соответствует
[рекомендациям Epic по UMG](https://dev.epicgames.com/documentation/en-us/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine).

Порядок детей `Overlay` критичен: нижний элемент в Hierarchy рисуется первым,
последний — поверх остальных.

Целевая иерархия после выполнения:

```text
WBP_UnitPortrait
└─ RootSizeBox
   └─ CardOverlay
      ├─ CardBackground
      │  └─ CardContent
      │     ├─ PortraitSizeBox
      │     │  └─ PortraitOverlay
      │     │     ├─ PortraitImage
      │     │     └─ ClassBadgeBorder
      │     │        └─ ClassIconSizeBox
      │     │           └─ ClassIcon
      │     └─ InfoBorder
      │        └─ ContentColumn
      │           ├─ HeaderRow
       │           │  ├─ NameText
       │           │  ├─ StatusSizeBox
       │           │  │  └─ StatusIcon
       │           │  └─ CoverSizeBox       ← C++ создаёт в runtime
       │           │     └─ CoverIcon       ← C++ создаёт в runtime
      │           ├─ HPBar
      │           └─ APRow
      ├─ SelectButton
      └─ SelectionFrameBorder
         └─ SelectionFrame
```

<details>
<summary>Уже выполненная сборка карточки — только справка, не повторять</summary>

#### 5.2.1 Задать размеры карточки в DA_TacticalHUDStyle

1. В Content Browser открыть
   `/Game/XRU1Game/Data/DA_TacticalHUDStyle`.
2. В группе `05. Разметка → Портрет` установить:
   - `Portrait Image Size`: `X=104, Y=104`;
   - `Portrait Class Icon Size`: `X=28, Y=28`;
   - `Portrait Status Icon Size`: `X=24, Y=24`;
   - `Portrait Cover Icon Size`: `X=26, Y=26`;
   - `Portrait Status Icon Padding`: `Left=4, Top=0, Right=0, Bottom=0`;
   - `Portrait Cover Icon Padding`: `Left=4, Top=0, Right=0, Bottom=0`;
   - `Selection Frame Padding`:
     `Left=2, Top=2, Right=2, Bottom=2`.
3. В группе `05. Разметка → Блоки` раскрыть `Portrait Card Layout` и
   установить:
   - `Portrait Card Layout → Desired Size`: `X=360, Y=112`;
   - `Portrait Card Layout → Padding`:
     `Left=4, Top=4, Right=4, Bottom=4`;
   - `Portrait Card Layout → Item Spacing`:
     `Left=8, Top=0, Right=0, Bottom=0`.
4. Нажать `Save`.
5. `CoverSizeBox/CoverIcon` вручную добавлять не надо: C++ создаёт их внутри
   существующего `HeaderRow`. Если позже ветка будет собрана в Designer с
   теми же точными именами, C++ найдёт и переиспользует её.

#### 5.2.2 Пересобрать точную иерархию Designer

1. Открыть `/Game/XRU1Game/UI/WBP_UnitPortrait` и перейти на вкладку
   `Designer`.
2. В `Hierarchy` выбрать текущий корневой `Size Box`, нажать `F2` и
   переименовать его в `RootSizeBox`.
3. У `RootSizeBox` в `Details`:
   - включить `Is Variable`;
   - включить `Width Override` и ввести `360`;
   - включить `Height Override` и ввести `112`.
4. Выбрать текущий дочерний `Overlay`, нажать `F2` и переименовать его в
   `CardOverlay`.
5. В `Palette` найти `Border`. Перетащить его точно на строку
   `CardOverlay` в Hierarchy и дождаться подсветки строки родителя.
   Переименовать созданный Border в `CardBackground`.
6. Перетащить `CardBackground` на первое место внутри `CardOverlay`.
7. У `CardBackground` включить `Is Variable` и задать:
   - `Slot → Horizontal Alignment = Fill`;
   - `Slot → Vertical Alignment = Fill`;
   - `Content → Padding = 4, 4, 4, 4`;
   - `Appearance → Brush Color` — тёмный непрозрачный цвет, например
     `R=0.02, G=0.035, B=0.05, A=0.92`.
8. В `Palette` найти `Horizontal Box` и перетащить его внутрь
   `CardBackground`. Переименовать в `CardContent`.
9. В Hierarchy перетащить уже созданный `PortraitSizeBox` с уровня
   `CardOverlay` на строку `CardContent`. После отпускания он должен стать
   первым дочерним элементом `CardContent`.
10. У `PortraitSizeBox` задать:
    - `Is Variable = true`;
    - `Width Override = 104`;
    - `Height Override = 104`;
    - `Slot → Size = Auto`;
    - `Slot → Horizontal Alignment = Fill`;
    - `Slot → Vertical Alignment = Fill`.
11. В `Palette` найти `Overlay` и перетащить его внутрь
    `PortraitSizeBox`. Переименовать в `PortraitOverlay`.
12. В `Palette` найти `Image` и перетащить его внутрь
    `PortraitOverlay`. Переименовать в `PortraitImage`.
13. У `PortraitImage` задать:
    - `Is Variable = true`;
    - `Slot → Horizontal Alignment = Fill`;
    - `Slot → Vertical Alignment = Fill`;
    - `Appearance → Color and Opacity =
      R=1, G=1, B=1, A=1`;
    - `Brush → Draw As = Image`;
    - `Brush → Resource Object = None`.
14. В `Palette` найти `Border` и перетащить его внутрь
    `PortraitOverlay` после `PortraitImage`. Переименовать в
    `ClassBadgeBorder`.
15. У `ClassBadgeBorder` задать:
    - `Slot → Horizontal Alignment = Left`;
    - `Slot → Vertical Alignment = Bottom`;
    - `Content → Padding = 2, 2, 2, 2`;
    - `Appearance → Brush Color =
      R=0.01, G=0.02, B=0.03, A=0.88`;
    - `Visibility = Hit Test Invisible`.
16. В Hierarchy перетащить уже созданный `ClassIconSizeBox` с уровня
    `CardOverlay` точно на строку `ClassBadgeBorder`.
17. У `ClassIconSizeBox` задать:
    - `Is Variable = true`;
    - `Width Override = 28`;
    - `Height Override = 28`.
18. В `Palette` найти `Image` и перетащить его внутрь
    `ClassIconSizeBox`. Переименовать в `ClassIcon`.
19. У `ClassIcon` задать:
    - `Is Variable = true`;
    - `Color and Opacity = R=1, G=1, B=1, A=1`;
    - `Brush → Draw As = Image`;
    - `Brush → Resource Object = None`;
    - `Visibility = Hit Test Invisible`.
20. В `Palette` найти `Border` и перетащить его внутрь
    `CardContent` после `PortraitSizeBox`. Переименовать в
    `InfoBorder`.
21. У `InfoBorder` задать:
    - `Is Variable = true`;
    - `Slot → Size = Fill` и коэффициент `1.0`;
    - `Slot → Horizontal Alignment = Fill`;
    - `Slot → Vertical Alignment = Fill`;
    - `Content → Padding = Left 8, Top 0, Right 0, Bottom 0`;
    - `Brush → Draw As = None`.
22. В Hierarchy раскрыть `SelectButton`. Перетащить существующий
    `ContentColumn` из `SelectButton` точно на строку `InfoBorder`.
    После отпускания `ContentColumn` должен стать единственным ребёнком
    `InfoBorder`, а `SelectButton` должен остаться пустым.
23. У `ContentColumn` задать `Horizontal Alignment = Fill` и
    `Vertical Alignment = Fill`.
24. Внутри `HeaderRow` выбрать `NameText` и установить его
    `Horizontal Box Slot → Size = Fill, 1.0`.
25. Выбрать уже существующий `StatusSizeBox` и задать:
    - `Is Variable = true`;
    - `Width Override = 24`;
    - `Height Override = 24`;
    - `Horizontal Box Slot → Size = Auto`;
    - `Horizontal Box Slot → Vertical Alignment = Center`.
26. У существующего `StatusIcon` задать:
    - `Is Variable = true`;
    - `Color and Opacity = R=1, G=1, B=1, A=1`;
    - `Visibility = Collapsed`.
27. В Hierarchy перетащить пустой `SelectButton` на уровень
    `CardOverlay` сразу после `CardBackground`.
28. У `SelectButton` задать:
    - `Slot → Horizontal Alignment = Fill`;
    - `Slot → Vertical Alignment = Fill`;
    - `Content Padding = 0`;
    - `Is Focusable = false`;
    - для `Style → Normal, Hovered, Pressed, Disabled` раскрыть Brush и
      поставить `Draw As = None / No Draw Type`.
    Это убирает белую заливку, но сохраняет существующее событие
    `OnClicked` на всей площади карточки.
29. Выбрать существующий `SelectionFrame`, нажать правой кнопкой:
    `Wrap With → Border`. Переименовать новый Border в
    `SelectionFrameBorder`.
30. У `SelectionFrameBorder` задать:
    - `Is Variable = true`;
    - `Slot → Horizontal Alignment = Fill`;
    - `Slot → Vertical Alignment = Fill`;
    - `Content → Padding = 2, 2, 2, 2`;
    - `Brush → Draw As = None`;
    - `Visibility = Hit Test Invisible`.
31. У `SelectionFrame` оставить текущее имя, включить `Is Variable` и
    задать `Visibility = Hit Test Invisible`.
32. Перетащить `SelectionFrameBorder` на уровень `CardOverlay` и
    поставить последним.
33. Сверить три дочерних элемента `CardOverlay` и их порядок:
    `CardBackground`, затем `SelectButton`, затем
    `SelectionFrameBorder`.
34. Нажать `Compile`. Если в Hierarchy остались отдельные
    `PortraitSizeBox` или `ClassIconSizeBox` на уровне `CardOverlay`,
    значит reparent выполнен неправильно: переместить их в родителей,
    показанные в целевой иерархии.

#### 5.2.3 Дополнить существующий Refresh

Новый `Refresh` создавать не нужно. Дополняется функция
`Functions → Refresh`, которая уже обновляет имя, HP, AP и прозрачность.

1. Перейти на вкладку `Graph` и открыть `Functions → Refresh`.
2. Найти первый `Is Valid`, проверяющий переменную `Unit`.
3. Отсоединить существующий execution-провод от выхода
   `Is Valid` и вставить на его место узел `Sequence`.
4. Соединить:
   - выход `Is Valid` → вход `Sequence`;
   - `Then 0` → первый узел старой цепочки имени/HP/AP.
5. В `Local Variables` функции `Refresh` нажать `+` и создать:
   - имя `Theme`;
   - тип `TacticalHUDStyleData Object Reference`.
6. От `Then 1` внешнего `Sequence` собрать цепочку:

   `Get Game Instance`
   → `Cast To TacticsGameInstance`
   → `Get UITheme`
   → `Set Theme`
   → `Is Valid(Theme)`.

7. От валидного выхода `Is Valid(Theme)` создать второй `Sequence`,
   назвать его комментарием `APPLY THEME` и кнопкой `Add Pin` сделать
   пять выходов: `Then 0`–`Then 4`.
8. Ветку `Is Not Valid` оставить без продолжения. Старая цепочка
   имени/HP/AP всё равно выполняется через `Then 0` внешнего
   `Sequence`.

#### 5.2.4 Then 0 — применить layout и размеры

1. Перетащить `Theme` на граф как `Get`.
2. Получить `Portrait Card Layout` и вызвать
   `Break XRU1UIBlockLayout`.
3. `Desired Size` разбить узлом `Break Vector2D`.
4. Собрать execution-цепочку:
   - `RootSizeBox → Set Width Override`, значение `Desired Size X`;
   - `RootSizeBox → Set Height Override`, значение `Desired Size Y`;
   - `CardBackground → Set Padding`, значение `Padding` из layout;
   - `InfoBorder → Set Padding`, значение `Item Spacing` из layout;
   - `CardBackground → Set Brush Color`, значение
     `Theme.PanelBackgroundColor`.
5. Получить `Portrait Image Size`, разбить `Break Vector2D` и далее в
   этой же execution-цепочке вызвать:
   - `PortraitSizeBox → Set Width Override` = `X`;
   - `PortraitSizeBox → Set Height Override` = `Y`.
6. Повторить для:
   - `Portrait Class Icon Size` → `ClassIconSizeBox`;
   - `Portrait Status Icon Size` → существующий `StatusSizeBox`.
7. Подключить начало этой цепочки к `APPLY THEME → Then 0`.

#### 5.2.5 Then 1 — подключить портрет

1. `Get Theme → Get Portrait For Unit`.
2. В pin `Unit` передать переменную `Unit`.
3. Результат проверить узлом `Is Valid`.
4. В валидной ветке вызвать по порядку:
   - `PortraitImage → Set Brush From Texture`;
   - `PortraitImage → Set Color and Opacity` = White;
   - `PortraitImage → Set Visibility` = Hit Test Invisible.
5. В невалидной ветке вызвать
   `PortraitImage → Set Visibility = Collapsed`.
6. Подключить этот блок к `APPLY THEME → Then 1`.

#### 5.2.6 Then 2 — подключить иконку класса

1. `Get Theme → Get Class Icon For Unit`.
2. В pin `Unit` передать переменную `Unit`.
3. Результат проверить узлом `Is Valid`.
4. В валидной ветке вызвать:
   - `ClassIcon → Set Brush From Texture`;
   - `ClassIcon → Set Color and Opacity` = White;
   - `ClassIcon → Set Visibility` = Hit Test Invisible.
5. В невалидной ветке вызвать
   `ClassIcon → Set Visibility = Collapsed`.
6. Подключить этот блок к `APPLY THEME → Then 2`.

#### 5.2.7 Then 3 — заменить старый блок статуса

1. В старой цепочке `Refresh` найти узел
   `Set Render Opacity` со значением `1.0`. Сейчас его выход `then` ведёт в
   старый визуальный блок статуса.
2. Отсоединить этот провод. После изменения `Set Render Opacity (1.0)` должен
   стать последним узлом своей ветки.
3. Удалить только старый визуальный блок статуса:
   `Is Evacuated`, `Is Downed`, оба
   `Has Matching Gameplay Tag` и прямые
   `Set Brush From Texture` для `T_Icon_Overwatch`,
   `T_Icon_HunkerDown`, `T_Icon_Evacuation` и
   `T_Icon_StatusDowned`, а также относящиеся к ним старые Branch,
   `StatusIcon → Set Visibility` и `StatusIcon`-getter.
4. Не удалять обновление `NameText`, `HPBar`, `Pip1`, `Pip2`, оба узла
   `Set Render Opacity` и проверку `Is Dead`.
5. От `APPLY THEME → Then 3` вызвать
   `Get Theme → Get Status Icon For Unit`.
6. В pin `Unit` передать переменную `Unit`.
7. Результат проверить узлом `Is Valid`.
8. В валидной ветке вызвать:
   - `StatusIcon → Set Brush From Texture`;
   - `StatusIcon → Set Color and Opacity` = White;
   - `StatusIcon → Set Visibility` = Hit Test Invisible.
9. В невалидной ветке вызвать
   `StatusIcon → Set Visibility = Collapsed`.

`Get Status Icon For Unit` уже соблюдает приоритет:
`Downed → Evacuated → Taunt → HunkerDown → Overwatch → Moving`.
Отдельный `Switch on EXRU1UIStatusIcon` в Blueprint не нужен.

#### 5.2.8 Then 4 — подключить рамку выбора

1. От `APPLY THEME → Then 4` получить из `Theme`
   `Selection Frame Texture` и проверить её узлом `Is Valid`.
2. В валидной ветке вызвать
   `SelectionFrame → Set Brush From Texture`.
3. Следом вызвать:
   - `SelectionFrame → Set Color and Opacity` =
     `Theme.SelectionColor`;
   - `SelectionFrameBorder → Set Padding` =
     `Theme.SelectionFramePadding`.
4. Не добавлять в `Refresh` узел изменения видимости
   `SelectionFrame`. Существующая функция `SetSelected` должна
   продолжать показывать рамку только у выбранного юнита.

Почему рамка остаётся, хотя сейчас `bShowOnlySelectedCard = true`: при выбранном
бойце она действительно дублирует сам факт видимости единственной карточки, но
не ломает логику и даёт полезный focus-accent. Она также сразу начинает работать
при временном отсутствии выбора, отключении режима «только одна карточка» или
будущем показе всего отряда. Поэтому удалять `SelectionFrame`/`SetSelected` не
нужно; за видимость карточки отвечает `UpdateSquadCardVisibility`, а за её
визуальное состояние — отдельно `SetSelected`.

</details>

#### 5.2.9 Проверка в Editor

`Refresh`, четыре портрета/class badge, матрица статусов, одновременный
cover/status и контрастная подложка уже проверены пользователем после полного
build/restart. Повторять функциональную матрицу для закрытия этого пакета не
нужно. Если badge станет тесным после смены арта, сначала увеличить
`TacticalStatusSlotSize` в `DA_UnitHUD_*`, а уже затем внутренний padding;
Blueprint-граф `Refresh` переделывать не надо.

`SelectionFrame` оставлен намеренно как focus-accent выбранного бойца. Его
runtime-текстуру, tint, padding и 9-slice margin задаёт тема; локальный brush в
Designer служит только fallback и не является вторым источником runtime-стиля.

### 5.3 WBP_UnitCoverIcon и HUD над головой

`UCoverIconWidget` и новый `UUnitStatusIconWidget` — **соседние независимые
элементы**. Cover читает `BestCoverAround`; status использует общий приоритет
темы. Один не заменяет и не скрывает другой. Официальный XCOM 2 manual также
перечисляет cover, overwatch и effects одновременно в `Unit Flag`; overhead-
дублирование у нас добавлено ради читаемости поля боя.

Status дополнительно строит `UBorder StatusBadgeBackground → Image StatusIcon`.
Это осознанно только для world-space status: карточка уже имеет контролируемую
тёмную панель, поэтому ещё одна локальная плашка там создала бы визуальный шум.
Для значимых standalone icons ориентир — контраст не ниже 3:1 к соседнему фону:
[W3C G207](https://www.w3.org/WAI/WCAG21/Techniques/general/G207).

После полного build/restart открыть сначала `DA_UnitHUD_Squad`, затем
`DA_UnitHUD_Enemy`. В каждом появится группа
`Layout → Tactical Status`:

1. `Show Tactical Status Icon` = `true`.
2. `Tactical Status Widget Class` оставить нативным
   `UnitStatusIconWidget` (constructor default). Отдельный WBP создавать не
   требуется.
3. `Tactical Status Vertical Index = -1` и
   `Tactical Status Horizontal Index = -1`: C++ поставит badge в последнюю
   занятую строку, сразу справа от последнего существующего слота.
4. `Tactical Status Slot Size = 28×28`.
5. `Tactical Status Slot Padding = Left 2, Top 0, Right 0, Bottom 0`. Это
   **внешний** промежуток между status и соседним слотом.
6. `Tactical Status Color = White`.
7. Существующий слот `WBP_UnitCoverIcon`/`UCoverIconWidget` оставить примерно
   `28×28`; texture внутри будет `24×24` из
   `UnitOverheadCoverIconSize`. Status внутри так же берёт
   `UnitOverheadStatusIconSize = 24×24`, а padding подложки `2+2` доводит
   итоговый badge ровно до `28×28`. В отличие от пункта 5,
   `UnitOverheadStatusBackgroundPadding` — **внутренний** отступ PNG от краёв
   подложки.
8. Локальные `HalfCoverTexture`/`FullCoverTexture` в старом cover WBP —
   только fallback; runtime сначала использует общую тему.

Если нужен другой порядок, задать status V/H явно. Не добавлять тот же
`UUnitStatusIconWidget` ещё и в `WidgetSlots`: автослот уже создаётся кодом.
При увеличении glyph или внутреннего padding увеличить и
`TacticalStatusSlotSize`; если это забыть, runtime один раз запишет warning и
безопасно уменьшит glyph до доступного места.

### 5.4 Остальные блоки WBP_TacticalHUD

Для блока, которому нужны настраиваемые padding/size, схема всегда одна:
`SizeBox` управляет размером, `Border` управляет padding, сам старый блок
остаётся внутри и сохраняет своё имя для C++ binding.

**Designer**

1. Открыть `WBP_TacticalHUD`.
2. Найти нужный блок в Hierarchy, например `ActionsPanel`, `SquadPanel`,
   `TargetPanel`, `TargetingBanner`, `EnemyCounter`.
3. Не переименовывать существующие виджеты, которые уже привязаны C++:
   `AttackBtn`, `OverwatchBtn`, `HunkerBtn`, `AbilityBtn`, `InteractBtn`,
   `SkipBtn`, `EndTurnBtn`, `ActionsPanel`, `SquadPanel`, `TargetPanel`,
   `TargetCoverIcon`, `TargetingBanner`.
4. ПКМ по блоку → **Wrap With → Border**. Новый Border назвать, например,
   `ActionsPanelBorder`.
5. Если блоку нужен фиксированный размер: ПКМ по `ActionsPanelBorder` →
   **Wrap With → Size Box**. Новый SizeBox назвать `ActionsPanelSizeBox`.
6. Включить **Is Variable** у новых `Border`/`SizeBox`, иначе их нельзя будет
   настроить из Graph.
7. Повторить для остальных блоков. Примеры имён:
   `SquadPanelBorder`, `SquadPanelSizeBox`, `TargetPanelBorder`,
   `EnemyCounterBorder`, `TurnPhaseBorder`, `TargetingBannerBorder`.

**Graph: простая функция ApplyBlockLayout**

1. Создать функцию `ApplyBlockLayout`.
2. Inputs:
   - `Layout` (`XRU1UIBlockLayout`);
   - `TargetBorder` (`Border Object Reference`);
   - `TargetSizeBox` (`SizeBox Object Reference`).
3. Внутри функции:
   `Break XRU1UIBlockLayout`.
4. `TargetBorder → Is Valid` → `Set Padding(Layout.Padding)`.
5. `TargetSizeBox → Is Valid`:
   - `Break Vector2D(Layout.DesiredSize)`;
   - если `X > 0`, вызвать `Set Width Override(X)`;
   - если `Y > 0`, вызвать `Set Height Override(Y)`.
   Нулевые значения не трогать: тогда UMG сам рассчитает размер.
6. Скомпилировать функцию.

**Graph: spacing детей Horizontal/VerticalBox**

У HorizontalBox/VerticalBox нет одного общего поля spacing, поэтому отступ
ставится на слот каждого ребёнка.

1. Создать функцию `ApplyHorizontalSpacing`.
2. Inputs: `Box` (`HorizontalBox Object Reference`), `Spacing` (`Margin`).
3. Внутри:
   `Box.Get Children Count` → `For Loop` от `0` до `Count - 1` →
   `Box.Get Child At(Index)` → `Slot As Horizontal Box Slot` →
   `Set Padding(Spacing)`.
4. Для вертикальных списков создать такую же `ApplyVerticalSpacing`:
   `Slot As Vertical Box Slot` → `Set Padding(Spacing)`.

**PreConstruct WBP_TacticalHUD**

1. В `Event PreConstruct` вызвать `GetUITheme`.
2. Если тема валидна:
   - `ApplyBlockLayout(Theme.ActionsPanelLayout, ActionsPanelBorder, ActionsPanelSizeBox)`;
   - `ApplyHorizontalSpacing(ActionsPanel, Theme.ActionsPanelLayout.ItemSpacing)`;
   - `ApplyBlockLayout(Theme.SquadPanelLayout, SquadPanelBorder, SquadPanelSizeBox)`;
   - `ApplyHorizontalSpacing(SquadPanel, Theme.SquadPanelLayout.ItemSpacing)`;
   - `ApplyBlockLayout(Theme.TargetPanelLayout, TargetPanelBorder, TargetPanelSizeBox)`;
   - аналогично для `TurnPhaseLayout`, `EnemyCounterLayout`,
     `TargetingBannerLayout`, `EndTurnLayout`.
3. Enemy-count texture/size/background **не подключать в Graph**: C++
   `UTacticalHUDWidget::ApplyStyle` уже берёт `Theme.EnemyCountIcon`,
   `Theme.EnemyCounterIconSize`, background texture/color/internal padding.
4. Существующий верхнеправый Image уже называется `EnemyCountIcon` и имеет
   **Is Variable**; вторую картинку не создавать.
5. Существующая строка с Image и `EnemyCountText` уже обёрнута во внутренний
   Border `EnemyCounterBackground` с **Is Variable** и
   `Horizontal/Vertical Alignment = Fill`. Он является дочерним для внешнего
   `EnemyCounterBorder`:

   ```text
   EnemyCounterBorder              ← только EnemyCounterLayout
   └─ EnemyCounterBackground       ← texture/color/internal padding из C++
      └─ существующая строка
         ├─ EnemyCountIcon
         └─ EnemyCountText
   ```

   Не применять `ApplyBlockLayout` к `EnemyCounterBackground`: иначе BP и C++
   будут по очереди писать его Padding.
6. При последующих изменениях нажать **Compile** и проверить Designer в
   1920×1080.

TargetCover уже получает текстуру и размер из C++.

## 6. Подключение меню и экранов

Для каждого наследника `UMenuScreenBase` доступен чистый узел `GetUITheme`.

### 6.0 Общая заготовка экрана меню

Эту схему повторить для `WBP_MainMenu`, `WBP_DifficultySelect`,
`WBP_Settings`, `WBP_AboutMenuWidget`, `WBP_PauseMenuWidget`, будущих
брифингов и результатов.

**Designer**

1. Открыть WBP экрана.
2. Корневым виджетом сделать `Overlay`.
3. Первым ребёнком `Overlay` добавить `Image`, назвать `BackgroundImage`,
   включить **Is Variable**. В Slot поставить Fill по горизонтали и вертикали.
4. Вторым ребёнком добавить `SizeBox` → внутри `Border` → внутри основной
   контент экрана (`VerticalBox`, кнопки, заголовки). Назвать:
   `ContentSizeBox`, `ContentBorder`, `ContentBox`.
5. У `ContentSizeBox`, `ContentBorder`, `ContentBox` включить **Is Variable**.
6. У `BackgroundImage` в Details поставить `Brush → Draw As = Image`,
   `Color and Opacity = White`. Конкретную текстуру будет ставить тема.

**Graph: функция ApplyScreenArtwork**

1. Создать функцию `ApplyScreenArtwork`.
2. Inputs:
   - `Screen` (`EXRU1UIScreenArt`);
   - `ContentLayout` (`XRU1UIBlockLayout`).
3. Внутри функции вызвать `GetUITheme`. Если тема невалидна, `Return`.
4. `Theme.GetScreenArtwork(Screen)` → `Break XRU1UIScreenArtwork`.
5. Для простого прототипа:
   `Texture → Load Asset Blocking` → `Cast To Texture2D` →
   `BackgroundImage.Set Brush From Texture`.
   Это проще для новичка и нормально для маленького проекта. Когда всё
   заработает, можно заменить на `Async Load Asset`, чтобы первый вход на экран
   не стопорил кадр.
6. `BackgroundImage.Set Color and Opacity(Tint)`.
7. Если `DesiredSize.X/Y > 0`, завернуть `BackgroundImage` в отдельный
   `BackgroundSizeBox` и выставлять ему Width/Height. Если фон должен всегда
   заполнять экран, оставить `DesiredSize = 0,0` и этот шаг пропустить.
8. Для `ContentLayout` создать такую же локальную функцию `ApplyBlockLayout`,
   как в `WBP_TacticalHUD` (можно просто повторить те же ноды):
   `ApplyBlockLayout(ContentLayout, ContentBorder, ContentSizeBox)`.
9. Если `ContentBox` это VerticalBox:
   `ApplyVerticalSpacing(ContentBox, ContentLayout.ItemSpacing)`.

**PreConstruct/OnActivated**

1. В `PreConstruct` вызвать `ApplyScreenArtwork` для превью Designer.
2. В `OnActivated` вызвать его ещё раз для рантайма. CommonUI активирует экран
   после пуша в стек, поэтому это надёжная точка для обновления.
3. Enum и layout брать по экрану:
   - Main Menu: `MainMenu`, `MainMenuContentLayout`;
   - Difficulty: `Difficulty`, `DifficultyContentLayout`;
   - Settings: `Settings`, `SettingsContentLayout`;
   - About: `About`, `AboutContentLayout`;
   - Pause: `Pause`, `PauseContentLayout`;
   - briefing: `TutorialBriefing` или `MissionBriefing`, `BriefingContentLayout`;
   - result: `VictoryResult` или `DefeatResult`, `ResultContentLayout`;
   - demo complete: `DemoComplete`, `ResultContentLayout`.

### 6.1 CommonUI стили кнопок и текста

Стили создаются один раз и затем назначаются через тему. Не надо настраивать
цвета кнопок отдельно в каждом WBP.

1. В Content Browser создать папку `/Game/XRU1Game/UI/Styles`.
2. ПКМ в папке → **Blueprint Class** → раскрыть **All Classes**.
3. В поиске набрать `CommonButtonStyle`, выбрать `CommonButtonStyle`, создать:
   - `CBS_Menu_Primary`;
   - `CBS_Menu_Secondary`;
   - `CBS_Menu_Danger`.
4. Так же создать классы от `CommonTextStyle`:
   - `CTS_Title`;
   - `CTS_Body`;
   - `CTS_Caption`.
5. Открыть `CBS_Menu_Primary` → Class Defaults.
6. В Details через поиск найти поля `Normal`, `Hovered`, `Pressed`, `Disabled`,
   `Padding`, `Text Style`. Названия групп в UE могут слегка отличаться, но
   смысл один: задать brush/цвета состояний и стиль текста.
7. Для `Primary` использовать основной cyan/тёмный стиль, для `Secondary` более
   спокойный серо-синий, для `Danger` красный акцент для выхода/опасных действий.
8. Открыть `CTS_Title/Body/Caption`, настроить размер шрифта и цвет. Старт:
   Title 42–56, Body 22–28, Caption 16–18.
9. Открыть `DA_TacticalHUDStyle` → группа `07. CommonUI`:
   - `PrimaryMenuButtonStyle = CBS_Menu_Primary`;
   - `SecondaryMenuButtonStyle = CBS_Menu_Secondary`;
   - `DangerMenuButtonStyle = CBS_Menu_Danger`;
   - `TitleTextStyle = CTS_Title`;
   - `BodyTextStyle = CTS_Body`;
   - `CaptionTextStyle = CTS_Caption`.
10. В каждом WBP меню выбрать CommonButton в Designer и поставить Style из
    соответствующего поля темы. Если в Designer неудобно тянуть из темы, можно
    временно назначить `CBS_*` напрямую, но итоговая проверка: такие же классы
    должны лежать в `DA_TacticalHUDStyle`.
11. Для варианта через тему создать в каждом меню функцию `ApplyMenuStyles`:
    `GetUITheme` → для главных кнопок `Set Style(PrimaryMenuButtonStyle)`,
    для вторичных `Set Style(SecondaryMenuButtonStyle)`, для выхода/опасных
    действий `Set Style(DangerMenuButtonStyle)`.
12. Если используются `CommonTextBlock`, в той же функции назначить:
    заголовкам `TitleTextStyle`, обычному тексту `BodyTextStyle`, мелким
    подписям `CaptionTextStyle`. Вызывать `ApplyMenuStyles` из `PreConstruct`
    и `OnActivated`.

### 6.2 Интро

**Media assets**

1. В Content Browser открыть `/Game/Movies`.
2. Проверить, что есть `FMS_TU_Intro` и что внутри он смотрит на
   `Content/Movies/TU_Intro.mp4`.
3. ПКМ → **Media → Media Player**.
4. Назвать `MP_TU_Intro`.
5. В окне создания включить галку **Video output Media Texture asset**. UE
   создаст рядом MediaTexture, назвать её `MT_TU_Intro`, если редактор дал
   другое имя.
6. Создать Material `M_IntroVideo`.
7. Открыть `M_IntroVideo`:
   - Details → `Material Domain = User Interface`;
   - `Blend Mode = Translucent` или `Opaque`, если alpha не нужна;
   - добавить `Texture Sample`, выбрать `MT_TU_Intro`;
   - RGB подключить в `Final Color`;
   - Alpha подключить в `Opacity`, если выбран Translucent.
8. Save всех трёх ассетов.
9. Открыть `DA_TacticalHUDStyle`:
   - `IntroMediaSource = FMS_TU_Intro`;
   - `IntroVideoMaterial = M_IntroVideo`;
   - `IntroFallbackArt.Texture = T_MainMenu_BG` или отдельный fallback-кадр.

**WBP_IntroPlayer**

1. Создать Widget Blueprint:
   Content Browser → `/Game/XRU1Game/UI/Menus` → ПКМ →
   **User Interface → Widget Blueprint**.
2. В выборе родителя открыть **All Classes**, найти `IntroPlayerWidget`
   (`UIntroPlayerWidget`) и выбрать его. Назвать `WBP_IntroPlayer`.
3. Designer:
   - Root = `Overlay`;
   - добавить `Image` `VideoImage`, Fill, **Is Variable**;
   - добавить `Image` `FallbackImage`, Fill, **Is Variable**, Visibility =
     `Collapsed`;
   - добавить `Button` или `CommonButton` `SkipButton`, текст
     «Пропустить», **Is Variable**;
   - опционально добавить `TextBlock` с короткой подсказкой.
4. В `VideoImage` можно сразу поставить Brush Material = `M_IntroVideo`, чтобы
   видеть превью. В рантайме граф всё равно возьмёт материал из темы.
5. Graph → Variables создать `IntroMediaPlayer`:
   - Type = `MediaPlayer Object Reference`;
   - Default Value = `MP_TU_Intro`.
6. Graph → Event `OnActivated`:
   - `GetUITheme`;
   - `IntroVideoMaterial → Load Asset Blocking`;
   - `VideoImage.Set Brush From Material`;
   - `IntroMediaSource → Load Asset Blocking`;
   - `IntroMediaPlayer.Open Source(Loaded Media Source)`;
   - если return true: `IntroMediaPlayer.Play`, `VideoImage Visible`,
     `FallbackImage Collapsed`;
   - если false: вызвать custom event `ShowIntroFallback`.
7. `ShowIntroFallback`:
   - `GetUITheme → GetScreenArtwork(IntroFallback)`;
   - `Texture → Load Asset Blocking`;
   - `FallbackImage.Set Brush From Texture`;
   - `FallbackImage Visible`;
   - `VideoImage Collapsed`;
   - текст кнопки можно заменить на «Продолжить».
8. Для завершения:
   - `SkipButton.OnClicked` → `FinishIntro`;
   - `IntroMediaPlayer.OnEndReached` → `FinishIntro`;
   - `IntroMediaPlayer.OnMediaOpenFailed` → `ShowIntroFallback`.
9. Event `OnDeactivated` или `Destruct`: `IntroMediaPlayer.Close`, чтобы видео
   не продолжало играть после перехода.
10. Открыть `WBP_DifficultySelect` → Class Defaults →
    `IntroScreenClass = WBP_IntroPlayer`.
11. Проверка: главное меню → новая игра → выбор сложности → интро → клик Skip
    или конец видео → хаб.

### 6.3 Брифинги

Брифинг можно сделать одним универсальным WBP с переменной enum, но для
новичка проще и надёжнее создать два почти одинаковых экрана: меньше условий в
графе, легче проверить глазами.

1. В `/Game/XRU1Game/UI/Menus` создать `WBP_TutorialBriefing` от
   `UMenuScreenBase`.
2. Повторить заготовку из §6.0:
   `Overlay` → `BackgroundImage` → `ContentSizeBox/ContentBorder/ContentBox`.
3. В `ContentBox` добавить:
   - `CommonTextBlock` `TitleText`;
   - `CommonTextBlock` `BodyText`;
   - `CommonButton` или `Button` `StartButton`;
   - `CommonButton` или `Button` `BackButton`.
4. В Designer включить **Is Variable** у всех этих виджетов.
5. В Graph → `PreConstruct` и `OnActivated` вызвать:
   `ApplyScreenArtwork(TutorialBriefing, BriefingContentLayout)`.
6. Текст поставить из `02_LORE_SCRIPT.md`: название полигона, короткая вводная,
   кнопка «Начать обучение».
7. `BackButton.OnClicked` → `RequestBack`.
8. `StartButton.OnClicked`:
   - если экран открывается из POI, вызвать тот же стартовый event/функцию,
     который раньше сразу грузил уровень;
   - если делается временно напрямую, использовать `Open Level` на `L_Tutorial`.
9. Создать `WBP_MissionBriefing` тем же способом, но в `ApplyScreenArtwork`
   выбрать `MissionBriefing`, а текст/кнопку взять для станции «Узел-7».
10. После создания обоих экранов открыть их в Designer 1920×1080 и проверить,
    что фон берётся из `T_Brief_Tutorial` / `T_Brief_Mission01`, а не стоит
    прямой Brush из старого WBP.

### 6.4 Результаты и DemoComplete

`UMissionResultWidget` уже готов в C++: GameMode вызывает `SetupResult`, а WBP
должен только обновить визуал в событии `OnResultReady`.

**WBP_MissionResult**

1. В `/Game/XRU1Game/UI/Menus` создать Widget Blueprint от
   `MissionResultWidget` (`UMissionResultWidget`), назвать `WBP_MissionResult`.
2. Designer собрать как экран из §6.0:
   `BackgroundImage`, `ContentSizeBox`, `ContentBorder`, `ContentBox`.
3. В `ContentBox` добавить и включить **Is Variable**:
   - `TitleText` (`CommonTextBlock` или `TextBlock`);
   - `ReasonText`;
   - `StatsText` (можно пока заглушку «Операция завершена»);
   - `RetryButton`;
   - `HubButton`;
   - `MainMenuButton`.
4. Graph → Event `OnResultReady(bInVictory, bInDefeatByTimeout)`.
5. Внутри:
   - если `bInVictory = true`: `ApplyScreenArtwork(VictoryResult, ResultContentLayout)`;
   - иначе: `ApplyScreenArtwork(DefeatResult, ResultContentLayout)`;
   - `TitleText`: «ПОБЕДА» или «ПОРАЖЕНИЕ»;
   - `ReasonText`: при `bInDefeatByTimeout` написать «Таймер заряда истёк»,
     иначе «Отряд не может продолжать операцию»;
   - `RetryButton.Set Visibility`: показывать только при поражении;
   - `HubButton.Set Visibility`: показывать для возврата на базу;
   - `MainMenuButton.Set Visibility`: можно оставить всегда.
6. Кнопки:
   - `RetryButton.OnClicked` → `RetryMission`;
   - `HubButton.OnClicked` → `GoToHub`;
   - `MainMenuButton.OnClicked` → `GoToMainMenu`.
7. Открыть `GM_Tactics` или BP-наследник GameMode миссии →
   `MissionResultWidgetClass = WBP_MissionResult`.

**WBP_DemoComplete**

1. Создать `WBP_DemoComplete` от `UMenuScreenBase` или `UAboutMenuWidget`.
2. Собрать экран по §6.0.
3. В `PreConstruct`/`OnActivated` вызвать
   `ApplyScreenArtwork(DemoComplete, ResultContentLayout)`.
4. Добавить текст «Демо завершено», автор/курс/использованные ассеты.
5. Кнопка «В главное меню»:
   `Get Game Instance` → `Cast To TacticsGameInstance` → `TravelToMainMenu`.
6. Переход `Mission01 victory → WBP_DemoComplete` подключается в этапе 7,
   когда будет собран полный flow миссии. Сейчас главное — чтобы сам экран
   существовал и брал арт из `DA_TacticalHUDStyle`.

## 7. Настройки самих Texture2D

Эти настройки уже пакетно применены и ассеты сохранены; повторять вручную не
нужно:

- 22 glyph icons и `T_UI_SelectionFrame_White` (всего 23 Texture2D):
  `Texture Group = UI`,
  `Compression Settings = Uncompressed (RGBA8)` (`TC_EditorIcon`),
  `Mip Gen Settings = NoMipmaps`, `Max Texture Size = 256`, `sRGB = true`,
  `Never Stream = true`; alpha сохранена;
- 5 portraits и 7 backgrounds: `Texture Group = UI`, `Compression Settings =
  Default`, `Mip Gen Settings = FromTextureGroup`, `sRGB = true`, streaming
  разрешён;
- портреты имеют source size `512×512`; UI-фоны — исходники примерно 16:9 с
  фактическими размерами `1920×1072`, `1820×1024` и `1679×937`. Их не следует
  насильно считать `1920×1080`: кадрирование задаёт конкретный экран;
- source icons могут оставаться `1024×1024`, а selection frame — `512×288`:
  `Max Texture Size = 256` ограничивает их игровой размер без повторного
  импорта. Отображаемый размер Image по-прежнему задаёт тема;
- при добавлении нового PNG-glyph повторить параметры первой строки и в Texture
  Editor проверить `Source Alpha Detected`, `Displayed`, `Max In-Game`; не
  включать `Compress Without Alpha`.

Справка: [Epic Texture Asset Editor](https://dev.epicgames.com/documentation/unreal-engine/texture-asset-editor-in-unreal-engine?lang=en-US).

## 8. Финальная проверка

1. Validate `DA_TacticalHUDStyle` без errors. До выполнения §6.1–6.2 допустимы
   только warnings про CommonUI styles и `IntroVideoMaterial`.
2. Designer 1920×1080, DPI 1.0: проверить меню и HUD.
3. Preview 1280×720 и 2560×1440; угловые блоки не обрезаются Safe Zone.
4. PIE: Normal action glyph уже яркий; hover/pressed заметны; disabled явно
   тусклее, но форма кнопки читается.
5. По очереди выбрать четыре класса — portrait/class/ability меняются.
6. Для Overwatch/Hunker/Taunt/Downed/Moving и Half/Full cover проверить:
   cover + status одновременно видны и в карточке, и над головой; скрытие
   одного не скрывает второй. `Evacuated` проверить отдельно как resolver/card
   state до скрытия: после `Evacuate()` Actor вместе с overhead
   `WidgetComponent` намеренно скрывается, поэтому требовать post-evac glyph над
   головой нельзя. Отдельно посмотреть status на светлом полу и тёмной/синей
   стене: подложка должна сохранять форму glyph, не перекрашивать PNG и не
   обрезаться внутри `28×28`.
7. Меню → Difficulty → Intro → Hub; оба брифинга; Victory/Defeat/DemoComplete.
8. После сохранения ассетов повторно **Fix Up Redirectors** и Validate.
