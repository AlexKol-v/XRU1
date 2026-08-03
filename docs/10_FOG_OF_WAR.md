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
- допускается **один** полный пересчёт на событие и **ноль пересчётов** в кадрах,
  где ничего не произошло. Буквально ноль работы не выходит: в тике остаётся
  проверка «бежит ли кто-нибудь» — обход двух массивов юнитов со сравнением
  статуса path following, десятки наносекунд. Это сознательный размен: иначе
  пришлось бы завести подписки на старт/финиш каждого перемещения и получить
  третьего владельца знания о движении;
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

### 2.6 Как игрок понимает, куда идти

**Решение. Туман прячет ПРОТИВНИКА, а не ЦЕЛЬ.** Бомба, зона эвакуации, зоны
сценария и любой objective видны всегда и с самого начала. Специальный механизм
для этого не нужен: они не участвуют в тумане, и предикат отвечает про них
«видно» по правилу, а не по случайности (§5.4).

**Почему так — это ровно модель XCOM 2, проверено по исходникам:**

1. `XComGameState_InteractiveObject::ForceModelVisible()` возвращает
   **`eForceVisible` безусловно**, без единой проверки. Рядом
   `CanEverSee() = FALSE`, `CanEverBeSeen() = TRUE`: цель миссии, дверь, терминал —
   объекты, которых видно всегда и которые сами не видят.
2. `XComGameState_EvacZone` вообще **не реализует** `X2GameRulesetVisibilityInterface`
   (только `X2VisualizedInterface`) — зона эвакуации не является участником
   системы видимости, её актор `X2Actor_EvacZone` рисуется всегда.
3. Поверх этого XCOM ещё и **подсвечивает** нужный объект: «objective glint» —
   отдельный шейдер на интерактивном объекте, включаемый по ходу миссии
   (`SeqAct_SetObjectiveShaderEnabled` → `SetRequiresObjectiveGlint` → событие
   `ObjectiveGlintStatusChanged`). То есть игроку показывают не только «где
   объект», но и «какой из них сейчас нужен».
4. И **указывает направление**: `UISpecialMissionHUD_Arrows` +
   `XComGameState_IndicatorArrow` — 3D-стрелки к актору или точке, прижатые к
   краю экрана (`ScreenEdgePadding`), когда цель за кадром.
5. Плюс текстовый список целей `UIObjectiveList`.

**Что из этого у нас уже есть:** цель всегда видна (п. 1–2, по построению);
кольцо радиуса у бомбы и постоянное кольцо зоны эвакуации по `ZoneRadius`
(аналог glint); беат D1 наводит камеру на зону эвакуации; текст цели — в трекере
обучения. **Чего нет:** экранных стрелок к цели за кадром (п. 4). Для демо это
не блокер — тактическая зона помещается в кадр, — но если на плейтесте
«непонятно, куда идти» повторится, брать надо именно этот механизм, а не
ослаблять туман.

**Отдельно про затемнение местности.** Поскольку `Unknown`/`Explored` в демо
выключены профилем (§2.1), карта видна целиком с первого кадра. То есть этот
слой **вообще не ухудшает** ориентирование: игрок всегда видит рельеф, укрытия,
бомбу и зону эвакуации, и не видит только живых противников вне зрения отряда.

### 2.7 Полная сверка с XCOM 2: что учтено и чего нет

Построчная ревизия механики (2026-08-03). «Есть» означает: реализовано и собрано.

| # | Механика XCOM 2 | У нас | Статус |
|---|---|---|---|
| **Правила** | | | |
| 1 | Кэш видимости на пару source×target (`GameRulesCache_VisibilityInfo`) | кэш «актор → виден отряду» (односторонний) | **есть, сознательно уже**: знание отдельного врага — это перцепция AI, отдельная система |
| 2 | Три уровня: `bClearLOS` / `bVisibleBasic` / `bVisibleGameplay` | `GetTargetStatus`: геометрия → радиус → squadsight | есть |
| 3 | Радиус обзора `eStat_SightRadius = 27` м | `SquadVisionRange = 2500` см | есть, числа совпадают |
| 4 | Squadsight: штраф `−2` за тайл сверх обзора + `−10` крит | плоские `−10` | **не берём**: дистанционная кривая уже есть отдельно (`GetAimDistanceModifier`), второй штраф был бы двойным счётом |
| 5 | До 25 трейсов на пару (5 точек × 5 точек) | до 16 (4 × 4) | есть, достаточно |
| 6 | Ранний выход по радиусу (`bBeyondSightRadius`) | дистанция проверяется до трейсов | есть |
| 7 | Условия-фильтры (`X2Condition_Visibility`, пять наборов) | один предикат | **не нужно**: у нас один потребитель-игрок, параметризовать нечего |
| 8 | Источник зрения без юнита (`XComGameState_SquadViewer`) | шов в `RecomputeNow` | **не нужно сейчас**, добавляется одной функцией |
| 9 | `ForceModelVisible`: свои, тела, matinee, скампер, removed-from-play | перенесено целиком в `ResolveDesiredVisibility` | есть |
| **Презентация** | | | |
| 10 | `SetVisibleToTeams` — одно скрытие визуализатора | `SetActorHiddenInGame` + экранный виджет + custom depth | есть (в UE одного вызова нет, разобрано в §2.5) |
| 11 | Тела и лежачие видны всегда | то же правило | есть |
| 12 | Не прятать, пока идёт действие | отложенное скрытие по монтажу | есть |
| 13 | Пересчёт видимости бегущей пешки на каждом тайле | троттлинг 0.1 с + выдержка перед скрытием 0.35 с | **есть, эквивалент**: у нас позиция непрерывная, а не тайловая, поэтому от мигания защищает выдержка, а не дискретность сетки |
| 14 | Сверка визуализаторов на простое (`OnVisualizationIdle`) | — | **не нужно**: применение идемпотентно и переутверждается на каждом пересчёте, расходиться нечему |
| **Каналы утечки** | | | |
| 15 | Флаги юнитов над головой | оверхед-худ врага | есть |
| 16 | Всплывающий текст | гейт в `PushEntry` | есть |
| 17 | Звук | гейт в `PlayUnitSound` | есть |
| 18 | Камера следует только за видимым | подписка в обе стороны | есть |
| 19 | Превью пути считает только видимых врагов (`FillPathTileData`) | `GetMovePreviewAt` | есть |
| 20 | Неразведанный противник продолжает блокировать тайлы | occupancy не трогаем | есть (осознанная «честная подсказка») |
| **Читаемость: куда идти** | | | |
| 21 | Цели миссии не прячутся никогда | у objective нет fog-компонента | есть (§2.6) |
| 22 | «Objective glint» — подсветка нужной цели | кольца у заряда и зоны эвакуации | есть |
| 23 | Стрелка к цели за кадром (`UISpecialMissionHUD_Arrows`) | `UObjectivePointerSubsystem` | **добавлено 2026-08-03** |
| 24 | Счётчик на стрелке (`CounterValue`) | остаток ходов у указателя на заряд | добавлено |
| 25 | Список целей (`UIObjectiveList`) | трекер обучения и таймер в HUD | есть |
| 26 | Стрелки прячутся, пока поднято меню выстрела | тот же предикат, что прячет оверхеды отряда | добавлено |
| **Осознанно НЕ берём** | | | |
| 27 | Тайловая сетка + FOW-текстура + затемнение местности | — | отложено в полировку (§2.1) |
| 28 | Кинематика обнаружения пода: скампер, `FirstSightedDelay = 0.75`, `RevealFOWRadius = 768` | — | **не берём**: подов у нас нет, а обучение уже ведёт режиссура тактов — вторая система, забирающая камеру, конфликтовала бы с ней. Пересмотреть, если появится незаскриптованный бой с несколькими группами |
| 29 | Concealment и `eStat_DetectionRadius` | — | вне демо |
| 30 | `XComLevelActor::HideableWhenBlockingObjectOfInterest` — гашение геометрии, загораживающей объект интереса | — | **это камера/окклюзия, а не туман**; отдельный пункт бэклога, если стены будут мешать смотреть на цель |
| 31 | Асинхронный многопоточный солвер видимости | синхронный пересчёт | не нужно: 4 бойца × 6 врагов |

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
| `XComGameState_InteractiveObject::ForceModelVisible()` → **`eForceVisible` безусловно**; `CanEverSee()=FALSE`, `CanEverBeSeen()=TRUE` | цель миссии, дверь, терминал видны ВСЕГДА и сами не видят | правило «туман скрывает только то, что ему поручили»: у objective-акторов нет `UFogRevealableComponent`, предикат отвечает про них «видно» | это и есть ответ на «как понять, куда идти»: XCOM не прячет цели вообще |
| `XComGameState_EvacZone` реализует только `X2VisualizedInterface`, но **не** `X2GameRulesetVisibilityInterface`; актор — `X2Actor_EvacZone` | зона эвакуации вне системы видимости | наш `AEvacZone` — обычный `AActor` без fog-компонента, ведёт себя так же | совпадение не случайное: зону, до которой надо дойти, прятать нельзя ни при каком тумане |
| «Objective glint»: `SeqAct_SetObjectiveShaderEnabled` → `XComGameState_InteractiveObject::SetRequiresObjectiveGlint` → событие `ObjectiveGlintStatusChanged` | подсветка ИМЕННО текущей цели поверх «видно всё» | идею «мало показать объект — надо показать, какой из них сейчас нужен» | у нас роль glint играют кольцо радиуса бомбы и кольцо зоны эвакуации |
| `UISpecialMissionHUD_Arrows` + `XComGameState_IndicatorArrow` (3D-стрелка к актору/точке, прижатая к краю экрана, `ScreenEdgePadding`) | указатель направления на цель за кадром | **пока не берём** | тактическая зона демо помещается в кадр; это первый механизм к внедрению, если плейтест покажет «непонятно, куда идти» |
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

### 5.2a Контракт «кем туман НЕ управляет»

Ошибиться здесь легко, поэтому правило записано явно.

Туман управляет ровно теми акторами, у которых есть `UFogRevealableComponent`.
Компонент создаётся дефолтным субобъектом в `AUnitBase` — значит **юнитами, и
только ими**. Всё остальное — `ABombObjective`, `AEvacZone`,
`ATacticalQuestZone`, декор, маркеры — обычные `AActor` без компонента, и для них
`UFogOfWarSubsystem::IsActorCurrentlyVisible` возвращает **true по правилу**
(§2.6). Это делает «цель миссии видна всегда» гарантией, а не побочным эффектом.

⚠️ **Не добавлять `UFogRevealableComponent` на цели миссии и зоны.** Один
добавленный в редакторе компонент — и зона эвакуации начнёт исчезать, когда её
никто не видит. Если объекту действительно нужен компонент (например, чтобы
использовать `Override`), ставить `Override = AlwaysVisible` в том же действии.

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
- [x] Закрыты дефекты **F0-1 … F0-7** (§5.1) — реализация 2026-08-03, см. F1.
- [ ] Functional tests: радиус, LOS, объединение зрения двух бойцов, вклад Downed (его быть не должно).

### F1 — actor gating, кэш и события *(обязательный объём демо)*

**Реализовано 2026-08-03, собрано.** Состав слоя:

- [x] `UTacticsCombatStatics::AnyUnitSees(Viewers, Target, Exclude)` — одна
      реализация геометрии «сторона видит цель»; `SquadHasLineOfSight` стала её
      обёрткой, подсистема зовёт её же (F0-4). Порядок проверок: живость →
      дистанция (квадраты) → сферо-свипы.
- [x] `UFogOfWarSubsystem` переведён на `UTickableWorldSubsystem`: кэш
      `TObjectKey<AActor> → bool`, событие `OnActorVisibilityChanged`,
      `MarkVisibilityDirty(Reason)` с коалесингом за кадр, `ResetForScenario`.
- [x] Три режима тика и ни одного лишнего кадра: событие → один пересчёт;
      кто-то в пути или есть отложенное скрытие → троттлинг
      `xru1.Fog.MoveRecheck` (0.1 с); движение кончилось → один точный пересчёт;
      иначе работы ноль.
- [x] Триггеры: `BeginPhase`, `RegisterUnitInCombat`/`UnregisterUnitFromCombat`,
      `AUnitBase::Die`/`SetDowned`/`Evacuate`, `SetActorScenarioActive`,
      регистрация нового скрываемого. Движение покрыто режимом «в пути» —
      отдельных хуков в AI не потребовалось.
- [x] `UFogRevealableComponent` — владелец презентации, дефолтный субобъект
      `AUnitBase` (`FogRevealable`). Скрывает `SetActorHiddenInGame` (он же
      накрывает оружие — Child Actor), гасит экранный оверхед-худ и Custom Depth.
- [x] Приоритеты решения в одном месте (`ResolveDesiredVisibility`): сценарное
      удержание → `Override` → сценарно неактивен → тело/Downed видимы всегда →
      расчётная видимость. Это наш `ForceModelVisible`.
- [x] Скрытие откладывается, пока актор доигрывает монтаж (`CanHideNow`) — юнит
      не исчезает посреди выстрела или смерти.
- [x] **Выдержка перед скрытием** `xru1.Fog.HideGrace` (0.35 с): появление
      мгновенное, исчезновение с паузой. Видимость считается по непрерывной
      позиции, а не по тайлам, поэтому боец, бегущий вдоль линии обзора, иначе
      мигал бы несколько раз в секунду (и засыпал бы журнал парами
      «СКРЫТ/ПОКАЗАН»). Асимметрия та же, что у перцепции AI в этом проекте:
      `SightRadius 2500` против `LoseSightRadius 2800` — увидеть легче, чем
      потерять.
- [x] Сторона игрока туманом не ведётся вовсе: её оверхедами по-прежнему владеет
      `UpdateSquadOverheadVisibility` (второй хозяин там уже ломал кадр выстрела).
- [x] Гейты: ховер (`UpdateHoverHighlight`), всплывающий текст
      (`UCombatFeedbackSubsystem::PushEntry`), звук (`AUnitBase::PlayUnitSound`).
- [x] Камера реагирует на туман ПОДПИСКОЙ в обе стороны
      (`ATacticalPlayerController::HandleFogVisibilityChanged`): подхватывает
      действующего врага, вышедшего из-за угла, и — что важнее — **отпускает**
      того, кто скрылся посреди хода. Ведомая камера за невидимой пешкой рисует
      игроку её маршрут. Прежний опрос в `PlayerTick` удалён: момент смены
      видимости знает подсистема.
- [x] Список видимых врагов поддерживается пересчётом (`VisibleEnemies`), а не
      считается по запросу: счётчик в HUD висит на биндинге и спрашивается
      каждый кадр. По той же причине проверка «бежит ли кто-нибудь» обходит
      реестр компонентов, а не стороны боя — `GetPlayerSideUnits()` возвращает
      массив по значению, и опрос сторон в тике означал бы две кучные аллокации
      каждый кадр.
- [x] Сценарные такты `Scripted Move` / `Scripted Enemy Turn` берут удержание
      показа на всё время такта (`FogRevealHold` — слабая ссылка на компонент,
      снимается парно в `ExitState`).
- [x] `ResetForScenario(ScenarioId, RunId)` зовёт `ATacticsGameMode::StartMissionCombat`
      до `StartCombat` — первый пересчёт проходит с пустым составом сторон, то
      есть бой начинается со всеми скрытыми, а не с кадром общей расстановки.
- [x] Коллизия/occupancy не трогаются (§2.5, §5.2).
- [x] `LogXRU1Fog` + `xru1.Fog.Explain` (разбор решений) и `xru1.Fog.Disable`
      (всех видимыми, отладка).
- [x] Момент обнаружения: камера коротко держит кадр на враге, увиденном ВПЕРВЫЕ
      за бой (`FirstSightedDelay = 0.75` — число XCOM). Реакция живёт в
      `HandleFogVisibilityChanged`, то есть там же, где камера уже узнаёт о
      смене видимости: отдельного механизма «кто кого впервые увидел» не
      заводилось. Постановка такта и кадр выстрела главнее акцента.
- [ ] **PIE-проверка** матрицы §8, в первую очередь туториал A1–D3.

**Указатель на цель — отдельный слой, не туман.**
`UObjectivePointerSubsystem` (`UI/ObjectivePointerSubsystem.*`) отвечает на вопрос
«куда идти», а не «кого видно», и тумана не спрашивает вовсе (§2.6). Цели
регистрирует `ATacticsGameMode::StartMissionCombat` там же, где подписывается на
их события; правило показа — **данные записи** (`WhileBombArmed`,
`WhileEvacActive`), поэтому забытая отписка не может оставить стрелку на снятом
заряде. Настройки — в `DA_TacticalHUDStyle`, раздел «08a. Указатель цели».

**Инварианты, проверенные аудитом 2026-08-03** (три круга правок, два чистых
круга подряд):

| Инвариант | Чем держится |
|---|---|
| Один источник правды | решение принимается в `ResolveDesiredVisibility`, кэш подсистемы заполняется из ПРИМЕНЁННОГО состояния компонента. `IsActorCurrentlyVisible` и `IsActorPresentationHidden` — два окна в одно решение, правило выбора записано в заголовке подсистемы |
| Выстрел не может выдать скрытого | доказательство в §4; проверено, что `GA_Attack`/`GA_Overwatch`/`TacticalClassAbilities` тумана не читают вовсе |
| AI не читает fog игрока | проверено: `UnitAIController`, `AIActionEvaluators`, `TacticalAIDirectorSubsystem` не ссылаются на слой ни разу |
| Сценарная постановка сильнее тумана | удержания показа у `Scripted Move` / `Scripted Enemy Turn`; цель сценарного выстрела видима ПО ПОСТРОЕНИЮ (стрелку нужна LOS ≤ 2500, а он — член отряда) |
| Состояние не переживает `RunId` | `ResetForScenario` чистит кэш, список видимых И сценарные удержания (оборванный StateTree может не дойти до `ExitState`) |
| Своих туман не трогает | двойная защита: подсистема их не считает, компонент отказывается применять презентацию к стороне игрока — иначе у оверхедов отряда снова стало бы два владельца |
| Скрытие не ломает ход врага | у мешей юнитов `AlwaysTickPose` (проверено на ассетах): монтажи и `FireCommit` у скрытого идут. Смена настройки на «только когда отрендерен» ловится предупреждением в лог |
| Ноль пересчётов в пустом кадре | тик без событий делает только обход реестра без аллокаций |

**`UFogVisionComponent` не заводился.** Источники зрения сейчас — ровно живые
юниты стороны игрока с общим `SquadVisionRange`, и компонент лишь повторил бы то,
что уже говорит `GetPlayerSideUnits()`; поле без потребителя заводить нельзя
([06_CONVENTIONS §3](06_CONVENTIONS.md)). Шов оставлен: список наблюдателей
собирается в ОДНОЙ строке `RecomputeNow`, и добавление источника без юнита
(аналог `XComGameState_SquadViewer`) — правка одной функции.

**DoD F1:** скрытый враг существует, ходит, стреляет по правилам, но не оставляет
ни одного визуального, UI, звукового, input- или camera-сигнала до обнаружения;
туториал проходится без изменений в `ST_Quest_Tutorial`.

### F2/F3 — визуальный слой *(следующий этап, отдельная сессия)*

Задание — [agents/BRIEF_FogOfWar_VisualLayer.md](agents/BRIEF_FogOfWar_VisualLayer.md).
Там собрано состояние на входе, уже принятые решения, проверенные технические
ловушки и порядок работ, чтобы не выводить всё это заново.

### F2 — сетка `Unknown/Explored/Visible` *(сделано 2026-08-03)*

- [x] `UFogOfWarConfigDataAsset` (`/Game/XRU1Game/Data/Core`, ссылка в
      `BP_TacticsGameInstance`) + `bStartFullyExplored` в
      `UTacticalScenarioDataAsset` (дефолт **false** — обе миссии идут с чёрной
      картой, решение 2026-08-03). Границы берутся из ВСЕХ загруженных
      `ANavMeshBoundsVolume` — они лежат в scenario sublevel, а не в persistent,
      и их несколько; отдельный `AFogOfWarBoundsVolume` не понадобился.
- [x] **Запекание** битовой маски блокеров один раз на `ResetForScenario`; в
      рантайме — DDA (Amanatides-Woo) по массиву, ни одного физического трейса.
- [x] Три `TBitArray`: `Blockers`, `Visible`, `Explored` (`Explored |= Visible`).
- [x] Обновление по событию `UFogOfWarSubsystem::OnVisibilityRecomputed`: сетка
      наследует и троттлинг движения, и правило «ноль работы в пустом кадре».
      Растеризация пропускается, если состав и позиции источников не изменились.
- [x] Тяжело раненый боец даёт КАРТИНКЕ маленький круг обзора (число XCOM:
      `BLEEDOUT_SIGHT_RADIUS = 3` м) — иначе свой же боец лежит в темноте, и
      приказ «дойти и поднять» отдаётся вслепую. Правил это не касается.
- [x] Сценарное раскрытие области (`AddScriptedReveal`, аналог
      `X2Action_RevealArea`): берётся тактами `Scripted Move` / `Scripted Enemy
      Turn` и фокусом беата — там же, где берётся `FogRevealHold`. Без него
      чёрная карта ломала режиссуру обучения: камера наводилась в неразведанное.

**Три ловушки, каждая из которых маскировалась под «баг материала»** (записаны,
чтобы никто не потратил на них второй день):

1. **`UTexture2D::CreateTransient` не инициализирует память.** Заливка только
   изменившегося прямоугольника оставляла в текстуре мусор — на экране случайные
   пятна, РАЗНЫЕ при каждом запуске. Первая заливка идёт целиком
   (`bTextureNeedsFullUpload`).
2. **Навмеш — не маска пола.** Промежуточная редакция брала пол клетки из
   `ProjectPointToNavigation`; при включённых Navigation Invokers навмеш живёт
   только вокруг бойцов, отсюда квадрат чистой местности вокруг юнита и разное
   число клеток от запуска к запуску (12039 / 9463 / 9238). Пол ищется трейсом
   **снизу вверх** — первая поверхность снизу это земля, а не крыша ангара.
3. **Блокером считается только ПОЛНОЕ укрытие.** Проба тонкая (10 см) и на
   высоте `UCoverTuningDataAsset::FullCoverHeight`: в XCOM низкое укрытие линию
   видимости не рвёт, из-за него стреляют поверх. Толстая проба превращала мешки
   с песком в стену, и за каждым бруствером тянулась тень.

**Снято из прежнего плана и почему:**

- Z-bands и «активный этаж в RT» — §2.4 (на карте нет боевого второго этажа;
  гейт акторов трёхмерен и без них).
- Требование «точная видимость actor остаётся дополнительным LOS» — не пункт
  работы, а инвариант: сетка вообще не участвует в решениях (§1).

### F3 — текстура и post-process *(сделано 2026-08-03)*

- [x] Транзиентный `UTexture2D` размером в сетку (`PF_B8G8R8A8`, sRGB off,
      `NeverStream`, Clamp, билинейная фильтрация): канал R — видно сейчас,
      G — `Explored`. Render Target не понадобился — писать в текстуру дешевле,
      чем рисовать в RT кистями.
- [x] Обновление по грязному прямоугольнику, не каждый кадр; сглаживание края во
      времени (`EdgeFadeSpeed`, аналог `FOWEnvelopeSpeed`).
- [x] `M_PP_FogOfWar` собран программно (Python через MCP-мост):
      `Blendable Location = Scene Color Before DOF`, позиция мира как
      `Camera Relative World Position (including Material Shader Offsets)` +
      `Camera Position WS`. Четыре сэмпла со сдвигом в долю клетки — иначе
      граница клеток читается квадратами. Явная проверка «внутри сетки»: за её
      границей состояние задаётся параметром, а не поведением sampler'а.
- [x] Blendable добавляется в `ATacticalCameraPawn::BeginPlay` **после**
      `M_OutlinePP`; MID регистрируется в сетке, она пишет в него параметры.

**Почему именно `Scene Color Before DOF` + camera-relative позиция.** В UE 5.7
эта точка исполняется в render resolution, и world-space привязка здесь
стабильна; более поздние точки работают уже в display resolution, а буфера
глубины там нет вовсе. Проверены и отвергнуты: `SceneDepth` через ноду
SceneTexture, движковая `WorldPositionBehindFromDepth_experimental`,
реконструкция через `SvPositionToTranslatedWorld` — все давали «плавающий» за
камерой туман (отладочный вывод UV показывал экранный градиент без изломов на
геометрии). Декаль вместо post-process рассматривалась и отклонена: под
наклонной камерой под неё можно заглянуть.

**Неразведанное — не заливка, а притушенная сцена.** `Unknown` = обесцвеченная
сцена × `UnknownBrightness` × холодный оттенок; силуэты рельефа и построек
остаются различимы. Сплошной цвет стирал чувство карты и убивал дальний план.

**Мягкость границы — два разных параметра, и путать их нельзя.**
`MaskSmoothingRadius` размывает САМУ МАСКУ (доля видимых соседей в окне) — от
этого уходит лесенка пикселей. Но размытие даёт полутень и ВНУТРИ зоны обзора,
поэтому поверх него стоит порог `EdgeSoftness`: ядро зоны получает полную
яркость, мягкой остаётся только кромка. Правило — что боец видит, он видит
целиком.

**Кадр выстрела раскрывает местность вокруг участников.** Кадр показывает бой
крупно и с чужого ракурса, неразведанного в нём куда больше обычного — без
раскрытия выстрел играется в темноте. Берётся в `EnterShotFraming`, снимается в
ОБЕИХ точках выхода из кадра (`ClearShotFraming` и `AbandonShotFraming`);
времени жизни намеренно нет — кадр прицеливания бессрочен.

**Беат обучения держит и актора, и местность.** Раскрыть местность мало: если
камера наведена на врага, а удержания показа нет, кадр смотрит в пустоту —
местность вокруг открыта, а сам враг скрыт (поймано прогоном на беате
`A8_ReturnFire`). Оба удержания берутся и снимаются парно, как в тактах
`Scripted Move` / `Scripted Enemy Turn`.

### Диагностика визуального слоя

| Cvar / команда | Зачем |
|---|---|
| `xru1.Fog.Grid 0` | выключить затемнение, не трогая правила |
| `xru1.Fog.GridDump` | распечатать сетку в журнал текстовой картой (`#` блокер, `*` видно, `.` разведано) |
| `xru1.Fog.Blockers 0` | растеризация без окклюзии — остаются чистые круги обзора; разделяет «баг маски» и «баг материала» |
| `xru1.Fog.ScriptedReveals 0` | убрать сценарные раскрытия из картины на время отладки |
| `xru1.Fog.ExplainBlockers 1` | при следующем запекании напечатать, какие акторы сформировали блокеры |

В строке запекания печатается `hash` маски блокеров: одинаковый хеш в двух
подряд запусках — проверка, что запекание детерминировано.

**Что журнал печатает всегда, а что только под разбором.** Всегда — запекание
(размер, блокеры, хеш, время) и `Reset` сессии: по этим двум строкам
восстанавливается, с чем работает слой. Сценарные раскрытия — ТОЛЬКО под
`xru1.Fog.Explain`: их берёт не только режиссура, но и каждый кадр выстрела, то
есть в бою это десятки пар «взято/снято». Правило то же, что у слоя правил:
рутина не имеет права топить значимое.

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
| Враг становится видим во время своего бега | reveal происходит на бегу, не позже `xru1.Fog.MoveRecheck` |
| Враг бежит ВДОЛЬ границы обзора | не мигает: показ мгновенный, скрытие с выдержкой `xru1.Fog.HideGrace` |
| Цель миссии за краем кадра | у края экрана стрелка с подписью; у заряда рядом остаток ходов |
| Цель миссии в кадре | стрелки нет, только подпись над целью |
| Заряд обезврежен / зона эвакуации ещё не включена | указателя нет вовсе |
| Идёт прицеливание или кадр выстрела | указатели скрыты (как стрелки XCOM при поднятом меню выстрела) |
| Труп / Downed | остаются видимыми независимо от LOS (правило XCOM, §2.5) |
| Сценарный такт показывает врага вне LOS | override работает, такт не ломается (§5.3) |
| Кадр выстрела / смерти | юнит не исчезает посреди монтажа |
| Tutorial → Mission01 на `Main_Map_Showreel` | новая fog-сессия; кэш и подписки прошлого run не переносятся |
| Retry сценария | нет видимых один кадр врагов прошлого запуска |
| Low scalability / fog material off | механические гейты продолжают работать |
| Профиль | один полный пересчёт ≈1 мс; в кадре без событий пересчётов нет |

---

## 9. План внедрения и проверки

### 9.1 Настраивать не нужно ничего

Это результат, а не оговорка. Слой спроектирован так, чтобы у него не было
шага ручной сборки:

| Что могло потребовать настройки | Почему не требует |
|---|---|
| Компонент скрытия на каждом враге | `UFogRevealableComponent` — дефолтный субобъект `AUnitBase`, его получают ВСЕ юниты автоматически, включая уже расставленных на карте и будущих |
| Компонент зрения на бойцах | не заводился: источники зрения = живые юниты стороны игрока, это и так известно `TurnManager` |
| Data Asset конфигурации тумана | нет — у слоя нет геометрических настроек, все пороги общие с боевой LOS (`SquadVisionRange`) |
| Trace-канал `FogVision` | нужен только для сетки затемнения (F2), которая понижена до опциональной |
| Пометка целей миссии | не нужна: у них нет fog-компонента, значит они не прячутся — это правило, а не забывчивость (§2.6, §5.2a) |
| Указатель «куда идти» | цели регистрирует `ATacticsGameMode` при старте боя; работает и без назначенного `DA_TacticalHUDStyle` (дефолты полей) |
| Исключения для тактов обучения | берутся автоматически задачами `Scripted Move` / `Scripted Enemy Turn` |

**Единственное, что имеет смысл сделать руками — и только если не понравится
внешний вид**: `DA_TacticalHUDStyle` → раздел «08a. Указатель цели» (цвета,
размер стрелки, отступ от края, кегль).

### 9.2 Первый запуск

1. Собрать проект (`.\Build-XRU1.ps1`) — обязательно, добавлены новые классы.
2. Открыть `Main_Map_Showreel`, PIE через `PreviewScenario` Director'а
   (Tutorial) — как обычно.
3. В консоли PIE включить разбор: `xru1.Fog.Explain 1`.
4. Пройти проверку §9.3.
5. Выключить разбор (`xru1.Fog.Explain 0`) и пройти туториал целиком без него —
   так виден реальный игровой темп.

### 9.3 Матрица PIE-проверки

Порядок важен: сначала базовое поведение, потом обучение, потом граничные случаи.

**A. База (2–3 минуты, любая расстановка)**

| Шаг | Что делать | Ожидание | Где смотреть |
|---|---|---|---|
| A1 | Старт боя | Ни один враг не виден до первого хода; счётчик врагов = числу реально видимых | `[Fog] Reset: сценарий … run …`, затем `[Fog] пересчёт «состав сторон после старта»` |
| A2 | Навести курсор туда, где стоит невидимый враг | Ничего не подсвечивается, панель цели пуста | — |
| A3 | Подвести бойца так, чтобы враг попал в LOS | Враг появляется вместе с оверхед-худом, счётчик растёт | `[Fog] BP_Unit_Marauder2 → ПОКАЗАН (виден отряду)` |
| A4 | Увести бойца обратно за стену | Через ~0.35 с враг исчезает; обводка не остаётся | `[Fog] … → СКРЫТ (вне зрения отряда)` |
| A5 | Пробежать бойцом ВДОЛЬ края стены | Враг не мигает | в журнале не должно быть серий «СКРЫТ/ПОКАЗАН» подряд |

**B. Ход врага и камера**

| Шаг | Что делать | Ожидание | Где смотреть |
|---|---|---|---|
| B1 | Завершить ход, когда враги не видны | Камера не летает по пустым местам, ходы идут «за кадром» | `[Fog] камера …` не появляется |
| B2 | Враг выбегает в зону видимости в свой ход | Камера подхватывает его на бегу | `[Fog] камера подхватила …: он стал виден отряду` |
| B3 | Тот же враг забегает за укрытие | Камера отпускает его, кадр не едет за пустотой | `[Fog] камера отпустила …: он ушёл из зрения отряда` |
| B4 | Враг стреляет по бойцу | Он ОБЯЗАН быть видим (§4). Если стреляет невидимый — это баг симметрии Ф5, а не тумана | `[FireAction]` + отсутствие `СКРЫТ` у стрелка |

**C. Обучение A1–D3 — главный риск**

| Шаг | Что проверять |
|---|---|
| C1 | Секция A целиком: голограмма, по которой идёт сценарный выстрел, видна в момент выстрела (это гарантировано построением, но проверить глазами) |
| C2 | Секция B: Кадет выбегает и своим зрением вскрывает противника — противник появляется на его бегу, а не в конце хода |
| C3 | Секция C: ход Holo_D (`Scripted Enemy Turn`) показывается ЦЕЛИКОМ — выход, реакция Танка, отбегание. Ни один кадр не должен идти по пустому месту |
| C4 | Секция C: после конца такта Holo_D прячется, если ушёл из зрения |
| C5 | Секция D: зона эвакуации — как только включилась, у края экрана появляется стрелка «ЭВАКУАЦИЯ»; при взгляде на зону стрелка пропадает, остаётся подпись |
| C6 | Реплики и подсказки обучения не перекрываются стрелкой (она в самом нижнем слое) |

**D. Границы**

| Шаг | Что проверять |
|---|---|
| D1 | Retry сценария: нет ни одного кадра со «старыми» видимыми врагами |
| D2 | Tutorial → хаб → Mission01: `[Fog] Reset` печатает НОВЫЙ `ScenarioId/run`; состояние прошлого прогона не переносится |
| D3 | Mission01: у заряда указатель «ЗАРЯД · N», где N — остаток ходов; после обезвреживания стрелка пропадает, появляется «ЭВАКУАЦИЯ» |
| D4 | Прицеливание (кнопка «Огонь»): указатели целей скрываются на время кадра |
| D5 | Смерть врага: тело остаётся видимым, шкала HP над ним не возвращается |
| D6 | `stat unit` во время боя: пересчёты тумана не должны быть заметны в Game-времени |

### 9.4 Ручки тюнинга (консоль PIE)

| Cvar | Дефолт | Зачем |
|---|---|---|
| `xru1.Fog.Explain 1` | 0 | Полный разбор: причина пересчёта и итог по каждому актору |
| `xru1.Fog.Disable 1` | 0 | Временно показать всех — быстро отличить «баг тумана» от «баг расстановки» |
| `xru1.Fog.MoveRecheck` | 0.1 | Как часто переоценивать видимость на бегу |
| `xru1.Fog.HideGrace` | 0.35 | Выдержка перед скрытием. 0 — мгновенно (увидите мигание на границе) |

### 9.4a Что печатает журнал

Контракт логирования зафиксирован разбором реального прохождения (2026-08-03,
647 строк тумана за прогон — из них 553 были бесполезны).

**Всегда, без единого cvar** — по этим строкам восстанавливается вся картина:

| Строка | Когда |
|---|---|
| `[Fog] Reset: сценарий … run …` | новая fog-сессия |
| `[Fog] <актор> → СКРЫТ/ПОКАЗАН (причина)` | каждая смена видимости |
| `[Fog] пересчёт «причина»: … изменений=N …` | только если что-то изменилось |
| `[Fog] <актор>: сценарное удержание показа взято/снято` | границы постановочных тактов |
| `[Fog] <актор>: скрытие отложено (актор доигрывает действие)` | редкий случай: юнит завис в монтаже |
| `[Fog] камера подхватила/отпустила <актор>` | реакция камеры на туман |
| `Warning: [Fog] У … меш стоит в OnlyTickPoseWhenRendered` | настройка BP сломает ход врага |

**Только под `xru1.Fog.Explain 1`**: «пересчёт запрошен: <источник>» и построчный
разбор `<актор>: виден=N (причина)`.

Два правила, за которые пришлось заплатить разбором прохождения:

1. **Диагностика не пишется в `Verbose`.** Уровень категории по умолчанию `Log`,
   поэтому `Verbose`-строки отфильтрованы — включённый `xru1.Fog.Explain 1`
   печатал только сводку, а сам разбор не доходил. Гейт — cvar, уровень —
   `Display`.
2. **Рутинный пересчёт без изменений молчит даже под разбором.** Пересчёты
   «движение» идут сотнями за бой; печатать их полностью значит утопить сигнал.
   Под разбором из них печатаются только изменившиеся акторы.

### 9.5 Если что-то не так

| Симптом | Наиболее вероятная причина | Куда смотреть |
|---|---|---|
| Врага не видно, хотя должно | боец не даёт зрения (Downed/эвакуирован) либо нет LOS | `xru1.Fog.Explain 1` → строка актора; затем `xru1.LOS.Explain` |
| Враг виден, хотя не должен | сценарное удержание не снялось или `Override` | в строке актора причина: «сценарное удержание» / «Override=…» |
| Шаг обучения ждёт того, кого не видно | такт без камеры не берёт удержание — нужен `Override = AlwaysVisible` на его акторе | `[Tutorial]` + `[Fog]` рядом по времени |
| Ход врага завис | меш переведён в `OnlyTickPoseWhenRendered` | Warning `[Fog] У … меш стоит в OnlyTickPoseWhenRendered` |
| Враг мигает | `HideGrace` слишком мал для этой геометрии | поднять `xru1.Fog.HideGrace` до 0.5 |
| Стрелки цели нет | цель не зарегистрирована или правило её гасит (заряд снят, зона выключена) | `[Objective] указатель «…» → …` при старте боя |

### 9.6 Только если делаем F2/F3 (опционально, после проверки)

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
