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

1. **Maps & Modes**: Game Instance Class = `BP_TacticsGameInstance`;
   Editor/Game Default Map = `L_MainMenu` (когда появится).
2. **Input**: убедиться, что Default Player Input Class = Enhanced Player Input
   (UE5.7 по умолчанию так).
3. **Collision**: канал укрытий не нужен — используем `ECC_WorldStatic`
   (уже дефолт `CoverTraceChannel`); стены-укрытия должны блокировать WorldStatic.
4. **Navigation Mesh**: Runtime Generation = Static (карты статичные).
5. **Packaging** (этап 9): List of maps to include — все 4 карты.

## 3. UI-каркас (этап 3)

### 3.1 BP_TacticsGameInstance
`Content/XRU1Game/Core/` → BP от `UTacticsGameInstance`. В Details задать:
`HubLevel = L_Hub`, `MainMenuLevel = L_MainMenu`. Прописать в Project Settings.

### 3.2 WBP_PrimaryGameLayout
`Content/XRU1Game/UI/` → Widget Blueprint, **Parent Class = `UPrimaryGameLayout`**.
Иерархия (Overlay на весь экран), имена ДОЛЖНЫ совпадать (BindWidget):
```
Overlay
 ├─ CommonActivatableWidgetStack  «GameStack»      (Fill)
 ├─ CommonActivatableWidgetStack  «GameMenuStack»  (Fill)
 ├─ CommonActivatableWidgetStack  «MenuStack»      (Fill)
 └─ CommonActivatableWidgetStack  «ModalStack»     (Fill)
```

### 3.3 Экраны меню
`Content/XRU1Game/UI/Menus/`, каждый — Widget Blueprint от своего C++ класса
(`UMainMenuWidget`, `UDifficultySelectWidget`, `USettingsMenuWidget`,
`UAboutMenuWidget`, `UPauseMenuWidget`). Кнопки (CommonButtonBase) вешают
события на **готовые методы**: `RequestNewGame`, `RequestContinue`,
`RequestSettings`, `RequestAbout`, `RequestQuit`, `RequestBack`,
`ChooseDifficulty(Easy/Medium/Hard)`.
В WBP_MainMenu в Details заполнить `SettingsScreenClass`, `AboutScreenClass`,
`DifficultyScreenClass`. Доступность «Продолжить» — бинд на `CanContinue`.

### 3.4 Уровень меню
- `BP_MenuPlayerController` от `AMenuPlayerController`: задать
  `RootLayoutClass = WBP_PrimaryGameLayout`, `InitialScreenClass = WBP_MainMenu`.
- `GM_MainMenu` от GameModeBase: PlayerController = BP_MenuPlayerController,
  Default Pawn = None, HUD = None.
- Карта `L_MainMenu`: пустая сцена с фоном (камера смотрит на декорацию из
  кита) → World Settings → GameMode Override = GM_MainMenu.
- Пауза в игровых уровнях: по Esc пушить WBP_PauseMenu на слой Menu
  (после этапа 1 это делает `ATacticalPlayerController`; в хабе — контроллер хаба).

### 3.5 Интро-видео (после выбора сложности)
1. Готовый ролик (07_CONTENT_PROMPTS §2) положить как `Content/Movies/Intro.mp4`
   (папка Movies пакуется в билд как есть; LFS подхватит mp4).
2. Создать: **FileMediaSource** (`MS_Intro`, путь `./Movies/Intro.mp4`),
   **MediaPlayer** (`MP_Intro`, ✔ автосоздание MediaTexture `MT_Intro`),
   Material `M_IntroVideo` (Unlit, Emissive = MT_Intro).
3. `WBP_IntroPlayer` (наследник `UMenuScreenBase`): Image на весь экран с
   `M_IntroVideo`; в Construct — `MP_Intro.OpenSource(MS_Intro)` + Play;
   OnEndReached ИЛИ любой клик (OnMouseButtonDown) → `GetTacticsGameInstance()
   → TravelToHub()`. Подсказка «Пропустить — клик» в углу.
4. В `WBP_DifficultySelect.ChooseDifficulty` BP-достройка: вместо мгновенного
   `TravelToHub` (C++ уже сделал кампанию и трэвел) — ПРОЩЕ: перед вызовом
   `ChooseDifficulty` пушить WBP_IntroPlayer, а его завершение вызывает
   `ChooseDifficulty(выбранная)`. Либо (ещё проще): хранить выбор в переменной
   экрана, интро → по окончании вызвать ChooseDifficulty. Fallback без видео —
   те же кадры слайдами (Image + таймер 6 сек/кадр).

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
`Content/XRU1Game/Units/` → BP от `AUnit_Assault` / `AUnit_Sniper` /
`AUnit_Healer` / `AUnit_Tank`:
1. Mesh = SKM_Manny_Simple (медик/снайпер — Quinn для разнообразия), ABP_Soldier.
2. Прицепить меш винтовки в сокет руки (создать сокет `weapon_r` на скелете).
3. **Team**: `DefaultTeamId = 1`.
4. **AI**: AIControllerClass = `AUnitAIController`, Auto Possess AI =
   Placed in World or Spawned (перцепция нужна и юнитам игрока — Overwatch;
   приказы игрока идут через этот же контроллер).
5. Статы (категория Tactics|Stats): BaseAim/ShotDamage/AttackRange/MoveRange
   уже заданы в C++ по GDD §7 — в BP только проверить/подстроить. HP — через
   `StartupEffects` (GE-инициализация атрибутов) по GDD §7.
6. **Способности** (категория Tactics|Abilities) — заполнить классы:
   - `AttackAbilityClass` = BP_GA_Attack (BP-наследник `UGA_Attack` с монтажом),
   - `OverwatchAbilityClass` = BP_GA_Overwatch,
   - `HunkerAbilityClass` = BP_GA_HunkerDown,
   - `ClassAbilityClass`: медик BP_GA_Heal, штурмовик BP_GA_RunAndGun,
     танк BP_GA_Taunt, снайпер — ПУСТО (Squadsight — пассивка, bHasSquadsight
     уже true в C++).
7. HUD над головой: `HUDWidgetClass` уже есть у `ATDCombatant` — расширить WBP
   пипсами AP (`OnActionPointsChanged`), иконкой укрытия (`OnCoverStateChanged`)
   и статуса (`OnUnitStateChanged`: ранен/эвакуирован).

### 4.3 Враг
`BP_Unit_Marauder` от `AUnitBase` (не от AUnit_*): `DefaultTeamId = 2`, меш
Manny с тёмным материалом, статы: BaseAim 65 / ShotDamage 20 (HP и aim
переопределит `ATacticsGameMode` по сложности). Способности врагу не нужны
(его ход ведёт `AUnitAIController` напрямую через ResolveShot).
**Патруль**: на размещённом на карте экземпляре врага заполнить массив
`PatrolPoints` (TargetPoint'ы уровня) — без точек юнит стоит на посту;
тревога поднимется от шума выстрелов или визуального контакта (GDD §8).

## 4.5 Боевой контроллер, ввод и GameMode (после этапа 1 — классы готовы)

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
   `RootLayoutClass = WBP_PrimaryGameLayout`, `PauseMenuClass = WBP_PauseMenu`,
   `MoveRangeVisualizerClass = BP_MoveRangeVisualizer`.
4. **BP_MoveRangeVisualizer** от `AMoveRangeVisualizer`: материалы зон —
   полупрозрачные unlit (`ZoneOneMaterial` = синий M_MoveZone_Blue,
   `ZoneTwoMaterial` = жёлтый M_MoveZone_Yellow, Translucent, ~0.3 альфы).
   Для ленты превью пути — `PathOneMaterial`/`PathDashMaterial` (те же цвета,
   но плотнее, ~0.8 альфы; пусто = возьмёт материалы зон). Параметры `CellSize`
   (шаг сетки, 50 см — меньше = глаже/дороже) и `PathWidth` — там же в дефолтах.
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

## 4.6 Подсветка юнитов: обводка при наведении + кольцо выбора

C++ уже всё делает (ховер-трейс, Render Custom Depth со stencil 1 = отряд /
2 = враг, показ кольца в `SelectUnit`); `r.CustomDepth=3` включён в
DefaultEngine.ini (проверить: Project Settings → Rendering → Postprocessing →
Custom Depth-Stencil Pass = **Enabled with Stencil**). В редакторе нужно два
материала и два назначения.

1. **M_OutlinePP** (`Content/XRU1Game/Materials/`) — post-process обводка:
   - Details материала: Material Domain = **Post Process**; Blendable Location =
     **Before Tonemapping** (в новых версиях пункт называется *Scene Color
     Before Bloom*) — так обводку сглаживает TSR/TAA.
   - Граф (классический 4-тап edge-detect, без Custom-нод):
     1. `SceneTexture: CustomStencil` (UVs пустые) → `Mask(R)` — stencil в
        центре пикселя, далее **C**.
     2. Четыре `SceneTexture: CustomStencil` с UVs = `ScreenPosition` +
        смещение на 1 пиксель: смещение = `float2(±1,0)/float2(0,±1)` ×
        скалярный параметр **Thickness** (деф. 2) × `ViewSize` (нода
        *ViewProperty → InvSize*). Их `Mask(R)` свести попарно нодами `Max` →
        **N** (максимальный stencil соседей).
     3. Маска ребра: `If (C < 0.5 && N > 0.5)` → практично: `1 - saturate(C)`
        умножить на `saturate(N)` → **EdgeMask** (обводка рисуется СНАРУЖИ
        юнита: центр пуст, сосед закрашен).
     4. Цвет: `If (N == 1)` → VectorParam **AllyColor** (голубой ~(0.1,0.8,1)),
        иначе VectorParam **EnemyColor** (красный ~(1,0.15,0.1)); сравнение —
        `If` с порогом 1.5.
     5. Итог: `Lerp( SceneTexture:PostProcessInput0 , Цвет×Intensity(деф. 5) ,
        EdgeMask )` → **Emissive Color**.
2. **M_SelectionRing** (там же) — кольцо-декаль:
   - Material Domain = **Deferred Decal**, Blend Mode = **Translucent**.
   - Маска кольца: `RadialGradientExponential` (Radius 0.5, Density 8) минус
     `RadialGradientExponential` (Radius 0.4, Density 8) → **Opacity**;
     VectorParam **RingColor** (жёлто-зелёный, ×3 эмиссив) → **Emissive Color**.
   - (По желанию текстура-кольцо вместо градиентов — просто в Opacity.)
3. **BP_TacticalCameraPawn** → Details → Tactics|Camera → `OutlineMaterial =
   M_OutlinePP`. Материал вешается блендаблом на **unbound-`PostProcessComponent`**
   пешки в BeginPlay — отдельный PostProcessVolume на карте НЕ нужен (если добавлял
   вручную для теста — можно убрать, иначе обводка примёнится дважды).
4. **BP-юниты** (BP_Unit_* и враг): компонент `SelectionDecal` → Decal Material
   = `M_SelectionRing`. Дефолты положения/размера уже заданы в C++ (опущен к
   ногам `Z=-88`, `DecalSize` X=120 глубина / Y/Z=60 радиус) — трогать только
   если у юнита нестандартная капсула (напр. у танка увеличить радиус).

Логика подсветки (C++, уже работает):
- **кольцо выбора** — только в свою (игрока) фазу; в ход врага прячется, чтобы
  не загромождать картинку, и возвращается в начале нашего хода;
- **ховер-обводка** в фазу игрока — и свои (голубым), и враги (красным);
- **ховер-обводка** в фазу врага — свои НЕ подсвечиваются, враги подсвечиваются
  (видно их ход);
- смерть/эвакуация гасят и обводку, и кольцо.

Проверка на Lvl_TopDown: навёл на бойца — голубая обводка, на врага — красная;
ЛКМ по бойцу — под ногами кольцо, ЛКМ в пусто — гаснет; заверши ход — в фазу
врага кольцо выбранного пропадает и наведение на своих обводку не даёт, а
враги при наведении подсвечиваются.

## 5. Уровень-туториал (этап 7)

1. Карта `L_Tutorial` (~60×40 м) из кита (НЕ примитивы): **секции A–D** по
   сценарию 02_LORE_SCRIPT §4 — A: старт + «пустырь» + бетонный блок (full);
   B: дальняя дистанция с мешками (half) для танка/снайпера; C: окопавшийся
   враг за укрытием; D: сектор для overwatch + площадка эвакуации у выхода.
   Низкие укрытия ~60 см, высокие ~150 см (блокируют WorldStatic).
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
- **Юнит не в укрытии** → стена не блокирует WorldStatic или ниже 60 см.
- **AI не ходит** → нет NavMeshBoundsVolume/не сгенерился навмеш (клавиша P).
- **После смены уровня нет UI** → layout пересоздаётся контроллером уровня;
  у контроллера должен быть задан `RootLayoutClass`.
- **Изменил C++** → пересобрать при закрытом редакторе (`.\Build-XRU1.ps1`).
