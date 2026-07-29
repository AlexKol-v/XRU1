# Tutorial и Mission01 на общей карте

Актуально на 2026-07-29. Документ фиксирует production-архитектуру обучения
«Полигон “Купол”» и миссии «Станция “Узел-7”» на **одном persistent World**:
`/Game/US_Military/Levels/Showreel_Scene`.

Карта **не использует World Partition**. Различия между запусками хранятся в
двух обычных streaming sublevel и двух `UTacticalScenarioDataAsset`. Создавать
копии `Showreel_Scene`, писать gameplay в Level Blueprint или определять режим
по имени открытого map package запрещено.

Сценарные тексты и порядок A1–D3 остаются в
[02_LORE_SCRIPT.md](02_LORE_SCRIPT.md), правила боя — в
[01_GDD.md](01_GDD.md). Этот файл отвечает за архитектуру, Editor-настройку и
проверку.

## 1. Итоговая схема

```text
Hub POI
  └─ UTacticsGameInstance::StartCombatScenario(DA_Scenario_*)
       ├─ ActiveScenario = DA_Scenario_*
       └─ OpenLevel(Showreel_Scene)
            └─ BP_TacticalScenarioDirector (persistent)
                 ├─ читает ActiveScenario
                 ├─ загружает ровно один streaming sublevel
                 ├─ ждёт его фактической загрузки/показа
                 ├─ регистрирует акторов и запускает бой
                 └─ StartConfiguredQuest()
                      ├─ Inactive → MakeAvailable
                      └─ Available → Start
                           └─ AQuestRunnerActor + StateTree
                                ├─ принимает подтверждённые Quest.Event.Tactical.*
                                └─ обновляет objective/HUD
```

Разделение ответственности:

| Слой | За что отвечает | За что не отвечает |
|---|---|---|
| Persistent `Showreel_Scene` | общий арт, collision, свет, NavMesh, камера, `GM_Tactics`, один Scenario Director | состав конкретного сценария |
| `SL_Showreel_Tutorial` | бойцы аттестации, голограммы A–D, зоны, anchors, tutorial-only FX | общие здания/свет, логика последовательности |
| `SL_Showreel_Mission01` | отряд миссии, враги, бомба, эвакуация, mission-only FX | общие здания/свет, логика результата |
| `DA_Scenario_*` | выбор sublevel, quest, типа сценария, fog-профиля | runtime-прогресс |
| `StateTree` | последовательность шагов и objective-прогресс | разрешение ввода, урон, AP, прямое управление анимациями |
| Gameplay-домен | истинный результат движения/атаки/ability/эвакуации | решение, какой tutorial-шаг сейчас активен |
| Action Gate | какие команды сейчас разрешены | зачёт objective и изменение HP/AP |
| `ATacticsGameMode`/Scenario Director | старт/сброс запуска и единая финализация победы/поражения | authored-тексты шага |

Главный инвариант: **квест получает событие только после подтверждённого
результата доменной механики**. Нажатие кнопки, начало montage, выдача
`AI MoveTo` или попытка активировать ability не завершают шаг.

## 2. География одной физической карты

Оба сценария используют одну и ту же геометрию, но разные постановочные наборы:

| Сектор мира | Tutorial | Mission01 | Где хранится геометрия |
|---|---|---|---|
| Юг | A: открытое поле/full-cover; D: итоговая эвакуация | старт отряда и будущая эвакуация | стены, дорога, грузовик, мешки — persistent; зоны/юниты — sublevel |
| Центр | B: Молот + Оса; дальняя голограмма | группа врагов B, контейнеры | крупные blockers/cover — persistent |
| Север | C: укреплённая голограмма | бомба и группа врагов C | ретранслятор/бетон — persistent; бомба/враги — mission sublevel |

Крупный объект, влияющий на NavMesh или cover math в обоих режимах, должен
лежать в persistent. Sublevel содержат прежде всего юнитов, интерактивные цели,
триггеры, маркеры и косметику. Это исключает разные NavMesh на одной карте.

## 3. СДЕЛАНО

### 3.1 Выбор сценария и единый travel

- `UTacticalScenarioDataAsset` хранит `ScenarioId`, `Kind`, `QuestDefinition`,
  `ScenarioSublevel`, `TurnLimit`, `FogProfileId`, `bResetFogOnStart`:
  [TacticalScenarioDataAsset.h](../Source/XRU1/Tactics/TacticalScenarioDataAsset.h).
- `UTacticsGameInstance` хранит `SharedCombatLevel`, transient
  `ActiveScenario` и открывает общую карту через `StartCombatScenario`:
  [TacticsGameInstance.h](../Source/XRU1/Tactics/TacticsGameInstance.h),
  [TacticsGameInstance.cpp](../Source/XRU1/Tactics/TacticsGameInstance.cpp).
- `ATacticalScenarioDirector` читает `ActiveScenario`, даёт BP-хук
  `OnScenarioSelected` и запускает настроенный quest только после явного вызова
  `StartConfiguredQuest`:
  [TacticalScenarioDirector.h](../Source/XRU1/Tactics/TacticalScenarioDirector.h).
- `StartConfiguredQuest` уже соблюдает правильный порядок:
  `Inactive → MakeQuestAvailable → Available → StartQuestById`. При невозможном
  состоянии возвращает `false`, а не делает вид, что квест стартовал.
- При уходе из World активный квест abandon-ится, чтобы не оставить
  `UGameInstanceSubsystem` с живым instance без world-owned runner.
- После старта Director один раз публикует `Scenario.Ready`. Метод
  `FinalizeConfiguredScenario` публикует единственный `Succeeded/Failed` leaf и
  согласованно переводит quest в terminal state до result screen.

### 3.2 Quest registry и безопасный runner

- `DefaultGame.ini` сканирует primary assets типа `Quest` только в
  `/Game/XRU1Game/Quests`.
- `UQuestDefinition` — `UPrimaryDataAsset`, а логика хранится отдельно в
  `StateTree`.
- Старт без `QuestLogic` отклоняется до spawn. Spawn `AQuestRunnerActor`
  исправлен: instance становится `Active` только после успешного `SpawnActor`;
  при ошибке он не залипает навсегда без runner:
  [QuestSubsystem.cpp](../Plugins/STQuestSystem/Source/STQuestSystem/Private/QuestSubsystem.cpp).
- `Quest Objective` и `Quest Objective Group` событийные, не поллят мир каждый
  кадр и поддерживают восстановление счётчиков.

### 3.3 Тактические каналы

`UTacticalQuestEvents` объявляет и публикует следующие leaf-каналы:

- `Quest.Event.Tactical.Camera.Adjusted`;
- `Quest.Event.Tactical.Unit.Selected`;
- `Quest.Event.Tactical.Zone.Entered`;
- `Quest.Event.Tactical.Movement.Settled.Open`;
- `Quest.Event.Tactical.Movement.Settled.InCover`;
- `Quest.Event.Tactical.Turn.Ended`;
- `Quest.Event.Tactical.Turn.Player.Started`;
- `Quest.Event.Tactical.Combat.Attack.Normal`;
- `Quest.Event.Tactical.Combat.Attack.Squadsight`;
- `Quest.Event.Tactical.Combat.Attack.Overwatch`;
- `Quest.Event.Tactical.Combat.Enemy.Eliminated`;
- `Quest.Event.Tactical.Ability.Heal.Normal`;
- `Quest.Event.Tactical.Ability.Heal.Revive`;
- `Quest.Event.Tactical.Ability.Taunt.Activated`;
- `Quest.Event.Tactical.Ability.RunAndGun.Activated`;
- `Quest.Event.Tactical.Ability.Overwatch.Activated`;
- `Quest.Event.Tactical.Ability.Hunker.Activated`;
- `Quest.Event.Tactical.Objective.Defuse.Progressed`;
- `Quest.Event.Tactical.Objective.Defuse.Completed`;
- `Quest.Event.Tactical.Objective.Evac.Unit`;
- `Quest.Event.Tactical.Objective.Evac.Squad`;
- `Quest.Event.Tactical.Scenario.Ready`;
- `Quest.Event.Tactical.Scenario.Succeeded`;
- `Quest.Event.Tactical.Scenario.Failed`.

`BroadcastQuestEvent` принимает только дочерний тег `Quest.Event.*` и
положительный `Amount`, затем отправляет `FQuestEventData` через
`UGameplayMessageSubsystem`:
[TacticalQuestEvents.cpp](../Source/XRU1/Tactics/TacticalQuestEvents.cpp).

`ATacticalQuestZone` подходит тактическим бойцам XRU1: он проверяет живого юнита
стороны игрока через `TurnManager`, а не `IsPlayerControlled` (бойцами владеют
`AIController`). Поддержаны one-shot на юнита и отключение после первого входа:
[TacticalQuestZone.cpp](../Source/XRU1/Tactics/TacticalQuestZone.cpp).

Автоматические emitters уже есть у `ATacticalQuestZone`, подтверждённых
player-turn, player attack/squadsight, heal/revive, Hunker/Taunt/RunAndGun,
Overwatch, enemy-elimination, defuse/evac outcomes и границ
`Scenario.Ready/Result`. Selection, camera, move settlement и scripted enemy
attacks ещё требуют orchestration/payload gate из P1. Это фундамент, а не
готовый туториал.

## 4. РУЧНАЯ НАСТРОЙКА В EDITOR

Нативный streaming bootstrap уже переведён на явную границу готовности:
при наличии `ActiveScenario` `ATacticsGameMode` ждёт вызова Director и не
сканирует persistent World по таймеру. Ниже остаётся Editor-работа — самих двух
scenario sublevel, Scenario/Quest Data Asset и StateTree в `Content/` пока нет.

### 4.1 Подготовка после сборки C++

1. Закрыть Unreal Editor, собрать проект через `Build-XRU1.ps1`, открыть Editor.
2. Убедиться, что включены `STQuestSystem`, `GameplayMessageRouter`, `StateTree`.
3. Проверить Project Settings → Maps & Modes:
   - Game Instance Class = `/Game/XRU1Game/Core/BP_TacticsGameInstance`;
   - боевой GameMode = `/Game/XRU1Game/Core/GM_Tactics`.
4. Открыть `BP_TacticsGameInstance` и назначить:
   - `SharedCombatLevel` = `/Game/US_Military/Levels/Showreel_Scene`;
   - существующие `HubLevel` и `MainMenuLevel` оставить без изменения.
5. Перезапустить Editor после первого появления native gameplay tags и
   AssetManager scan. Иначе `Quest.Tutorial`/`Quest.Mission01` могут не появиться
   в picker, а новые Quest Definition — в registry текущего GameInstance.

### 4.2 Создание двух streaming sublevel

1. Открыть `/Game/US_Military/Levels/Showreel_Scene`.
2. Window → Levels. Persistent level должен быть выделен жирным.
3. Создать и сохранить два **новых scenario-specific** уровня:
   - `/Game/XRU1Game/Maps/Scenarios/SL_Showreel_Tutorial`;
   - `/Game/XRU1Game/Maps/Scenarios/SL_Showreel_Mission01`.
4. Добавить оба в Levels persistent-карты и для каждого выбрать
   **Change Streaming Method → Blueprint**.
5. Оба новых scenario sublevel должны быть не загружены и не видимы по
   умолчанию. На runtime Scenario Director показывает ровно один из них.
   Существующие art/lighting streaming levels набора `US_Military` сохраняются:
   правило «ровно один» относится только к Tutorial/Mission01-паре.
6. В persistent оставить:
   - общий арт, Landscape/дороги/здания и collision;
   - DirectionalLight, SkyLight, ExponentialHeightFog, PostProcess;
   - `NavMeshBoundsVolume`/RecastNavMesh;
   - общие camera bounds и рабочий `PlayerStart` рядом с боевой областью;
   - один `BP_TacticalScenarioDirector`.
7. Не переносить lighting и NavMesh в scenario sublevel. Не ставить второй
   `PlayerStart`, если старт camera pawn общий. После stream директор фокусирует
   камеру на scenario-specific `TargetPoint`.
8. Для редактирования делать нужный sublevel Current и включать видимость только
   ему. Проверить World Outliner фильтром Level, что actors не попали в
   persistent случайно.

Рекомендуемые папки World Outliner:

```text
Persistent/ScenarioBootstrap
Persistent/SharedCamera
Tutorial/Units
Tutorial/Holograms
Tutorial/Zones
Tutorial/Anchors
Mission01/Units
Mission01/Enemies
Mission01/Objectives
Mission01/Anchors
```

### 4.3 Scenario Director

1. Создать `/Game/XRU1Game/Core/BP_TacticalScenarioDirector` от
   `ATacticalScenarioDirector`.
2. Поставить **один** экземпляр в persistent.
3. В `OnScenarioSelected(Scenario)`:
   - проверить `IsValid(Scenario)` и ненулевой `ScenarioSublevel`;
   - вызвать `Load Stream Level (by Object Reference)` для
     `Scenario.ScenarioSublevel`;
   - `Make Visible After Load = true`;
   - не использовать произвольный `Delay` как признак готовности;
   - с latent `Completed`/`OnLevelShown` перейти к scenario initialization;
   - после регистрации акторов вызвать `StartConfiguredQuest` ровно один раз;
   - `true` означает, что запуск принят, но `Scenario.Ready` намеренно придёт на
     следующем tick, а `OnScenarioReady` — ещё через один tick после обработки
     события StateTree; если вернулся `false`, вывести явную ошибку.
4. Реализовать `OnScenarioReady`: только здесь применить первый Action Gate и
   разблокировать разрешённый ввод. C++ вызывает хук через дополнительный tick
   после публикации `Scenario.Ready`, чтобы StateTree успел войти в первый
   action-state. Не открывать ввод по return value `StartConfiguredQuest` или
   напрямую после event — это создаёт гонку с первым objective.
5. Между `OnLevelShown` и стартом допустим `Set Timer for Next Tick`, чтобы все
   streamed actors закончили `BeginPlay`; фиксированный delay в секундах не
   является контрактом.
6. Не вызывать `StartQuestById` отдельно в Blueprint: `StartConfiguredQuest`
   уже делает `MakeAvailable → Start` и проверяет состояние.
7. Level Blueprint persistent и обоих sublevel оставить пустыми.

Нативный порядок `StartConfiguredQuest` уже фиксирован и должен оставаться таким:

```text
OnLevelShown
 → BuildScenarioActorRegistry
 → Set Timer for Next Tick
 → StartConfiguredQuest
    → MakeAvailable / Start Quest / SetTrackedQuest
    → GameMode: Bind Bomb/Evac/Units + StartScenarioCombat
 → next tick: Broadcast Scenario.Ready
 → next tick: StateTree processes Ready
 → timer boundary: OnScenarioReady → ApplyFirstGate + EnableAllowedInput
```

### 4.4 Scenario Data Assets

Создать в `/Game/XRU1Game/Data/Scenarios`:

| Поле | `DA_Scenario_Tutorial` | `DA_Scenario_Mission01` |
|---|---|---|
| `ScenarioId` | `Tutorial` | `Mission01` |
| `Kind` | `Tutorial` | `Mission` |
| `QuestDefinition` | `DA_Quest_Tutorial` | `DA_Quest_Mission01` |
| `ScenarioSublevel` | `SL_Showreel_Tutorial` | `SL_Showreel_Mission01` |
| `TurnLimit` | `0` | `-1`; лимит 12/10/8 берётся из difficulty profile |
| `FogProfileId` | `Tutorial` | `Mission01` |
| `bResetFogOnStart` | true | true |

Семантика уже едина: `-1` наследует правило `GameMode`/сложности, `0` явно
отключает таймер, `>0` задаёт точный лимит. Tutorial хранит намеренное `0`, а
Mission01 при `-1` получает 12/10/8.

В Hub у POI полигона назначить `Scenario = DA_Scenario_Tutorial`, у POI Узла-7
— `DA_Scenario_Mission01`; `LevelToLoad` очистить. `RequiredCompletedMission`
для Mission01 = `Tutorial`, для Tutorial = `None`. Поле `MissionId` остаётся
legacy/UI fallback: runtime ID нового пути берётся из `Scenario.ScenarioId`.

### 4.5 Quest Definition и StateTree

Создать папку `/Game/XRU1Game/Quests`; только она сканируется AssetManager.

До payload-aware task и Action Gate из P1 здесь можно собрать **skeleton**:
имена states, Description, objective IDs и переходы. Шаги, где нужно проверить
конкретный unit/target/zone/hit/damage/distinct actor, не считать production-ready
только на основании стандартного donor `QuestTask_TrackObjective`: текущий runner
передаёт StateTree канал, но не `FQuestEventData.Source`.

1. Создать `ST_Quest_Tutorial` и `ST_Quest_Mission01` типа StateTree.
2. Schema обоих ассетов = `StateTreeComponentSchema`.
3. Создать через пункт **Quest Definition**:

| Поле | `DA_Quest_Tutorial` | `DA_Quest_Mission01` |
|---|---|---|
| `QuestId` | `Quest.Tutorial` | `Quest.Mission01` |
| `DisplayName` | `Аттестация: Полигон «Купол»` | `Операция: Узел-7` |
| `Category` | `Main` | `Main` |
| `Difficulty` | `Trivial` | `Normal` |
| `QuestLogic` | `ST_Quest_Tutorial` | `ST_Quest_Mission01` |
| `Prerequisites` | пусто | пусто; campaign-гейт остаётся у POI |
| `ChainLinks` | пусто | пусто |
| `Rewards` | пусто | пусто |

Не дублировать гейт Mission01 в Quest chain: источник campaign-прогресса —
`UTacticsSaveGame.CompletedMissions`, а POI Mission01 требует `Tutorial`.

В каждом action-state добавить `Quest Objective` или `Quest Objective Group`.
Переход — `On State Succeeded → следующий state`. В `Description` хранить
короткую текущую инструкцию для HUD, а не всю режиссёрскую логику.
На корневом state обоих деревьев добавить глобальный переход
`On Event Quest.Event.Tactical.Scenario.Failed → Tree Failed`, чтобы поражение
из любого шага не зависело от достижения финального `WaitResult`.
У каждой tactical `Quest Objective` и у каждого spec внутри
`Quest Objective Group` включить `bRequireExactChannel = true` и указывать
точный leaf. Parent-match оставлен в плагине только для совместимости с
donor-квестами: lethal shot публикует Attack + Enemy.Eliminated, а финальная
эвакуация — Evac.Unit + Evac.Squad, поэтому широкий parent даст ложный счёт.

Gameplay tags для objective ID уже добавлены в
`Config/DefaultGameplayTags.ini` (они не являются event-каналами). Полный
буквальный список для picker:

```text
Quest.Objective.Tutorial.A1
Quest.Objective.Tutorial.A1.SelectMedic
Quest.Objective.Tutorial.A1.CameraAdjusted
Quest.Objective.Tutorial.A2
Quest.Objective.Tutorial.A3
Quest.Objective.Tutorial.A4
Quest.Objective.Tutorial.A5
Quest.Objective.Tutorial.A6
Quest.Objective.Tutorial.A7
Quest.Objective.Tutorial.A8
Quest.Objective.Tutorial.A9
Quest.Objective.Tutorial.B1
Quest.Objective.Tutorial.B2
Quest.Objective.Tutorial.B3
Quest.Objective.Tutorial.B4
Quest.Objective.Tutorial.B5
Quest.Objective.Tutorial.C1
Quest.Objective.Tutorial.C2
Quest.Objective.Tutorial.C2.Activate
Quest.Objective.Tutorial.C2.Move01
Quest.Objective.Tutorial.C2.Move02
Quest.Objective.Tutorial.C2.Attack
Quest.Objective.Tutorial.D1
Quest.Objective.Tutorial.D1.Overwatch01
Quest.Objective.Tutorial.D1.Overwatch02
Quest.Objective.Tutorial.D1.Hunker01
Quest.Objective.Tutorial.D1.Hunker02
Quest.Objective.Tutorial.D1.EndTurn
Quest.Objective.Tutorial.D2
Quest.Objective.Tutorial.D3
Quest.Objective.Mission01.Defuse
Quest.Objective.Mission01.Defuse.Progressed
Quest.Objective.Mission01.Defuse.Completed
Quest.Objective.Mission01.Evacuate
Quest.Objective.Mission01.Evacuate.Unit
Quest.Objective.Mission01.Evacuate.Squad
```

Для составных шагов использовать дочерние ID, например
`Quest.Objective.Tutorial.A1.SelectMedic` и `.A1.CameraAdjusted`. После
настройки запустить **Asset Actions → Validate Assets**: validator проверит
`QuestId`, `QuestLogic` и прямые циклы chain links.

Референсы в donor-проекте, только для просмотра структуры:

- `Content/Quests/DA_Quest_FirstHunt.uasset`;
- `Content/Quests/ST_Quest_FirstHunt.uasset`;
- `Content/Quests/ST_Quest_KillAndCollect.uasset`.

Копировать их сюжетные теги/actors не нужно: в XRU1 используются собственные
`Quest.Event.Tactical.*` и `ATacticalQuestZone`.

## 5. Полная раскладка Tutorial A1–D3

В таблице «событие» означает подтверждённый результат. Префикс
`Quest.Event.Tactical.` опущен только для краткости. Ограничение действий
реализует отдельный Action Gate; один `Quest Objective` сам по себе ничего не
блокирует.

| Шаг | StateTree state / objective | Подтверждённое событие и условие | Actors и постановка | Gate на время шага |
|---|---|---|---|---|
| A1 | `A1_SelectAndCamera`, Objective Group: Select + Camera | `Unit.Selected` после фактической смены selected unit на Медика; `Camera.Adjusted` один раз после выполнения порога вращения **и** zoom | `Unit_Tutorial_Medic`, `Anchor_Camera_Tutorial` | выбор только Медика; разрешены pan/rotate/zoom/pause |
| A2 | `A2_MoveOpen` | два успешных `Movement.Settled.Open` и вход Медика в `QZ_A2_OpenGround`; итог AP=0 | два move anchors, `QZ_A2_OpenGround` | только Медик и только допустимые точки маршрута; attack/ability/end turn закрыты |
| A3 | `A3_EndTurn` | `Turn.Ended` только после перехода `TurnManager` на Enemy side | — | разрешены камера/pause/End Turn |
| A4 | `A4_ExposedHit` | scripted attack проходит общий attack pipeline; `Combat.Attack.Normal`, цель = Медик, hit=true, применено 30 damage; затем подтверждён новый Player turn | `Holo_A_OpenField`, firing anchor | gameplay-ввод закрыт, камера/pause остаются; следующий шаг не стартует по montage notify |
| A5 | `A5_FullCover` | единый `MoveSettlement` завершён после cover-hug/turn-in-place; leaf `Movement.Settled.InCover`, cover=Full и стена действительно между Holo A и Медиком | `QZ_A5_FullCover`, full-cover blocker, highlight | двигаться может только Медик к подсвеченной области |
| A6 | `A6_SelfHeal` | `Ability.Heal.Normal` после реального роста HP на 50, source/target = Медик, AP/charge списаны ровно один раз | Медик | только Field Medicine с self-target; камера/pause |
| A7 | `A7_CoverMiss` | scripted attack через общий pipeline; `Combat.Attack.Normal`, hit=false, HP не изменился; подтверждён Player turn | тот же `Holo_A_OpenField` | ввод закрыт до полного завершения action |
| A8 | `A8_ReturnFire` | player `Combat.Attack.Normal` по Holo A и postcondition `Holo_A` уничтожен; не raw click и не `FireCommit` | Holo A с учебным HP, чтобы сценарный выстрел добил | только атака Медиком по Holo A; движение закрыто |
| A9 | `A9_ReviveAssault` | `Ability.Heal.Revive` после снятия Downed и установки 30 HP у Клина | `Unit_Tutorial_Assault` проявляется/активируется Downed у стены, revive anchor | Медик: move + revive только Клина |
| B1 | `B1_EnterSector`, RequiredCount=2 | два **разных** бойца Tank и Sniper вошли в `QZ_B1_Sector` | `Unit_Tutorial_Tank`, `Unit_Tutorial_Sniper`, большая one-shot-per-unit зона | выбор/move только этих двух; нельзя накрутить счётчик одним бойцом |
| B2 | `B2_TankAdvance` | Tank завершил два `MoveSettlement`; финальный leaf `Movement.Settled.InCover` подтверждает Half cover против Holo B | `Holo_B_Range`, `QZ_B2_TankHalfCover`, мешки | только Tank, два разрешённых destination |
| B3 | `B3_Taunt` | `Ability.Taunt.Activated` после успешной ability activation и появления gameplay tag/status | Tank | только Taunt; повторный rejected activation не считается |
| B4 | `B4_AbsorbShot` | scripted `Combat.Attack.Normal`: target=Tank, hit=true, итоговый damage уменьшен провокацией вдвое; затем Player turn | Holo B | ввод закрыт до завершения enemy action |
| B5 | `B5_SquadsightKill` | `Combat.Attack.Squadsight` после разрешённой атаки Sniper через sight Tank; Holo B уничтожен | Sniper остаётся на дальней позиции, Tank видит Holo B | только Sniper Attack по Holo B, move запрещён |
| C1 | `C1_Brief` | presentation task завершил реплику/субтитр и активировал Holo C; это не combat event | `Holo_C_Cover`, cover в глубине | камера/pause; боевые команды закрыты до конца beat |
| C2 | parent `C2_RunAndGun`, последовательные children: Activate → Move → Attack | `Ability.RunAndGun.Activated` → два подтверждённых `Movement.Settled.*` → `Combat.Attack.Normal` и Holo C уничтожен | Assault, Holo C, два move anchors | только Assault; порядок ability → 2 move → attack обязателен |
| D1 | `D1_PrepareAmbush`, последовательность/group | `Ability.Overwatch.Activated` от двух разных бойцов; `Ability.Hunker.Activated` от двух остальных; затем `Turn.Ended` | четыре бойца, заранее обозначенные позиции | разрешены только Overwatch/Hunker для подходящих бойцов, затем End Turn |
| D2 | `D2_ReactionKill` | Holo D движется общим route executor; один specific leaf `Combat.Attack.Overwatch` публикуется после полного разрешения reaction shot; Holo D уничтожен | `Holo_D_Overwatch`, enemy route spline/anchors | ввод закрыт; движение врага нельзя телепортировать через trigger |
| D3 | `D3_Evacuate`, RequiredCount=4 | четыре разных `Objective.Evac.Unit` после входа в активную зону, траты AP и подтверждённого `SetEvacuated`; победа подтверждается `Objective.Evac.Squad` | `Evac_Tutorial`, синий дым, зона у южных ворот | move + Interact/Evac; победа только после последнего подтверждённого evac |

Связки `Attack.* + Enemy.Eliminated` в A8, B5 и D2 — один подтверждённый
action-result, а не два последовательных event-state: оба leaf приходят в одном
frame. До payload-aware task оставлять один exact objective на тип атаки и
проверять уничтожение actor через Action Gate/postcondition; не строить следом
отдельный state, ожидающий уже отправленный `Enemy.Eliminated`.

### 5.1 Actors в `SL_Showreel_Tutorial`

Минимальный набор:

```text
Units:
  Unit_Tutorial_Medic
  Unit_Tutorial_Tank
  Unit_Tutorial_Sniper
  Unit_Tutorial_Assault        (до A9 hidden/inactive, затем Downed)
Holograms:
  Holo_A_OpenField
  Holo_B_Range
  Holo_C_Cover
  Holo_D_Overwatch
Zones:
  QZ_A2_OpenGround
  QZ_A5_FullCover
  QZ_B1_Sector
  QZ_B2_TankHalfCover
  Evac_Tutorial
Anchors:
  Anchor_Camera_Tutorial
  Move_A2_01 / Move_A2_02
  Fire_A4 / Fire_A7
  Move_C2_01 / Move_C2_02
  Route_D2_Start / Route_D2_End
```

Все голограммы, кроме активной для текущего шага, должны быть выключены для
AI, collision, perception и fog presentation, а не только скрыты mesh-ем.
`BaseAim=40`, базовый урон 10; A4/A7 меняют только roll/damage override в общем
attack pipeline согласно GDD.

### 5.2 Дополнительные leaf-события

Native tags уже объявлены вместе с основным набором:

```text
Quest.Event.Tactical.Zone.Entered
Quest.Event.Tactical.Ability.Hunker.Activated
Quest.Event.Tactical.Combat.Enemy.Eliminated
Quest.Event.Tactical.Turn.Player.Started
Quest.Event.Tactical.Scenario.Ready
Quest.Event.Tactical.Scenario.Succeeded
Quest.Event.Tactical.Scenario.Failed
```

Emitters для `Zone.Entered`, Hunker, Enemy.Eliminated, Player.Started и всех
Scenario-границ уже подключены. Не подменять каналы широким `Objective.*`:
иначе разные механики начнут засчитывать друг друга. До payload-aware задач
Action Gate обязан гарантировать нужного unit/target, но это временная
страховка, не финальная верификация.

## 6. Mission01 на том же persistent World

### 6.1 Actors в `SL_Showreel_Mission01`

```text
Units:
  Unit_Mission_Assault
  Unit_Mission_Sniper
  Unit_Mission_Medic
  Unit_Mission_Tank
Enemies:
  Enemy_Mission_B_01 ... Enemy_Mission_B_03
  Enemy_Mission_C_01 ... Enemy_Mission_C_03
Objectives:
  Bomb_Mission01        (ABombObjective, RequiredInteractions=2)
  Evac_Mission01        (AEvacZone, inactive at start)
Anchors:
  Anchor_Camera_Mission01
  Spawn_Player_South_*
  Spawn_Enemy_Center_*
  Spawn_Enemy_North_*
```

Настройка:

1. Отряд стартует на юге; эвакуация существует, но неактивна.
2. Бомба стоит у аппаратной на севере и требует ровно 2 подтверждённых
   взаимодействия по 1 AP.
3. Враги распределены 2–3 в центре и 2–3 на севере; Easy/Medium/Hard используют
   правила GDD. Отсев лишних врагов, если понадобится 4/5/6, делает scenario
   bootstrap до `StartCombat`, а не уничтожение actors посреди первого хода.
4. После `Objective.Defuse.Completed` бомба меняет состояние на Disarmed,
   таймер выключается, включается `Evac_Mission01`, HUD обновляется сразу.
5. Победа: бомба обезврежена и все живые бойцы эвакуированы. Downed на карте
   считается потерей по правилам GDD, но не блокирует последнюю эвакуацию.
6. Поражение: истёк лимит 12/10/8 либо не осталось дееспособного отряда.

### 6.2 `ST_Quest_Mission01`

Рекомендуемая структура:

```text
Mission01
  Briefing
  DefuseStep01     exact Objective.Defuse.Progressed, RequiredCount=1
  DefuseStep02     exact Objective.Defuse.Completed, RequiredCount=1
  EvacuateUnits    exact Objective.Evac.Unit; число задаёт runtime roster/gate
  ConfirmSquad     exact Objective.Evac.Squad
  WaitResult       Quest Wait Outcome:
                     SuccessChannel = exact Scenario.Succeeded
                     FailureChannel = exact Scenario.Failed
```

`Scenario.Failed` нельзя добавлять вторым spec в обычный `Quest Objective
Group`: совпавшая objective всегда возвращает `Succeeded`, и runner ошибочно
переведёт проигранный quest в `Completed`. Для terminal state обоих StateTree
использовать нативную задачу `Quest Wait Outcome`: она возвращает `Succeeded`
только по success leaf и `Failed` по failure leaf. Кроме того, root-level
failure transition из §4.5 остаётся активен на всём сценарии. Нативный fallback
Director — страховка, а не основная логика графа.

GameMode остаётся авторитетом правил победы/поражения. StateTree показывает
цели и реплики, но не вычисляет исход по статическому `RequiredCount=4`: живой
состав может измениться. Финальная точка должна быть единой:

1. Последний `Evac.Unit` публикуется после подтверждённой эвакуации юнита.
2. На следующем tick GameMode проверяет живой состав, публикует `Evac.Squad` и
   сообщает Director подтверждённый result candidate.
3. Ещё на следующем tick Director публикует единственный leaf
   `Scenario.Succeeded`/`Scenario.Failed`.
4. StateTree получает короткий grace period; если он не завершил quest,
   Director применяет нативный terminal fallback.
5. Только после подтверждённого `Completed`/`Failed` открывается result screen и
   пишется campaign save.

Это исключает гонку «последний evac уже открыл результат, а objective ещё 3/4».

Реплики Mission01 из `02_LORE_SCRIPT.md` привязываются к доменным событиям:

| Реплика | Источник |
|---|---|
| старт хода 1 | `ScenarioReady` после старта боя и quest |
| первый визуальный контакт | первый переход enemy contact в Visible/Detected |
| первое убийство | первый подтверждённый `EnemyEliminated` |
| первый раненый | первый подтверждённый damage result по бойцу |
| половина таймера | переход через половину исходного лимита, one-shot |
| 3 хода | `TurnsRemaining == 3`, one-shot + ticking audio |
| заряд 1/2 | первый `Objective.Defuse.Progressed`, который реально увеличил bomb progress |
| заряд снят | `ABombObjective::OnDisarmed` |
| первая эвакуация | первый `Objective.Evac.Unit` |

## 7. Event contract: где именно публиковать

| Канал | Допустимая точка публикации | Недопустимая точка |
|---|---|---|
| `Unit.Selected` | после смены canonical `SelectedUnit` на живого бойца с provenance «player input» | raw LMB/клик карточки и автоматический select |
| `Camera.Adjusted` | после порогов реального yaw/zoom; one-shot на run | raw Q/E/wheel input |
| `Movement.Settled.Open` | единый `MoveSettlement`: route success, actor достиг tolerance, AP/action token зафиксирован и финальный turn-in-place завершён | `MoveTo` accepted, один `PathFollowing` success или overlap до конца доводки |
| `Movement.Settled.InCover` | тот же `MoveSettlement`, но только при валидном итоговом cover solution после cover-hug и turn-in-place | отдельный второй broadcast вместе с `Movement.Settled.Open` |
| `Turn.Ended` | после фактической смены active side | нажатие Enter |
| `Combat.Attack.Normal/Squadsight` | после hit roll/damage/miss и подтверждённого action result (`FireCommit` transaction owner) | клик цели или montage start |
| ability channels | после успешной activation, расхода и установленного effect/tag | попытка `TryActivateAbility` |
| `Combat.Attack.Overwatch` | reaction action разрешился или корректно завершился miss | вход в sight сам по себе |
| `Objective.Defuse.Progressed/Completed` | AP принят и progress действительно увеличен; на последнем шаге только `Completed` | нажатие F рядом с объектом или два события на финальном шаге |
| `Objective.Evac.Unit/Squad` | unit помечен evacuated; `Squad` — только после проверки всех живых и на следующем tick после последнего `Unit` | вход в evac volume или оба тега одной пачкой |

Один доменный факт публикуется **одним самым конкретным leaf-тегом**. Убийство
выстрелом законно создаёт два разных факта — `Combat.Attack.*` и
`Combat.Enemy.Eliminated`, — но их нельзя ставить двумя последовательными
objectives одного StateTree: оба события приходят в одном frame, а новый state
не увидит sibling event из уже обработанной пачки. Для такой связки нужен
payload-aware task/action-result либо один составной objective.

У tactical-objectives всегда включён `bRequireExactChannel = true`.
Parent-match в `STQuestSystem` оставлен только для совместимости с donor-квестами:
listener на родительском канале автоматически поймает дочерние leaf-события и
может дать ложный или двойной прогресс.

Таксономия уже нормализована: конкретный шаг атаки слушает leaf `Normal`,
`Squadsight` или `Overwatch`; move — только `Open` либо `InCover`. Для финального
обезвреживания публикуется только `Completed`, без дополнительного `Progressed`.

Также нельзя одновременно броадкастить один move из PlayerController, Unit и
`ATacticalQuestZone`. Зоны используют отдельный `UnitEnteredZone`, а движение —
единственный доменный `MoveSettlement` emitter.

## 8. Action Gate — отдельная система

StateTree описывает **что ждём**, но не должен сам становиться системой ввода.
Нужен `UTutorialActionGateSubsystem` (WorldSubsystem) либо эквивалентный сервис,
который хранит текущий `FTutorialActionPolicy`:

- разрешённые команды: Select, Camera, Move, EndTurn, Attack, ClassAbility,
  Overwatch, Hunker, Interact;
- допустимые units/roles;
- допустимые target actors;
- разрешённые destination anchors/zone;
- требуемый порядок и максимум повторов;
- текст причины отказа для HUD.

Правила интеграции:

1. И hotkey, и HUD-кнопка спрашивают один `CanIssueAction` до изменения мира.
2. Отказ не тратит AP, не запускает montage и не публикует quest event.
3. Серость кнопок — представление того же gate, а не отдельные BP-условия.
4. State enter применяет policy; state exit снимает её. Event bus сам ничего не
   блокирует.
5. Camera navigation и Pause остаются доступны. Во время scripted action игрок
   может отказаться от cinematic framing, но это не отменяет доменный action.
6. При retry/travel gate очищается до загрузки нового sublevel.

## 9. HUD и tutorial presentation

### 9.1 Ручной минимум

1. Создать `/Game/XRU1Game/UI/Tutorial/WBP_QuestTracker_Tactical` от
   `UQuestTrackerWidget`.
2. В `OnTrackedQuestRefreshed`:
   - показать `DisplayName`;
   - из `Objectives` показывать прежде всего `Active`, затем последний
     `Completed` коротким подтверждением;
   - выводить `Current/Required`, если Required > 1;
   - при пустом/неактивном quest скрывать панель.
3. Добавить widget в существующий `WBP_TacticalHUD` как самостоятельный блок:
   - Tutorial: крупная инструкция слева сверху, speaker/subtitle под ней;
   - Mission01: компактные «Обезвредить 0/2» / «Эвакуировать отряд»;
   - bomb timer остаётся в фазовом блоке HUD.
4. Проверить, что после успешного `StartConfiguredQuest` C++ Director назначил
   `Quest.Tutorial` или `Quest.Mission01` как tracked quest; отдельный BP-вызов
   `SetTrackedQuest` не добавлять, иначе появится второй источник lifecycle.
5. Проверить 1920×1080 и 1920×1200: панель не перекрывает phase/timer,
   enemy counter и action bar.

### 9.2 Production presentation

`FObjectiveProgress::Description` достаточно для короткой инструкции, но не
хранит speaker, audio, portrait, camera focus и highlight. Добавить data-driven
`FTacticalTutorialBeat`/Data Asset со следующими полями:

```text
BeatId, Speaker, Subtitle, Voice, FocusAnchorId,
HighlightActorIds, ObjectiveText, OptionalHint, HintDelay
```

StateTree state запускает beat через отдельную presentation task и ждёт
`LineFinished`; gameplay objective остаётся другой task. Реплики не размещать в
Level Blueprint и не дублировать в actor names.

## 10. ДАЛЬНЕЙШИЙ КОД

### P0 — bootstrap уже закрыт кодом, остаются Editor-ассеты

- [x] `ATacticsGameMode` при `ActiveScenario` не сканирует units/bomb/evac и не
      запускает бой из таймера `BeginPlay`; идемпотентный `StartScenarioCombat`
      вызывается Director после `OnLevelShown`.
- [x] Bomb/Evac bindings и сбор сторон выполняются после stream; повторный
      callback не создаёт второй бой, подписки или HUD.
- [x] `MissionId`, tutorial/mission difficulty rule и `TurnLimit` выводятся из
      `ActiveScenario`: Tutorial не получает enemy HP/Aim кампании.
- [x] `AMissionPointOfInterest::Scenario` вызывает `StartCombatScenario`;
      `LevelToLoad/OpenLevel` оставлен только legacy fallback.
- [x] Добавить в Director идемпотентную terminal-точку
      `FinalizeConfiguredScenario` для quest/result contract.
- [x] Подключить `HandleCombatEnded` GameMode к terminal-финализации Director до
      campaign save и result screen.
- [x] Retry переоткрывает shared World через `RestartActiveScenario`, сбрасывает
      quest runtime и получает новый `ScenarioRunId`; WorldSubsystem, actors,
      TurnManager и HUD создаются заново.
- [x] Добавить монотонный `ScenarioRunId`/generation в GameInstance и Director.
- [ ] Передавать `ScenarioRunId` в async action/fog/quest context и игнорировать
      late callbacks старого запуска после travel.
- [x] `ResetQuestRuntime` очищает `Active/Completed/Failed`, objectives, owner,
      runner и tracking; `RetryMission()` повторно вызывает
      `StartCombatScenario(ActiveScenario)`. Уже выданные plugin rewards не
      откатываются, поэтому Scenario Quest не должен выдавать неидемпотентные
      награды — campaign unlock записывается отдельно через `AddUnique`.
- [x] После старта автоматически `SetTrackedQuest`; при уходе из World очищать.
      Retry также обязан пройти через ту же очистку lifecycle.
- [x] `FinalizeConfiguredScenario` — единая terminal-граница; GameMode не пишет
      save и не открывает result screen, если Director не подтвердил исход.

### P1 — честные события и tutorial orchestration

- [x] Встроить `BroadcastQuestEvent` в confirmed turn, player combat/class
      abilities, enemy elimination, defuse/evac и scenario callbacks из §7.
- [ ] Подключить selection/camera/move и scripted-enemy emitters к action-token/
      payload-aware orchestration; не добавлять параллельные BP-send.
- [x] Добавить leaf tags из §5.2. Уже подключены Zone, player turn,
      player combat/class abilities, kill, objectives и scenario result.
      Открыты только selection, camera, movement и scripted-enemy emitters.
- [x] Ввести единый `MoveSettlement` gate в controller: cover/HUD, списание AI
      AP и следующий шаг происходят только после route arrival, cover-hug step
      и финального turn-in-place.
- [ ] Публиковать quest move-event из этой единственной финализации после
      проверки action token; выбирать только `Movement.Settled.Open` либо
      `Movement.Settled.InCover`, не оба.
- [x] Нормализовать event taxonomy в parent/child и закрепить правило «один
      outcome → один leaf»; generic listeners используют `MatchesTag`, а не
      второй broadcast.
- [ ] Расширить событие/StateTree task так, чтобы проверять unit, target, zone,
      hit/damage и `ScenarioRunId`. Сейчас `AQuestRunnerActor` отбрасывает
      `FQuestEventData.Source` и пересылает в StateTree только tag.
- [ ] Реализовать Action Gate из раздела 8 и общий API для input/HUD.
- [ ] Добавить StateTree tasks: применить gate, показать beat, активировать
      staged actor, выполнить scripted shot через обычный attack pipeline,
      дождаться confirmed turn/result.
- [x] Добавить `Quest Wait Outcome`, который различает terminal success/failure;
      обычную Objective Group для исхода сценария не использовать.
- [ ] Добавить scenario actor registry по стабильным `AnchorId`; имена World
      Outliner использовать для диагностики, не как runtime API.
- [ ] Форс A4/A7 задавать через `FShotContext`, не прямым damage/ResolveShot из BP.

### P2 — UI, save и полировка

- [ ] Data Asset tutorial beats, subtitles/voice/hints/highlights.
- [ ] Mission notification one-shot flags и result statistics.
- [ ] Campaign write: `CompletedMissions.AddUnique(ScenarioId)` только на
      подтверждённой победе; runtime quest progress не подменяет campaign save.
- [ ] Packaged-build validation для soft refs, обоих sublevel и Quest primary
      assets.

## 11. Reset, retry и travel lifecycle

### Новый запуск из Hub

1. POI проверяет `RequiredCompletedMission`.
2. `StartCombatScenario` устанавливает `ActiveScenario` до travel.
3. Новый World создаёт новые WorldSubsystem/fog/runtime state.
4. Director загружает sublevel, сбрасывает run, запускает бой и quest.

### Retry

1. `UMissionResultWidget::RetryMission` вызывает `RestartActiveScenario`.
2. `ResetQuestRuntime` уничтожает runner, очищает progress/owner/tracking и
   возвращает quest в `Inactive` независимо от `Active/Completed/Failed`.
3. `StartCombatScenario` увеличивает `ScenarioRunId` и переоткрывает
   `SharedCombatLevel` с тем же Data Asset.
4. Новый World создаёт чистые actors, WorldSubsystem, TurnManager и HUD; новый
   запуск обязан начинаться с AP/turn 1 и позднее — с чистым fog.
5. До реализации P1 нельзя разрешать mid-action retry: поздние callbacks ещё не
   несут `ScenarioRunId`. Кнопка результата безопасна, потому что action уже
   terminal; pause-menu retry добавить только вместе с cancellation barrier.

### Выход в Hub/Main Menu

- Active world quest останавливается до уничтожения World.
- `TravelToHub/TravelToMainMenu` очищает `ActiveScenario`.
- Campaign completion остаётся в `UTacticsSaveGame`; world actor pointers,
  explored fog и tutorial-step runtime не переживают travel.
- Для текущего короткого демо mid-combat save не включать: восстановление
  streamed actors, action gate и scripted beat потребует отдельного формата
  scenario snapshot.

## 12. Риски STQuestSystem

| Риск | Почему | Мера |
|---|---|---|
| Все активные runners слушают родительский `Quest.Event` partial-match | два активных scenario quest засчитают одно событие | одновременно активен ровно один; старый abandon/reset до нового старта |
| `QuestTask_TrackObjective`/`Quest Objective Group` по умолчанию используют parent-match | слишком широкий parent channel ловит лишние leaf-события | у каждой tactical objective/spec включить `bRequireExactChannel` и слушать точный leaf |
| Runner теряет `Source`/payload | нельзя отличить Медика от Танка и Holo A от B | payload-aware StateTree task + gate; не полагаться только на generic tag |
| `Amount` реализован повторной отправкой StateTree event | ошибочный Amount быстро завершит счётчик | обычная операция всегда Amount=1; aggregation только осознанно |
| Objective Group может инкрементировать несколько specs одним иерархическим событием | parent tags пересекаются | уникальные leaf channels и тесты negative-path |
| Quest registry строится при initialize GameInstance | asset вне scan folder или созданный после старта неизвестен | хранить DA только в `/Game/XRU1Game/Quests`, перезапустить PIE/Editor |
| `Completed/Failed` не перезапускается Director | retry после финала не проходит `Available` | явный reset API до travel |
| Streamed actors появляются после `GameMode::BeginPlay` | пустые стороны, не найденная бомба/evac | deferred explicit scenario start после `OnLevelShown` |
| Только mesh hidden у staged actor | collision/perception/AI всё ещё активны | единый `SetScenarioActorActive` для presentation + gameplay |
| Direct PIE persistent map без ActiveScenario | Director закономерно не знает, что загрузить | тестировать из Hub; позднее editor-only PreviewScenario |

## 13. Тест-матрица

### 13.1 Bootstrap и streaming

- [ ] Hub → Tutorial: загружен persistent + только `SL_Showreel_Tutorial`.
- [ ] Hub → Mission01: persistent + только `SL_Showreel_Mission01`.
- [ ] В Tutorial нет bomb/mission enemies; в Mission нет holograms/zones A–D.
- [ ] После `OnLevelShown`: quest проходит `Inactive → Available → Active`,
      затем GameMode стартует бой, затем публикуется единственный `Scenario.Ready`.
- [ ] В Tools → Система квестов → Отладчик квестов виден один runner/quest.
- [ ] Прямой PIE без ActiveScenario запускает только явно обозначенный
      legacy/demo fallback и не создаёт scenario quest; сквозную проверку
      Tutorial/Mission01 всегда начинать через POI/Data Asset.
- [ ] NavMesh покрывает оба сценария; streamed triggers не меняют общий маршрут.

### 13.2 Tutorial positive path

- [ ] A1 требует именно Медика и реальные rotate+zoom.
- [ ] A2 не завершается по двум кликам или failed move; только по двум success и
      нужной зоне/AP=0.
- [ ] A3 считается после смены стороны.
- [ ] A4: 30 damage через общий shot action; UI не переходит раньше recover.
- [ ] A5: Full cover именно против Holo A.
- [ ] A6: HP +50 и один расход способности/AP.
- [ ] A7: forced miss не меняет HP.
- [ ] A8: miss/отмена атаки не закрывают objective; смерть Holo A закрывает.
- [ ] A9: только успешный revive Клина с 30 HP.
- [ ] B1 считает двух разных бойцов; повторный overlap одного не помогает.
- [ ] B3/B4: Taunt активен до shot, итоговый damage уменьшен вдвое.
- [ ] B5: Оса не двигается, squadsight реально разрешён sight Молота.
- [ ] C2 соблюдает Run&Gun → move → move → attack; неправильный порядок запрещён.
- [ ] D1: Overwatch у двух разных, Hunker у остальных, затем End Turn.
- [ ] D2: реакция запускается на реальное движение Holo D и завершается один раз.
- [ ] D3: вход в дым без F не эвакуирует; победа после четвёртого confirmed evac.

### 13.3 Tutorial negative path

- [ ] Неправильный unit/target/action отклоняется без AP/montage/event.
- [ ] Двойной клик, удержание hotkey и повторный overlap не накручивают objective.
- [ ] Failed navigation, отменённая ability и attack до `FireCommit` не дают зачёт.
- [ ] Пользовательская камера во время scripted shot не ломает state и не
      возвращается самопроизвольно после старого таймера.
- [ ] Смерть/Downed неожиданного бойца переводит сценарий в контролируемое
      поражение, а не оставляет gate закрытым навсегда.

### 13.4 Mission01

- [ ] Easy/Medium/Hard: таймер 12/10/8, tutorial от сложности не масштабируется.
- [ ] Bomb progress строго 0/2 → 1/2 → 2/2; rejected F не считается.
- [ ] На 2/2 таймер гаснет и evac активируется в том же confirmed result.
- [ ] Evac до обезвреживания недоступен и не публикует событие.
- [ ] Победа требует bomb disarmed + эвакуацию всех живых.
- [ ] Downed, оставшийся на карте, попадает в потери и не ломает правило победы.
- [ ] Timeout и squad wipe дают разные defeat reason/тексты.
- [ ] Реплики first contact/kill/wounded/half/3 turns/1-of-2/disarmed/first evac
      срабатывают ровно один раз.

### 13.5 Lifecycle и гонки

- [ ] Retry из active, timeout defeat и squad defeat начинает шаг A1/ход 1 с
      чистыми AP, fog, actors и objective counters.
- [ ] Пять циклов Retry не создают дубли HUD, message listeners или runners.
- [ ] Выход в Hub во время movement, aim, reaction и scripted shot не вызывает
      late event в следующем World.
- [ ] Tutorial victory пишет `Tutorial` один раз и разблокирует Mission01.
- [ ] Повторное прохождение не дублирует `CompletedMissions`.
- [ ] Один и тот же `Showreel_Scene` после Tutorial корректно стартует Mission01,
      не наследуя explored fog или staged actors.
- [ ] Packaged Development build содержит persistent, оба sublevel, оба Scenario
      Data Asset, оба Quest Definition и оба StateTree.

## 14. Definition of Done

Фича считается готовой, когда:

- один persistent World обслуживает оба сценария без World Partition;
- одновременно загружен только один scenario sublevel;
- все A1–D3 проходят positive и negative checks;
- objective меняется только после подтверждённого gameplay result;
- Action Gate одинаков для мыши, hotkeys и HUD;
- scripted shots используют обычный attack/action pipeline;
- Mission01 соблюдает bomb/timer/evac и difficulty;
- retry/travel не оставляет runner, listeners, fog, AP или async action;
- result screen открывается после согласованной финализации combat + quest;
- packaged build проходит полный Tutorial → Hub → Mission01 → финал.
