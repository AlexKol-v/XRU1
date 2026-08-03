# Туман войны

Актуально на 2026-08-03. Документ переписан по итогам сессии-исследования
([agents/BRIEF_FogOfWar_Research.md](agents/BRIEF_FogOfWar_Research.md)): разобраны
исходники XCOM 2 (`Rukan/XComGame`), рабочие ini (`russgray/xcom2-config`), мод
**Gotcha Again** (`Chaser324/XCOM2-GotchaAgain`), Highlander и практика UE.
Каждая развилка §2.1 брифа закрыта решением с причиной; план F1–F5 пересобран,
часть работ снята с обязательного объёма (§6).

Цель прежняя: скрывать не только геометрию, но и любую игровую информацию,
которая выдаёт противника. Источник истины — CPU-логика; картинка только
отображает её результат и **никогда** ничего не разрешает.

---

## 1. Главный вывод исследования

**Туман — это не сетка. Туман — это гейт презентации поверх уже существующей
парной видимости.**

В XCOM 2 таких слоёв ровно два, и они не смешаны:

| Слой | Класс XCOM 2 | Что решает | Наш эквивалент |
|---|---|---|---|
| Правила | `X2GameRulesetVisibilityManager` + кэш `GameRulesCache_VisibilityInfo` (пары source×target) | кто кого видит, можно ли стрелять, что показывает UI | `UTacticsCombatStatics::HasLineOfSight` + `UFogOfWarSubsystem` |
| Картинка | тайловая сетка `FOWTileStatus` + одна FOW-текстура в `XComWorldData` | затемнение местности и дешёвое «показывать ли пешку прямо сейчас, пока она бежит» | ещё не сделано (F3) |

Все геймплейные вопросы XCOM решает **парным кэшем, а не сеткой**. Сетка нужна
только для двух вещей: затемнить местность и дать `X2VisibilityObserver` дешёвый
пер-тайловый ответ во время движения пешки (`GetTileFOWValue`).

Отсюда наш порядок работ: **гейты акторов — обязательны и первыми; сетка,
Render Target и post-process — отдельный визуальный слой, полировка** (§6).

Второй вывод, важнее первого: **бóльшая часть каналов утечки в XRU1 закрывается
не новым кодом, а тем, что у нас уже есть.** См. §4 — «закрыто по построению».

---

## 2. Решения по развилкам

### 2.1 CPU-сетка `Unknown/Explored/Visible` против актороцентричной видимости

**Решение.** Гейты — актороцентрично (пары «боец × актор»), сетка — только для
визуального затемнения местности и только в фазе полировки. Трёхсостоянийная
модель GDD §5.9 сохраняется как модель, но `Unknown` включается **профилем
сценария**, а не безусловно: демо-профиль `Showreel` стартует полностью
разведанным (`bStartFullyExplored = true`).

**Почему.**

1. Firaxis сами так делят: `GameRulesCache_VisibilityInfo` (пары) авторитетен,
   `FOWTileStatus` (сетка) — рендер. Ни один геймплейный запрос
   (`GetAllVisibleToSource`, `CanSquadSeeTarget`, `X2Condition_Visibility`) сетку
   не читает.
2. Наши гейты всё равно обязаны решаться точным LOS: тактическая зона маленькая,
   а требование симметрии Ф5 ([13_LOS_TARGETING.md](13_LOS_TARGETING.md)) держится
   на наборах огневых/exposed-точек, которых у сетки нет в принципе. Сетка,
   принимающая решения, немедленно стала бы вторым источником правды.
3. Против безусловного `Unknown` — сам XCOM: в `XComWorldData` есть
   `bShowNeverSeenAsHaveSeen` («Setting for certain maps to make it so that never
   seen fog is shown as have seen») и kismet-функция
   `InitializeAllViewersToHaveSeenFog(bool)`. То есть «карта без неизвестного»
   — штатный, предусмотренный режим, а не отказ от механики.
4. Против `Unknown` конкретно у нас: туториал ведёт игрока по секторам A–D
   режиссурой камеры и беатами, брифинг перед миссией уже показывает карту, обе
   миссии живут на одном persistent-уровне, и `Explored` пришлось бы сбрасывать
   по `ScenarioRunId`. Ценность — атмосфера; цена — сетка, RT, post-process,
   этажи и новое состояние, которое обязано переживать retry. Для vertical slice
   размен невыгодный.
5. Геймплейная суть тумана («я не знаю, где враг») даётся **целиком** гейтом
   акторов. Затемнение местности не добавляет к ней ничего.

**Если сетку всё-таки делаем** — жёсткое правило: сетка **не имеет права**
дёргать физические трейсы в рантайме. XCOM квантует мир один раз при сохранении
карты (`XComWorldData::BuildWorldData` — «Run when users save a map, this will
quantize the playable game space and build a 3d tile grid») и дальше ходит по
воксельным данным (`VoxelRaytrace_Tiles`, `bUseLineChecksForFOW = false`).
Наш аналог: один раз на старте сценария запечь битовую маску «клетка блокирует
обзор» (по одному трейсу на клетку), дальше — DDA по массиву. 60×60 м при клетке
100 см — 3600 клеток; 4 источника × ~360 лучей × ~25 шагов ≈ 36 000 чтений
массива, доли миллисекунды. Наивный вариант «трейс из каждого источника в каждую
клетку» — 14 400 сферо-свипов на пересчёт, то есть заведомо неприемлемо.

### 2.2 Стоимость и способ обновления

**Решение.** Событийный пересчёт с кэшем видимых акторов + одно дешёвое
исключение для бегущего юнита + сверка на «простое». Точный CPU-предикат
остаётся синхронным, но вызывается по событию, а не из биндинга виджета.

**Почему.** Ровно эта схема в XCOM 2, тремя разными механизмами:

- `X2GameRulesetVisibilityManager::OnNewGameState` — пересчёт привязан к
  изменению состояния игры, а не к кадру. Рядом живёт
  `VisibilityDeltaMap` (что именно изменилось) и `ObjectIDToVisibilityCacheIndices`
  (какие пары затронул объект) — то есть инвалидация точечная.
- `X2VisibilityObserver::VisualizerUpdateVisibility(Visualizer, NewTile)` —
  пока пешка бежит, её видимость решается **не** полным пересчётом, а одним
  дешёвым запросом `XWORLD.GetTileFOWValue(NewTile, …)` на каждом новом тайле.
- `X2VisibilityObserver::OnVisualizationIdle` — полная сверка «состояние против
  визуализаторов», когда визуализация встала. В комментарии Firaxis прямо пишут,
  что это переходная мера и правильнее событийный путь, «but this can remain as a
  validation step to warn when the game state and visualizer disagree».

Конкретное число цены наивного пути — из их же кода
(`X2TacticalVisibilityHelpers::FillPathTileData`): «`GetAllVisibleEnemiesForLocation`
will rebuild UnitsVisibleToPlayer every frame, which is extremely expensive and
uses about **20 ms** on my i7 per call in debug scripts» — поэтому список видимых
игроку строится **один раз** на построение пути и переиспользуется для всех
тайлов. Наш `UFogOfWarSubsystem::GetCurrentlyVisibleEnemyCount()` сейчас — ровно
тот наивный вариант, и он висит на HUD (см. дефект F0-3 в §5).

**Бюджет (переформулирован).** Прежняя формулировка «≤2 мс на fog update» ничего
не измеряла. Новая:

- полный пересчёт = `N_отряда × N_врагов` парных проверок; на демо-составе
  4 × 6 = 24 пары; каждая пара — 4 сферо-свипа быстрым путём и до 4×4 = 16
  запасным ⇒ ≤ ~480 свипов, порядка **1 мс**;
- допускается **один** полный пересчёт на событие и **ноль** в кадрах, где
  ничего не произошло;
- во время бега — переоценка не чаще `FogMovingRecheckInterval` (дефолт 0.1 с,
  `EditDefaultsOnly`); сейчас контроллер уже так делает для подхвата вражеского
  хода камерой (`ATacticalPlayerController::PlayerTick`, троттлинг
  `LOSDebugInterval`) — этот приём становится общим правилом;
- ни один Widget Tick / property binding не имеет права звать предикат напрямую:
  HUD читает кэш и подписку `OnActorVisibilityChanged`.

### 2.3 Визуализация: один Render Target + post-process

**Решение.** Подтверждено: один RT и один post-process blendable на
`BP_TacticalCameraPawn`. `SceneCapture2D` на бойцах — запрещён. (Работа
опциональная, см. §6.)

**Почему.**

- XCOM держит **одну** FOW-текстуру (`XComWorldData.FOWUpdateTextureBuffer`) с
  флагом грязи (`bFOWTextureBufferIsDirty`), прямоугольником обновления
  (`CurrentUpdateBox` — «bounds of the current update area of the FOW texture (in
  texels)») и очередью углов последних обновлений (`UpdateCorners` — «Needed to
  support lerping over time»). То есть: одна текстура, частичные обновления,
  сглаживание края во времени. Скорость края вынесена в ini —
  `XComEngine.ini: FOWEnvelopeSpeed=170`; там же `bForceLowResFOW=False`,
  `bHighQualityFOWFiltering=False` (то есть качество фильтрации — настройка
  Scalability, а не константа).
- Практический подводный камень post-process тумана (форум Epic, тема
  «Is post-processed fog of war possible for a turn-based tile-based project?»):
  прямой `Absolute World Position` в PP-материале «плавает» при апскейле —
  туман едет за камерой. Рабочее сочетание: `Blendable Location = Scene Color
  Before DOF` и восстановление позиции как `Camera Relative World Position
  (including Material Shader Offsets) + Camera Position`.
- HUD не затемняется по построению: post-process действует на 3D-сцену, UMG
  композитится после. Наш оверхед-худ — `EWidgetSpace::Screen`
  (`ATDCombatant::ATDCombatant`), значит он тоже вне тумана. А вот кольца выбора,
  рамки зон и маркеры — это декали и меши, они затемнятся; это приемлемо, потому
  что они появляются только у видимых объектов.
- Порядок с `M_OutlinePP`: обводка добавляется в конструкторе камеры
  (`ATacticalCameraPawn::ATacticalCameraPawn` → `PostProcess->Settings.AddBlendable(OutlineMaterial, 1.f)`),
  туман добавляется **после** неё. Но настоящая защита не в порядке blendable, а
  в том, что у скрытого врага custom depth обязан быть выключен (§2.5) — обводка
  сквозь туман не должна существовать в принципе.

### 2.4 Многоэтажность

**Решение.** Z-bands в демо **не делаем**. В конфиге остаётся поле `FloorBands`
как точка расширения; сетка (если будет) — одна полоса.

**Почему.** Гейт акторов трёхмерен по построению — LOS считается сферо-свипами в
мире, этажи ему безразличны. Неверной от плоской сетки может стать только
картинка затемнения, и только там, где над тактической зоной реально стоит
боевой второй этаж. На `Main_Map_Showreel` бой идёт по земле; `AddViewFloorStep`
у камеры — это обзорный инструмент, а не признак многоэтажного боя.
Условие пересмотра записано в §7: как только в сценарии появляется враг,
стоящий над другим врагом по тем же X/Y, полоса этажа становится обязательной.

### 2.5 Скрытие актора без поломки симуляции

**Решение.** Один владелец презентации — `UFogRevealableComponent`. Он опирается
на `SetActorHiddenInGame` как на базу, но обязан добивать четыре вещи, которые
эта функция не покрывает. Коллизия и occupancy **не трогаются**.

**Что покрывает `SetActorHiddenInGame` (проверено по исходникам UE 5.7).**
`USceneComponent::ShouldRender()` (`SceneComponent.cpp`) сначала поднимается по
цепочке `Owner->GetParentComponent()` и возвращает false, если родительский
`UChildActorComponent` не рендерится, и только потом проверяет
`!Owner->IsHidden()`. Значит скрытие юнита **автоматически гасит оружие**: у
наших юнитов оружие — `Gun : ChildActorComponent` → отдельный актор
`BP_AssaultRifle_Default` с ~40 примитивами (проверено запросом к живому
редактору). Отдельно перебирать меши оружия не нужно.

**Что `SetActorHiddenInGame` НЕ покрывает — и что обязан добить компонент:**

| Канал | Почему не покрывается | Что делать |
|---|---|---|
| Оверхед-худ | `HUDWidgetComponent` создан как `EWidgetSpace::Screen`; экранные виджеты рисует `SWorldWidgetScreenLayer::Tick`, который смотрит на сам компонент и на виджет, но **никогда** на `Owner->IsHidden()` | звать `ATDCombatant::SetOverheadHUDVisible(false)`. Именно `SetHiddenInGame` на компоненте, а не `SetVisibility` — `SetVisibility(false)` разрушает Slate-виджет и «замораживает» AP-пипсы (это уже было в v2.8) |
| Вспышка/импакт | `AUnitBase::PlayShotVfx` спавнит систему через `UNiagaraFunctionLibrary::SpawnSystemAtLocation` — это мировой эффект, не потомок юнита | гейт на месте вызова |
| Трассер | `AShotTracerActor::Launch` спавнит **отдельный актор** | гейт на месте вызова |
| Звук | `AUnitBase::PlayUnitSound` → `PlayCueAttached` — 3D-звук с аттенюацией играет независимо от рендера | гейт на месте вызова (§4 объясняет, какие звуки реально опасны) |
| Обводка | `AUnitBase::SetHoverHighlight` включает `SetRenderCustomDepth(true)` | скрытый юнит не должен становиться `HoveredUnit` вообще (§5, дефект F0-1) |

**Чего НЕ делать.** Не выключать коллизию и не убирать юнита из
`UTacticsCombatStatics::GetUnitObstacles`. XCOM держит неразведанные поды
блокирующими тайлы и раскрывает их при контакте; если убрать occupancy, игрок
сможет отдать приказ «встать внутрь» невидимого врага. Побочный эффект — зона
хода и превью пути огибают пустое место — принимается как честная плата (та же
плата в XCOM). Готовый рецепт «выключить всё разом» в проекте уже есть и он
**не подходит**: `UTacticalScenarioSubsystem::SetActorScenarioActive` гасит
коллизию, тик и участие в бою — это правильно для сценарной голограммы, которой
ещё нет в мире, и категорически неправильно для живого врага в тумане.

**Исключения из скрытия** — берём список Firaxis целиком, он выстрадан
(`XComGameState_Unit::ForceModelVisible`, интерфейс
`X2GameRulesetVisibilityInterface::ForceModelVisible` → `eForceVisible` /
`eForceNotVisible` / `eForceNone`):

| Правило XCOM | Наш перевод |
|---|---|
| Свои юниты — всегда видимы | отряд игрока не гейтится никогда |
| Трупы, `IsBleedingOut`, `IsUnconscious`, `IsStasisLanced` — **всегда видимы** | тело и Downed остаются на экране после смерти/падения, туман их не съедает |
| `bRemovedFromPlay` прячет юнита, но **только если текущее действие уже завершено** | не прятать юнита посреди монтажа/эвакуации; hide откладывается до терминала действия |
| `m_bInMatinee` → `eForceVisible` | сценарные такты обучения (`FTacticalTask_ScriptedMove`, `FTacticalTask_ScriptedEnemyTurn`, беат-фокус) получают явный override «показывать» |
| Скампер (раскрытие пода) → `eForceVisible` | момент обнаружения принудительно показывает врага, пока играет реакция камеры |
| `bScanningProtocolOutline` → `eForceVisible` | задел под способность «разведка», если появится |
| Гражданские видимы, если так решил `BattleData` | objective-акторы получают правило `AlwaysKnown` из данных сценария |

---

## 3. Референс → что берём → почему

Точные имена классов и чисел. Формат — как §5.0.23 в
[11_SHARED_MAP_TUTORIAL.md](11_SHARED_MAP_TUTORIAL.md).

| Референс (класс / параметр) | Что делает у них | Что берём | Почему / что НЕ берём |
|---|---|---|---|
| `GameRulesCache_VisibilityInfo` (`X2GameRulesetVisibilityDataStructures.uc`) | запись видимости на пару source×target: `bClearLOS`, `bVisibleBasic`, `bVisibleGameplay`, `bVisibleFromDefault/ToDefault`, `TargetCover`, `PeekSide`, `bBeyondSightRadius` | структуру расслоения: **геометрия** (`bClearLOS`) ≠ **обнаружение** (`bVisibleBasic` = LOS + в радиусе) ≠ **правила** (`bVisibleGameplay`) | у нас это уже есть в `UGA_Attack::GetTargetStatus` (LOS всегда → радиус → squadsight). Совпадение подтверждает Ф5-модель. Кэш пар — берём (см. F1) |
| «up to 25 traces per Visibility info: 5 source locations (default + 4 peeks) to 5 target locations» (комментарий там же) | перебор пик-точек с обеих сторон | подтверждение нашей схемы `GetFiringPositions × GetTargetExposedPoints` | у нас до 4×4 вместо 5×5 — этого достаточно; расширять набор не нужно |
| `bBeyondSightRadius` («Cache is incomplete because the target is beyond the sight radius») | ранний выход по дистанции ДО трассировки | обязательный порядок проверок: дистанция → LOS | в `UFogOfWarSubsystem::IsActorCurrentlyVisible` уже так; закрепить как правило кэша |
| `CharacterBaseStats[eStat_SightRadius]=27` (`XComGameData_CharacterStats.ini`), `WORLD_METERS_TO_UNITS_MULTIPLIER=64` | обзор бойца = 27 м, единый для всех классов | подтверждение `SquadVisionRange = 2500` (25 м) | наше число практически совпадает с шипнутым XCOM; менять не нужно. Радиус един для всех классов — у нас тоже |
| `CharacterBaseStats[eStat_DetectionRadius]=9…15` | отдельный, много меньший радиус обнаружения скрытности | **не берём** | concealment в демо нет; отдельный радиус завёл бы третье число про «видимость» |
| `BLEEDOUT_SIGHT_RADIUS=3` | истекающий кровью видит на 3 м | идею «Downed не полноценный источник зрения» | у нас проще и достаточно: `IsUnitAlive` ложна для Downed ⇒ он вообще не даёт зрения. Расхождение осознанное |
| `SQUADSIGHT_CRIT_MOD=-10`, `SQUADSIGHT_DISTANCE_MOD=-2` + `X2AbilityToHitCalc_StandardAim` (штраф `-2 × (тайлы − собственный радиус)`) | штраф squadsight растёт с дистанцией | **не берём сейчас** | у нас плоские −10 (`UGA_Attack::SquadsightAimPenalty`), это записано в GDD §7. Дистанционная кривая уже есть отдельно (`GetAimDistanceModifier`) — второй дистанционный штраф был бы двойным счётом |
| `X2GameRulesetVisibilityInterface`: `GetVisibilityRadius()`, `CanEverSee()`, `CanEverBeSeen()`, `ForceModelVisible()`, `UpdateGameplayVisibility()` | интерфейс участника видимости; источники и цели — разные способности | разделение «источник зрения» ↔ «скрываемый объект» на **два** компонента (`UFogVisionComponent` / `UFogRevealableComponent`) и override `ForceModelVisible` | подтверждает, что F1 задуман правильно; `ForceModelVisible` — то, чего в нашем плане не было и без чего сломается туториал |
| `XComGameState_SquadViewer` (`ViewerTile`, `ViewerRadius`, `AssociatedPlayer`, `RevealUnits`) | НЕ-юнит как источник зрения, живёт в той же системе видимости | «источник зрения» — не обязательно боец: initial reveal сценария и objective-раскрытие делаются таким же источником | иначе появился бы второй механизм «эта зона открыта» мимо подсистемы |
| `XComWorldData::CreateFOWViewer(Location, Radius, ObjectID)` / `DestroyFOWViewer`, `X2Action_RevealArea` (`ScanningRadius = 768` = 8 тайлов = 12 м) | временная дыра в тумане по скрипту | тот же приём для сценарных «покажи сектор» | наш `initial reveal` из scenario data — это ровно временный/постоянный viewer, а не Level Blueprint |
| `X2Action_RevealAIBegin`: `RevealFOWRadius = 768.0` (8 тайлов), `XComCamera.ini: FirstSightedDelay=0.75` | при обнаружении пода туман принудительно раскрывается на 12 м вокруг, пауза 0.75 с на «первое знакомство» | числа как стартовые дефолты для нашего момента обнаружения | без такого раскрытия камера обнаружения смотрит в чёрное |
| `X2VisibilityObserver::OnVisualizationIdle` + `VisualizerUpdateVisibility` | сверка на простое + дешёвый пер-тайловый апдейт на бегу | двухскоростная схема: событие → полный пересчёт, бег → троттлинг, простой → сверка | их же комментарий признаёт сверку костылём — берём как страховку, а не как основной путь |
| `Visualizer.SetVisibleToTeams(eTeam_All / eTeam_None)` | ОДИН вызов скрывает визуализатор целиком | принцип «одна точка скрытия», но реализация другая | в UE такого одного вызова нет: `SetActorHiddenInGame` не гасит экранный `WidgetComponent`, мировые Niagara и трассер-актор (§2.5) |
| `FOWTileStatus` = `Seen / HaveSeen / NeverSeen`; `FOWNeverSeen`, `FOWHaveSeen` (float) | три состояния тайла, две константы затемнения | модель состояний (совпадает с GDD §5.9) | но включение `NeverSeen` — профилем сценария (§2.1) |
| `XComWorldData::bShowNeverSeenAsHaveSeen`, `InitializeAllViewersToHaveSeenFog(bool)` | штатный режим «карта без неизвестного» | прямое обоснование `bStartFullyExplored` в профиле | это не отказ от механики, это её штатная настройка |
| `XComWorldData`: `bUseLineChecksForFOW=false`, `BuildWorldData` (квантование при сохранении карты), `VoxelRaytrace_Tiles` | сетка запекается, рантайм ходит по вокселям | правило «сетка не трейсит в рантайме» (§2.1) | наивные трейсы по клеткам — главный способ убить кадр |
| `HasPendingVisibilityUpdates()`, `FlushCachedVisibility()`, `bUseSingleThreadedSolver`, `NumRebuilds` («helps the visibility job manager know when a job is stale») | асинхронный многопоточный солвер видимости | **не берём** | у нас 4 бойца × 6 врагов; асинхронность добавит гонок больше, чем сэкономит времени. Но помним: если сетка станет большой, без джобов не обойтись |
| `X2TacticalVisibilityHelpers::FillPathTileData` | превью пути: список видимых игроку врагов строится ОДИН раз и переиспользуется; «remove any enemies not visible to the player» | подтверждение нашего `ATacticalPlayerController::GetMovePreviewAt` (фильтрация по `Fog->IsActorCurrentlyVisible`) + требование кэшировать список | их же комментарий: наивный путь ~20 мс на вызов |
| `X2Condition_Visibility` (`bRequireLOS`, `bRequireBasicVisibility`, `bRequireGameplayVisible`, `bAllowSquadsight`, `bVisibleToAnyAlly`, `bDisablePeeksOnMovement`) и наборы `LivingGameplayVisibleFilter` / `LivingBasicVisibleFilter` / `LivingLOSVisibleFilter` | один предикат с параметрами вместо десяти проверок по месту | принцип: **одна функция с параметрами**, а не новый предикат на каждого потребителя | прямо отвечает правилу брифа «второй предикат = ошибка» |
| `bDisablePeeksOnMovement` («Exceptions are, for example, over watch») | реакционный выстрел не пользуется пик-точками против движущейся цели | **записано в бэклог, не в туман** | это правило Overwatch, а не видимости; трогать `GA_Overwatch` в рамках тумана нельзя |
| `bAllowPeeksForNonCoverUnits=false` (`XComGameCore.ini`) | юнит без укрытия не получает пик-точек | подтверждение нашего правила «peek строится только от зафиксированной `ActiveCover`» | совпадает с [13 §2](13_LOS_TARGETING.md) |
| Мод **Gotcha Again**, `LOSUtility_GA::GetLOSValues` | «увидят ли меня оттуда»: вызывает **штатный** `XComWorldData.CanSeeTileToTile(SourceTile, TargetTile, out VisibilityInfo)` из гипотетической клетки, сам считает только `DefaultTargetDist < SightRange`, флаг `DisallowStepout`, обратная проверка для юнитов без укрытия | подтверждение: превью обязано звать **ту же** геометрию с подменённой точкой глаз | у нас это уже так — `HasLineOfSightFromLocation(World, EyeAtPoint, Enemy, SelectedUnit)`. Мод не изобретает видимость, он переиспользует движковую — ровно то, что требует бриф |
| Мод Gotcha Again, `CacheUtility_GA` | кэширует **UI-объекты** (иконки, флаги), но не результаты видимости | ничего | проверено: ценности для нас нет, LOS он пересчитывает каждый раз |
| Highlander (`X2WOTCCommunityHighlander`) | из всей видимости тронуты только `X2Action_RevealAIBegin` (Issue #1492 — повторное проигрывание нарратива) и `UIChosenReveal` | вывод: сообщество за 8 лет **не чинило правила видимости**, только презентацию обнаружения | значит слой правил стабилен, а риск сосредоточен в моменте reveal — туда и класть внимание |

---

## 4. Что уже закрыто ПО ПОСТРОЕНИЮ (не писать код)

Разбор показал, что часть пунктов «известных каналов утечки» закрыта существующей
механикой. Это записано, чтобы никто не «чинил» их повторно.

**Выстрел скрытого врага не может выдать позицию.** Доказательство из трёх
фактов проекта:

1. `UGA_Attack::GetTargetStatus` требует геометрический LOS **всегда** и
   дистанцию ≤ `SquadVisionRange` (2500), если у стрелка нет squadsight;
2. `bHasSquadsight = true` только у снайпера игрока (`UnitClasses.cpp`), у
   мародёра и остальных врагов — `false` (дефолт `AUnitBase`);
3. инвариант Ф5: «A видит B ⟺ B видит A» держится по построению наборов точек.

Отсюда: враг, который стреляет, находится ≤2500 см от цели и имеет с ней
взаимный LOS ⇒ цель — живой боец отряда ⇒ `UFogOfWarSubsystem::IsActorCurrentlyVisible`
для этого врага истинна. Значит вспышка, трассер, звук выстрела, floating-урон
и кадр камеры выстрела **не могут** сработать от невидимого врага. Гейт в
`PlayShotVfx`/`PlayUnitSound(Fire)` нужен как страховка (assert/лог), а не как
механизм.

**Прицеливание не может раскрыть врага.** `CanTargetActor` ⊂ «видим отряду»: он
требует LOS от самого стрелка в радиусе 2500 (это подмножество условия тумана,
стрелок — член отряда) либо squadsight через союзника (тот тоже член отряда с
LOS в 2500). Поэтому `GetAttackTargets`, `HasAnyValidTarget` и серость кнопки
«Огонь» уже безопасны. Отдельного fog-фильтра в них добавлять **не нужно** —
это был бы второй предикат.

**Move preview уже фильтрует.** `ATacticalPlayerController::GetMovePreviewAt`
пропускает врагов через `Fog->IsActorCurrentlyVisible` — это в точности то, что
делает `FillPathTileData` в XCOM.

**Камера вражеского хода уже гейтится.** `HandleEnemyUnitActivated` +
отложенный подхват в `PlayerTick` (`PendingEnemyCameraUnit`) — поведение XCOM
«показывать ход с момента обнаружения, а не с его начала».

**Что при этом действительно течёт** — см. §5.

---

## 5. Дефекты текущего F0 и точный список мест под гейт

Собрано чтением кода 2026-08-03. Пункты с пометкой **[F0-N]** продублированы
задачами в [04_ROADMAP.md](04_ROADMAP.md) §4.

### 5.1 Подтверждённые утечки

| # | Место (файл → функция) | Что течёт | Что делать |
|---|---|---|---|
| **F0-1** | `Tactics/TacticalPlayerController.cpp` → `UpdateHoverHighlight` | наведение на скрытого врага зажигает custom-depth обводку (`AUnitBase::SetHoverHighlight`), делает его `HoveredUnit` и наполняет панель цели (`UTacticalHUDWidget::UpdateTargetPanel`) | враг, невидимый отряду, не может стать `NewHovered`. Один `if` в одном месте — обводка, панель и Tab-цикл починятся сами |
| **F0-2** | `UI/TacticalHUDWidget.cpp` → нет владельца; `Characters/TDCombatant.cpp` → `SetupUnitHUD` | оверхед-худ врага создаётся в `BeginPlay` и никем не гасится: `ATacticalPlayerController::UpdateSquadOverheadVisibility` управляет **только** отрядом игрока. HP-бар висит над скрытым врагом | оверхед врага подчиняется тому же декларативному правилу, что и отряд, плюс fog-гейт. Прятать через `SetOverheadHUDVisible` (не `SetVisibility` — см. §2.5) |
| **F0-3** | `Tactics/FogOfWarSubsystem.cpp` → `GetCurrentlyVisibleEnemies` / `GetCurrentlyVisibleEnemyCount` | нет кэша: каждый вызов — `N_отряда × N_врагов` сферо-свипов синхронно. Потребители: биндинг HUD (`GetVisibleEnemyCount`) и **повторяющийся таймер** `UTacticalHUDWidget::RefreshActiveEnemyCardVisibility` (`EnemyCardVisibilityCheckInterval`) | кэш видимых акторов + событийная инвалидация + `OnActorVisibilityChanged`; потребители читают кэш. Референс цены — 20 мс на наивный вызов у Firaxis |
| **F0-4** | `Tactics/TacticsCombatStatics.cpp` → `SquadHasLineOfSight` **против** `Tactics/FogOfWarSubsystem.cpp` → `IsActorCurrentlyVisible` | **два источника правды**: обе функции считают «видит ли кто-то из стороны цель» — живость + 2500 + LOS. Отличия только в `Ally != Unit` и в том, что fog жёстко берёт сторону игрока | вынести геометрию в один статик `UTacticsCombatStatics::AnyUnitOfSideSees(Side, Target, Exclude)`. Squadsight и AI зовут статик; `UFogOfWarSubsystem` остаётся **единственной** player-facing обёрткой с кэшем. Так и «одна реализация», и правило «AI не читает fog игрока» — оба соблюдены |
| **F0-5** | `Tactics/GA_Overwatch.cpp` → активация Overwatch, `Feedback->ShowStatusText(Unit, …)` | скрытый враг, уходящий в наблюдение, печатает всплывающий статус над невидимой позицией | `UCombatFeedbackSubsystem::PushEntry` отбрасывает записи, чей `Anchor` невидим отряду (одно место на все виды текста) |
| **F0-6** | `Tactics/UnitAIController.cpp` → `PlayUnitSound(EUnitSoundEvent::MoveSettled)`; `UAnimNotify_UnitFootstep` | шаги и «занял позицию» скрытого врага звучат 3D-звуком с аттенюацией — точная позиция на слух | не-боевые звуки скрытого юнита не проигрываются. Владелец гейта — `AUnitBase::PlayUnitSound` (одна точка), с явным белым списком событий, которые звучат всегда |
| **F0-7** | `UI/TacticalHUDWidget.cpp` → блок «пассивка Осы»: `UGA_Attack::CanTargetActor(Selected, Enemy) && !UTacticsCombatStatics::HasLineOfSight(Selected, Enemy)` | условие **мертво**: после ревизии 2026-07-31 `GetTargetStatus` требует геометрический LOS всегда, поэтому `!HasLineOfSight` при `CanTargetActor == true` недостижимо. Кнопка-индикатор Squadsight серая навсегда | индикатор должен гореть по признаку «цель дальше собственного обзора, обнаружена союзником»: `Distance > SquadVisionRange && CanTargetActor(...)`. Не fog, но найдено при разборе модели обнаружения |

### 5.2 Принятые «честные подсказки» (менять не будем, записано осознанно)

| Место | Что видно | Почему оставляем |
|---|---|---|
| `Tactics/MoveRangeVisualizer.cpp` → `BuildDistanceField` (через `UTacticsCombatStatics::GetUnitObstacles`) и `AdjustGoalOutOfUnits` | зона хода и превью пути огибают невидимого врага | иначе приказ «встать внутрь врага» станет возможным. XCOM ведёт себя так же (неразведанный под блокирует тайлы) |
| Тени/звук окружения | — | вне бюджета демо |

### 5.3 Места, которые обязаны получить **override**, а не гейт

Если гейт поставить наивно, сломается обучение:

| Место | Почему нужен override |
|---|---|
| `Tactics/TacticalQuestTasks.cpp` → `FTacticalTask_ScriptedMove` (`bCameraFollowUnit` → `Camera->SetFollowTarget(Unit)`) | сценарная перебежка Осы/врага показывается намеренно |
| `Tactics/TacticalQuestTasks.cpp` → `FTacticalTask_ScriptedEnemyTurn` (та же связка + арбитраж с реакцией) | ход Holo_D в C0/C1 — постановка, её обязан видеть игрок |
| `Tactics/TutorialPresentation.cpp` → фокус беата | реплика показывает точку/актора |
| `Tactics/ScenarioActorRegistry.cpp` → `SetActorScenarioActive` | staged-актор уже скрыт своим механизмом; туман не должен пытаться его «показать» при reveal |
| objective-акторы (бомба, зона эвакуации) | правило `AlwaysKnown` из данных сценария |

Формально это `ForceModelVisible` из XCOM: перечисление `eForceVisible /
eForceNotVisible / eForceNone` на скрываемом компоненте, которое побеждает
расчёт. Одна точка приоритета — не набор `if`-ов по месту.

---

## 6. Пересобранный план

### F0 — единый предикат (сделано, но с долгами)

- [x] `UFogOfWarSubsystem` и перевод squad visibility / камеры врага / move preview на него.
- [x] Безопасный `GetVisibleEnemyCount` для HUD.
- [ ] Закрыть дефекты **F0-1 … F0-7** (§5.1).
- [ ] Functional tests: радиус, LOS, объединение зрения двух бойцов, вклад Downed (его быть не должно).

### F1 — actor gating, кэш и события *(обязательный объём демо)*

- [ ] `UTacticsCombatStatics::AnyUnitOfSideSees(...)` — одна реализация геометрии;
      `SquadHasLineOfSight` и `UFogOfWarSubsystem` становятся её вызовами (F0-4).
- [ ] Кэш видимых акторов в подсистеме + `OnActorVisibilityChanged(Actor, bVisible)`
      + `ResetForScenario(ScenarioId, RunId)`.
- [ ] Триггеры пересчёта: конец шага движения и settle, spawn/`SetActorScenarioActive`,
      смерть/Downed/подъём/эвакуация, смена фазы хода, дверь. Во время бега —
      не чаще `FogMovingRecheckInterval` (0.1 с). Ноль работы в «пустом» кадре.
- [ ] `UFogVisionComponent` (источник зрения: радиус, высота глаз, участие) —
      бойцам игрока; **не** врагам.
- [ ] `UFogRevealableComponent` (владелец презентации, §2.5 + `ForceModelVisible`).
- [ ] Перевести на API подсистемы: hover, оверхед врага, панель цели, floating
      feedback, не-боевые звуки, автофокус камеры.
- [ ] Коллизия/occupancy не трогаются (§2.5, §5.2).
- [ ] Момент обнаружения: короткое удержание камеры и пауза «первого знакомства»
      (стартовые числа XCOM — `FirstSightedDelay = 0.75`).
- [ ] Логи в `LogXRU1Fog`: кто, кого, по какому источнику и по какому событию
      стал видим/скрыт.

**DoD F1:** скрытый враг существует, ходит, стреляет по правилам, но не оставляет
ни одного визуального, UI, звукового, input- или camera-сигнала до обнаружения;
туториал проходится без изменений в `ST_Quest_Tutorial`.

### F2 — сетка `Unknown/Explored/Visible` *(понижено до опционального)*

Основание понижения — §2.1. Делать только после F1 и только если остаётся время.

- [ ] `UFogOfWarConfigDataAsset` + `AFogOfWarBoundsVolume`: origin, extents, cell
      size (старт 100 см), `bStartFullyExplored`, `FloorBands` (пока одна полоса).
- [ ] **Запечь** битовую маску блокеров один раз на старте сценария; DDA по
      массиву. Никаких физических трейсов на клетку в рантайме.
- [ ] Два `TBitArray` на полосу: `CurrentVisible`, `Explored` (`Explored |= CurrentVisible`).
- [ ] Batching: несколько перемещений за кадр — один пересчёт.

**Снято из прежнего плана и почему:**

- Z-bands и «активный этаж в RT» — §2.4 (на карте нет боевого второго этажа;
  гейт акторов трёхмерен и без них).
- Требование «точная видимость actor остаётся дополнительным LOS» — не пункт
  работы, а инвариант: сетка вообще не участвует в решениях (§1).

### F3 — Render Target и post-process *(опционально, после F2)*

- [ ] Один `RT_Fog_Showreel` (`R8G8B8A8`, sRGB off, Clamp, старт 256×256):
      канал текущей видимости + канал `Explored`.
- [ ] Обновление по грязному прямоугольнику, не каждый кадр; сглаживание края во
      времени (аналог `FOWEnvelopeSpeed`).
- [ ] `M_PP_FogOfWar`: `Blendable Location = Scene Color Before DOF`, позиция мира
      как `Camera Relative World Position + Camera Position` (§2.3).
- [ ] Blendable добавляется в `BP_TacticalCameraPawn` **после** `M_OutlinePP`.

### F4 — сценарии и исключения *(частично обязателен)*

- [x] Mid-combat save в scope демо не поддерживается.
- [ ] **(обязательно)** `ResetForScenario` вызывается Director'ом до fade-in;
      кэш и подписки не переживают `ScenarioRunId`.
- [ ] **(обязательно)** Правила objective: `AlwaysKnown` / `RevealWhenVisible`
      из `UTacticalScenarioDataAsset`; враг — всегда `RevealWhenVisible`.
- [ ] **(обязательно)** `ForceModelVisible`-override для сценарных тактов (§5.3).
- [ ] Initial reveal сценария — источником зрения без юнита (аналог
      `XComGameState_SquadViewer` / `CreateFOWViewer`), не Level Blueprint.
- [ ] Debug: `xru1.Fog.Explain` — почему актор виден/скрыт, каким источником,
      каким событием обновлён.
- [ ] Last-known markers — **не делаем** в демо (GDD §5.9 их и не обещает).

### F5 — оптимизация и регрессия

- [ ] Профиль в Insights на максимальном составе; цель — один полный пересчёт
      ≈1 мс и ноль работы в кадрах без событий (§2.2).
- [ ] Матрица §8 целиком, включая Tutorial → Mission01 и retry.
- [ ] Точный CPU-предикат работает и при выключенном post-process (Low).

---

## 7. Риски и порядок работ

**Порядок.** F0-долги → F1 → (F4 обязательная часть) → пауза и оценка времени →
F2/F3 только если демо уже проходится.

| Риск | Вероятность | Защита |
|---|---|---|
| Гейт видимости прячет актора, которого обязан показать такт обучения | **высокая** — самый вероятный способ сломать демо | `ForceModelVisible`-override (§5.3) вводится **в том же коммите**, что и гейт, а не после. Прогон туториара A1–D3 — часть DoD F1 |
| Юнит прячется посреди монтажа (выстрел, смерть, эвакуация) | высокая | правило XCOM: `bRemovedFromPlay` не прячет, пока текущее действие не завершено. Скрытие откладывается до терминала действия (у нас терминалы уже есть — fire-action, `ReturnToAnchor`) |
| Кэш переживает `ScenarioRunId` и показывает врагов прошлого запуска один кадр | средняя | `ResetForScenario` до fade-in — обязательный пункт F4, а не «когда-нибудь» |
| Второй источник правды заводится незаметно (кто-то пишет свой `if HasLineOfSight` в новом виджете) | средняя | F0-4 сводит геометрию в один статик; ревью-правило: player-facing вопрос — только через `UFogOfWarSubsystem` |
| Оверхед-худ «замерзает» после скрытия | средняя — уже было в v2.8 | только `SetHiddenInGame` на `WidgetComponent`, никогда `SetVisibility` |
| Скрытая коллизия выдаёт врага маршрутом | принята | §5.2 — осознанная плата, как в XCOM |
| Сетка съедает кадр наивными трейсами | высокая, если делать F2 «в лоб» | запекание блокеров + DDA (§2.1); при первом же профиле хуже 1 мс — F2 откладывается |
| Материал тумана и гейт расходятся на краю стены | средняя | картинка никогда ничего не разрешает; расхождение границы — косметика |
| Многоэтажность | низкая сейчас | пересмотреть §2.4, как только в сценарии появится враг над врагом по тем же X/Y |

**Что можно не делать в демо:** сетка, RT, post-process, Z-bands, last-known
markers, mid-combat save, дистанционный штраф squadsight, отдельный
`DetectionRadius`, асинхронный солвер видимости.

---

## 8. Acceptance matrix

| Сценарий | Ожидаемый результат |
|---|---|
| Враг в радиусе, но за глухой стеной | меш, оружие, оверхед, обводка скрыты; hover не берёт его; панель цели пуста; counter не меняется |
| Наведение курсора на скрытого врага | ничего не подсвечивается, панель цели не появляется (**F0-1**) |
| Враг выходит из-за угла | один reveal-event; актор и UI появляются; камера имеет право показать его ход |
| Враг снова теряет LOS | актор полностью скрыт; живой обводки нет |
| Скрытый враг уходит в Overwatch | всплывающего статуса нет (**F0-5**) |
| Скрытый враг бежит и занимает позицию | шагов и «занял позицию» не слышно (**F0-6**) |
| Скрытый враг стреляет | невозможно по построению (§4); если случилось — это баг Ф5, а не тумана |
| Два бойца видят разные зоны | видимость = объединение источников |
| Источник умер / Downed / эвакуирован | его вклад исчезает после одного пересчёта |
| Preview движения рядом со скрытым врагом | нет счётчиков угрозы/фланга; зона хода огибает его (принято, §5.2) |
| Враг становится видим во время своего бега | reveal происходит на бегу, не позже `FogMovingRecheckInterval` |
| Труп / Downed | остаются видимыми независимо от LOS (правило XCOM, §2.5) |
| Сценарный такт показывает врага вне LOS | override работает, такт не ломается (§5.3) |
| Кадр выстрела / смерти | юнит не исчезает посреди монтажа |
| Tutorial → Mission01 на `Main_Map_Showreel` | новая fog-сессия; кэш и подписки прошлого run не переносятся |
| Retry сценария | нет видимых один кадр врагов прошлого запуска |
| Low scalability / fog material off | механические гейты продолжают работать |
| Профиль | один полный пересчёт ≈1 мс; в кадре без событий пересчётов нет |

---

## 9. РУЧНАЯ НАСТРОЙКА в Unreal Editor

### Сразу после F1

1. `WBP_TacticalHUD`: счётчик врагов должен звать `GetVisibleEnemyCount`, а не
   `GetAliveEnemyCount`. Проверить в PIE: враг за глухой стеной счётчик не растит.
2. Добавить `FogVisionComponent` четырём бойцам игрока. Врагам — **не** добавлять.
3. Добавить `FogRevealableComponent` врагам и скрываемым objective-акторам;
   у сценарных акторов проставить `ForceVisibility` там, где такт обязан их показать.
4. В `DA_Scenario_Tutorial` / `DA_Scenario_Mission01` задать правила objective
   (`AlwaysKnown` / `RevealWhenVisible`) и initial reveal.

### Только если делаем F2/F3

1. `Project Settings → Collision`: канал `FogVision`, Default Response `Ignore`;
   `Block` — непрозрачной архитектуре, крупным укрытиям, закрытым дверям, земле;
   `Ignore` — пешкам, VFX, мелкому декору. Стекло — решить явно и одинаково.
2. `DA_Fog_Showreel` (`UFogOfWarConfigDataAsset`, кладётся в
   `/Game/XRU1Game/Data/Core` — 06_CONVENTIONS §3): cell size 100 см, bounds только
   вокруг тактической зоны, `bStartFullyExplored = true` для туториала.
3. Один `BP_FogOfWarBoundsVolume` на `Main_Map_Showreel`, локальные X/Y выровнены с
   материалом.
4. `RT_Fog_Showreel` (`R8G8B8A8`, sRGB off, Clamp, 256×256 → 512 только по профилю).
5. `M_PP_FogOfWar` (`Material Domain = Post Process`, `Blendable Location = Scene
   Color Before DOF`), инстанс `MI_PP_FogOfWar`; текстура назначается runtime
   dynamic instance (MPC не хранит texture parameters).
6. В `BP_TacticalCameraPawn → PostProcess → Blendables` добавить туман **после**
   `M_OutlinePP` и проверить, что скрытый враг не проступает обводкой ни при
   каком порядке.

---

## 10. Источники

**Исходники XCOM 2** (`Rukan/XComGame`, полный дамп UnrealScript, файлы в корне):
`X2GameRulesetVisibilityDataStructures.uc`, `X2GameRulesetVisibilityManager.uc`,
`X2GameRulesetVisibilityInterface.uc`, `X2VisibilityObserver.uc`,
`X2TacticalVisibilityHelpers.uc`, `X2Condition_Visibility.uc`,
`XComWorldData.uc`, `XComGameState_Unit.uc`, `XComGameState_SquadViewer.uc`,
`X2Action_InitFOW.uc`, `X2Action_UpdateFOW.uc`, `X2Action_RevealArea.uc`,
`X2Action_RevealAIBegin.uc`, `X2AbilityToHitCalc_StandardAim.uc`,
`X2Effect_Squadsight.uc`.

**Конфиги** (`russgray/xcom2-config`): `XComGameData_CharacterStats.ini`
(`eStat_SightRadius=27`, `eStat_DetectionRadius=9…15`), `XComGameCore.ini`
(`SQUADSIGHT_CRIT_MOD=-10`, `SQUADSIGHT_DISTANCE_MOD=-2`,
`BLEEDOUT_SIGHT_RADIUS=3`, `bAllowPeeksForNonCoverUnits=false`),
`XComCamera.ini` (`FirstSightedDelay=0.75`), `XComEngine.ini`
(`FOWEnvelopeSpeed=170`, `bForceLowResFOW`, `bHighQualityFOWFiltering`).

**Моды:** [Chaser324/XCOM2-GotchaAgain](https://github.com/Chaser324/XCOM2-GotchaAgain)
(`LOSUtility_GA.uc`, `CacheUtility_GA.uc`) — проверено: надстройка над штатным
`CanSeeTileToTile`, своей видимости не заводит.
[X2CommunityCore/X2WOTCCommunityHighlander](https://github.com/X2CommunityCore/X2WOTCCommunityHighlander)
— в видимости тронуты только `X2Action_RevealAIBegin` и `UIChosenReveal`.

**Исходники движка UE 5.7** (проверка, а не пересказ):
`Runtime/Engine/Private/Components/SceneComponent.cpp` →
`USceneComponent::ShouldRender()` (наследование скрытия через
`Owner->GetParentComponent()`);
`Runtime/UMG/Private/Slate/SWorldWidgetScreenLayer.cpp` (экранный виджет не
смотрит на `Owner->IsHidden()`);
`Runtime/UMG/Private/Components/WidgetComponent.cpp`.

**Практика UE:**
[Epic — Post Process Materials](https://dev.epicgames.com/documentation/unreal-engine/post-process-materials-in-unreal-engine?lang=en-US),
[Epic — Collision Settings](https://dev.epicgames.com/documentation/unreal-engine/collision-settings-in-the-unreal-engine-project-settings),
[Epic — Custom Primitive Data](https://dev.epicgames.com/documentation/en-us/unreal-engine/storing-custom-data-in-unreal-engine-materials-per-primitive),
[Epic Forums — post-processed fog of war для пошагового проекта](https://forums.unrealengine.com/t/is-post-processed-fog-of-war-possible-for-a-turn-based-tile-based-project/2719016)
(починка «плавающего» тумана: `Scene Color Before DOF` + `Camera Relative World
Position + Camera Position`),
[Epic Forums — Widget Component и SetHiddenInGame](https://forums.unrealengine.com/t/widget-component-and-sethiddeningame/345169).

**Внутренние документы:** [01_GDD.md §5.9](01_GDD.md),
[13_LOS_TARGETING.md](13_LOS_TARGETING.md),
[11_SHARED_MAP_TUTORIAL.md](11_SHARED_MAP_TUTORIAL.md),
[04_ROADMAP.md §4](04_ROADMAP.md).
