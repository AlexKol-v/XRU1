# Findings глобального архитектурного аудита XRU1

## Как читать список

- `Fact` — непосредственно подтверждено кодом, конфигурацией, Asset Registry, Blueprint query или тестом.
- `Inference` — вывод из нескольких подтверждённых фактов; указан недостающий runtime-test.
- `Hypothesis` — возможный runtime-эффект, который нельзя утверждать без измерения.
- Confidence относится к достоверности формулировки, а не к вероятности проявления.
- Severity откалибрована после независимого critic review. Критик понизил release/test gaps до `Medium` и отделил High loading UX от недоказанной необходимости async fog.

## Сводная таблица

| ID | Severity | Confidence | Категория | Система | Краткая проблема | Доказательство | Последствие | Рекомендация |
| -- | -------- | ---------- | --------- | ------- | ---------------- | -------------- | ----------- | ------------ |
| MOVE-001 | High | High | Correctness / contract | Movement, AP, tutorial, quest events | Failed/aborted path проходит через успешный settlement | `UnitAIController.cpp:2870-2912,2940-3030`; `TacticalPlayerController.cpp:1607-1615`; GDD `:165-168` | ложный tutorial/quest progress, звук успеха, неверный AP при stuck-at-anchor | Typed move transaction/result; публиковать settled только при фактическом success; truth-table tests |
| UI-001 | High | High | Information boundary | Fog, HUD | HUD показывает число всех живых врагов, включая скрытых | WBP node GUID `A62206904B9B469926B54983E1BF4B29`; `TacticalHUDWidget.cpp:121-132` | мета-информация раскрывает unseen encounters | Привязать `EnemyCountText` к `GetVisibleEnemyCount`; BP validation test |
| TURN-001 | High | Medium | Lifecycle / liveness | TurnManager, AI | Нет activation token/watchdog при исчезновении текущего врага | `TurnManagerSubsystem.cpp:119-145,493-529`; `ScenarioActorRegistry.cpp:176-216` | возможен зависший enemy phase или пропуск следующего врага | Tokenized activation; unregister current атомарно cancel+advance; functional fault-injection test |
| UX-001 | High | High | Runtime UX | Scenario bootstrap | Нет readiness-driven loading overlay до `Scenario.Ready` | `docs/04_BACKLOG.md:55-69`; `FogGridSubsystem.cpp:170-208`; scenario bootstrap flow | пользователь видит паузу и раннее раскрытие расстановки | Поднять overlay при запросе и снять только после readiness; async fog — лишь после профиля |
| REL-001 | Medium | High | Build / release | UBT, cook, package | Нет воспроизводимого Game/Shipping/cook/stage контура | `Build-XRU1.ps1:13,57`; `XRU1.Target.cs:10-13`; `DefaultGame.ini:8-14`; пустой `.github/workflows` | editor build не доказывает packaged build и cook soft assets | Добавить локальный validation script и затем self-hosted CI gate |
| TEST-001 | Medium | High | Testability | Gameplay lifecycle | Семь тестов проверяют только pure AI scoring | `XRU1AITests.cpp:5-23,59-253`; backlog `:121-130,264-282`; текущий run 7/7 pass | movement/turn/fog/stream/retry/save regressions обнаруживаются вручную | Ввести `XRU1.Smoke` и functional lifecycle matrix |
| DEP-001 | Medium | High | Dependencies | Logical areas inside XRU1 | Folder boundaries двунаправленны и не защищены | 608 includes, 170 cross-area; `UI↔Tactics`, `Tactics↔Audio`, `UI↔Subtitles`, др. | изменение домена распространяется на presentation и обратно | Зафиксировать allowed edges и автоматический include-boundary gate; не называть это UE module cycle |
| MOD-001 | Medium | High | Module contract | Build.cs, editor code | Все 26 deps Public, Private пуст; есть missing direct deps и editor authoring внутри runtime | `XRU1.Build.cs:11-84`; `FogGridSubsystem.cpp:21,944`; editor libraries | транзитивная хрупкость, большой compile surface, размытый runtime/editor boundary | Исправить direct deps/visibility; добавить `XRU1Editor` с compatibility plan |
| CODE-001 | Medium | High | Maintainability | PlayerController, AIController | Два façade концентрируют слишком много изменяющихся обязанностей | PC 4054 строк; AI controller 4557; многочисленные subsystem lookups | высокая regression surface и трудные unit tests | Сохранить façade, по контрактным тестам извлечь cohesive collaborators |
| SAVE-001 | Medium | High | Persistence | Campaign save | Результаты save/load теряются, повреждённый слот обрабатывается молча | `TacticsGameInstance.cpp:26-60`; `MenuWidgets.cpp:308-327`; `TacticsGameMode.cpp:691-715` | потеря прогресса/неработающий Continue без объяснения | Typed persistence result, logging/UI error, quarantine corrupt slot, negative tests |
| LOAD-001 | Medium | Medium | Asset lifecycle / performance | Audio, UI, scenario | 23 sync loads и hard startup music closure; hitch не измерен | `LoadSynchronous` scan; `TacticsAudioSubsystem.h:82-96`; GI→audio DA asset edge | возможные cold-cache hitches и ранняя residency крупных треков | Packaged LoadTime trace; затем scenario/screen bundles и async preload, если подтвердится |
| RUN-001 | Medium | Medium | Generation safety | Retry, quest events | Public restart не имеет C++ mid-action guard, emitters читают текущий RunId | `TacticsGameInstance.h:149-155`, `.cpp:115-175`; `TacticalQuestEvents.cpp:79-129` | поздний callback старого run потенциально маркируется новым generation | Capture `FScenarioRunHandle`; guard restart; abort/retry fault tests |
| SEC-001 | Medium | High | Developer security | UnrealClaude | Shared config auto-approves script execution; HTTP tool path без auth | `DefaultEditor.ini:15-16`; `ScriptExecutionManager.cpp:84-103,287-325`; MCP server routes | локальный процесс может использовать editor code-execution surface | Safe shared default; per-session local opt-in/token; не считать shipping issue |
| DOC-001 | Medium | High | Documentation | Agent/workflow contracts | Source-of-truth docs и AGENTS/code comments расходятся | `AGENTS.md:15-18,58-59,78,88`; GDD fog/result/cover; broken doc refs | агенты и разработчики получают неверные входные правила | Синхронизировать AGENTS и GDD после correctness fixes; archive historical briefs |
| FLOW-001 | Low | High | Layering | GameMode, UI | GameMode напрямую создаёт concrete combat UI | `TacticsGameMode.cpp:23-25,490-499,707-719`; PC root init `:100-106` | скрытая BeginPlay-order dependency; ограниченная testability | После High fixes публиковать typed combat events; presentation owner пушит экран |
| BP-001 | Low | High | Blueprint maintainability | Attack/Overwatch presentation | Два больших latent presentation graph почти дублируют topology | `BP_GA_Attack` 115 nodes; `BP_GA_Overwatch` 107; по 5 unit dependents | presentation fixes могут расходиться | После action tests выделить общий native/presentation helper, не переносить механику в BP |
| LIFE-001 | Low | High | Cleanup | Enhanced Input, delegates | Mapping context и часть subscriptions не снимаются симметрично | `TacticalPlayerController.cpp:172-233`; отсутствие AI `EndPlay` | возможны дубли при нестандартном travel/possess; сейчас scope ограничен | Идемпотентный teardown и repeated-travel test |
| DATA-001 | Low | High | Data evolution | SaveGame | У формата кампании нет явной версии и migration pipeline | `TacticsSaveGame.h:19-60` | будущие enum/roster changes сложнее мигрировать | Добавить version перед несовместимым schema change и fixtures старых слотов |

## Подробные findings

### MOVE-001 — failure-path выдаёт наружу успешное завершение перемещения

**Статус:** Fact. **Impact:** высокий. **Likelihood:** средняя, зависит от path failure/abort/stuck. **Change risk:** High, контракт касается player/AI/tutorial/quest. **Effort:** Medium.

`AUnitAIController::OnMoveCompleted` продолжает route только при `Result.IsSuccess()`; при failure вызывает `StopRoute()` и идёт в тот же `BeginMoveSettlement` (`UnitAIController.cpp:2870-2912`). `TryFinalizeMoveSettlement` не сохраняет `FPathFollowingResult`, origin или факт выхода из anchor. Для player-ordered move он безусловно играет `MoveSettled`, публикует один из `Event_Tactical_Movement_Settled_*` и сообщает tutorial gate о достигнутой **точке приказа**, не фактической позиции (`:2940-3014`). AI AP затем списывается независимо от результата (`:3023-3030`). У игрока AP списывается при успешном принятии path request (`TacticalPlayerController.cpp:1607-1615`).

Независимый critic уточнил: расход AP не всегда ошибочен — GDD `:165-168` считает AP потраченным после реального выхода из anchor даже при interruption. Ошибка всегда присутствует в semantic result/event, а AP становится ошибкой для no-route/reservation/engine-stuck у anchor.

**Исправление:** `FMoveTransaction {ActionId, Origin, RequestedDestination, ReservedCost, bLeftAnchor}` + `EMoveTerminalResult {Succeeded, FailedBeforeLeave, InterruptedAfterLeave, Aborted, PawnLost}`. Settlement позы и cover может оставаться общим, но quest/tutorial/audio и AP policy должны потреблять typed result. Нужна таблица automation/functional tests на request rejected, abort before leave, stuck at anchor, abort after leave, success with cover hug и pawn destroyed.

### UI-001 — счётчик HUD нарушает fog knowledge boundary

**Статус:** Fact. **Impact:** высокий для правил fog/encounters. **Likelihood:** постоянная в бою. **Change risk:** Low. **Effort:** Small.

Read-only Blueprint query `/Game/XRU1Game/UI/WBP_TacticalHUD` показал подключённую цепь `OnUnitsStateChanged → Get Alive Enemy Count` (node GUID `A62206904B9B469926B54983E1BF4B29`) → text conversion → `EnemyCountText`. Native API `GetAliveEnemyCount` читает полную сторону TurnManager (`TacticalHUDWidget.cpp:121-125`), а рядом уже реализован безопасный `GetVisibleEnemyCount`, делегирующий `UFogOfWarSubsystem` (`:128-132`). Архитектурный контракт прямо требует visibility-filtered HUD (`docs/03_ARCHITECTURE.md:1087-1092`).

**Исправление:** заменить узел и повторно прочитать graph. Добавить Blueprint structure validation: свойство `EnemyCountText` не должно иметь transitive dependency на alive-count API. Functional test: один visible + один hidden enemy показывает 1, а победа всё равно использует полный alive count внутри TurnManager.

### TURN-001 — последовательная enemy activation не имеет liveness guarantee

**Статус:** Inference, production reachability не доказана. **Impact:** потенциально высокий. **Likelihood:** неизвестна. **Change risk:** Medium–High. **Effort:** Medium.

`ActivateCurrentEnemyUnit` вызывает `ExecuteUnitTurn` и ждёт единственный `HandleEnemyUnitFinished` callback (`TurnManagerSubsystem.cpp:493-529`). Watchdog/token в TurnManager отсутствует. `UnregisterUnitFromCombat` удаляет врага и корректирует индекс только при `EnemyIndex < EnemyTurnIndex`, не при удалении текущего (`:119-145`). Scenario registry способен сначала выключить actor/controller tick и затем unregister (`ScenarioActorRegistry.cpp:176-216`). Если callback не придёт, фаза зависнет; если придёт поздно после уплотнения массива, `++EnemyTurnIndex` способен пропустить нового элемента на текущем индексе.

Критик подтвердил алгоритмическую дыру, но потребовал runtime evidence, что production flow действительно деактивирует active enemy до callback. Поэтому finding не переведён в Fact.

**Исправление:** TurnManager создаёт `FEnemyActivationToken` с generation/unit/once-only completion. Unregister current вызывает idempotent cancel/complete; late callbacks по старому token игнорируются. Bounded watchdog нужен как последний liveness guard с diagnostic reason, не как штатный таймер геймплея. Тест должен удалить, down, unpossess и deactivate current AI в разных шагах и доказать переход ровно к следующему.

### UX-001 — bootstrap миссии виден игроку и не закрыт readiness-driven overlay

**Статус:** Fact. **Impact:** высокий для первого впечатления и fog secrecy. **Likelihood:** высокая. **Change risk:** Low–Medium. **Effort:** Small–Medium.

Project backlog прямо фиксирует видимую паузу: streaming scenario sublevel, fog bake, encounter spawn и camera staging происходят до первого хода, а loading widget отсутствует (`docs/04_BACKLOG.md`). `UFogGridSubsystem::ResetForScenario` синхронно строит grid, растеризует visibility и загружает texture (`FogGridSubsystem.cpp:170-208`); build проходит по сетке с world collision queries (`:356-410`). Однако текущий лог показывает fog bake порядка 85–95 мс, поэтому утверждение «fog вызывает многосекундный stall» было **отвергнуто** critic review.

**Исправление:** presentation owner поднимает fullscreen overlay при подтверждении сценария и снимает его по `Scenario.Ready`, не по fixed delay. Сначала UX seam; затем Insights решает, нужно ли инкрементальное построение. World collision queries нельзя механически переносить на worker thread.

### REL-001 — editor build не является release pipeline

**Статус:** Fact для отсутствия gate; actual cook failure — Unknown. **Impact:** средний process/release risk. **Likelihood:** высокая, пока packaged contour не создан. **Change risk:** Low. **Effort:** Medium.

`Build-XRU1.ps1` выбирает `Development` и вызывает только `XRU1Editor Win64` (`:13,57`). Game target существует (`Source/XRU1.Target.cs:10-13`), но project config не содержит `MapsToCook`, `DirectoriesToAlwaysCook`, `BuildCookRun` или package profile; `.github/workflows` пуст. Quest scan указан с `CookRule=Unknown` (`DefaultGame.ini:8-14`). Backlog честно оставляет cook и Development→Shipping→clean machine незакрытыми (`:129-130,317-319`).

Критик понизил finding с High: отсутствие CI не доказывает runtime bug, а выпуск ещё официально не принят. Рекомендуется сначала локальный `Validate-XRU1.ps1`, использующий тот же registry-based UE 5.7 discovery: Editor Development, Game Development/Shipping, tests, Data Validation, explicit-map cook/stage, затем smoke. CI — self-hosted Windows gate после стабилизации команды.

### TEST-001 — тестовый контур не покрывает stateful gameplay

**Статус:** Fact. **Impact:** средний regression risk. **Likelihood:** высокая при изменении lifecycle. **Change risk:** Low. **Effort:** Medium–High.

В проектном `Source/` один test file и 7 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`; файл сознательно ограничен world-free `ScorePositionFacts` (`XRU1AITests.cpp:5-23,59-253`). Нет project functional tests для movement failure, enemy activation, streaming, fog knowledge, GAS lifecycle, quest retry, save/corruption или UI. Все семь тестов в текущем editor-run прошли; после 600-секундного FPS preflight сами выполнились примерно за три секунды. Это позитивный baseline, но не доказательство vertical slice.

Критик понизил severity до Medium: отсутствие теста повышает риск, но само по себе не является gameplay defect. Минимум `XRU1.Smoke` описан в [08_TEST_AND_VALIDATION_STRATEGY.md](08_TEST_AND_VALIDATION_STRATEGY.md).

### DEP-001 — каталоги работают как features, но зависимости не подтверждают границы

**Статус:** Fact. **Impact:** средний maintainability/rebuild risk. **Likelihood:** постоянная. **Change risk:** Low для gate, Medium для cleanup. **Effort:** Medium.

Прямой include parser нашёл 608 intra-XRU1 include edges, из них 170 пересекают верхние каталоги (28%), образуя 27 направленных area pairs. Крупнейшие: `UI→Tactics` 46, `Tactics→Root` 29, `Tactics→Audio` 13, `Tactics→UI` 12. Есть двунаправленные pairs `UI↔Tactics`, `Tactics↔Audio`, `Tactics↔Subtitles`, `Tactics↔Characters`, `UI↔Subtitles`, `UI↔Hub`.

Это **не** UE module cycles: все каталоги входят в один `XRU1` module, а настоящий module graph ацикличен. Поэтому immediate multi-module split не является исправлением. Сначала нужен machine-readable allowlist и запрет новых reverse edges; текущие edges убирать через DTO/read models/commands по мере изменения feature.

### MOD-001 — module declaration скрывает реальный compile contract

**Статус:** Fact. **Impact:** средний compile/boundary risk. **Likelihood:** высокая при non-unity/physical split. **Change risk:** Medium. **Effort:** Medium.

Все 26 runtime dependencies `XRU1` объявлены `Public`, `PrivateDependencyModuleNames` пуст, а `PublicIncludePaths` экспортирует все feature folders (`XRU1.Build.cs:11-84`). При этом `FogGridSubsystem.cpp:21,944` напрямую использует `RHI`, но `RHI` не объявлен. Аналогичные direct/public contract mismatches есть в plugin modules: UnrealClaude использует `LevelEditor`, public headers используют `DeveloperSettings`/`Json`; TeamManager public controller header включает Engine, объявленный private; GameplayMessageRuntime public header требует CoreUObject, объявленный private. Текущая editor compilation может получать это транзитивно; non-unity/consumer build должен это проверить.

Две reflected editor authoring libraries находятся в runtime `XRU1`, хотя editor deps conditional (`Build.cs:54-69`). Рекомендуемый единственный немедленный physical split — `XRU1Editor`, но перенос reflected classes меняет `/Script/XRU1` path. Нужны compatibility wrappers/CoreRedirects, asset resave и cook smoke; простой file move опасен.

### CODE-001 — controller façade стали responsibility hotspots

**Статус:** Inference на основе size/responsibility facts. **Impact:** средний. **Likelihood:** высокая при расширении. **Change risk:** High. **Effort:** High.

`ATacticalPlayerController` содержит 3185 строк `.cpp` + 869 `.h`, около 97 definitions и не менее 30 текстовых `GetSubsystem<` sites. Он объединяет UI/input lifecycle, hover/path, tutorial gates, selection, move/attack/ability commands, pause, camera, fog/enemy camera/range/auto-end. `AUnitAIController` — 3377 + 1180 строк, около 63 definitions: perception, decision scoring, executor, GAS fire, patrol, route, scripted actions, settlement и terminal cleanup.

Размер сам по себе не доказывает God Object, а controller естественно координирует. Finding основан на числе независимо меняющихся clusters и service-locator surface. Не нужен wholesale rewrite: оставить controller façade; сначала вынести pure scorer/transaction records, затем route/activation executor и controller collaborators под characterization tests. Не менять AI weights одновременно.

### SAVE-001 — persistence failures не имеют типизированного результата

**Статус:** Fact. **Impact:** средний, возможна потеря последнего прогресса. **Likelihood:** низкая–средняя, зависит от corrupt/read-only/disk failure. **Change risk:** Low–Medium. **Effort:** Small–Medium.

`HasSaveGame` проверяет только наличие (`TacticsGameInstance.cpp:26-29`). `LoadCampaign` возвращает cast result без error reason (`:57-60`); `CanContinue` активирует кнопку для любого существующего слота, а failed load молча оставляет экран (`MenuWidgets.cpp:308-327`). `StartNewCampaign` игнорирует результат первой записи (`TacticsGameInstance.cpp:31-45`), победа меняет `CompletedMissions` и игнорирует `SaveCampaign` перед показом result (`TacticsGameMode.cpp:691-715`). Аналогично сохраняются hub flags/POI.

Нужен `FPersistenceResult {Status, UserMessage, TechnicalReason}`; corrupt/wrong-class slot карантинируется или предлагает новую кампанию. UI/travel после New Game и подтверждение victory progression должны явно учитывать write result. Tests: corrupt/wrong class, unavailable path, simulated write fail и recovery.

### LOAD-001 — asset loading policy не отделяет startup, screen boundary и hot path

**Статус:** Hypothesis для hitch; Fact для references/calls. **Impact:** потенциально средний. **Likelihood:** неизвестна. **Change risk:** Medium. **Effort:** Medium.

В `Source/XRU1` найдено 23 `LoadSynchronous()` и ни одного фактического `RequestAsyncLoad`/`FStreamableHandle`. Наиболее чувствительны voice event, tutorial beat и first ability targeting; menu/briefing/result calls происходят на естественной screen boundary и менее опасны. `BP_TacticsGameInstance` hard-references audio DA; его пять music/stinger `TObjectPtr` (`TacticsAudioSubsystem.h:82-96`) дают startup closure около 82.25 MiB on-disk proxy, из них 75.17 MiB — пять audio packages. Disk size не равен RAM residency.

Нельзя объявлять performance bug до packaged cold-cache LoadTime trace. Если hitch/residency подтвердятся, определить `ScenarioCore`, `ScenarioPresentation`, `Menu`, `Hub`, `Combat` bundles; async preload держит strong handle до конца run. Не переводить все soft refs механически.

### RUN-001 — generation safety не захватывается в latent action

**Статус:** Inference/Hypothesis. **Impact:** средний при нестандартном вызове. **Likelihood:** низкая через текущий UI, неизвестна через Blueprint/API. **Change risk:** Medium. **Effort:** Medium.

`RestartActiveScenario` — public `BlueprintCallable` без terminal-state guard (`TacticsGameInstance.h:149-155`, `.cpp:172-175`). `PrepareScenarioRun` сбрасывает quest runtime и увеличивает current RunId до travel (`:115-157`). `UTacticalQuestEvents` в момент broadcast читает **текущий** GI RunId (`TacticalQuestEvents.cpp:79-129`), а не generation, захваченный начавшим latent action. Нормальная кнопка result вызывается после terminal finalization, поэтому production bug не доказан.

Нужен `FScenarioRunHandle` в director/action contexts; event publish принимает captured handle, а stale handle отбрасывается. C++ guard разрешает retry только из terminal/explicit abort state. Tests: restart during move, aim, reaction, fire before/after commit и voice timer.

### SEC-001 — доверенная editor automation включена shared-конфигом

**Статус:** Fact для configuration/call path; compromise scenario — Inference. **Impact:** средний для developer workstation, отсутствует для packaged runtime. **Likelihood:** низкая при доверенной локальной среде. **Change risk:** Low. **Effort:** Small.

`UnrealClaude` — `Editor/PostEngineInit` module, поэтому это не shipping attack surface. Но `DefaultEditor.ini:15-16` хранит `bAutoApproveScripts=True`; `ScriptExecutionManager.cpp:84-103` обходит permission dialog, а execution path пишет/исполняет Python (`:287-325`). MCP HTTP routes не показывают auth token; engine default bind — loopback, CORS не аутентифицирует non-browser local clients. Docs называют это сознательным компромиссом.

Shared default должен быть safe; auto-approve включается per-user/per-session. Дополнительно — случайный session token или explicit server enable window. Android dev token в отчёте намеренно не воспроизводится; shipping inclusion отключён.

### DOC-001 — документация больше не является однозначным source of truth

**Статус:** Fact. **Impact:** средний для agent-driven development. **Likelihood:** постоянная. **Change risk:** Low. **Effort:** Small–Medium.

`AGENTS.md` указывает удалённые `03_CODE_OVERVIEW.md`, `04_ROADMAP.md`, `05_EDITOR_GUIDE.md`, `06_CONVENTIONS.md` и несуществующие `Variant_Strategy/Variant_TwinStick`; актуальная схема — `03_ARCHITECTURE`, `04_BACKLOG`, `05_WORKFLOW`. GDD одновременно называет несуществующий `ResolveCoverAgainstDirection`, говорит о fully explored Showreel при фактическом default/обоих profiles false и отмечает `WBP_MissionResult` как не созданный. Код/config содержат ссылки на отсутствующие docs 09/10/13/14. Подробная таблица — [09_DOCUMENTATION_DRIFT.md](09_DOCUMENTATION_DRIFT.md).

Сначала исправить gameplay truth, затем синхронизировать GDD/architecture/backlog/AGENTS одним documentation-only change. Исторические AI briefs пометить archived/superseded, а не использовать как актуальный контракт.

### FLOW-001 — authority напрямую создаёт presentation

**Статус:** Fact. **Impact:** низкий в текущем prototype. **Likelihood:** постоянная. **Change risk:** Medium. **Effort:** Small–Medium.

`ATacticsGameMode` включает concrete HUD/result widget headers и пушит их через `UGameUIManagerSubsystem` (`TacticsGameMode.cpp:23-25,490-499,707-719`). Root layout создаётся PlayerController в BeginPlay, поэтому корректность зависит от порядка и null guards. Критик справедливо отметил, что для single-player prototype это допустимо и не требует срочного service layer.

После High fixes достаточно typed `FCombatStarted/FCombatResult` event/DTO; presentation owner решает, какой widget push. Не вводить универсальный mediator framework.

### BP-001 — duplicated latent presentation graphs

**Статус:** Fact для topology; maintainability impact — Inference. **Impact:** низкий. **Likelihood:** средняя при изменении fire presentation. **Change risk:** Medium. **Effort:** Small–Medium.

`/Game/XRU1Game/Tactics/BP_GA_Attack` содержит 115 nodes/8 variables, `BP_GA_Overwatch` — 107/8; их montage/fire presentation topology близка, оба имеют fan-in от пяти unit Blueprints. Механика остаётся в C++ и защищена `ActionId`, поэтому это не gameplay rule duplication.

После action transaction tests общий native helper или presentation component может унифицировать montage/notify/camera/audio teardown. Не переносить `FireCommit` или damage в BP.

### LIFE-001 — teardown не полностью симметричен startup

**Статус:** Fact. **Impact:** низкий в текущем travel model. **Likelihood:** низкая–средняя при repeated possess/travel. **Change risk:** Low. **Effort:** Small.

Enhanced Input context добавляется в `TacticalPlayerController.cpp:219-233`, но `EndPlay:172-216` не вызывает `RemoveMappingContext`. Часть subscriptions tutorial gate/selected unit также не имеет явного парного removal; AI controller не переопределяет `EndPlay`, хотя timers/delegate чистятся в normal `FinishUnitTurn`. Dynamic multicast/world destruction ограничивают риск, поэтому это Low.

Сделать `TeardownRuntimeBindings()` идемпотентным и вызывать из EndPlay/UnPossess/re-init. Functional test повторяет travel/possess и проверяет одно input action/один callback.

### DATA-001 — save schema не имеет явной версии

**Статус:** Fact для отсутствия version; migration risk — Inference. **Impact:** низкий сейчас. **Likelihood:** растёт при schema change. **Change risk:** Low. **Effort:** Small.

`UTacticsSaveGame` содержит difficulty, missions, POI, roster, hub flag и legacy settings, но нет `SaveFormatVersion`/custom version или invariant validation (`TacticsSaveGame.h:19-60`). UE tagged-property serialization переносит простые добавления полей, а legacy fields уже осознанно сохранены, поэтому немедленной потери совместимости не доказано.

Перед изменением enum/roster/nested structs добавить version, migration switch и fixtures минимум двух старых slots. Не строить сложную migration framework до реального несовместимого изменения.

## Спорные наблюдения, не повышенные до findings

- `/Game/XRU1Game/Maps/Main_Map_Showreel` имеет 26 soft references на legacy/showreel sublevels. Факт references подтверждён, но streaming flags и намерение автора не разобраны; cook/load bloat остаётся Unknown.
- `TeamManager` включён, но не используется C++ и не найден binary-string scan в XRU assets. Asset-level отсутствие не доказано достаточно для автоматического отключения; нужен referencer scan + cook smoke.
- Fog grid fade, move-range sampling и AI LOS/path scoring — кандидаты на Insights, а не подтверждённые performance defects.
- Три `/Game` default paths в style headers и `Build-XRU1.ps1 -StopEditor` с `Stop-Process -Force` — локальные Low observations; они не влияют на выбранную target architecture.
- Первый editor automation run ждал 600 секунд из-за `Current FPS=3`; это environment-specific feedback issue, не доказанный defect проекта.
