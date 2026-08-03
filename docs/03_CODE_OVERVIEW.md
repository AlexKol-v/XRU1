# Обзор кодовой базы XRU1

Актуально на 2026-07-29. Документ описывает действующую архитектуру, а не
историю её появления. Игровой модуль — `Source/XRU1`, UE 5.7.

## 1. Границы модулей

| Область | Каталоги | Ответственность |
|---|---|---|
| Тактическое ядро | `Source/XRU1/Tactics/` | ходы, AP, перемещение, укрытия, бой, AI, способности, камера, миссия и save |
| Игровой UI | `Source/XRU1/UI/`, `UI/Menus/` | HUD, unit widgets, CommonUI-стеки и экраны |
| Хаб | `Source/XRU1/Hub/` | голо-карта выбора миссий, её камера, контроллер и маркеры POI |
| GAS-иерархия | `Characters/` | ASC, атрибуты и базовые combatant-классы, перенесённые из донора |
| Интеракции | `Interaction/` | детектор объектов и prompt |
| PCG | `PCG/` | вспомогательные PCG-ноды для окружения |
| Плагины | `Plugins/` | `STQuestSystem`, `TeamManager`, `GameplayMessageRouter`, editor-only `UnrealClaude` |

Старые `Variant_Strategy`, `Variant_TwinStick` и шаблонные
`XRU1Character/GameMode/PlayerController` не участвуют в тактическом режиме.

## 2. Главные владельцы состояния

| Класс | Что хранит и решает |
|---|---|
| `ATacticsGameMode` | состав сторон, сложность, запуск боя, mission outcome |
| `UTurnManagerSubsystem` | текущая сторона/активация, лимит атакующих, конец боя |
| `ATacticalPlayerController` | выбор, targeting mode, команды, hover/path preview, auto-advance |
| `ATacticalCameraPawn` | ракурс игрока (yaw/наклон/зум/обзор/этаж), focus/follow, кадр выстрела и арбитраж владения камерой |
| `AUnitBase` | параметры юнита, GAS, visual state, оружие, cover anchor и BP-события презентации |
| `UActionPointsComponent` | AP и единый контракт затрат |
| `UCoverDetectionComponent` | локальное Half/Full cover, стороны стены и peek edge |
| `AMoveRangeVisualizer` | поле достижимости, маршрут и визуализация зоны хода |
| `AUnitAIController` | тревога, контекст решения, utility evaluators и исполнение маршрута |
| `UAIBehaviorProfileDataAsset` | единый DataAsset тюнинга perception/nav/alert/position/target/evaluators |
| `UFogOfWarSubsystem` | player-facing gameplay visibility и безопасный список видимых врагов |
| `UTacticsGameInstance` | save/UI/cover и выбранный scenario одной общей боевой карты |
| `UGamePauseSubsystem` | единственный владелец паузы: стек причин, `SetGamePaused`, режим ввода и приглушение боевого звука |
| `AHologramMapActor` / `AHubPOIMarker` | голо-карта хаба (вращение по yaw, автоспин) и маркеры миссий на её `RotationRoot` |
| `AHubPlayerController` / `AHubCameraPawn` | ввод хаба (ПКМ-drag, ЛКМ-выбор, колесо, Q/E) и орбитальная камера |
| `ATacticalScenarioDirector` | вход в scenario sublevel и запуск квеста после его загрузки |
| `UTacticalQuestEvents` / `ATacticalQuestZone` | подтверждённые доменные события обучения и зоны тактических бойцов |
| `UTacticalScenarioSubsystem` | реестр акторов сценария по `AnchorId` и единое включение staged-акторов |
| `UTutorialActionGateSubsystem` | политика текущего шага обучения: что игрок вправе сделать прямо сейчас |
| `UTutorialPresentationSubsystem` | активный такт обучения: спикер, субтитр, фокус камеры, подсветки |
| `STutorialHintOverlay` | Slate-трекер целей обучения поверх viewport (без WBP), настройки — в `DA_Tutorial_Style` |
| `UTutorialStyleData` | презентация обучения: подсказки-цели и мировые декали шага (маркер точки, рамка зоны); резолвится `Get()` → GameInstance → CDO |
| StateTree-задачи «XRU1 Tutorial» (`TacticalQuestTasks`) | Tactical Objective с payload, Apply Action Gate, Set Scenario Actor Active, Scripted Shot, Tutorial Beat |
| `TacticsDebug` | общий реестр debug-cvar (`xru1.AI.LogCombat`, `xru1.Tutorial.LogGate`, …) и команда `xru1.LOS.Explain` |
| `UTacticsAudioSubsystem` | единственная точка воспроизведения звука и применения громкостей |
| `UTacticsCombatStatics` | LOS, шанс, firing positions, cover shield, урон и общие predicates |

Правило владения: механическое состояние находится в C++; Blueprint отображает
состояние, запускает монтаж/VFX/SFX и сообщает строго определённые presentation
сигналы. BP не рассчитывает урон, AP, LOS, укрытие или исход действия.

## 3. Перемещение и укрытия — закрытая база

Игрок и AI используют одни и те же строительные блоки:

1. навигационный путь;
2. бюджет пути на 1/2 AP;
3. учёт живых юнитов как динамической занятости;
4. корректировка конечной точки из чужой капсулы;
5. исполнение ломаной через `MoveAlongRoute`;
6. переоценка укрытия после прибытия.

`UTacticsCombatStatics::GetUnitClearance` и `GetUnitObstacles` — общий контракт
занятости. `PlanMoveTo`/поле достижимости отвечает за допустимость, route
executor — только за исполнение уже принятого маршрута.

Укрытие разделено на два слоя:

- локальный визуальный слой (`CoverSides`, `BestCoverDirection`, anchor,
  `FindPeekEdgeSide`) определяет, к какой стене прижат юнит и как его показать;
- боевой слой (`GetCoverShieldAgainst`, firing positions, LOS) отвечает на
  вопрос, защищает ли геометрия от конкретного стрелка.

Юниты и трупы не считаются стеной. Half/Full определяется геометрией от уровня
пола. Flanked вычисляется реальной линией атаки, а не направлением анимации.

## 4. Канонический action flow выстрела

Обычный выстрел и Overwatch следуют одному принципу синхронизации:

```text
команда/реакция
  → C++ ability создаёт action context и фиксирует target/firing stance
  → презентация стартует В ТОТ ЖЕ КАДР: камера едет, боец идёт на огневую точку
  → для стоек Open/OverCover сразу стартует ДОВОРОТ к цели
  → BP-ветка ждёт доворот + микропаузу + остаток наводки камеры
    (Face Shot Target (Latent)) и запускает montage
  → AnimNotify_FireCommit подтверждает конкретный montage instance
  → активная ability один раз вызывает ResolveShotMechanics
  → HitReact или Death presentation
  → Wait Shot Hold (Latent) держит кадр (урон читается)
  → StepOut возвращается в сохранённый anchor одновременно с уходом камеры
  → ability завершает action, после чего разрешён auto-advance
```

Ключевые классы: `UGA_Attack`, `UGA_Overwatch`, `UAnimNotify_FireCommit`,
`FTacticalFireActionContext`, `UTacticsCombatStatics`.

**Фаза доворота (AimTurn).** Стрелок разворачивается к цели ПЛАВНО и до старта
montage; мгновенного `FaceActorTowards` между приходом на позицию и выстрелом
больше нет (он был источником «выбежал вбок → щёлкнул → выстрелил»). Владелец —
`UTacticalAbility`:

- `StartAimTurnTowardsTarget(ActionId, Reason)` — запуск поворота (через
  `AUnitBase::FaceTowardsSmooth`, скорость `AimTurnRate` = 420 °/с). Для стоек
  Open/OverCover зовётся из C++ ВМЕСТЕ с наводкой камеры, поэтому поворот идёт
  внутри уже существующей паузы `PreShotCameraSettleDelay` и презентацию не
  удлиняет;
- `FaceShotTargetLatent(ActionId)` — latent-узел BP перед montage. Для StepOut
  он же и запускает доворот: до прибытия на огневую точку корпус ведёт path
  following, разворачивать раньше бессмысленно;
- ожидание всегда ограничено `AimTurnMaxWait` (1.2 с), а если транзакция
  закрылась за это время — latent завершается БЕЗ продолжения ветки, то есть
  montage устаревшего действия не запускается;
- порядок фаз зависит от стойки: **Open** — доворот до montage (вставать не
  надо), **StepOut** — доворот после прибытия на огневую точку, **OverCover** —
  сначала montage (боец встаёт), и уже стоя доворот, запланированный от
  ФАКТИЧЕСКОГО старта анимации (`NotifyPresentationMontageStarting`) так, чтобы
  закончиться до `FireCommit` (`ScheduleAimTurnAfterRise`, `AimTurnRiseDelay`;
  при большом угле старт сдвигается раньше, а скорость растёт до
  `AimTurnRateMax`). Разворот сидя с последующим вставанием читался как лишнее
  движение;
- `AimTurnSettleDelay` (0.25 с) — микропауза между концом доворота и стартом
  montage. Ставится только после реального доворота и после StepOut (боец
  тормозит после перебежки). Без неё выстрел склеивался с последним кадром
  движения и читался как «стрелял на ходу»;
- `PreShotCameraSettleDelay` (0.4 с) больше НЕ задерживает само действие:
  раньше на нём стоял таймер до `OnFireActionStarted`, и между командой игрока
  и первым движением бойца висела мёртвая пауза. Теперь транзакция стартует в
  тот же кадр, а `GetCameraSettleRemaining` считает остаток **до момента
  выстрела**: из паузы вычитается время до `FireCommit` внутри montage, потому
  что анимация подъёма/вскидывания и есть фора для камеры. Практический эффект:
  у OverCover (выстрел на 0.40 с) montage стартует немедленно, у StepOut пауза
  уже исчерпана перебежкой, у Open остаётся короткое ожидание, которое боец
  тратит на доворот;
- `Wait Shot Hold (Latent)` — фаза удержания кадра ПЕРЕД возвратом StepOut.
  Боец остаётся на огневой позиции, пока читается урон, и уходит в укрытие
  вместе с уходом камеры. Отработанный здесь hold помечается
  (`MarkPresentationHoldDone`), поэтому терминал не держит кадр второй раз;
  сорванный до выстрела montage возвращает бойца сразу (hold = 0);
- `UTacticsCombatStatics::GetFacingErrorDegrees` — общая метрика «куда смотрит
  корпус относительно цели»; печатается в `[AimTurn]`, `[FireAction] Begin/Commit`
  и `[ReactionAction] Begin/Commit`.

Диагностика в PIE — по префиксам:

| Префикс | Что показывает |
|---|---|
| `[AimTurn]` | старт/финиш доворота, план vs факт, пауза стабилизации, лимит, пропуск |
| `[FireAction] Begin` | стойка, montage, дистанция, отклонение корпуса, settle |
| `[FireAction] BP запросил план` | BP-ветка презентации дошла до плана и какую стойку получила |
| `[FireAction] Commit` | отклонение корпуса в момент выстрела (0° = довернулся) |
| `[FireCommit]` | КАДР выстрела внутри montage: позиция/длина и **вес** montage (вес < 1 = монтаж уже гаснет, выстрел в переходной позе) |
| `[Pose]` | смена позы юнита (встал/сел/лёг) с укрытием и признаком доворота |
| `[FireAction] Watchdog abort` | зависшая презентация + стойка, montage, привязанный instance |

Связка `[Pose]` + `[FireCommit]` — единственный способ отличить «встал, сел и
выстрелил сидя» от смены укрытия: montage играет поверх позы и сам по себе в
логе не виден.

Инварианты:

- AP резервируется/списывается по общему контракту ability; abort не оставляет
  частично выполненное действие;
- урон не применяется до `FireCommit`;
- один action принимает только notify своего montage instance;
- смена юнита и следующий ход запрещены, пока action не достиг terminal state;
- shot camera может быть отменена ручным pan/rotate/zoom, но сама не завершает
  gameplay action;
- StepOut хранит неизменяемые home anchor, сторону выхода и target до возврата.

## 5. Анимации — принятая архитектура

`ABP_Solider` — единственный AnimBP всех пяти тактических BP. Постоянная поза
берётся из `FUnitVisualState`; разовые действия принадлежат montage pipeline.

| Состояние/действие | Владелец |
|---|---|
| locomotion, crouch, Full/Half cover, Overwatch pose | Anim State Machine по `FUnitVisualState` |
| fire, HitReact, Overwatch enter, death | Montage через C++/BP presentation hook |
| yaw/cover anchor/StepOut movement | `AUnitBase` и активная ability |
| фактический урон | `FireCommit` → `UTacticsCombatStatics` |

**Поза следует за ФАКТИЧЕСКИМ перемещением, а не за «занятостью».**
`FUnitVisualState::bMoving` = `AUnitBase::IsPhysicallyMoving()`
(`AUnitAIController::IsFollowingPath()` плюс подшаг `HugCover`). Общий предикат
`IsMoving()` для этого НЕ годится: он намеренно включает settlement (подшаг и
доворот на месте), и пока он стоял здесь, каждый доворот перед выстрелом
поднимал бойца из укрытия в локомоцию и тут же сажал обратно — «встал, резко
сел, потом выстрел».

Тот же предикат сверяется каждый `Tick`: презентация StepOut двигает бойца
узлом «AI Move To» прямо из BP, и публиковать visual state оттуда некому — без
сверки юнит уезжал на огневую точку в позе укрытия («проскальзывает сидя»).

**Момент выстрела внутри montage.** `FireCommit` notify обязан стоять ПОСЛЕ
blend-in, иначе выстрел уходит в старой позе: у обоих fire-монтажей он стоял на
кадре 0.00 при весе montage 0.05, и боец стрелял ещё сидя в укрытии. Сейчас
`AM_Fire_Open` — 0.18 с (длина 0.53), `AM_Fire_OverCover` — 0.40 с (длина 0.80,
внутри есть подъём из-за укрытия). Проверяется строкой `[FireCommit]`: вес
должен быть ≈1.00.

**Доворот к цели анимацией пока НЕ сопровождается — осознанно.** Состояние
`TurnInPlace` в `ABP_Solider` существует, но не используется, и включать его
в текущем виде нельзя:

- клипы поворота в проекте только приседные (`M_Neutral_Crouch_Idle_Turn_*`,
  8 шт.); стоячих нет ни здесь, ни в доноре;
- у них `Enable Root Motion = True`, а у ABP `RootMotionMode = Montages Only`,
  то есть из стейт-машины root motion не применяется: поворот ведёт C++, а клип
  живёт своей длиной (2.5 с против 0.2–0.75 с самого поворота);
- условие `bShouldTurnInPlace` требует `Pose ∈ {CrouchCover, HighCover,
  Hunkered}`, а `PendingTurnYaw` выставляет только `PreviewAimAtTarget`, который
  работает лишь ВНЕ укрытия, — условия взаимоисключающие;
- у состояния нет переходов обратно в `Idle`/`Locomotion` (только в позы
  укрытия), поэтому вход из `Stand` был бы залипанием.

Поэтому фаза AimTurn зовёт `FaceTowardsSmooth(bPlayTurnAnimation=false)`.
Ожившая анимация доворота и Aim Offset (`AO_Rifle` есть, но ничем не управляется
— переменных Aim Yaw/Pitch в ABP нет) — отдельные задачи роадмапа.

Death запускается один раз монтажом и заканчивается terminal Dead; параллельной
death sequence в state machine нет. Этот блок, включая cover hug/peek/StepOut,
считается завершённым. Новые симптомы оформляются как отдельные баги.

## 6. Юниты и оружие

| BP | C++ база | Оружие |
|---|---|---|
| `BP_Unit_Assault` | `AUnit_Assault` | `BP_AssaultRifle_Default` |
| `BP_Unit_Sniper` | `AUnit_Sniper` | `BP_Sniper_Default` |
| `BP_Unit_Medic` | `AUnit_Healer` | `BP_SMG_Default` |
| `BP_Unit_Tank` | `AUnit_Tank` | `BP_LMG_Default` |
| `BP_Unit_Marauder` | `AUnitBase` | `BP_AssaultRifle_Default` |

Оружие прикреплено и различается по классам. Будущая косметическая задача —
поддерживать левую руку на цевье/прикладе через IK/Control Rig и socket/effector
оружия; она не должна менять боевой или animation state flow.

## 7. AI

`AUnitAIController` уже содержит `Patrol/Investigate/Combat`, `FAIDecision`,
набор `UAIEval_*`, скоринг позиций против нескольких угроз, SafeToMove,
Overwatch/Hunker и общий route executor. Это рабочая настраиваемая база, но
качество решений ещё не принято. Текущая программа настройки и тестов —
[08_AI.md](08_AI.md).

Введён `UAIBehaviorProfileDataAsset`: один ассет задаёт perception, навигацию,
alert FSM, target/position scoring и при необходимости instanced evaluators.
Вариативность utility/Investigate использует `DecisionSeed`, построенный из
карты, номера хода, стабильного имени юнита и номера решения; FPS и
`GFrameCounter` больше не влияют на выбор.

## 8. Туман войны

`UFogOfWarSubsystem` централизует текущую actor visibility. На него уже
переведён `ATacticalPlayerController::IsVisibleToSquad`, move preview не считает
скрытых врагов, а HUD получил `GetVisibleEnemyCount`. Это F0 — gameplay-основа,
а не готовый визуальный туман.

Следующие слои: actor/UI/VFX gating, CPU-grid `Unknown/Explored/Visible`, один
Render Target и post-process. Полный контракт — [10_FOG_OF_WAR.md](10_FOG_OF_WAR.md).

## 9. Общая карта, сценарии и квесты

`UTacticalScenarioDataAsset` разделяет логические Tutorial/Mission01, не создавая
копий World. `UTacticsGameInstance::StartCombatScenario` сохраняет активный
DataAsset и всегда открывает `SharedCombatLevel`; `ATacticalScenarioDirector`
в persistent level загружает указанный streaming sublevel и только после этого
запускает `UQuestDefinition`. Каждый запуск получает монотонный
`ScenarioRunId`; Director автоматически назначает активный quest tracked и
очищает tracking/active runner при уходе из World. `Scenario.Ready` публикуется
после `PlayerStarted`, а BP-хук открытия Action Gate — ещё через один tick,
после обработки события StateTree. `FinalizeConfiguredScenario(Success)`
образует единую границу исхода;
GameMode ждёт `OnLevelShown`, после stream регистрирует units/bomb/evac и не
пишет campaign save/не показывает result screen при ошибке финализации.
`RestartActiveScenario` очищает quest runtime, увеличивает `ScenarioRunId` и
переоткрывает тот же shared World.

`STQuestSystem` уже был перенесён из донора. Добавлены Asset Manager scan
`/Game/XRU1Game/Quests`, зависимости `STQuestSystem`/`GameplayMessageRuntime`,
нативные каналы `Quest.Event.Tactical.*`, единый broadcaster и
`ATacticalQuestZone`, который распознаёт AI-controlled бойцов стороны игрока.
Состояние квеста становится Active только после валидного `QuestLogic` и
успешного spawn runner. Confirmed turn, player ability/attack, kill,
defuse/evac и scenario-result hooks уже публикуют leaf-события; selection,
camera, move и scripted enemy attack требуют tutorial action/payload gate.
Tactical objectives и specs группы поддерживают exact channel; отдельная
`Quest Wait Outcome` возвращает правильный terminal `Succeeded`/`Failed`.
Content-only objective IDs уже перечислены в `Config/DefaultGameplayTags.ini`.

`FQuestEventData` несёт `Source`, `Target` и `ScenarioRunId`, а
`AQuestRunnerActor` кладёт её в `FStateTreeEvent::Payload`. Поэтому игровые
задачи `Tactical Objective`, `Apply Action Gate`, `Set Scenario Actor Active`,
`Scripted Shot` и `Tutorial Beat` (категория **XRU1 Tutorial**,
[TacticalQuestTasks.h](../Source/XRU1/Tactics/TacticalQuestTasks.h)) проверяют
конкретного бойца, конкретную цель и «N разных источников», не размножая каналы.
Ссылки на акторов сценария идут только через `AnchorId`
`UScenarioActorIdComponent`; имена World Outliner остаются диагностикой.

Сценарный выстрел обучения не подменяет урон: `FScriptedShotOverride` меняет
только шанс и урон в snapshot'е `UGA_Attack`, а приказ «стрелять по этому
бойцу» ставится `AUnitAIController::SetScriptedAttackOrder`. Roll, GE, montage,
`FireCommit`, HitReact и quest-события остаются общим attack pipeline.

Наполнение графов StateTree и обоих scenario sublevel делается в Editor по
[11_SHARED_MAP_TUTORIAL.md](11_SHARED_MAP_TUTORIAL.md) §5.3–5.4.

## 9.5. Звук

Слой звука построен вокруг одного правила: **звук привязан к подтверждённому
доменному факту, а не к запуску montage**. Точки вызова те же, что у
quest-событий, поэтому отменённое действие не может прозвучать как выполненное.

| Блок | Файл | Роль |
|---|---|---|
| `UTacticsAudioSubsystem` | [TacticsAudioSubsystem.h](../Source/XRU1/Audio/TacticsAudioSubsystem.h) | воспроизведение 2D/3D/attached; громкости ставятся прямо в `SoundClass` (не через SoundMix — см. [09_UI_HUD §5.5](09_UI_HUD.md)) |
| `UTacticsAudioSettingsDataAsset` | там же | дефолтные громкости, SoundMix, пять SoundClass и общие звуки интерфейса |
| `UUnitAudioDataAsset` | [UnitAudioDataAsset.h](../Source/XRU1/Audio/UnitAudioDataAsset.h) | профиль класса: выстрел, боль, смерть, способности, шаги по поверхностям |
| `UAnimNotify_UnitFootstep` | [AnimNotify_UnitFootstep.h](../Source/XRU1/Audio/AnimNotify_UnitFootstep.h) | шаг с выбором звука по физматериалу под ногой |
| `FTacticsAudioSettings` / `FTacticsVideoSettings` | [TacticsAudioTypes.h](../Source/XRU1/Audio/TacticsAudioTypes.h) | структуры настроек; хранит их `UTacticsUserSettings`, а НЕ слот кампании |
| `UTacticsUserSettings` | [TacticsUserSettings.h](../Source/XRU1/Tactics/TacticsUserSettings.h) | единственный источник правды настроек звука и изображения (`GameUserSettings.ini`) |
| `UGamePauseSubsystem` | [GamePauseSubsystem.h](../Source/XRU1/Tactics/GamePauseSubsystem.h) | единственный владелец паузы: стек причин, мир, звук |

`AUnitBase::PlayUnitSound(EUnitSoundEvent)` — единственный вход для звуков
юнита; Blueprint ничего подключать не обязан. Громкости применяются
`UTacticsGameInstance::ApplySavedUserSettings()` сразу после создания или
загрузки слота, а меню настроек пишет в тот же слот.

Ассетов звука под бой в проекте пока нет: каркас рассчитан на подстановку
файлов в Data Asset без единой правки кода.

## 9.6. Визуал выстрела

Слой зеркален звуковому: «когда рисуем» — тот же подтверждённый `FireCommit`,
«что рисуем» — Data Asset. План этапа — [04_ROADMAP §5.6](04_ROADMAP.md).

| Блок | Файл | Роль |
|---|---|---|
| `UUnitVfxDataAsset` | [UnitVfxDataAsset.h](../Source/XRU1/FX/UnitVfxDataAsset.h) | вспышка, трассер, импакты по `EPhysicalSurface`; имена и значения user-параметров Niagara + их применение к компоненту |
| `AShotTracerActor` | [ShotTracerActor.h](../Source/XRU1/FX/ShotTracerActor.h) | носитель трассера: летит от дула к точке удара, по прилёте гасит эмиссию и даёт шлейфу дожить |
| `AUnitBase::PlayShotVfx` | [UnitBase.h](../Source/XRU1/Tactics/UnitBase.h) | единственный вход: одна трассировка задаёт и конец трассера, и точку импакта; импакт откладывается на время полёта |

Ключевое, что легко потерять: трассерные системы Niagara Examples **сами считают
полёт** по своим user-параметрам (`User.SpawnPosition`, `User.Hit`,
`User.InitialSpeed`, `User.TrailDuration`). Не передать их — значит получить
трассер по дефолтным точкам системы, то есть «мимо выстрела». Тип параметра
(Position или Vector) спрашивается у самой системы через `GetExposedParameters()`.

## 10. UI

`UTacticalHUDWidget`, unit/attribute widgets и `UTacticalHUDStyleData` дают
функциональный боевой HUD и единую тему. Остаток относится к presentation и
полноте экранов, а не к переделке боевого ядра. См. [09_UI_HUD.md](09_UI_HUD.md).

## 10.1. Data Assets — где они лежат

Все дизайнерские Data Assets сведены в одно дерево `/Game/XRU1Game/Data`
(`Core` — глобальные, ссылка в `BP_TacticsGameInstance`; `Units` — профили
юнита; `AI` — профили поведения; `Missions` — сценарии и квесты). Правило
размещения, именования и заведения нового ассета — [06_CONVENTIONS §3](06_CONVENTIONS.md).
Из этого же рефакторинга (2026-08-03): презентация обучения выделена из UI-темы
в `UTutorialStyleData`, а editor-библиотека вёрстки больше не грузит тему по
зашитому пути — ищет её по классу через AssetRegistry.

## 11. Blueprint API, который считается контрактом

- `OnFireActionStarted` / `OnReactionFireActionStarted` — выбрать и запустить
  монтаж, не наносить урон;
- `OnShotFired` / `OnReactionShot` — косметика после commit;
- `OnHitReact` и death presentation hooks — один montage, без gameplay-логики;
- getters visual state/cover/firing stance — только чтение для AnimBP/UI;
- `UTacticalAbility` и `CanIssueCommand` — единая проверка допустимости команды;
- `BroadcastQuestEvent` вызывается после подтверждённого результата механики,
  не из `OnClicked` и не в момент старта montage.

При добавлении ability: механика в C++, BP-наследник — ассеты презентации и
настройка. Урон всегда проходит через `UTacticsCombatStatics`.

## 12. Диагностика

Все переключатели собраны в одном реестре
[TacticsDebug.h](../Source/XRU1/Tactics/TacticsDebug.h); `xru1.Debug.List`
печатает их текущие значения прямо в консоль во время теста.

| Команда | Назначение |
|---|---|
| `xru1.Debug.List` | показать все переключатели и категории логов |
| `xru1.AI.LogCombat 1` | варианты AI, score, принятое действие и причина |
| `xru1.AI.DebugDraw 1` | решение AI прямо в мире: цель, точка манёвра, скор |
| `xru1.Quest.LogEvents 1` | каждое quest-событие с источником, целью и RunId |
| `xru1.Tutorial.LogGate 1` | применение политик Action Gate и причины отказов |
| `xru1.Audio.LogEvents 1` | звуковые события и незаполненные реплики |
| `xru1.LOS.Debug 1` | firing positions и линия огня |
| `xru1.Cover.Debug 1` | геометрия, засчитанная укрытием |
| `xru1.MoveRange.LogBuildTime 1` | стоимость построения поля хода |

Логи разведены по доменным категориям вместо общего `LogTemp`:
`LogXRU1AI`, `LogXRU1Combat`, `LogXRU1Turns`, `LogXRU1Scenario`,
`LogXRU1Quest`, `LogXRU1Audio`, `LogXRU1UI`
([XRU1Log.h](../Source/XRU1/XRU1Log.h)). Подробность включается точечно:
`Log LogXRU1AI Verbose`.

После C++-изменений: короткая сборка UE 5.7 при закрытом редакторе, затем PIE.
После правок BP: Compile, Save, повторное чтение графа через UnrealClaude и PIE.

## 13. Открытый технический backlog

P0/P1 на ближайшие этапы:

- настройка и отладка AI по [08_AI.md](08_AI.md);
- actor gating и renderer тумана по [10_FOG_OF_WAR.md](10_FOG_OF_WAR.md);
- баланс и presentation четырёх классовых способностей;
- завершение HUD/экранов по [09_UI_HUD.md](09_UI_HUD.md);
- Editor-интеграция двух сценариев `Main_Map_Showreel` по
  [11_SHARED_MAP_TUTORIAL.md](11_SHARED_MAP_TUTORIAL.md).

Отложено:

- общая переработка тактической/action-camera;
- IK второй руки;
- PCG-полировка окружения и косметические эффекты.
