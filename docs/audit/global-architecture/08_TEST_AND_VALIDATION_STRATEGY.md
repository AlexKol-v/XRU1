# Стратегия тестирования и валидации

## 1. Диагноз

XRU1 имеет полезное, но очень узкое автоматизированное основание: семь тестов чистого позиционного scoring AI и три вида editor data validation. Критический игровой цикл — загрузка сценария, навигация, ход, перемещение, выстрел, туман, завершение, retry/abort и повторный world travel — автоматическими проверками проекта не покрыт. Cook, staged package и clean-machine smoke не подтверждены.

Это **не доказывает**, что текущая сборка или package сломаны. Независимый critic reviewer откалибровал сам пробел build/test evidence как **Medium**. Отдельно отсутствие экрана загрузки при многостадийном старте сценария остаётся пользовательским риском **High**, но аудит не доказал, что именно fog reset является дорогим или обязан быть async: для этого нужен trace.

Цель стратегии — не максимизировать число тестов, а поставить дешёвые гейты на найденные failure modes и сделать release evidence воспроизводимым.

## 2. Что существует сейчас

### 2.1 Gameplay automation tests

`Source/XRU1/Tactics/Tests/XRU1AITests.cpp:23-255` содержит семь `WITH_DEV_AUTOMATION_TESTS`:

| Test path | Что проверяет |
|---|---|
| `XRU1.AI.Position.CoverBeatsOpen` | закрытая позиция выигрывает у открытой |
| `XRU1.AI.Position.RetreatBreaksLineOfSight` | отход предпочитает разрыв LOS |
| `XRU1.AI.Position.RouteRiskPenalised` | риск маршрута уменьшает score |
| `XRU1.AI.Position.AntiPendulum` | штраф возврата к прошлой позиции |
| `XRU1.AI.Position.CrowdingDampensPositiveOnly` | crowding уменьшает только положительный score |
| `XRU1.AI.Position.IdealRangeAttracts` | score тяготеет к идеальной дистанции |
| `XRU1.AI.Position.AdvanceIgnoresLostLineOfFire` | наступление не штрафуется устаревшим lost-LOF фактом |

Фактический прогон в открытом редакторе:

- команда: `Automation RunTests XRU1.AI`;
- discovery: `Saved/Logs/XRU1.log:4261-4268` — найдено семь тестов;
- result: `Saved/Logs/XRU1.log:4274-4314` — семь `Result={Success}`;
- summary: `Saved/Logs/XRU1.log:4318` — `7 tests performed`;
- editor preflight до discovery занял приблизительно **600 секунд** при editor FPS около 3;
- сами тесты выполнились примерно за **3 секунды**.

Положительный вывод: чистая функция scoring уже имеет test seam и быстро проверяется после discovery. Ограничение: эти тесты не создают World, AIController, navigation path, turn queue или Gameplay Ability и потому не защищают lifecycle.

Тесты editor-only плагина `UnrealClaude` существуют в `Plugins/UnrealClaude/Source/UnrealClaude/Private/Tests`, но они тестируют сторонний editor bridge и не считаются покрытием игры XRU1. В рамках этого аудита они не запускались.

### 2.2 Data validators

| Validator | Evidence | Реальная зона |
|---|---|---|
| `UQuestDefinitionValidator` | `Plugins/STQuestSystem/Source/STQuestSystemEditor/Private/QuestDefinitionValidator.h:10-21`, `.cpp:10-61` | `QuestId`, StateTree, тривиальные self-links цепочки |
| `UUnitHUDLayoutData::IsDataValid` | `Source/XRU1/UI/UnitHUDLayoutData.h:114`, `.cpp:14-119` | конфликты слотов, размеры, padding, индексы и tactical status slot |
| `UTacticalHUDStyleData::IsDataValid` | `Source/XRU1/UI/TacticalHUDStyleData.h:689`, `.cpp:229-315` и далее | размеры, отступы и обязательные UI theme references |

Есть полезная ручная диагностика `xru1.Mission.Validate`, описанная в `docs/03_ARCHITECTURE.md:875-886`; `docs/04_BACKLOG.md:207` всё ещё требует прогнать её на обеих картах. Это диагностическая команда, а не CI gate.

### 2.3 Отсутствующие гейты

- в `.github/` нет workflow-файлов, только `.github/copilot-instructions.md`;
- gameplay functional tests не найдены;
- project smoke/cook/package tests не найдены;
- `Config/DefaultGame.ini:14` сканирует `Quest` как Primary Asset с `CookRule=Unknown`;
- явный `MapsToCook`/эквивалентный release manifest в проектных конфигах не найден;
- `docs/04_BACKLOG.md:129-130` прямо оставляет packaged soft-reference validation открытой;
- `docs/04_BACKLOG.md:262-282` оставляет lifecycle/retry-приёмку открытой;
- `docs/04_BACKLOG.md:317-319` оставляет packaging maps, Development/Shipping и чистую машину открытыми;
- `docs/README.md:52` честно отмечает около 90 написанных, но не подтверждённых ручным прогоном проверок.

## 3. Принципы новой пирамиды

1. **Чистая логика — в быстрые automation tests.** Не поднимать World для таблицы исходов движения, расчёта AP, видимости счётчика или version migration.
2. **Lifecycle — в functional tests с реальным World.** Ошибки регистрации, делегатов, streaming и turn queue нельзя достоверно симулировать одной чистой функцией.
3. **Blueprint и контент — editor validation.** Ошибки parent class, hard reference, обязательных bind widgets и map ownership должны падать до cook.
4. **Release contract — только build/cook/stage/package.** Успешный Editor build не заменяет Game/Shipping/cook.
5. **Performance — сначала baseline, потом gate.** Количество `Tick` или `LoadSynchronous` выбирает точки измерения, но не задаёт допустимое время.
6. **Каждый тест привязан к failure mode.** Если тест не предотвращает конкретную регрессию или не подтверждает критерий сдачи, он не должен становиться обязательным гейтом.

## 4. Быстрые C++ automation tests

### P0 — перед исправлением критичных потоков

| Набор | Обязательные случаи | Инвариант |
|---|---|---|
| Movement transaction | success; abort до выхода из anchor; abort после фактического смещения; path failure; partial multi-segment failure | `Movement.Settled` содержит реальный outcome; tutorial gate получает успех только при подтверждённом достижении; AP refund/списание следует одному контракту |
| Enemy activation token | нормальное завершение; controller уничтожен; unit unregister во время своего хода; stale callback прошлого activation; watchdog timeout | enemy phase всегда либо переходит к следующему token, либо завершается; одно завершение учитывается один раз |
| Fog-safe UI read model | 0/1/N скрытых врагов, появление/исчезновение из LOS, мёртвые враги | UI-visible count никогда не использует полный alive enemy roster |
| Scenario run identity | новый run; retry; callback старого `RunId`; abort во время действия | событие одного запуска не может завершить action/quest другого запуска |
| Save schema | current version; отсутствующий slot; устаревшая версия; обрезанные/некорректные данные | ошибка загрузки имеет явный результат и безопасный fallback, а не молчаливое частичное состояние |

### P1 — после фиксации контрактов

- таблица AP-cost и допустимости всех команд;
- единый расчёт cover для текущей позиции и preview-кандидата;
- attack `ActionId`: duplicate notify, stale notify, watchdog, target death до commit;
- Overwatch: один trigger, duplicate overlap/event, invalid target, teardown;
- objective/outcome state machine: victory, squad defeat, timeout, повторный terminal callback;
- deterministic AI decision при фиксированном seed и одинаковом snapshot.

Эти тесты должны выполняться без карты и не зависеть от wall-clock. Таймеры моделируются явным tick/clock seam, случайность — фиксированным seed, а глобальный `GameInstance` не подменяется скрытой singleton-ссылкой.

## 5. Functional tests в Unreal World

Нужны небольшие test maps/fixtures с минимальным контентом, а не использование 8,9 GiB showcase-карты для каждой проверки.

### 5.1 Обязательные сценарии

| Группа | Сценарий | Критерий успеха |
|---|---|---|
| Bootstrap | MainMenu → Hub | правильные GameInstance/UI root, нет боевых subsystem side effects |
| Scenario selection | Hub → Tutorial | persistent world + только tutorial sublevel, нужный Quest/StateTree, fog reset нового run |
| Scenario selection | Hub → Mission01 | persistent world + только mission sublevel, tutorial actors отсутствуют |
| Turn liveness | несколько врагов; текущий enemy disabled/unregistered/destroyed | очередь продолжает работу и возвращается игроку |
| Movement | success, blocked, aborted path | position/AP/event/gate согласованы |
| Fire | direct attack и Overwatch через montage notify | механика commit ровно один раз; presentation failure не дублирует урон |
| Fog/visibility | враг вне LOS, входит в LOS, покидает LOS, умирает | model, overhead, hover/targeting, camera и HUD count не раскрывают скрытого врага |
| Terminal outcomes | победа, timeout, гибель отряда | один result screen, корректный save/outcome, timers/delegates очищены |
| Retry/abort | два запуска подряд в одном процессе и два чистых запуска | новый `ScenarioId/RunId`, нет actors/delegates/quest state прошлого run |
| Pause/travel | pause → settings → resume; pause → menu | input mapping, time dilation/pause, audio/subtitles и UI layers восстановлены |

### 5.2 Приёмка showcase-карты

Отдельный, более медленный набор запускает `Main_Map_Showreel` с каждым scenario Data Asset. Он должен покрывать матрицу `docs/04_BACKLOG.md:159-282`, включая:

- три последовательных боя на Hard;
- расстановку encounter groups;
- tutorial A1–D3 и отрицательные пути;
- Mission01, подкрепления, defuse/evacuation;
- два последовательных retry/abort без старого World state;
- отсутствие одновременно загруженных tutorial и mission sublevels.

Пока эта матрица не автоматизирована, каждый ручной пункт закрывается только ссылкой на лог/скрин и commit SHA — то же правило уже задано в `docs/04_BACKLOG.md:12-16`.

## 6. Blueprint validation

На каждом content change и перед cook:

1. Compile всех Blueprint в `/Game/XRU1Game`; `Error` и stale skeleton — fail.
2. Проверить, что 46 известных XRU Blueprint имеют разрешённый native parent и не создают неожиданную BP→BP inheritance chain.
3. Для `WBP_TacticalHUD` запретить связь enemy counter с `GetAliveEnemyCount`; разрешённый источник — visibility-safe read model.
4. Проверить обязательные `BindWidget`/`BindWidgetOptional` контракты ключевых экранов: root layout, tactical HUD, pause, briefing, result.
5. Фиксировать connected `Event Tick` как review-required finding. Само наличие несвязанного default Tick node не должно падать.
6. Для `BP_GA_Attack` и `BP_GA_Overwatch` проверить наличие единственного fire-commit path, cleanup path и ActionId guard; изменение графа требует paired functional test.
7. Проверять delegate binds на парный unbind/weak lifetime там, где owner переживает World travel.

Не следует запрещать любые dynamic cast или Tick числовым лимитом: текущий XRU-граф не показывает cast chains, а 39 найденных Tick nodes не имеют connected exec. Гейт должен ловить исполняемое поведение, а не форму шаблонного графа.

## 7. Asset и map validation

### 7.1 Автоматические правила

- Asset Registry перечисляет все ожидаемые startup maps, scenario Data Assets, Quest, StateTree, UI root и unit Blueprint;
- отсутствуют redirectors в `/Game/XRU1Game`;
- каждый scenario имеет существующие `ScenarioSublevel`, `QuestDefinition` и обязательные presentation assets;
- tutorial и Mission01 ссылаются на разные scenario sublevels;
- persistent map не имеет неразрешённой hard/soft зависимости на оба scenario sublevel или donor showreel sublevels; исключения — только в versioned allowlist с объяснением;
- Quest primary assets разрешаются через Asset Manager и имеют явную release cook policy;
- все soft references, нужные до первого хода и на result screen, разрешаются в cooked registry;
- release map manifest содержит MainMenu, Hub, `Main_Map_Showreel` и оба scenario sublevel;
- startup hard-reference closure и самые крупные packages публикуются как отчёт; budget вводится только после baseline.

### 7.2 Existing validators как CI-команда

Запуск `DataValidation` должен охватывать `/Game/XRU1Game`, а не только выбранные вручную ассеты. Результат:

- `Invalid` — fail;
- `Warning` — сначала отчёт и triage, затем только согласованный warning allowlist;
- `NotValidated` — допустим для классов без validator, но не для трёх перечисленных типов с существующим контрактом.

## 8. Dependency validation

Каждый PR должен строить и сравнивать три разных графа:

1. **UBT module/plugin graph** из `.Build.cs`, `.uplugin`, `.uproject` — циклы и неправильное editor→runtime направление.
2. **Direct include graph** — header использует только прямо объявленный модуль; проверяются известные seams `RHI`, `LevelEditor`, `DeveloperSettings`, `Json`, `Engine`, `CoreUObject`.
3. **Внутримодульный folder graph** — новые `UI↔Tactics`, `Tactics↔Audio`, `Tactics↔Subtitles`, `Tactics↔Characters`, `UI↔Hub` edges должны быть либо удалены, либо явно приняты в allowlist до появления.

Первоначальный gate фиксирует baseline и запрещает **новые** циклы/forbidden edges. Он не должен требовать одномоментно устранить весь существующий долг. `codebase-memory` может ускорять навигацию, но CI-решение строится детерминированным parser script из checkout, а не из пользовательского SQLite cache.

## 9. Build, cook, stage и package gates

Все release-проверки выполняются только UE **5.7**, путь движка разрешается через тот же registry mechanism, что и `Build-XRU1.ps1`.

| Gate | Конфигурация | Что доказывает |
|---|---|---|
| UHT/Editor build | `XRU1Editor Win64 Development` | reflection, editor/runtime compile и линковка |
| Game build | `XRU1 Win64 Development` | runtime target без editor-only зависимостей |
| Non-unity build | Editor и Game, по расписанию/перед release | прямые include/module declarations не маскируются unity/PCH |
| Shipping build | `XRU1 Win64 Shipping` | shipping-only compilation и editor exclusion |
| Cook | Win64, явный map manifest | все required maps, Quest и soft references попадают в cook |
| Stage/package | Development, затем Shipping | manifest, pak/IoStore, config и startup работают вне Editor |
| Clean smoke | новая папка/чистая машина | нет зависимости от local DerivedDataCache, uncooked assets и editor install paths |

Release smoke обязан пройти:

1. запуск MainMenu;
2. New Game → difficulty → intro skip/end → Hub;
3. Hub → Tutorial → result → Hub;
4. Hub → Mission01 → victory и один defeat/retry path;
5. settings/save persistence после перезапуска;
6. отсутствие missing package/class/PrimaryAsset ошибок в логе.

Build/cook данного baseline в рамках аудита **не запускались**, поэтому эта секция — требуемый gate, а не уже полученный результат.

## 10. Performance baselines

### 10.1 Что измерять

Снимать Unreal Insights trace для cold и warm запусков на зафиксированной машине и build profile:

- process start → interactive MainMenu;
- нажатие Start Mission → первый управляемый ход;
- отдельно: package streaming, scenario readiness, fog `ResetForScenario`, encounter spawn, Quest/StateTree start, UI push;
- длительность enemy phase и решения одного AI при 4/5/6/13 enemies;
- Game/Render/RHI thread frame time и hitch events во время fog recompute, move preview и камеры выстрела;
- peak resident memory и asset-loading closure MainMenu/Hub/combat;
- synchronous load callstacks для найденных **23** `LoadSynchronous()` sites.

Статический поиск не обнаружил активного `RequestAsyncLoad`/`FStreamableManager` flow в `Source/XRU1`; это повод измерить first-use paths, но не доказательство видимого hitch.

### 10.2 Политика порогов

Числовые thresholds сейчас **не назначаются**: у аудита нет репрезентативного Insights baseline. Правильный порядок:

1. зафиксировать hardware, OS, UE commit/version, build config, карту, scenario и cold/warm cache;
2. выполнить повторяемую серию и сохранить raw trace;
3. опубликовать median, tail percentile и worst case;
4. согласовать UX budget для first-interactive и first-turn;
5. только после этого version-control thresholds и допустимую regression tolerance.

До шага 5 performance job работает informational и сравнивает тренд, но не красит PR произвольным числом.

### 10.3 Калибровка loading finding

`docs/04_BACKLOG.md` подтверждает отсутствие loading screen при streaming, fog preparation, spawn и Quest start. Поэтому защита пользователя от промежуточного состояния — **High** priority даже до профилирования. Однако перенос именно fog в async, изменение потоков или оптимизация grid не должны планироваться без trace: «fog async bottleneck» остаётся **Unknown**, а не Fact.

## 11. Self-hosted Windows UE 5.7 CI

Для проекта с локальным UE 5.7 и крупным LFS-контентом практичен self-hosted Windows runner с закреплённой версией движка. Runner не должен использовать пользовательский редакторский процесс или общую mutable Saved-папку.

### PR — быстрый обязательный контур

- checkout + LFS integrity;
- проверка baseline descriptors и dependency rules;
- format/static sanity без массового форматирования;
- UHT/Editor Development build;
- `XRU1.AI.*` и новые чистые automation tests;
- Blueprint compile + DataValidation для изменённых XRU assets;
- публикация Automation report и логов.

### Main/nightly — интеграционный контур

- clean Editor/Game non-unity build;
- полный Blueprint compile/DataValidation `/Game/XRU1Game`;
- все project automation/functional tests;
- обе scenario-map матрицы и retry/abort;
- dependency и Asset Registry snapshot diff;
- informational Insights baseline на репрезентативной машине.

### Release candidate — release-контур

- Development cook/stage/package + smoke;
- Shipping cook/stage/package + smoke;
- clean-folder или clean-machine run;
- manifest soft-reference/PrimaryAsset validation;
- сохранение logs, Automation JSON/XML, Asset Registry report, cook manifest, package hash и performance traces.

Large LFS upload/download и полный cook могут быть отдельными queued jobs; это не повод исключать их из release gate.

## 12. Этапы внедрения

| Этап | Сначала добавить | Почему |
|---|---|---|
| 0 — evidence | единый формат логов, commit/engine stamp, запуск существующих 7 тестов | делает результат воспроизводимым |
| 1 — correctness | movement transaction, enemy activation, visibility count, scenario run tests | защищает исправления самых серьёзных findings |
| 2 — lifecycle | минимальные functional maps, retry/abort, fog visibility | ловит World/stream/delegate ошибки |
| 3 — content | Blueprint compile, DataValidation, scenario/map/soft-ref validators | переносит ошибки в authoring/CI до cook |
| 4 — release | Game/Shipping, cook/stage/package, clean smoke | создаёт реальное release evidence |
| 5 — performance | Insights baseline, затем согласованные thresholds | оптимизация следует измерениям |

На этапах 1–2 не следует одновременно декомпозировать крупные controller-классы: сначала фиксируются observed contracts тестами, затем меняется структура.

## 13. Формат доказательства

Каждый CI/manual результат должен содержать:

- commit SHA и отсутствие незаявленных локальных изменений;
- UE version/installation identity;
- точную команду и build configuration;
- machine profile для performance;
- test/asset/map selector;
- start/end timestamp и exit code;
- полный лог и машинно-читаемый report;
- для ручной проверки — screenshot/video как дополнение, но не вместо лога.

Гейт считается пройденным только по артефакту текущего commit. Старые DLL, старый screenshot или документ «зелёный» без ссылки не заменяют результат.
