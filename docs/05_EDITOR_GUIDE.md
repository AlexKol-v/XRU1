# Гайд по работе в редакторе

Пошаговые инструкции «что и где создавать» под каждый этап роадмапа.
Имена ассетов — по [06_CONVENTIONS.md](06_CONVENTIONS.md). Все создаваемые
BP-классы наследуются от **уже существующих C++ классов** (см. 03_CODE_OVERVIEW).

---

## 1. Структура Content/ (создать папки один раз)

```
Content/
  XRU1Game/                  ← ВЕСЬ наш контент только здесь
    Core/        (GameInstance, GameModes, контроллеры)
    UI/          (WBP_* экраны, HUD, стили, иконки)
    Units/       (BP юнитов, ABP, монтажи, GA_* BP-наследники)
    Maps/        (L_MainMenu, L_Hub, L_Tutorial, L_Mission01)
    Input/       (IMC_*, IA_*)
    Data/        (DataAsset'ы квестов-подсказок, DT при необходимости)
    Audio/       (звуки/музыка или ссылки на ThirdParty)
    FX/          (ниагара/декали: круг радиуса, маркеры)
  ThirdParty/    (паки с Fab/Megascans, каждый в своей папке)
  Characters/, MWLandscapeAutoMaterial/, TopDown/  ← перенесённое, не трогаем
```

## 2. Настройки проекта (Project Settings) — этап 3

1. **Maps & Modes**: `BP_TacticsGameInstance` уже назначен через
   `Config/DefaultEngine.ini`; повторно выбирать его в Project Settings не
   нужно. Editor/Game Default Map переключить на `L_MainMenu`, когда карта
   будет создана.
2. **Input**: убедиться, что Default Player Input Class = Enhanced Player Input
   (UE5.7 по умолчанию так).
3. **Collision**: канал укрытий не нужен — используем `ECC_WorldStatic`
   (уже дефолт `CoverTraceChannel`); стены-укрытия должны блокировать WorldStatic.
4. **Navigation Mesh**: Runtime Generation = Static (карты статичные).
5. **Packaging** (этап 9): List of maps to include — все 4 карты.

## 3. UI-каркас (этап 3)

### 3.1 BP_TacticsGameInstance
`BP_TacticsGameInstance` уже создан в `Content/XRU1Game/Core/` от
`UTacticsGameInstance`, а `UITheme` уже указывает на
`/Game/XRU1Game/Data/DA_TacticalHUDStyle`. Класс уже назначен строкой
`GameInstanceClass` в `Config/DefaultEngine.ini`; повторно выбирать его в
Project Settings не нужно. После создания карт останется задать только
`HubLevel = L_Hub` и `MainMenuLevel = L_MainMenu`.

### 3.2 WBP_PrimaryGameLayout
`WBP_PrimaryGameLayout` уже существует в `Content/XRU1Game/UI/` с
**Parent Class = `UPrimaryGameLayout`**. В Designer сверить иерархию; имена
ДОЛЖНЫ совпадать (BindWidget):
```
Overlay
 ├─ CommonActivatableWidgetStack  «GameStack»      (Fill)
 ├─ CommonActivatableWidgetStack  «GameMenuStack»  (Fill)
 ├─ CommonActivatableWidgetStack  «MenuStack»      (Fill)
 └─ CommonActivatableWidgetStack  «ModalStack»     (Fill)
```

### 3.3 Экраны меню
Пять BP уже существуют в `Content/XRU1Game/UI/Menus/`:
`WBP_MainMenu`, `WBP_DifficultySelect`, `WBP_Settings`,
`WBP_AboutMenuWidget`, `WBP_PauseMenuWidget`. Их C++ родители правильные:
`UMainMenuWidget`, `UDifficultySelectWidget`, `USettingsMenuWidget`,
`UAboutMenuWidget`, `UPauseMenuWidget`. Кнопки (CommonButtonBase) вешают
события на **готовые методы**: `RequestNewGame`, `RequestContinue`,
`RequestSettings`, `RequestAbout`, `RequestQuit`, `RequestBack`,
`ChooseDifficulty(Easy/Medium/Hard)`.
В WBP_MainMenu в Details заполнить `SettingsScreenClass`, `AboutScreenClass`,
`DifficultyScreenClass`. Доступность «Продолжить» — бинд на `CanContinue`.
В WBP_DifficultySelect заполнить `IntroScreenClass = WBP_IntroPlayer` после
создания интро. Полная схема темы/арта — `10_UI_THEME_GUIDE.md`.

### 3.4 Уровень меню
- `BP_MenuPlayerController` от `AMenuPlayerController`: задать
  `RootLayoutClass = WBP_PrimaryGameLayout`, `InitialScreenClass = WBP_MainMenu`.
- `GM_MainMenu` от GameModeBase: PlayerController = BP_MenuPlayerController,
  Default Pawn = None, HUD = None.
- Карта `L_MainMenu`: пустая сцена с фоном (камера смотрит на декорацию из
  кита) → World Settings → GameMode Override = GM_MainMenu.
- Пауза в игровых уровнях: по Esc пушить `WBP_PauseMenuWidget` на слой Menu
  (после этапа 1 это делает `ATacticalPlayerController`; в хабе — контроллер хаба).

### 3.5 Интро-видео (после выбора сложности)
1. Ролик уже добавлен как `Content/Movies/TU_Intro.mp4`, а FileMediaSource —
   `/Game/Movies/FMS_TU_Intro` (папка Movies пакуется как есть; LFS хранит mp4).
2. Создать **MediaPlayer** (`MP_TU_Intro`, ✔ автосоздание MediaTexture
   `MT_TU_Intro`), затем Material `M_IntroVideo`: `Material Domain = User
   Interface`, RGB `MT_TU_Intro` подключить в `Final Color` (не в Emissive).
3. В `DA_TacticalHUDStyle`: `IntroMediaSource = FMS_TU_Intro`,
   `IntroVideoMaterial = M_IntroVideo`, заполнить `IntroFallbackArt`.
4. `WBP_IntroPlayer` (наследник `UIntroPlayerWidget`): Image на весь экран с
   `M_IntroVideo`; в Construct — `MP_TU_Intro.OpenSource(FMS_TU_Intro)` + Play;
   OnEndReached ИЛИ любой клик (OnMouseButtonDown) → `FinishIntro`.
   Подсказка «Пропустить — клик» в углу.
5. `WBP_DifficultySelect` → Class Defaults →
   `IntroScreenClass = WBP_IntroPlayer`. C++ создаёт кампанию, пушит интро и
   переходит в хаб только после `FinishIntro`; если класс не задан — сразу хаб.
   Fallback без видео — `IntroFallbackArt` + кнопка «Продолжить».

## 4. Юниты (этап 4)

### 4.0 Перенос анимаций из Game Animation Sample (проверено по факту)
GASP уже скачан в `D:/UE5/UnrealProjects/GameAnimationSample`. ВАЖНО: его
анимации лежат на скелете **UEFN_Mannequin** (`Characters/UEFN_Mannequin/
Animations/…`), а НЕ на нашем SK_Mannequin — прямой Migrate не подойдёт.
Epic положил готовый конвейер ретаргета:

1. Открыть проект GASP (той же версии UE). PoseSearch/motion matching НЕ трогаем.
2. В Content Browser выделить нужные **AnimSequence** на UEFN-скелете:
   - `Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Crouch_Idle_Loop`
     (+ `Crouch_Idle_Break_v01..05` для вариативности),
   - `Animations/Idle/M_Neutral_Stand_Idle_Break_v01..` (idle-вариации),
   - `Animations/Crouch/` — циклы crouch-перемещения (Walk-эквиваленты),
   - `Animations/Walk/M_Neutral_Walk_Loop_F` и соседние (если надо).
3. ПКМ по выделенному → **Retarget Animation Assets → Duplicate and Retarget**
   → IK Retargeter = `Characters/UE5_Mannequins/Rigs/RTG_UEFN_to_UE5_Mannequin`
   (целевой скелет — UE5 SK_Mannequin). Результат сложить в отдельную папку.
4. Полученные (уже UE5-Manny) секвенции → ПКМ → Asset Actions → **Migrate** →
   `XRU1/Content/XRU1Game/Units/Anims/GASP/`.
5. В XRU1 проверить, что секвенции указывают на наш `SK_Mannequin`
   (`Content/Characters/Mannequins/Meshes/SK_Mannequin`); если продублировался
   скелет — Retarget → Replace Skeleton.

Death/HitReact в GASP НЕТ — используем донорские (`MM_Death_*`, `MM_HitReact_*`).

### 4.1 ABP_Soldier — расширение стокового ABP (решение — 04_ROADMAP этап 4)

**Статус:** базовая локомоция (idle/бег) у юнитов УЖЕ работает — на стоковом
ABP шаблона. Двигает юнита `AIController`, поэтому в C++ включён
`bUseAccelerationForPaths` (конструктор `AUnitBase`) — без него стоковое
условие `Should Move` (= `Speed>0 AND Acceleration≠0`) не видит AI-движение
(path following по умолчанию задаёт скорость напрямую, минуя Acceleration).
Ту же логику Should Move сохранять и в новых ветках ABP.

**Почему НЕ GASP-риг целиком:** motion matching в GASP предсказывает
траекторию по вводу игрока — при движении через `AI MoveTo` даёт футслайдинг
и неверные направления; для дискретных стоек тактики он и не нужен.
Из GASP берём только отдельные секвенции через ретаргет §4.0.

**Шаги:**
1. Дублировать стоковый ABP (который сейчас стоит на юнитах, ABP_Manny) →
   `Content/XRU1Game/Units/ABP_Soldier`. НЕ child-класс — дубликат: свободно
   правим state machine, не боясь обновлений шаблона.
2. Ретаргет недостающих секвенций из GASP по §4.0: crouch idle, crouch walk
   loop; сложить в `Content/XRU1Game/Units/Anims/GASP/`.
3. Event Graph — добавить переменные из Pawn-владельца (`AUnitBase`;
   каст к нему делать ОТДЕЛЬНО от расчёта Speed, чтобы локомоция не зависела
   от успеха каста):
   - `bIsDead`, `bIsDowned` (по событиям смерти этапа 1),
   - **`CoverState`** — из `CoverDetection->BestCoverAround` (None/Half/Full;
     обновлять по делегату `OnCoverStateChanged`),
   - `bOverwatch` — наличие тега `State.Overwatch` на ASC.
4. State Machine — добавить состояния (правило стоек — GDD §12.1):
   - `Idle_Open` ↔ `Move` — уже есть в стоке (idle/run по Should Move)
   - `Idle_HalfCover` — **crouch idle (GASP)**; движение у укрытия — crouch walk
   - `Idle_FullCover` — rifle idle + лёгкий lean к стене (упрощённо — та же idle)
   - `Overwatch` — ADS-поза (`MF_Rifle_Idle_ADS` из донора)
   - `Downed` — лежит (кадр Death-анимации на паузе), `Death` — по bIsDead
5. Slot «UpperBody» для монтажей. Монтажи: `MM_Rifle_Fire` → AM_Fire,
   `MM_HitReact_*` → AM_HitReact, `MM_Death_*` → AM_Death (донорские).
6. Назначить ABP_Soldier в Mesh всех BP-юнитов (взамен стокового).

### 4.2 BP-классы юнитов

Актуальное состояние: в `/Game/XRU1Game/Units/` уже созданы и сохранены
`BP_Unit_Assault`, `BP_Unit_Sniper`, `BP_Unit_Medic` (родитель
`Unit_Healer`) и `BP_Unit_Tank`. На `Lvl_TopDown` уже стоит по одному экземпляру
каждого класса; три лишних Assault заменены с сохранением Transform. Повторять
создание и замену сейчас не нужно.

#### Если при запуске открылось «Восстановить пакеты»

Это штатное окно после некорректного завершения Editor, а не признак порчи
проекта. Для текущих autosave `BP_Unit_Sniper`, `BP_Unit_Medic`,
`BP_Unit_Tank` от 2026-07-23 выбрать **«Пропустить восстановление»**: актуальная
карта уже содержит все четыре класса с правильными Transform, а повторная
попытка восстановления одного пакета ранее частично прошла и только запутывает
состояние.

После открытия проекта:

1. В `World Outliner` убедиться, что есть `BP_Unit_Assault`,
   `BP_Unit_Sniper`, `BP_Unit_Medic`, `BP_Unit_Tank` и два
   `BP_Unit_Marauder`.
2. Нажать **File → Save All**.
3. Помнить, что World Partition хранит размещённых юнитов как External Actors:
   при переносе на вторую машину в коммит должны войти не только
   `Lvl_TopDown.umap`, но **все** изменения (`M`, `D`, `??`) в
   `Content/__ExternalActors__/TopDown/Lvl_TopDown/`: удаления относятся к
   заменённым старым Assault, добавления — к новым классам.

Важно: класс размещённого Actor нельзя поменять полем в `Details`. `UnitRole`
тоже не является переключателем класса. `GetPortraitForUnit` определяет тип по
реальному C++-классу (`IsA`), поэтому Assault с вручную изменённым названием
всё равно останется Assault.

Если в будущем понадобится заменить класс юнита вручную:

1. В `Content Browser` открыть `/Game/XRU1Game/Units/`.
2. В `World Outliner` выбрать старого юнита. В `Details → Transform` нажать
   правой кнопкой по заголовку `Transform` → `Copy`.
3. Перетащить нужный `BP_Unit_*` из Content Browser во viewport.
4. Выбрать нового Actor, правой кнопкой по заголовку `Transform` → `Paste`.
5. Убедиться, что новый Actor появился в той же точке, и только после этого
   удалить старый Actor. Нажать `Ctrl+S`.

Если нужен новый BP-класс, а не экземпляр на карте:

1. `Content Browser → /Game/XRU1Game/Units/ → Add (+) → Blueprint Class`.
2. В окне выбора нажать `All Classes` и найти ровно один из классов:
   `Unit_Assault`, `Unit_Sniper`, `Unit_Healer`, `Unit_Tank`.
3. Назвать ассет `BP_Unit_Assault/Sniper/Medic/Tank` соответственно. Для медика
   имя ассета `Medic`, но Parent Class должен быть именно `Unit_Healer`.
4. Открыть BP → `Class Defaults` и проверить:
   - `DefaultTeamId = 1`;
   - `AIControllerClass = UnitAIController`;
   - `Auto Possess AI = Placed in World or Spawned`;
   - `HUDWidgetClass = WBP_UnitHUD`, layout = `DA_UnitHUD_Squad`;
   - `Tactics|Stats → BaseMaxHealth`: Assault `100`, Sniper `80`, Medic `90`,
     Tank `150` (это стартовые `Health` и `MaxHealth`, не `StartupEffects`);
   - `AttackAbilityClass = GA_Attack`;
   - `OverwatchAbilityClass = GA_Overwatch`;
   - `HunkerAbilityClass = GA_HunkerDown`;
   - Assault: `ClassAbilityClass = GA_RunAndGun`;
   - Sniper: `ClassAbilityClass = None`, `bHasSquadsight = true`;
   - Medic: `ClassAbilityClass = GA_Heal`;
   - Tank: `ClassAbilityClass = GA_Taunt`.
5. HP, позывные и базовые способности имеют нативные C++-defaults. Поэтому у
   нового BP с правильным Parent Class уже будут `100/80/90/150`, `Клин`,
   `Оса`, `Шприц`, `Молот` и соответствующие способности. Если нужен другой
   позывной, изменить только `Tactics|Unit → UnitDisplayName`; класс от этого
   не изменится.
6. Меш/AnimBP/оружие относятся к визуальной полировке: сейчас четыре BP уже
   используют `SKM_Manny_Simple` + `ABP_Unarmed` и общий HUD. Позже заменить их
   на `ABP_Soldier` и прикрепить винтовку в сокет `weapon_r` по §4.1.

### 4.3 Враг
`BP_Unit_Marauder` от `AUnitBase` (не от AUnit_*): `DefaultTeamId = 2`, меш
Manny с тёмным материалом, статы: BaseAim 65 / ShotDamage 20 (HP и aim
переопределит `ATacticsGameMode` по сложности). Способности врагу не нужны
(его ход ведёт `AUnitAIController` напрямую через ResolveShot).
**Патруль**: на размещённом на карте экземпляре врага заполнить массив
`PatrolPoints` (TargetPoint'ы уровня) — без точек юнит стоит на посту;
тревога поднимется от шума выстрелов или визуального контакта (GDD §8).

### 4.4 Боевой контроллер, ввод и GameMode (после этапа 1 — классы готовы)

1. **IMC_Tactical** (`Content/XRU1Game/Input/`) + Input Actions:
   `IA_Select` (ЛКМ, Digital), `IA_Command` (ПКМ), `IA_EndTurn` (Enter),
   `IA_Overwatch` (Y), `IA_Hunker` (X), `IA_ClassAbility` (**R**),
   `IA_Interact` (F), `IA_SkipTurn` (Backspace), `IA_NextUnit` (Tab),
   `IA_Pause` (Esc), `IA_CameraPan` (Axis2D: WASD со Swizzle/Negate),
   `IA_CameraRotate` (Axis1D: **Q** с Negate, **E** — как в XCOM),
   `IA_CameraZoom` (Axis1D: колесо), `IA_SelectSlot1..4` (клавиши 1–4).
2. **BP_TacticalCameraPawn** от `ATacticalCameraPawn` (дефолты ок; поведение
   XCOM уже в C++: плавный полёт к цели `FocusInterpSpeed`, следование за
   бегущим/действующим юнитом, разрыв follow при ручной панораме). Edge scroll
   мышью у края — параметры на КОНТРОЛЛЕРЕ (`bEdgeScrollEnabled`,
   `EdgeScrollMarginPx`).
3. **BP_TacticalPlayerController** от `ATacticalPlayerController`: назначить
   IMC + все IA (слоты 1–4 — в массив `SelectSlotActions` по порядку),
   `RootLayoutClass = WBP_PrimaryGameLayout`, `PauseMenuClass = WBP_PauseMenuWidget`,
   `MoveRangeVisualizerClass = BP_MoveRangeVisualizer`.
4. **BP_MoveRangeVisualizer** от `AMoveRangeVisualizer`: материалы зон —
   полупрозрачные unlit (`ZoneOneMaterial` = синий M_MoveZone_Blue,
   `ZoneTwoMaterial` = жёлтый M_MoveZone_Yellow, Translucent, ~0.3 альфы).
   Для ленты превью пути — `PathOneMaterial`/`PathDashMaterial` (те же цвета,
   но плотнее, ~0.8 альфы; пусто = возьмёт материалы зон). Параметры `CellSize`
   (шаг сетки, 50 см — меньше = глаже/дороже) и `PathWidth` — там же в дефолтах.
   **Кайма зоны** (сглаженная XCOM-обводка по краю): рисуется автоматически;
   ширина `BorderWidth` (10 см; 0 = выключить), сглаживание
   `BorderSmoothIterations` (2), материалы `BorderOneMaterial`/`BorderTwoMaterial`
   (пусто = материалы пути → зон; для чёткой линии сделай непрозрачные MI).
   Зона строится волновым Dijkstra по сетке сэмплов и рисуется гладким контуром
   (marching squares), путь — лентой по рельефу; всё по метрике валидации приказа.
5. **GM_Tactics** от `ATacticsGameMode`: PlayerController = BP_TacticalPlayerController,
   Default Pawn = BP_TacticalCameraPawn; `MissionResultWidgetClass =
   WBP_MissionResult`, `TacticalHUDClass = WBP_TacticalHUD`; `MissionId` и
   `DifficultyParams` — на КОПИЯХ GameMode per-уровень (GM_Tutorial: MissionId
   =Tutorial, bWinWhenAllEnemiesDead=false — финал ведёт скрипт; GM_Mission01:
   MissionId=Mission01).
6. На боевой карте: юниты отряда (TeamId=1) и враги (TeamId=2) расставлены,
   NavMeshBoundsVolume есть → GameMode сам соберёт стороны и запустит бой
   через ~0.3 с после старта.
7. **Бомба и эвакуация** (миссия): разместить `BP_BombObjective` (меш заряда,
   InteractRadius ~200) и `BP_EvacZone` (радиус ~400, `bActiveFromStart=false`;
   в туториале зону активирует скрипт: GameMode->`ActivateEvacuation()`).
   Юнит рядом с бомбой / в зоне жмёт **F**.
8. После C++ build/restart проверить новую защиту команд без Blueprint-узлов:
   - Q/E → выбрать другого бойца: угол камеры сохраняется;
   - «Огонь» → во время прицеливания Overwatch/Hunker/ClassAbility disabled,
     их хоткеи ничего не запускают; ПКМ/Esc отменяет mode;
   - после отмены Overwatch снова доступен и активируется;
   - timed shot доигрывается, а выбор следующего бойца не наследует его yaw.
   В `WBP_TacticalHUD` верхнеправая картинка уже называется
   `EnemyCountIcon` и имеет **Is Variable**. Строка с `EnemyCountIcon` и
   `EnemyCountText` уже обёрнута в `Border` с именем
   `EnemyCounterBackground` и **Is Variable**.
   Если уже есть внешний `EnemyCounterBorder` для `EnemyCounterLayout`, оставить
   его снаружи: внешний Border отвечает за позиционный layout, внутренний —
   только за texture/color/internal padding из `DA_TacticalHUDStyle`.

### 4.5 Подсветка юнитов: обводка при наведении + кольцо выбора ✅ сделано

Материалы созданы, назначены и проверены в PIE (2026-07-15) — в репозитории
через LFS, на новой машине пересоздавать не нужно. Логика на стороне C++
(ховер-трейс, Custom Depth stencil 1=отряд/2=враг, показ кольца в
`SelectUnit`) без правок в редакторе. Формула на случай, если материалы
понадобится пересобрать (`Content/XRU1Game/Materials/`):

- **M_OutlinePP** — Post Process, Blendable Location = Before Tonemapping
  (сглаживается TSR/TAA); 4-тап edge-detect по `SceneTexture:CustomStencil`
  (стенсил в центре пикселя vs максимум четырёх соседей со смещением
  `Thickness × ViewSize`) → маска ребра снаружи силуэта → цвет по стенсилу
  (`AllyColor` голубой / `EnemyColor` красный) → Emissive. Назначен в
  `BP_TacticalCameraPawn → OutlineMaterial` (вешается блендаблом на
  unbound-`PostProcessComponent` в BeginPlay — отдельный PostProcessVolume
  на карте не нужен).
- **M_SelectionRing** — Deferred Decal, Translucent; кольцо = разность двух
  `RadialGradientExponential` (Radius 0.4/0.5) в Opacity, `RingColor`
  эмиссивом. Назначен как `SelectionDecal` во всех BP-юнитах (позиция/размер
  из C++, трогать только у нестандартных капсул).

Поведение: кольцо выбора видно только в свою фазу (в ход врага прячется);
ховер-обводка в фазу игрока красит и своих (голубым), и врагов (красным),
в фазу врага — только врагов; смерть/эвакуация гасят обе подсветки.

## 5. Уровень-туториал (этап 7)

1. Карта `L_Tutorial` (~60×40 м) из кита (НЕ примитивы): **секции A–D** по
   сценарию 02_LORE_SCRIPT §4 — A: старт + «пустырь» + бетонный блок (full);
   B: дальняя дистанция с мешками (half) для танка/снайпера; C: окопавшийся
   враг за укрытием; D: сектор для overwatch + площадка эвакуации у выхода.
   Целевые низкие укрытия ~60 см, высокие ~150 см (блокируют WorldStatic).
   До исправления Ф2 из `11_COVER_AND_ENEMY_PLAN.md` текущий детектор ошибочно
   требует примерно 148/238 см; финальную карту под этот дефект не подгонять.
2. NavMeshBoundsVolume на всю площадку (P — проверить покрытие).
3. Спавны: 4 бойца (tag «SquadSpawn»), 4 врага-голограммы — по одному на
   секции A–D (полупрозрачный материал, BaseAim 40 / урон 10 в BP), каждый
   изначально **выключен** (Hidden + AI off) — включается скриптом своей секции.
4. World Settings → GameMode Override = `GM_Tactics` (BP от `ATacticsGameMode`,
   PlayerController = `BP_TacticalPlayerController`, Pawn = `BP_TacticalCameraPawn`).
5. Скрипт шагов (level BP + STQuestSystem):
   - шаги = objectives `DA_Quest_Tutorial`; тексты — 02_LORE_SCRIPT §4;
     нотификации — `UQuestNotificationWidget` (примеры структуры — в доноре
     `Content/Quests/`, только смотреть);
   - триггеры: box-триггеры зон («пустырь», «укрытие», «зона B»…), события
     `OnCoverStateChanged`, `OnActionPointsChanged`, `OnCombatEnded`, кастомные
     события способностей;
   - **форс-выстрелы**: level BP вызывает `UTacticsCombatStatics::ResolveShot`
     с BaseHitChance = 100 (шаг A4) или 0 (шаг A7) от имени врага;
   - **раненый Клин** (шаг A9): Клин с самого старта спавнится в стороне,
     скрыт; по триггеру телепорт к точке + `SetDowned(true)` + показать;
   - подсветка целевых точек — декаль-маркер (P2, по желанию);
   - зона эвакуации (`AEvacZone`) в секции D активируется последним шагом.
6. Финал: все эвакуированы → GameMode пишет `CompletedMissions += Tutorial`,
   `SaveCampaign`, WBP_MissionResult → «В хаб» → `TravelToHub`.

## 6. Хаб (этап 7)

1. Карта `L_Hub`: ландшафт-миниатюра из `MWLandscapeAutoMaterial` (пример
   MountainRange, уменьшить), поверх — полупрозрачный эмиссив «голограмма».
2. Пешка-камера: SpringArm + Camera, вращение при зажатой ЛКМ, зум колесом
   (можно BP-only; или переиспользовать `ATacticalCameraPawn` с ограничениями).
3. Два актора `AMissionPointOfInterest` (BP-наследник с мешем-маркером):
   заполнить `MissionId` («Tutorial»/«Mission01»), `Title`, `Description`
   (тексты 02_LORE_SCRIPT §3), `LevelToLoad`, `PopupWidgetClass = WBP_POIPopup`.
4. WBP_POIPopup: заголовок/описание/кнопка «Начать» → `SelectPointOfInterest()`.
5. Контроллер хаба: `ACSTPlayerController`-наследник (layout уже поднимает) или
   свой лёгкий BP-контроллер: включить Mouse Events (Click/Over) для POI.
6. GameMode `GM_Hub`: pawn = камера хаба, controller из п.5. Esc → пауза.

## 7. Боевая миссия (этап 7)

Карта 60×60 по зонам A/B/C (02_LORE_SCRIPT §5):
1. Зона A (юг): старт отряда + место под `AEvacZone` (неактивна до цели).
2. Зона C (север): `ABombObjective` (меш заряда + интеракт-радиус) у аппаратной.
3. Спавн-точки врагов с тегами зон; `GM_Tactics` читает `Difficulty` из
   GameInstance → количество/HP/aim (GDD §10) и **TurnLimit** таймера
   (12/10/8) в TurnManager.
4. HUD: счётчик «До взрыва», прогресс «Обезвреживание: X/2», список целей.
5. Скриптовые реплики — нотификации по событиям (первый контакт, первое
   убийство, половина таймера, 3 хода, заряд 1/2, заряд обезврежен).
6. Победа (заряд + все живые эвакуированы) → WBP_MissionResult →
   WBP_DemoComplete → `TravelToMainMenu`. Поражения: таймер истёк ИЛИ отряд
   уничтожен → результат с «Повторить операцию» (reload level) / «На базу».

## 8. Сборка билда (этап 9)

1. Project Settings → Packaging: Build Configuration = Shipping (сначала
   Development для проверки), Full Rebuild off, «List of maps» = 4 карты.
2. Platforms → Windows → Package Project. Билд гонять из чистой папки.
3. Если в билде нет карт/ассетов — проверить, что все ссылки идут через
   уровни/GameInstance (иначе добавить в Additional Asset Directories to Cook).

## 9. Частые грабли

- **BindWidget**: имена стеков в WBP_PrimaryGameLayout должны в точности
  совпадать с C++ (`GameStack`, `GameMenuStack`, `MenuStack`, `ModalStack`).
- **Перцепция не видит врагов** → проверь `DefaultTeamId` (1 vs 2) на ОБОИХ BP.
- **Юнит не в укрытии** → проверить `WorldStatic`, затем известный дефект
  высот: до Ф2 из `11_COVER_AND_ENEMY_PLAN.md` ящик 60 см не срабатывает,
  временные пороги около 148/238 см вместо целевых 60/150.
- **AI не ходит** → нет NavMeshBoundsVolume/не сгенерился навмеш (клавиша P).
- **После смены уровня нет UI** → layout пересоздаётся контроллером уровня;
  у контроллера должен быть задан `RootLayoutClass`.
- **Изменил C++** → пересобрать при закрытом редакторе (`.\Build-XRU1.ps1`).
