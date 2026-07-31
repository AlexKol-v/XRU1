# Tutorial и Mission01 на общей карте

Актуально на 2026-07-30. Документ фиксирует production-архитектуру обучения
«Полигон “Купол”» и миссии «Станция “Узел-7”» на **одном persistent World**.

## 0. Фактические пути ассетов

Ниже — реальные пути в `Content/`. Persistent-карта переехала из исходного пака
`US_Military` в `XRU1Game/Maps`; прежнее рабочее имя `Showreel_Scene` больше не
используется:

| Что | Путь |
|---|---|
| Persistent World | `/Game/XRU1Game/Maps/Main_Map_Showreel` |
| Sublevel обучения | `/Game/XRU1Game/Maps/SubLavel/SL_Showreel_Tutorial` |
| Sublevel миссии | `/Game/XRU1Game/Maps/SubLavel/SL_Showreel_Mission01` |
| Scenario Data Assets | `/Game/XRU1Game/Data/DA_Scenario_Tutorial`, `DA_Scenario_Mission01` |
| Quest Definitions | `/Game/XRU1Game/Data/DA_Quest_Tutorial`, `DA_Quest_Mission01` |
| StateTree-графы | `/Game/XRU1Game/Quests/ST_Quest_Tutorial`, `ST_Quest_Mission01` |
| Scenario Director | `/Game/XRU1Game/Core/BP_TacticalScenarioDirector` |

> Папка `SubLavel` — опечатка в имени, оставленная как есть: переименование
> тянет за собой fixup редиректоров в `Maps/`. Runtime-ссылка идёт из
> `DA_Scenario_*.ScenarioSublevel`, поэтому имя папки ни на что не влияет.

Asset Manager сканирует на primary assets типа `Quest` **две** папки —
`/Game/XRU1Game/Quests` и `/Game/XRU1Game/Data` (`PrimaryAssetTypesToScan` в
`Config/DefaultGame.ini`). Quest Definition вне них в registry не попадёт:
`MakeQuestAvailable` молча ничего не сделает, и сценарий упадёт с
«Quest ... нельзя запустить из состояния 0». Перенос `DA_Quest_*` в новую папку
требует правки скана и **перезапуска редактора** — registry строится на старте.
Director явно диагностирует этот случай отдельной ошибкой в логе.

Карта **не использует World Partition**. Различия между запусками хранятся в
двух обычных streaming sublevel и двух `UTacticalScenarioDataAsset`. Создавать
копии `Main_Map_Showreel`, писать gameplay в Level Blueprint или определять режим
по имени открытого map package запрещено.

Сценарные тексты и порядок A1–D3 остаются в
[02_LORE_SCRIPT.md](02_LORE_SCRIPT.md), правила боя — в
[01_GDD.md](01_GDD.md). Этот файл отвечает за архитектуру, Editor-настройку и
проверку. Требования к взаимному расположению акторов (дистанции, укрытия,
линии огня, бюджет AP) вынесены в
[12_TUTORIAL_LAYOUT_SPEC.md](12_TUTORIAL_LAYOUT_SPEC.md).

## 1. Итоговая схема

```text
Hub POI
  └─ UTacticsGameInstance::StartCombatScenario(DA_Scenario_*)
       ├─ ActiveScenario = DA_Scenario_*
       └─ OpenLevel(Main_Map_Showreel)
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
| Persistent `Main_Map_Showreel` | общий арт, collision, свет, NavMesh, камера, `GM_Tactics`, один Scenario Director | состав конкретного сценария |
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

### 3.4 Payload, реестр акторов, Action Gate и задачи обучения

Закрыто кодом 2026-07-29; в редакторе это уже готовые к использованию блоки.

- **Payload квест-событий.** `FQuestEventData` получил `Target` и
  `ScenarioRunId`, а `AQuestRunnerActor` кладёт всю структуру в
  `FStateTreeEvent::Payload`. Задача-цель теперь видит, КТО и ПО ЧЕМУ подтвердил
  результат: [QuestTypes.h](../Plugins/STQuestSystem/Source/STQuestSystem/Public/QuestTypes.h),
  [QuestRunnerActor.cpp](../Plugins/STQuestSystem/Source/STQuestSystem/Private/QuestRunnerActor.cpp).
  Публикация — `UTacticalQuestEvents::BroadcastQuestEventEx(Channel, Source, Target)`;
  `ScenarioRunId` подставляется автоматически.
- **Реестр акторов по `AnchorId`.** `UScenarioActorIdComponent` вешается на
  любого актора сценария, `UTacticalScenarioSubsystem` индексирует их и даёт
  единый `SetScenarioActorActive` (скрытие + collision + tick + участие в
  сторонах боя). `AScenarioAnchorPoint` — пустой якорь для точек камеры,
  разрешённых точек перемещения и вершин маршрута:
  [ScenarioActorRegistry.h](../Source/XRU1/Tactics/ScenarioActorRegistry.h).
  Имена в World Outliner остаются диагностикой, runtime-ссылка — только `AnchorId`.
- **Action Gate.** `UTutorialActionGateSubsystem` + `FTutorialActionPolicy`
  (раздел 8) встроены в `CanIssueCommand`, `SelectUnit`, `TryMoveSelectedUnit`,
  `TryAttackTarget`, `HandleAbilityTargetClick`, `RequestEndTurn` и
  автозавершение хода: [TutorialActionGate.h](../Source/XRU1/Tactics/TutorialActionGate.h).
  Камера и пауза не блокируются никогда.
- **Честные emitters.** `Unit.Selected` публикуется после фактической смены
  canonical `SelectedUnit` и только для пользовательского выбора (автовыбор XCOM
  молчит). `Camera.Adjusted` — one-shot после порогов реального поворота **и**
  зума (`AdjustedYawThreshold`/`AdjustedZoomThreshold` на камере).
  `Movement.Settled.Open`/`InCover` публикует единственная финализация
  `AUnitAIController::TryFinalizeMoveSettlement` и только для перемещения,
  помеченного `MarkPlayerOrderedMove`.
- **Сценарный выстрел.** `FScriptedShotOverride` меняет только числа snapshot'а
  в `UGA_Attack::ActivateAbility`; roll, GE урона, `FireCommit`, HitReact,
  камера и quest-события остаются общим pipeline. Приказ «стрелять именно по
  этому бойцу» ставится `AUnitAIController::SetScriptedAttackOrder`.
- **Такты презентации.** `UTutorialPresentationSubsystem` + `FTacticalTutorialBeat`
  дают HUD спикера, субтитр, озвучку, фокус камеры и список подсветок:
  [TutorialPresentation.h](../Source/XRU1/Tactics/TutorialPresentation.h).

StateTree-задачи в категории **XRU1 Tutorial**
([TacticalQuestTasks.h](../Source/XRU1/Tactics/TacticalQuestTasks.h)):

| Задача | Назначение | Ключевые поля |
|---|---|---|
| `Tactical Objective` | цель с проверкой payload | `ObjectiveId`, `EventChannel`, `bRequireExactChannel`, `RequiredCount`, `Description`, `RequiredSourceAnchor`, `RequiredTargetAnchor`, `bRequireDistinctSources` |
| `Apply Action Gate` | держит политику шага, пока состояние активно | `Policy` (`AllowedActions`, `AllowedUnitAnchors`, `AllowedTargetAnchors`, `AllowedDestinationAnchors`, `DestinationOwners`, `DestinationTolerance`, `bSequentialDestinations`, `bLockGameplayInput`, `DenialReason`) |
| `Set Scenario Actor Active` | «проявление»/выключение staged-актора | `AnchorIds`, `bActive`, `bRestoreOnExit` |
| `Scripted Shot` | сценарный выстрел через обычный pipeline | `ShooterAnchorId`, `TargetAnchorId`, `Shot` (шанс/урон), `Timeout` |
| `Tutorial Beat` | реплика «Купола», субтитр, фокус камеры | `Beat` (`Speaker`, `Subtitle`, `Voice`, `FocusAnchorId`, `HighlightAnchorIds`, `Duration`) |
| `Scripted Move` | постановочная перебежка бойца (свой/враг) к якорю вне обычного хода — **общим пайплайном перемещения**: occupancy-план + `MoveAlongRoute` + общий финиш (прижатие к укрытию, `EvaluateSurroundings`, доворот), так что cover-кэши и анимации те же, что при клике игрока; AP и quest-события движения не участвуют; прибытие = маршрут завершён И осадка закончена; камера сопровождает бегущего; невзятый сразу приказ повторяется каждые 0.5 с до `Timeout` | `UnitAnchorId`, `DestinationAnchorId`, `AcceptanceRadius`, `Timeout`, `bDrainActionPointsOnArrival`, `bCameraFollowUnit` |
| `Force Next Shot` | форс СЛЕДУЮЩЕГО выстрела бойца игрока (учебные гарантированные попадания A8/B5) | `UnitAnchorId`, `Shot` |

`Policy.DestinationOwners` (map «якорь точки → якорь бойца») закрепляет точку за
конкретным бойцом: Танк не может занять точку Осы, маркеры показывают выбранному
бойцу только его личные и общие точки, `bSequentialDestinations` открывает
очередь точек НЕЗАВИСИМО по каждому владельцу.

`Tactical Objective` полностью заменяет donor-овскую `Quest Objective` в
tactical-шагах: она умеет всё то же самое плюс payload. Donor-задачи остаются
для совместимости, но новые шаги на них не строим.

Дополнительно закрыто в правилах боя: победа зачисткой (`bAutoWinWhenEnemiesDead`)
отключается не только при наличии бомбы, но и при наличии `AEvacZone`. Иначе
после D2 все голограммы мертвы и бой заканчивался бы победой ДО шага D3.
`UTurnManagerSubsystem::RegisterUnitInCombat/UnregisterUnitFromCombat` вводят и
убирают staged-голограмму из сторон боя посреди боя, а `StartMissionCombat`
пропускает акторов, помеченных `bStartDeactivated`.

## 4. РУЧНАЯ НАСТРОЙКА В EDITOR

Нативный streaming bootstrap уже переведён на явную границу готовности:
при наличии `ActiveScenario` `ATacticsGameMode` ждёт вызова Director и не
сканирует persistent World по таймеру.

**Статус на 2026-07-30.** Пункты 4.1–4.4 выполнены (см. §4.0). Остаются
StateTree-графы (§4.5) и HUD (§9) — их редактор не отдаёт скриптам.

### 4.0 Что уже сделано в Content

- Оба scenario sublevel созданы, подключены к persistent как
  **Blueprint (LevelStreamingDynamic)** и по умолчанию `bShouldBeLoaded=false`,
  `bShouldBeVisible=false`.
- В persistent остались только общие вещи: арт, свет, `PlayerStart`,
  `NavMeshBoundsVolume` + `RecastNavMesh`, один `BP_TacticalScenarioDirector`.
  Юнитов в persistent нет — иначе `StartMissionCombat` собрал бы их в стороны
  боя обоих сценариев (`TActorIterator<AUnitBase>` обходит весь World).
- `SL_Showreel_Tutorial` и `SL_Showreel_Mission01` заселены полным набором
  акторов из §5.1 и §6.1; у каждого стоит `UScenarioActorIdComponent` с
  `AnchorId`, равным имени из документа, и нужный `bStartDeactivated`.
- `DA_Scenario_*` заполнены целиком; `DA_Quest_*` получили `QuestId`
  (`Quest.Tutorial` / `Quest.Mission01` — нативные теги) и `QuestLogic`
  (`ST_Quest_*`).
- **Позиции акторов черновые.** Они выбраны автоматически по проекции на
  NavMesh и трассировке укрытий, все точки проходимы, но постановочную
  выразительность (ракурсы, дистанции, точность full/half cover) нужно
  доводить глазами в редакторе. `AnchorId` при перетаскивании не теряется.

### 4.0.1 Отклонение: streaming делает C++, а не BP-граф

Загрузка `ScenarioSublevel` и старт сценария реализованы нативно в
`ATacticalScenarioDirector` (`bAutoStreamScenarioSublevel`, по умолчанию
включено). Порядок остался ровно тем, что предписан §4.3:

```text
BeginPlay → OnScenarioSelected → SetShouldBeLoaded/Visible
 → OnLevelShown → Set Timer for Next Tick → StartConfiguredQuest → …
```

BP-граф Director больше не обязателен; хуки `OnScenarioSelected` и
`OnScenarioReady` остаются для косметики (VFX, фокус камеры, первый gate).
Одновременно двух загрузчиков быть не должно: либо нативный флаг, либо BP-граф.
**Фактическое состояние проекта:** в `BP_TacticalScenarioDirector` собран
BP-граф загрузки, поэтому `bAutoStreamScenarioSublevel` на нём **выключен**;
C++-дефолт остаётся `true` для чистых наследников.

Дополнительно у Director появилось поле `PreviewScenario`
(`Scenario|Preview`): при прямом запуске `Main_Map_Showreel` из редактора,
когда Hub/POI ещё не пройден и `ActiveScenario` пуст, Director принимает этот
Data Asset через `UTacticsGameInstance::AdoptScenarioInPlace` и запускает
сценарий без travel. Настоящий bootstrap из Hub всегда приоритетнее.

### 4.1 Подготовка после сборки C++

1. Закрыть Unreal Editor, собрать проект через `Build-XRU1.ps1`, открыть Editor.
2. Убедиться, что включены `STQuestSystem`, `GameplayMessageRouter`, `StateTree`.
3. Проверить Project Settings → Maps & Modes:
   - Game Instance Class = `/Game/XRU1Game/Core/BP_TacticsGameInstance`;
   - боевой GameMode = `/Game/XRU1Game/Core/GM_Tactics`.
4. Открыть `BP_TacticsGameInstance` и назначить:
   - `SharedCombatLevel` = `/Game/XRU1Game/Maps/Main_Map_Showreel`;
   - существующие `HubLevel` и `MainMenuLevel` оставить без изменения.
5. Перезапустить Editor после первого появления native gameplay tags и
   AssetManager scan. Иначе `Quest.Tutorial`/`Quest.Mission01` могут не появиться
   в picker, а новые Quest Definition — в registry текущего GameInstance.

### 4.2 Создание двух streaming sublevel

1. Открыть `/Game/XRU1Game/Maps/Main_Map_Showreel`.
2. Window → Levels. Persistent level должен быть выделен жирным.
3. Создать и сохранить два **новых scenario-specific** уровня:
   - `/Game/XRU1Game/Maps/SL_Showreel_Tutorial`;
   - `/Game/XRU1Game/Maps/SL_Showreel_Mission01`.
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

Пункты 3–6 ниже описывают BP-вариант. С 2026-07-30 то же самое делает C++
(см. §4.0.1), и вручную собирать граф не нужно. Раздел оставлен как контракт
порядка вызовов.

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

Создать в `/Game/XRU1Game/Data`:

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

Папка `/Game/XRU1Game/Quests` создана, оба `DA_Quest_*` заполнены и связаны со
своими `ST_Quest_*`. **Остались сами графы StateTree** — их структуру
(состояния, задачи, переходы) Python-скриптингом не построить:
`UStateTreeEditorData::SubTrees` не экспонирован, а задачи хранятся во
`FInstancedStruct`. Это единственная оставшаяся ручная работа по §5.4 и §6.2.

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

### 5.0 Режиссура v2 секций B–D (2026-07-31)

Пользовательская правка сценария; таблицы v1 для B–D ниже (§5.4) считать
устаревшими. Реплики — [02_LORE_SCRIPT.md](02_LORE_SCRIPT.md) §5 (v2).
Секция A не менялась (Клин теперь активен и лежит Downed с самого старта —
`bStartDeactivated=false`, `bStartDowned=true`; поднимается в A9 с 30 HP).

| Состояние | Задачи (все Objectives — exact, все состояния — Tasks Completion=All) |
|---|---|
| `B0_EnterSector` | **Set Scenario Actor Active** №1: `[Unit_Tutorial_Tank, Unit_Tutorial_Sniper]`, bActive **true** — появляются к своей секции.<br>**Set Scenario Actor Active** №2: `[Unit_Tutorial_Medic, Unit_Tutorial_Assault]`, bActive **false** — секция A закончена, Медик и Клин уходят со сцены.<br>**Beat**: FocusAnchorId `Anchor_Camera_B`, реплика B0.<br>**Gate**: `[Select, Move]`, Units `[Unit_Tutorial_Tank, Unit_Tutorial_Sniper]`.<br>**Objective**: `Tutorial.B1`, канал `Zone.Entered`, Target `QZ_B1_Sector`, Count 2, Distinct **true**. |
| `B1_EndTurn` | **Gate**: `[EndTurn]`.<br>**Objective**: канал `Turn.Ended`, Count 1. |
| `B2a_AllyRetreat` | **Gate**: `bLockGameplayInput=true`.<br>**Set Scenario Actor Active**: `[Unit_Tutorial_Assault_B]`, bActive true.<br>**Beat**: реплика Кадета B2.<br>**Scripted Move**: Unit `Unit_Tutorial_Assault_B` (Кадет, 10 HP, до этого скрыт — включает Set Scenario Actor Active в этом же state) → `Point_B_AssaultRetreat`. |
| `B2b_EnemyEnters` | **Gate**: lock.<br>**Set Scenario Actor Active**: `[Holo_B_Range]` (если стартует деактивированным).<br>**Scripted Move**: Unit `Holo_B_Range` → `Point_B_EnemySpot`.<br>**Objective**: канал `Turn.Player.Started`, Count 1. |
| `B3_PairSetup` | **Gate**: `[Select, Move, ClassAbility]`, Units `[Tank, Sniper]`, Destinations `[Move_B_TankCover, Move_B_Sniper01, Move_B_Sniper02]`, **DestinationOwners**: `Move_B_TankCover→Unit_Tutorial_Tank`, `Move_B_Sniper01→Unit_Tutorial_Sniper`, `Move_B_Sniper02→Unit_Tutorial_Sniper`, `bSequentialDestinations=true` (очередь точек у каждого бойца своя: Танку — его укрытие, Осе — 01 затем 02), `DestinationTolerance=140` (диск 300 у точки Танка частично прятался за сплошной стеной от `Point_B_EnemySpot`).<br>Провокация до занятия точки отклоняется автоматически (`bRequirePositionBeforeActions`).<br>**Objective 1**: `Ability.Taunt.Activated`, Source Tank.<br>**Objective 2**: `Movement.Settled.Open`, Source Sniper, Count **2** (обе точки Осы — без укрытий рядом!).<br>**Objective 3**: `Movement.Settled.InCover`, Source Tank, Count 1. |
| `B4_EnemyShot` | **Gate**: `[EndTurn]`.<br>**Scripted Shot**: `Holo_B_Range` → Tank, Hit 100, Damage 30 (провокация ополовинит своим GE).<br>**Objective**: `Turn.Player.Started`. |
| `B5_SquadsightKill` | **Gate**: `[Select, Attack]`, Unit Sniper, Target Holo_B.<br>**Objective 1**: `Combat.Attack.Squadsight`, Source Sniper, Target Holo_B.<br>**Objective 2** (Id пустой): `Combat.Enemy.Eliminated`, Target Holo_B. |
| `C0_PrepareAmbush` | **Beat**: реплика Осы «ещё один противник» (§02 C0).<br>**Set Scenario Actor Active**: `[Holo_D_Overwatch]` (его старт — дальше 2800 см от всех; патруль поведёт его позже).<br>**Gate**: `[Select, Move, Overwatch, Hunker, EndTurn]`, Units `[Tank, Unit_Tutorial_Assault_B]`, Destinations `[Move_C0_AssaultCover]`.<br>**Objective 1**: `Ability.Overwatch.Activated`, Source Tank.<br>**Objective 2**: `Movement.Settled.InCover`, Source `Unit_Tutorial_Assault_B`, Count 1.<br>**Objective 3**: `Ability.Hunker.Activated`, Source `Unit_Tutorial_Assault_B`.<br>**Objective 4**: `Turn.Ended`, Count 1. |
| `C1_ReactionOnApproach` | **Gate**: `bLockGameplayInput=true`.<br>*Сближение делает ШТАТНЫЙ ход Holo_D: он не вскрыт, у него `PatrolPoints=[Point_D_Approach]` — патруль ведёт его в обзор Танка, Наблюдение стреляет (25 из 50 HP).* <br>**Objective 1**: `Combat.Attack.Overwatch`, Source Tank, Target Holo_D.<br>**Objective 2**: `Turn.Player.Started`, Count 1. |
| `C2_HoldTheLine` | **Beat**: реплика C2 (оборона держит).<br>**Gate**: `[EndTurn]`.<br>**Scripted Shot** (арминг в фазу игрока — инвариант §5.3-3): Shooter `Holo_D_Overwatch`, Target `Unit_Tutorial_Assault_B`, Hit **0**, Damage 0 — промах по Глухой обороне.<br>**Objective**: `Turn.Ended`, Count 1. |
| `C3_FlankKill` | **Beat**: реплика C3 (обход фланга).<br>**Gate**: `[Select, Move, ClassAbility, Attack]`, Unit `[Unit_Tutorial_Assault_B]`, Target `[Holo_D_Overwatch]`, Destinations `[Move_Flank_01, Move_Flank_02]`, `bSequentialDestinations`.<br>**Objective 1**: `Ability.RunAndGun.Activated`, Source `Unit_Tutorial_Assault_B`.<br>**Objective 2**: `Combat.Attack.Normal`, Source `Unit_Tutorial_Assault_B`, Target Holo_D.<br>**Objective 3** (Id пустой): `Combat.Enemy.Eliminated`, Target Holo_D. *(25 урона по оставшимся 25 HP.)* |
| `C4_DefuseBomb` | **Set Scenario Actor Active**: `[Bomb_Tutorial]` (`RequiredActions=2` — «как в бою»).<br>**Beat**: реплика C4.<br>**Gate**: `[Select, Move, Interact, EndTurn]`, Unit `[Unit_Tutorial_Assault_B]`.<br>**Objective**: `Objective.Defuse.Completed`, Count 1 (HUD сам покажет 1/2 → 2/2). |
| `D1_Evacuate` | **Set Scenario Actor Active**: включить `[Evac_Tutorial]` и evac-двойников дальних бойцов, выключить оригиналы (приём «отряд подтянулся»).<br>**Beat**: реплика D1.<br>**Gate**: `[Select, Move, Interact, EndTurn]`.<br>**Objective**: `Objective.Evac.Unit`, Count по числу живых, Distinct **true**. |
| `WaitResult` | `Quest Wait Outcome` (exact Succeeded/Failed). |

Вопрос Overwatch/Hunker закрыт в v2.1: урок встроен тактами C0–C2
(Наблюдение Танка встречает подход, Глухая оборона Клина «съедает» выстрел).

⚠️ Тонкость B2a/B2b: перебежки идут в ФАЗУ ВРАГА. `Scripted Move` двигает бойца
напрямую (AP не тратятся, quest-события движения не публикуются), но Holo_B к
началу своего хода уже должен иметь `SetScriptedAttackOrder`-приказ ЛИБО не
иметь целей в обзоре — иначе его обычный AI-ход вклинится в постановку. Если
голограмма начнёт «своевольничать», ставьте её активацию в B2b и держите её вне
обзора отряда до этого шага.

Новые акторы v2 (дополнить `SL_Showreel_Tutorial`):

```text
Bomb_Tutorial            (ABombObjective, RequiredActions=2 — «как в бою», bStartDeactivated)
Point_B_AssaultRetreat   (якорь — куда отступает Клин)
Point_B_EnemySpot        (якорь — куда выбегает Holo_B; «отмеченная точка»)
Move_B_TankCover         (полуукрытие Танка: преграда 60–150 см, ≤120 см, между ним и Point_B_EnemySpot)
Move_B_Sniper01/02       (две перебежки Осы; точка 02 — огневая: >2500 см от Point_B_EnemySpot, LOS ЧИСТАЯ, ≤5000)
Anchor_Camera_B          (камера-фокус Beat'а B0)
Move_C0_AssaultCover     (укрытие Клина под Глухую оборону: любая преграда ≤120 см)
Point_D_Approach         (цель патруля Holo_D: в обзоре Танка ≤2500 и в его дальности ≤3000)
Move_Flank_01/02         (обход фланга Клином; точка 02 — ВНЕ защитной дуги укрытия Holo_D)
Holo_D_Overwatch         (на экземпляре: BaseMaxHealth=50 — реакция Танка 25 = «пол-HP»; PatrolPoints=[Point_D_Approach])
Unit_Tutorial_Assault_B  (Кадет: BP_Unit_Assault, InitialHealth=10, bStartDeactivated;
                          «другой штурмовик», выбегающий в B2a; ведёт секции C/D)
Unit_*_Evac              (опц. двойники дальних бойцов у зоны эвакуации, bStartDeactivated)
```

Подсветка прямоугольной зоны шага: `Tactical Objective`, чей
`RequiredTargetAnchor` — `ATacticalQuestZone`, автоматически включает декаль
по габаритам её бокса (синяя рамка `M_TutorialZoneFrame`; материал/параметры —
`DA_TacticalHUDStyle.TutorialZoneMarkerMaterial`) и гасит её на выходе из шага.

Черновые позиции новых точек расставлены скриптом 2026-07-31 в валидной зоне —
двигать свободно, AnchorId при перетаскивании сохраняется. Старые `Move_B2_01/02`
и `Move_C2_01/02` (v1) в v2.1 не используются — можно удалить.

⚠️ Тайминг пустой фазы врага: после смерти Holo_A/Holo_B живых врагов может не
быть, тогда фаза врага возвращается мгновенно и постановочные перебежки
(B2a/B2b) доигрываются уже под баннером «ВАШ ХОД» при закрытом вводе — states
держатся задачами Scripted Move, порядок не ломается. В C1 сближение делает
штатный ход Holo_D (патруль), поэтому там фаза врага полноценная.

У раненых постановочных бойцов текущее HP задаётся на экземпляре полем
`InitialHealth` (0 = полное); быстрый рантайм-инструмент — `SetHealthDirect`.

### 5.1 Actors в `SL_Showreel_Tutorial`

Минимальный набор:

```text
Units:
  Unit_Tutorial_Medic
  Unit_Tutorial_Tank
  Unit_Tutorial_Sniper
  Unit_Tutorial_Assault        (v2: активен со старта, лежит Downed — bStartDowned)
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
Anchors (AScenarioAnchorPoint):
  Anchor_Camera_Tutorial
  Move_A2_01 / Move_A2_02
  Move_A5_Cover
  Move_B2_01 / Move_B2_02
  Move_C2_01 / Move_C2_02
  Route_D2_Start / Route_D2_End
```

**Каждому** актору из списка нужен `UScenarioActorIdComponent` с `AnchorId`,
буквально равным имени из этого списка: StateTree и Action Gate обращаются к
акторам только по нему. Для пустых точек ставьте готовый `AScenarioAnchorPoint`
— компонент в нём уже есть, достаточно вписать `AnchorId`.

`bStartDeactivated = true` ставится на `Holo_C_Cover`, `Holo_D_Overwatch`,
`Bomb_Tutorial` и evac-двойников (v2; Клин БОЛЬШЕ НЕ деактивирован — он лежит
Downed с самого старта через `bStartDowned`): скрытые физически стоят на карте,
без коллизии, не тикают и не входят в стороны боя. `Holo_A_OpenField` и
`Holo_B_Range` активны с начала — они нужны уже в секции A/B.

`BaseAim=40`, базовый урон 10; A4/A7/B4 меняют только roll/damage override
внутри общего attack pipeline согласно GDD.

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
иначе разные механики начнут засчитывать друг друга. Проверку конкретного
unit/target теперь делает сама `Tactical Objective` через payload; Action Gate
отвечает за то, что игрок физически не может сделать неверное действие.

### 5.3 Правила сборки состояний StateTree

Три инварианта редактора, из которых следует вся раскладка ниже.

1. **Состояние завершается, когда завершены ВСЕ его задачи — но только если у
   состояния выставлено `Tasks Completion = All`.** Дефолт UE 5.7 — `Any`, и с
   ним состояние закрывается по первой же завершившейся задаче. `Apply Action
   Gate` возвращает `Succeeded` прямо в `EnterState`, поэтому при `Any` всё
   дерево пролетает за один кадр, quest уходит в `Completed`, а в логе видно
   `Quest ... завершился до открытия Action Gate`. **`All` ставится вручную на
   каждом состоянии.** Любая `Failed` немедленно валит состояние в любом режиме.
   Отсюда: несколько `Tactical Objective` в одном состоянии дают логическое «И»,
   и это единственный корректный способ собрать A1 (выбор **и** камера) и A8
   (атака **и** уничтожение цели).
2. **`Apply Action Gate` и `Set Scenario Actor Active` возвращают `Succeeded`
   сразу.** Они не удерживают состояние: политика и активация живут до
   `ExitState`, который вызывается в любом случае. Поэтому «Consider for
   completion» у них трогать не нужно, а состояние из ОДНОЙ только gate-задачи
   бессмысленно — оно завершится мгновенно.
3. **Сценарный выстрел арминг-задачей `Scripted Shot` нужно ставить в состояние,
   активное ещё в ФАЗУ ИГРОКА.** Задача только выдаёт приказ AI-контроллеру;
   сам выстрел происходит в ближайшую активацию голограммы. Если поставить её в
   состояние, которое включается уже во время хода врага, голограмма может успеть
   отработать раньше приказа, и шаг подвиснет до `Timeout`.

Общий скелет боевого шага:

```text
State XX_Name
  ├─ Apply Action Gate          (что сейчас разрешено; Succeeded сразу)
  ├─ Tactical Objective         (что считаем подтверждённым результатом)
  └─ [Scripted Shot / Beat]     (если шаг режиссируется)
  Transition: On State Succeeded → следующее состояние
```

На корне дерева — глобальный переход
`On Event Quest.Event.Tactical.Scenario.Failed → Tree Failed`.
Последнее состояние — `WaitResult` с нативной `Quest Wait Outcome`
(`SuccessChannel = ...Scenario.Succeeded`, `FailureChannel = ...Scenario.Failed`,
`bRequireExactChannel = true` в обоих полях).

### 5.4 Точная конфигурация A1–D3

Префикс `Quest.Event.Tactical.` и `Quest.Objective.` опущен. Все
`Tactical Objective` идут с `bRequireExactChannel = true`. В `Allowed Actions`
`Camera` можно не указывать: gate её не блокирует никогда.

| Состояние | Задачи и настройки |
|---|---|
| `A1_SelectAndCamera` | **Gate**: Actions `[Select]`, UnitAnchors `[Unit_Tutorial_Medic]`, Reason «Выберите Медика».<br>**Objective 1**: Id `Tutorial.A1.SelectMedic`, канал `Unit.Selected`, SourceAnchor `Unit_Tutorial_Medic`, Count 1.<br>**Objective 2**: Id `Tutorial.A1.CameraAdjusted`, канал `Camera.Adjusted`, Count 1. |
| `A2_MoveOpen` | **Gate**: Actions `[Move]`, UnitAnchors `[Unit_Tutorial_Medic]`, DestinationAnchors `[Move_A2_01, Move_A2_02]`, Tolerance 300.<br>**Objective**: Id `Tutorial.A2`, канал `Movement.Settled.Open`, SourceAnchor `Unit_Tutorial_Medic`, Count **2**, DistinctSources **false** (это один и тот же боец дважды). |
| `A3_EndTurn` | **Gate**: Actions `[EndTurn]`.<br>**Objective**: Id `Tutorial.A3`, канал `Turn.Ended`, Count 1.<br>**Scripted Shot**: Shooter `Holo_A_OpenField`, Target `Unit_Tutorial_Medic`, HitChance **100**, Damage **30**, Timeout 45. Арминг здесь — по инварианту 3. Состояние завершится, когда ход передан **и** выстрел разрешён. |
| `A4_ExposedHit` | **Gate**: `bLockGameplayInput = true`.<br>**Objective**: Id `Tutorial.A4`, канал `Turn.Player.Started`, Count 1. |
| `A5_FullCover` | **Gate**: Actions `[Move]`, UnitAnchors `[Unit_Tutorial_Medic]`, DestinationAnchors `[Move_A5_Cover]`.<br>**Objective**: Id `Tutorial.A5`, канал `Movement.Settled.InCover`, SourceAnchor `Unit_Tutorial_Medic`, Count 1. |
| `A6_SelfHeal` | **Gate**: Actions `[ClassAbility]`, UnitAnchors `[Unit_Tutorial_Medic]`, TargetAnchors `[Unit_Tutorial_Medic]`.<br>**Objective**: Id `Tutorial.A6`, канал `Ability.Heal.Normal`, SourceAnchor и TargetAnchor `Unit_Tutorial_Medic`, Count 1. |
| `A7_CoverMiss` | **Gate**: Actions `[EndTurn]`.<br>**Scripted Shot**: Shooter `Holo_A_OpenField`, Target `Unit_Tutorial_Medic`, HitChance **0**, Damage 0, Timeout 45.<br>**Objective**: Id `Tutorial.A7`, канал `Turn.Player.Started`, Count 1. |
| `A8_ReturnFire` | **Gate**: Actions `[Attack]`, UnitAnchors `[Unit_Tutorial_Medic]`, TargetAnchors `[Holo_A_OpenField]`.<br>**Objective 1**: Id `Tutorial.A8`, канал `Combat.Attack.Normal`, Source `Unit_Tutorial_Medic`, Target `Holo_A_OpenField`.<br>**Objective 2**: `ObjectiveId` **пустой**, канал `Combat.Enemy.Eliminated`, Target `Holo_A_OpenField`. Оба события приходят одной пачкой, и обе задачи одного состояния их видят. |
| `A9_ReviveAssault` | **Set Scenario Actor Active**: AnchorIds `[Unit_Tutorial_Assault]`, bActive true (Downed ставит флаг `bStartDowned` на его `UScenarioActorIdComponent`, BP-хук не нужен).<br>**Gate**: Actions `[Move, ClassAbility, EndTurn]`, UnitAnchors `[Unit_Tutorial_Medic]`, TargetAnchors `[Unit_Tutorial_Assault]`.<br>**Objective**: Id `Tutorial.A9`, канал `Ability.Heal.Revive`, Source `Unit_Tutorial_Medic`, Target `Unit_Tutorial_Assault`.<br>`EndTurn` обязателен: A8 — атака, а она сжигает активацию Медика, поэтому подъём физически возможен только со следующего хода. |
| `B1_EnterSector` | **Gate**: Actions `[Select, Move]`, UnitAnchors `[Unit_Tutorial_Tank, Unit_Tutorial_Sniper]`.<br>**Objective**: Id `Tutorial.B1`, канал `Zone.Entered`, TargetAnchor `QZ_B1_Sector`, Count **2**, **DistinctSources = true**. |
| `B2_TankAdvance` | **Gate**: Actions `[Move]`, UnitAnchors `[Unit_Tutorial_Tank]`, DestinationAnchors `[Move_B2_01, Move_B2_02]`.<br>**Objective**: Id `Tutorial.B2`, канал `Movement.Settled.InCover`, Source `Unit_Tutorial_Tank`, Count 1. |
| `B3_Taunt` | **Gate**: Actions `[ClassAbility]`, UnitAnchors `[Unit_Tutorial_Tank]`.<br>**Objective**: Id `Tutorial.B3`, канал `Ability.Taunt.Activated`, Source `Unit_Tutorial_Tank`. |
| `B4_AbsorbShot` | **Gate**: Actions `[EndTurn]`.<br>**Scripted Shot**: Shooter `Holo_B_Range`, Target `Unit_Tutorial_Tank`, HitChance 100, Damage 30 (провокация уполовинит урон своим GE — не занижайте его здесь вручную).<br>**Objective**: Id `Tutorial.B4`, канал `Turn.Player.Started`. |
| `B5_SquadsightKill` | **Gate**: Actions `[Select, Attack]`, UnitAnchors `[Unit_Tutorial_Sniper]`, TargetAnchors `[Holo_B_Range]` (Move не разрешаем — Оса стоит).<br>**Objective 1**: канал `Combat.Attack.Squadsight`, Source `Unit_Tutorial_Sniper`, Target `Holo_B_Range`.<br>**Objective 2**: канал `Combat.Enemy.Eliminated`, Target `Holo_B_Range`. |
| `C1_Brief` | **Set Scenario Actor Active**: `[Holo_C_Cover]`, bActive true.<br>**Gate**: `bLockGameplayInput = true`.<br>**Tutorial Beat**: Speaker «Купол», Subtitle из `02_LORE_SCRIPT.md`, FocusAnchorId `Holo_C_Cover`, Duration 5. |
| `C2_RunAndGun` | Родительское состояние с **Gate**: Actions `[Move, ClassAbility, Attack]`, UnitAnchors `[Unit_Tutorial_Assault]`, TargetAnchors `[Holo_C_Cover]`, DestinationAnchors `[Move_C2_01, Move_C2_02]`. Дети последовательно:<br>`C2_Activate` — Objective Id `Tutorial.C2.Activate`, канал `Ability.RunAndGun.Activated`, Source `Unit_Tutorial_Assault`.<br>`C2_Move` — Objective Id `Tutorial.C2.Move01`, канал `Movement.Settled.Open`, Source `Unit_Tutorial_Assault`, Count **2**.<br>`C2_Attack` — Objective Id `Tutorial.C2.Attack`, канал `Combat.Attack.Normal`, Source `Unit_Tutorial_Assault`, Target `Holo_C_Cover` + вторая Objective на `Combat.Enemy.Eliminated`. |
| `D1_PrepareAmbush` | **Gate**: Actions `[Select, Overwatch, Hunker, EndTurn]`.<br>**Objective 1**: Id `Tutorial.D1.Overwatch01`, канал `Ability.Overwatch.Activated`, Count **2**, **DistinctSources = true**.<br>**Objective 2**: Id `Tutorial.D1.Hunker01`, канал `Ability.Hunker.Activated`, Count **2**, **DistinctSources = true**.<br>**Objective 3**: Id `Tutorial.D1.EndTurn`, канал `Turn.Ended`, Count 1.<br>Отдельные Id `...Overwatch02`/`...Hunker02` больше не нужны: «двое разных» считает одна задача. |
| `D2_ReactionKill` | **Set Scenario Actor Active**: `[Holo_D_Overwatch]`, bActive true.<br>**Gate**: `bLockGameplayInput = true`.<br>**Objective 1**: Id `Tutorial.D2`, канал `Combat.Attack.Overwatch`, Target `Holo_D_Overwatch`.<br>**Objective 2**: канал `Combat.Enemy.Eliminated`, Target `Holo_D_Overwatch`. |
| `D3_Evacuate` | **Set Scenario Actor Active**: `[Evac_Tutorial]`, bActive true (либо активация зоны из BP).<br>**Gate**: Actions `[Select, Move, Interact]`.<br>**Objective**: Id `Tutorial.D3`, канал `Objective.Evac.Unit`, Count **4**, **DistinctSources = true**. |
| `WaitResult` | **Quest Wait Outcome**: Success `Scenario.Succeeded`, Failure `Scenario.Failed`, exact в обоих. |

Пояснения к неочевидным местам:

- **Пустой `ObjectiveId` = скрытое условие.** Задача с пустым Id считает события
  и удерживает состояние, но не попадает в снимок прогресса и в трекер HUD.
  Так оформляются «уничтожение цели», «эвакуация подтверждена» и прочие
  постусловия, которые не должны показываться игроку отдельной строкой.
- **Почему `DistinctSources = false` в A2 и C2_Move.** Там один и тот же боец
  делает два перемещения; включённый флаг засчитал бы только первое.
- **Почему в A8/B5/C2/D2 две задачи в одном состоянии.** Смертельный выстрел
  публикует `Combat.Attack.*` и `Combat.Enemy.Eliminated` в одном кадре. Два
  последовательных состояния второй тег потеряли бы; две задачи одного состояния
  видят одну и ту же пачку событий.
- **Почему у B5 нет `Move` в gate.** Шаг проверяет именно Squadsight: если Оса
  подойдёт и получит свою LOS, канал будет `Combat.Attack.Normal`, и шаг не
  закроется. Запрет движения делает ошибку невозможной, а не «непонятной».
- **Почему A3 и A7 разрешают `EndTurn`.** Иначе автозавершение хода при нулевых
  AP тоже заблокировано (оно проходит через тот же gate), и сценарный выстрел
  никогда не состоится.

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
- разрешённые destination anchors/zone; пройденная точка гаснет и повторно не
  разрешается, `bSequentialDestinations` открывает точки строго по одной;
- `DestinationOwners` (map «точка → боец») закрепляет точку за бойцом: чужому
  бойцу она запрещена и не подсвечивается, приход чужого её не «гасит», очередь
  `bSequentialDestinations` считается по каждому владельцу независимо;
- `bRequirePositionBeforeActions` (по умолчанию true): пока у бойца остаются
  открытые точки шага, ему разрешены только Select/Move/EndTurn — способность
  или выстрел «с полпути» ломали постановку следующего шага (Провокация из
  чистого поля в B3 оставляла голограмму B4 без видимой цели);
- `DestinationTolerance` — не только радиус клика, но и гарантия ПОЗИЦИИ для
  следующих шагов: допуск должен целиком лежать в зоне, откуда постановка
  работает (в B3 допуск 140 — диск 300 частично прятался за сплошной стеной);
- в шаге с destination anchors приказ перемещения дороже 1 AP отклоняется:
  рывок «через точку» давал одно `Settled` вместо двух и вечные 1/2;
- текст причины отказа для HUD (оверлей показывает его 3 секунды).

Видимая подсветка равна проверке по построению: декали-маркеры открытых точек
рисуются радиусом `DestinationTolerance` той же политики (материал — в
`DA_TacticalHUDStyle`).

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

### 9.0 Нативный трекер уже есть (2026-07-30)

`STutorialHintOverlay` ([TutorialHintOverlay.h](../Source/XRU1/Tactics/TutorialHintOverlay.h))
— Slate-оверлей без WBP, создаётся `ATacticalPlayerController` автоматически.
Показывает: имя tracked quest, все активные цели с Description и счётчиком
`(x/y)`, последний отказ Action Gate (3 секунды, оранжевым). Цели с пустым
Description (скрытые условия) не показываются; если у шага нет ни одного
Description, выводится DenialReason политики. Description целей секции A
заполнены. WBP_QuestTracker из §9.1 остаётся production-заменой — при его
появлении оверлей отключить в контроллере.

Позиция, размеры шрифтов и ширина переноса настраиваются в
`DA_TacticalHUDStyle` → категория «07. Обучение | Подсказки» (без пересборки).

Там же — `TutorialDestinationMarkerMaterial`: контроллер при каждой смене
политики шага (и при смене выбранного бойца) рисует декали-маркеры открытых
`AllowedDestinationAnchors` — выбранный боец видит только свои личные и общие
точки (радиус = `DestinationTolerance` политики), перестраивает зону хода и
пересчитывает серость кнопок. Дефолтный материал — `M_SelectionRing`.

Панель отряда HUD пересобирается в C++ при смене состава боя
(`RebuildSquadPanel` в [TacticalHUDWidget.cpp](../Source/XRU1/UI/TacticalHUDWidget.cpp)):
staged-бойцы (Танк/Оса в B0, Кадет в B2a) получают карточки, выключенные
(Медик/Клин после A9) — теряют. BP строит панель один раз на Construct и сам
состав не отслеживает.

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
- [x] Передавать `ScenarioRunId` в quest context: он лежит в `FQuestEventData`,
      а `Tactical Objective` отбрасывает события чужого запуска. Fog/async
      action-контексты — остаток задачи.
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
- [x] Подключить selection/camera/move и scripted-enemy emitters к action-token/
      payload-aware orchestration; параллельных BP-send нет.
- [x] Добавить leaf tags из §5.2. Уже подключены Zone, player turn,
      player combat/class abilities, kill, objectives и scenario result.
      Открыты только selection, camera, movement и scripted-enemy emitters.
- [x] Ввести единый `MoveSettlement` gate в controller: cover/HUD, списание AI
      AP и следующий шаг происходят только после route arrival, cover-hug step
      и финального turn-in-place.
- [x] Публиковать quest move-event из этой единственной финализации после
      проверки action token; выбирается только `Movement.Settled.Open` либо
      `Movement.Settled.InCover`, не оба.
- [x] Нормализовать event taxonomy в parent/child и закрепить правило «один
      outcome → один leaf»; generic listeners используют `MatchesTag`, а не
      второй broadcast.
- [x] Расширить событие/StateTree task так, чтобы проверять unit, target и
      `ScenarioRunId`: runner кладёт `FQuestEventData` в `FStateTreeEvent::Payload`,
      а `Tactical Objective` фильтрует по `AnchorId` источника и цели.
      Проверка конкретного hit/damage остаётся за Scripted Shot и Action Gate.
- [x] Реализовать Action Gate из раздела 8 и общий API для input/HUD.
- [x] Добавить StateTree tasks: применить gate, показать beat, активировать
      staged actor, выполнить scripted shot через обычный attack pipeline,
      дождаться confirmed turn/result — категория **XRU1 Tutorial**.
- [x] Добавить `Quest Wait Outcome`, который различает terminal success/failure;
      обычную Objective Group для исхода сценария не использовать.
- [x] Добавить scenario actor registry по стабильным `AnchorId`; имена World
      Outliner используются только для диагностики.
- [x] Форс A4/A7/B4 задаётся через `FScriptedShotOverride` в snapshot'е
      `UGA_Attack`, а не прямым damage/ResolveShot из BP.

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

### 13.0 Черновые позиции акторов (проверить глазами)

- [ ] A5: стена X≈4625 действительно между `Holo_A_OpenField` и `Move_A5_Cover`,
      cover считается Full.
- [ ] A2/A4: с `Move_A2_02` видно `Holo_A_OpenField` (LOS не перекрыт стеной).
- [ ] B2: `Move_B2_02` даёт Half против `Holo_B_Range`.
- [ ] B5: `Unit_Tutorial_Sniper` со стартовой позиции не имеет прямого LOS до
      `Holo_B_Range`, но Танк — имеет (иначе шаг закроется как `Attack.Normal`).
- [ ] Юниты не проваливаются и не висят: у всех Z поверхности + 88.
- [ ] Mission01: бомба доступна с навмеша, evac-зона у южного края.

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
- [ ] Один и тот же `Main_Map_Showreel` после Tutorial корректно стартует Mission01,
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
