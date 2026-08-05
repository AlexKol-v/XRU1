# Поэтапная миграция архитектуры

Дата плана: 2026-08-04. Стратегия — не big bang: сначала воспроизводимость, затем correctness и release path, после этого dependency seams и локальная декомпозиция. Физический split runtime-модуля — опциональный последний этап.

## Приоритеты и stop/go

| Этап | Приоритет | Результат | Зависит от | Усилие |
|---|---|---|---|---|
| 0. Baseline | P0 prerequisite | Воспроизводимые тесты, графы, метрики и rollback points | — | S/M |
| 1. Correctness | **High** | Истинное movement settlement, живой enemy turn, fog-safe HUD | 0 | L |
| 2. Release и lifecycle | High для loading UX; Medium для build/test gap | Управляемая загрузка, run cancellation, явный cook/package и clean-machine smoke | 1 | L |
| 3. Editor/dependency seams | Medium | `XRU1Editor`, корректный Build.cs, include-gate | 1; build baseline из 2 | M/L |
| 4. Controller decomposition | Medium | Малые collaborators при неизменном поведении и стабильных BP parents | 1–3 | L |
| 5. Asset/performance | Medium, после измерений | Суженная startup closure, async preload на доказанных cold paths, очищенный persistent map | 2; profiling baseline | M/L |
| 6. Optional physical split | Только по триггерам | UBT-enforced runtime boundaries | 0–5 и второй consumer/ownership/build evidence | XL |

**Stop rule:** пока не закрыт этап 1, запрещено начинать 4 или 6. Физическое перемещение классов не является способом исправить gameplay defect.

## Этап 0 — baseline и наблюдаемость

| Поле | План |
|---|---|
| Цель | Получить повторяемую точку сравнения до изменения кода, карт и ассетов. |
| Системы | Automation, UBT/UHT, Asset Registry, dependency/include graph, сценарный lifecycle, logs/performance. |
| Зависимости | Нет. Редактор и git должны быть в известном состоянии; карта сохраняется до запуска commandlet. |
| Усилие | S для фиксации уже собранных данных; M вместе с первым packaged baseline. |

Конкретные изменения:

1. Зафиксировать audit manifest: commit SHA, UE 5.7, список 2664 `/Game` packages, 92 BP-family assets, 42 World assets, текущий module/plugin graph и 608 intra-module include edges.
2. Сохранить текущий успешный результат `Automation RunTests XRU1.AI`: семь тестов `ScorePositionFacts`, перечисленных в `Source/XRU1/Tactics/Tests/XRU1AITests.cpp:59-250`. Отдельно записать, что они не проверяют world/GAS/lifecycle — это прямо сказано в `XRU1AITests.cpp:3-16`.
3. Добавить исполняемый test manifest с категориями `Unit`, `WorldFunctional`, `AssetValidation`, `BuildCookSmoke`; на этом этапе допустимы ожидаемо красные записи для найденных defects.
4. Снять cold run timeline: запрос сценария → sublevel shown → fog ready → `Scenario.Ready` → первый ввод; отдельно записать hitch/memory, а не угадывать причину.
5. Сохранить Reference Viewer/Asset Registry snapshots для `Main_Map_Showreel`, `BP_TacticsGameInstance`, `DA_TacticsAudio`, двух scenario DA и editor libraries.
6. Выполнить Editor/Game non-unity build и один Development package как baseline. Если команда выходит за пару минут, её запускает пользователь/CI по готовой команде из `Build-XRU1.ps1`; длительный процесс не должен блокировать audit session.

Риски:

- Считать существующие логи старого запуска актуальным доказательством.
- Смешать изменения ассетов с baseline resave.
- Принять disk size за resident memory либо длительность editor preflight за gameplay performance.

Проверки:

- test manifest указывает команду, ожидаемый результат и артефакт лога;
- asset count повторяется без failures;
- dependency scan различает UE modules и логические top-level folders;
- build/cook результат привязан к SHA/configuration.

Definition of Done:

- baseline доступен команде и повторяется;
- семь AI tests зелёные;
- известные Blocker/High cases заведены как красные/ручные acceptance tests;
- есть отдельный rollback point до map/code/asset изменений.

Rollback: удалить только новые audit/test artifacts; gameplay/content не меняются.

Нельзя менять одновременно: любые карты/Blueprint graphs/Build.cs и baseline snapshot. Сначала снимок, затем отдельные изменения.

## Этап 1 — correctness: move, enemy liveness, HUD

| Поле | План |
|---|---|
| Цель | Устранить дефекты, способные заблокировать демо или сообщить системе ложный игровой факт. |
| Системы | movement/AP/tutorial events, TurnManager/AI activation, fog/HUD. |
| Зависимости | Завершён этап 0; для map-правок редактор открыт на копии/ветке с сохранённым baseline. |
| Усилие | L; выполнять четырьмя независимыми вертикальными срезами. |

### 1A. Истинный movement transaction — High

Доказательство: AP игрока списывается при принятии маршрута (`TacticalPlayerController.cpp:1607-1615`). Затем `OnMoveCompleted` различает success только для продолжения segment, но success/failure сходятся в `BeginMoveSettlement` (`UnitAIController.cpp:2854-2912`). Финализация не хранит `FPathFollowingResult`, публикует `Movement.Settled.*` и гасит tutorial destination (`UnitAIController.cpp:2940-3014`). Это противоречит заявленному контракту «маршрут успешен, tolerance достигнут» (`docs/03_ARCHITECTURE.md:654-665`).

Конкретные изменения:

1. Ввести `FTacticalMoveTransaction`/`FTacticalMoveResult` с `ActionId`, start/requested/final positions, path result, cost, фактом выхода из start tolerance.
2. Свести player и AI move completion в одну idempotent `CompleteMove(Result)`; не читать потерянный last result из mutable controller state.
3. Разделить `Succeeded`, `FailedBeforeMovement`, `AbortedAfterMovement`, `CancelledByRun`; явно закрепить AP/refund policy в GDD до реализации, если она отличается от текущего правила.
4. Публиковать `Movement.Settled` и `NotifyDestinationReached` только для success + destination tolerance; отдельный failure result доступен UI/log/test, но не засчитывает quest progress.
5. Late callback проверяет action/run handle.

Проверки: success; invalid path; abort до displacement; abort после displacement; pawn destroyed; travel/retry; cover hug/turn still pending; segmented 2-AP route. Для каждого проверяются AP, финальная позиция, ровно одно событие и tutorial counter.

Definition of Done: truth table зелёная, ложный `Settled` невозможен по API, fire service movement не публикует player quest event, старый run игнорируется.

Rollback: сохранить старые public controller methods как facade; новый transaction можно отключить feature flag только до миграции quest events. После миграции rollback должен возвращать весь vertical slice, а не половину event policy.

Нельзя менять одновременно: latent fire/overwatch Blueprint graphs, cover geometry/tolerances и controller decomposition.

### 1B. Liveness enemy turn — High

Доказательство: `TurnManager` ждёт единственный callback от `AUnitAIController` (`TurnManagerSubsystem.cpp:493-529`), а unregister текущего элемента корректирует индекс только при `EnemyIndex < EnemyTurnIndex`, не при равенстве (`TurnManagerSubsystem.cpp:119-145`). Это создаёт правдоподобный freeze/skip при deactivation/destruction текущего enemy; confidence Medium, поэтому тест должен предшествовать широкому рефакторингу.

Конкретные изменения:

1. Выдавать `FEnemyActivationHandle(TurnId, Sequence, Unit)` и принимать idempotent complete/cancel только для active token.
2. `UnregisterUnitFromCombat`, death/deactivation, controller loss и EndPlay текущего unit атомарно cancel-ят token и планируют следующего.
3. Добавить bounded watchdog с диагностикой action/StateTree/move state; watchdog восстанавливает очередь, но не скрывает первопричину.
4. Очистить timers/delegates в EndPlay/StopCombat.

Проверки: normal finish, hidden unit, death до activate, death во время move/fire, scenario deactivation, controller detached, duplicate/late callback, stop combat, last unit. Проверяется ровно один advance и возврат player phase.

Definition of Done: ни один terminal path не оставляет active token; duplicate callbacks безопасны; watchdog test детерминирован и логирует контекст.

Rollback: token adapter может вызывать прежний `HandleEnemyUnitFinished`, пока все terminal paths не мигрированы; не удалять старый callback в том же change, где добавляется первый token.

Нельзя менять одновременно: evaluator weights/StateTree behavior и порядок enemy queue.

### 1C. Visibility-safe HUD — High

Доказательство: C++ имеет `GetAliveEnemyCount` и `GetVisibleEnemyCount` (`TacticalHUDWidget.cpp:121-132`), но `/Game/XRU1Game/UI/WBP_TacticalHUD` подключает `Get Alive Enemy Count → ToText → EnemyCountText`, раскрывая скрытых врагов.

Конкретные изменения: подключить visible read model; убрать full count из designer-facing HUD API либо явно пометить debug; добавить BP/functional assertion при одном скрытом и одном видимом enemy.

Проверки: fog reset, reveal/hide/death, tutorial fully-explored policy и повторный run. Число не должно раскрывать скрытых и должно обновляться по одному canonical visibility event.

Definition of Done: HUD показывает только известное игроку; automation/asset test запрещает вызов full count в tactical HUD graph.

Rollback: одна BP graph правка с screenshot/export baseline; C++ full-count API временно остаётся для debug.

Нельзя менять одновременно: fog algorithm/material/grid threading. Асинхронность fog не доказана и к этому исправлению не относится.

## Этап 2 — release path и lifecycle

| Поле | План |
|---|---|
| Цель | Сделать запуск/загрузку/retry/travel/cook проверяемым продуктовым потоком. |
| Системы | Scenario GI/Director/GameMode, loading UI, Asset Manager/cook, save/settings, input/delegates, build/package. |
| Зависимости | Этап 1 зелёный; scenario-ready и action/run completion имеют наблюдаемые события. |
| Усилие | L. Loading UX — **High**; отсутствие широкого build/test pipeline — **Medium**, а не доказанный runtime failure. |

Конкретные изменения:

1. Добавить полноэкранный loading screen до travel/stream и снимать по факту `Scenario.Ready`, не по таймеру. Точки подключения уже описаны в `docs/04_BACKLOG.md`. Он должен закрывать первый кадр до fog update и показывать recoverable error при timeout подготовки сценария или ассетов.
2. Ввести `FScenarioRunHandle` и захватывать его во всех delayed callbacks/quest messages. `RestartActiveScenario` разрешать в C++ только из terminal/idle state; mid-action retry отклонять (`docs/03_ARCHITECTURE.md:793-803`).
3. На travel/EndPlay: cancel active move/fire/AI tokens, reset quest listeners/timers, `RemoveMappingContext` для добавленного в `TacticalPlayerController.cpp:219-233` context, снять selected-unit/tutorial/fog delegates.
4. Зафиксировать Asset Manager/cook policy: shared map, два scenario sublevels, scenario DA, quest definitions/StateTrees, unit BP/abilities, UI/media/voice. Добавить explicit maps/rules вместо надежды на случайную reference closure.
5. Добавить save schema version, migration и invalid/corrupt slot flow. Текущий GI напрямую cast-ит результат `LoadGameFromSlot` (`TacticsGameInstance.cpp:57-59`), а `UTacticsSaveGame` уже содержит legacy settings (`TacticsSaveGame.h:47-59`).
6. Создать Windows UE 5.7 CI/локальный gate: UHT, Editor/Game non-unity, семь unit tests, world/asset tests, Development cook/package, Shipping package, clean-machine smoke. `.github/workflows` сейчас не содержит pipeline.

Риски:

- Loading widget сам становится hard-reference hub.
- Cook «проходит», но soft scenario/voice отсутствуют на чистой машине.
- Retry UI безопасен, но BlueprintCallable/C++ API оставляет обход.
- Одновременное изменение save schema и gameplay progression затрудняет миграцию.

Проверки:

- Hub→Tutorial→Hub→Mission и оба retry path как чистые запуски;
- cancel во время stream/readiness wait/voice/action, затем отсутствие late events старого run;
- missing/corrupt save, upgrade legacy slot, write failure;
- Development и Shipping package запускаются без editor plugins; clean machine имеет все scenario/quest/media assets;
- loading screen появляется до первого exposed frame и исчезает только после ready/error.

Definition of Done:

- пользователь никогда не видит незапечённый fog/расстановку во время запуска;
- terminal/retry/travel имеют единый cancel contract;
- cook/package и clean-machine smoke воспроизводимы и привязаны к SHA;
- release manifest содержит карты/primary assets;
- тестовый пробел классифицирован Medium и закрывается gates, не переименовывается в «доказанный Critical crash».

Rollback: loading screen и manifest меняются отдельными slices; старый map travel остаётся fallback только до успешного packaged smoke. Save migration никогда не удаляет исходный slot до подтверждённой записи новой версии.

Нельзя менять одновременно: content compression/форматы media, campaign progression rules и save migration; quest framework split и cook rules; async asset refactor и loading screen lifecycle.

## Этап 3 — editor/runtime и dependency seams

| Поле | План |
|---|---|
| Цель | Получить одну физически полезную границу (`XRU1Editor`) и остановить рост обратных include без дробления runtime. |
| Системы | `.uproject`, Target/Build.cs, authoring libraries, include graph, plugin module declarations. |
| Зависимости | Correctness зелёный; есть Editor/Game non-unity и packaged gates из этапа 2. |
| Усилие | M/L из-за UCLASS/asset-path проверки. |

Конкретные изменения:

1. Добавить `XRU1Editor` Type=Editor; перенести `UI/Editor/XRU1WidgetAuthoringLibrary` и `Tactics/Editor/XRU1StateTreeAuthoringLibrary`. Сейчас editor modules условно подключаются внутри runtime build rules (`XRU1.Build.cs:54-69`), а обе библиотеки экспортируются `XRU1_API` (`XRU1WidgetAuthoringLibrary.h:26-27`, `XRU1StateTreeAuthoringLibrary.h:29-30`).
2. До переноса получить Asset Registry referencers для обоих UCLASS. При наличии serialized references использовать forwarding facade либо `CoreRedirects`, открыть/resave/cook затронутые assets.
3. Сузить `XRU1.Build.cs`: internal engine deps private по умолчанию; public только используемые public headers. Добавить недостающую прямую `RHI` для `FogGridSubsystem.cpp:21,944`; исправить public/private mismatches GameplayMessageRuntime, TeamManager и UnrealClaude отдельными plugin changes.
4. Убрать broad `PublicIncludePaths` (`XRU1.Build.cs:72-84`) после root-qualified includes. Не делать одномоментный rewrite: включить warning baseline и запрещать новые нарушения.
5. Ввести архитектурный include gate: запрещены новые Tactics→concrete UI, Contracts→outer layers, runtime→Editor; текущие 6 bidirectional folder pairs уменьшаются по touched-file policy.
6. Доказать/отключить `TeamManager`: он enabled (`XRU1.uproject:46-48`), но прямой C++/asset consumer аудитом не найден. Отсутствие grep не использовать как единственное доказательство удаления.

Риски: изменение `/Script` package editor utilities, скрытые transitive dependencies, unity build маскирует missing includes, plugin patch усложняет upstream update.

Проверки: UHT; Editor/Game Development non-unity; Shipping package; launch without UnrealClaude; authoring utility smoke; Blueprint/DataAsset load without redirect warnings; include graph не получает новых forbidden edges.

Definition of Done: runtime target не видит UnrealEd/UMGEditor/StateTreeEditor; `XRU1Editor` загружается только в editor; прямые module dependencies объявлены; architecture gate работает на изменённых файлах.

Rollback: сначала новый module и копия/facade, затем перенос implementation, и только после asset validation удаление старого пути. Plugin dependency fixes — отдельные commits.

Нельзя менять одновременно: runtime module split, gameplay UCLASS parents, mass asset resave и editor-module move.

## Этап 4 — декомпозиция controller-ов и presentation seam

| Поле | План |
|---|---|
| Цель | Уменьшить responsibility hotspots при неизменных BP parents, rules и serialized paths. |
| Системы | `ATacticalPlayerController`, `AUnitAIController`, `ATacticsGameMode`, UI manager, tutorial/camera/preview/AI route execution. |
| Зависимости | Move/activation contracts этапа 1 и lifecycle/build gates этапов 2–3. |
| Усилие | L, малыми behavior-preserving slices. |

Конкретные изменения:

1. Оставить `ATacticalPlayerController` facade и Blueprint API. По одному извлечь collaborators: selection/read model, move preview/planning adapter, command coordinator, camera controller, tutorial presentation adapter. `CanIssueCommand` остаётся единственным policy entry до появления contract tests (`TacticalPlayerController.h:50-76,223-224`).
2. Оставить `AUnitAIController` UE/navigation facade. Извлечь pure scorer первым, затем `FEnemyActivationContext`, route executor и отдельно perception/decision adapter. `ExecuteUnitTurn` продолжает делегировать старому BP/TurnManager API на время миграции (`UnitAIController.h:69-79,159,609,702`).
3. Перенести UI push из GameMode в presentation owner через `FCombatStartedView`/`FMissionResultView`; GameMode публикует terminal fact, `UGameUIManagerSubsystem` выбирает widget. Текущая связь (`TacticsGameMode.cpp:490-499,707-719`) допустима до этого этапа и не требует срочного service layer.
4. Сохранить `ATacticsGameMode`, controller classes и unit classes в `/Script/XRU1`; collaborators делать plain structs/UObjects/components только там, где нужен UE lifetime/reflection.
5. Для BP_GA_Attack/Overwatch сначала characterization test action id/watchdog/montage paths. Общий presentation helper выделять отдельным change, mechanics C++ не трогать одновременно.

Риски: размножение manager/coordinator types, новый порядок BeginPlay, duplicate subscriptions, потеря BlueprintCallable API, изменение timing latent callbacks.

Проверки: существующие BP parents/graphs compile; command truth table; selection/camera/tutorial functional cases; fire normal/squadsight/overwatch; AI turn liveness; PIE+Standalone+packaged travel. Сравнить event trace до/после для одинакового seed.

Definition of Done: фасады тоньше по responsibilities, не обязательно по строкам; каждый collaborator имеет owner/lifetime/test; domain больше не push-ит concrete widgets; нет новых subsystem/service locator lookups в leaf code; gameplay trace эквивалентен baseline.

Rollback: один collaborator за change; старый facade method переключается на него одной точкой и может вернуться к inline implementation без asset redirect.

Нельзя менять одновременно: GDD numbers/AI weights, cover geometry, UCLASS inheritance, physical module ownership и Blueprint latent presentation graph.

## Этап 5 — asset graph и производительность

| Поле | План |
|---|---|
| Цель | Убрать доказанные лишние загрузочные связи и hitch на критических переходах, не вводя необоснованную многопоточность. |
| Системы | GI/audio settings, scenario/UI/voice soft refs, Asset Manager bundles, persistent map, menu/tutorial loading, BP presentation duplication. |
| Зависимости | Packaged/cook gates этапа 2; stable owners этапа 4; Unreal Insights/asset closure baseline этапа 0. |
| Усилие | M/L, по одному dependency hub. |

Конкретные изменения:

1. Разорвать startup hard closure `BP_TacticsGameInstance → DA_TacticsAudio → music/stingers`: пять music/stinger assets дают около 75.17 MiB disk proxy из 82.25 MiB startup closure. Перевести current-state music на soft refs + owned preload/cancel.
2. Классифицировать 23 `LoadSynchronous` sites. Начать с travel/briefing/tutorial/first-use paths (`TacticsGameInstance.cpp:128`, `TacticalScenarioDirector.cpp:176`, `TutorialPresentation.cpp:119`, `UI/Menus/MenuWidgets.cpp:507-509,670`); оставить редкий harmless sync только с измерением и документированным fallback.
3. Убрать 26 донорских sublevel references из `Main_Map_Showreel` после проверки streaming flags/referencers/cook diff. Два XRU1 scenario sublevel сохраняются data-driven.
4. Определить bundles `ScenarioCore`, `Briefing`, `CombatAudio`, `TutorialVO`, `Result`; preload привязан к loading screen/run handle, release — к terminal/travel.
5. Для BP_GA_Attack/Overwatch вынести повторяющуюся presentation sequence только после regression trace; не объединять domain mechanics ради сокращения node count.
6. Установить budgets на cold start, scenario-ready, first action hitch, resident memory и package size из baseline, а не произвольные абсолютные числа.
7. Fog/AI parallelization рассматривать только если Unreal Insights показывает hotspot и определён immutable snapshot/cancellation. Текущий аудит **не доказал**, что fog async нужен.

Риски: asset отсутствует в cook, async callback приходит после run, визуальный popup ждёт bundle, слишком ранний unload обрывает voice/music, package size падает ценой runtime hitch.

Проверки: Asset Audit before/after closure; cold/warm launches; искусственно замедленная загрузка; cancel/travel; missing asset fallback; packaged clean-machine; Unreal Insights bookmark по каждому lifecycle milestone; memory после двух retries не растёт.

Definition of Done: startup closure и critical hitch измеримо улучшены; ни один required asset не зависит от editor reference closure; callbacks проверяют run handle; cook manifest зелёный; fog остаётся sync либо меняется только с отдельным доказательством/планом thread safety.

Rollback: каждый dependency hub мигрируется отдельно, старый hard/sync path сохраняется как временный fallback до packaged validation; bundle rules versioned.

Нельзя менять одновременно: async lifetime и save/retry protocol; music soft refs и audio mixer policy; persistent map references; fog algorithm и threading.

## Этап 6 — опциональный physical runtime split

| Поле | План |
|---|---|
| Цель | Получить UBT-enforced границы только при подтверждённой организационной/повторной/build-time выгоде. |
| Системы | `XRU1Contracts`, `XRU1Tactics`, `XRU1Scenario`, `XRU1Presentation`, composition `XRU1`, `XRU1Editor`. |
| Зависимости | Этапы 0–5; stable contract tests; clean package; redirect plan; минимум два триггера из `06_TARGET_ARCHITECTURE.md`. |
| Усилие | XL. Этап может остаться невыполненным без архитектурного долга для текущего scope. |

Триггеры:

- второй независимый consumer тех же contracts/tactics;
- отдельные команды/релизный cadence Scenario и Presentation;
- UBT profile показывает измеримую выгоду incremental build;
- logical include gate стабилен и forbidden edges уже устранены;
- публичные contracts пережили release cycle.

Конкретные изменения:

1. Сначала создать leaf `XRU1Contracts` только с value types/tags и contract tests; composition остаётся в `XRU1`.
2. Следующим переносить слой с минимальным UCLASS/asset referencers. Существующие actor/widget parents оставлять facade в `XRU1`, пока выгода переноса не превышает redirect risk.
3. Для каждого UCLASS: referencer report → `CoreRedirects`/facade → UHT/build → open/resave → cook/package → redirect log clean → только затем удаление старого класса.
4. Не допускать Tactics→Presentation/Scenario и Presentation→concrete GameMode. Root может зависеть от всех, leaf modules — нет.
5. После каждого leaf split переснимать UBT times, include/module graph и package result. Если выгоды нет или появляется cycle, остановить split.

Риски: `/Script/XRU1` breakage, Blueprint parent loss, DataAsset class redirect, экспортные макросы, circular modules, двойные facade API, mass resave conflicts на двух машинах.

Проверки: все contract/world/asset tests; non-unity Editor/Game; Development/Shipping cook/package; Blueprint compile/load; redirect audit; Hub→Tutorial→Mission clean machine; UBT before/after profile.

Definition of Done: каждый модуль имеет владельца, узкий public API и доказанную выгоду; module graph acyclic; old facades удалены только после asset migration; gameplay/release metrics не хуже baseline.

Rollback: один leaf-module на отдельной ветке/commit chain; вернуть implementation за facade, сохранить redirects до следующего release; никогда не откатывать mass resave частично.

Нельзя менять одновременно: два runtime leaf modules; class module и class inheritance/name; module split и gameplay behavior; redirects и unrelated content resave.

## Рекомендуемые первые три действия

1. Завершить baseline и закрыть первый vertical slice movement truth.
2. Реализовать и покрыть truth table `FTacticalMoveResult`, затем activation token; не рефакторить controller-ы параллельно.
3. После зелёного gameplay пройти loading/cook/clean-machine path; только затем создавать `XRU1Editor` и dependency gate.

Этот порядок оставляет проект в запускаемом состоянии после каждого этапа и сохраняет возможность остановиться на варианте A без незавершённой «полумодульной» архитектуры.
