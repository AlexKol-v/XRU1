# Глобальный архитектурный аудит XRU1 — executive summary

Дата среза: **2026-08-04**  
Репозиторий: `D:/Unrial_Projects/XRU1`  
Git-срез: `c8edbc8027a607f480307eef21359526f2e18654` (`main`, рабочее дерево до аудита было чистым)

## Итоговый диагноз

XRU1 — уже не набор разрозненных прототипов, а связный single-player vertical slice: сценарий проходит через data-driven bootstrap, пошаговое ядро, GAS-действия, fog/visibility, StateTree-квест, CommonUI и метапрогресс. Сильнейшая сторона текущего устройства — **механическая истина в C++ при презентационном Blueprint-слое**. Все 46 Blueprint-family ассетов `/Game/XRU1Game`, которые были прочитаны структурно, имеют прямого native parent; в проектном BP-графе нет циклов наследования, расчёта урона/AP/LOS и подключённых `Tick`-цепочек. UE-модульный и plugin-графы также ацикличны.

Главный архитектурный риск находится не в выборе «неправильного шаблона», а на стыках жизненного цикла: failure-path перемещения выдаёт наружу сигнал успешного settlement, текущая последовательная активация врага не имеет liveness-гарантии, а release/cook и stateful gameplay flows не защищены воспроизводимыми gates.

Аудит **не рекомендует** сейчас дробить runtime на множество UE-модулей, вводить Game Features, сетевые `GameState`/replication abstractions, ECS или общий «framework» из каждой подсистемы. Для масштаба курсового vertical slice это увеличит риск Blueprint/StateTree `/Script`-redirects, не исправив gameplay defects.

## Калибровка findings

| Severity | Количество | Смысл |
|---|---:|---|
| Blocker | 0 | подтверждённых проблем этого уровня нет |
| Critical | 0 | подтверждённых crash/data-loss/network defects такого уровня нет |
| High | 4 | movement truth, fog-safe HUD, enemy-turn liveness, loading UX |
| Medium | 10 | release/test gates, dependency contracts, hotspots, save/load, editor trust, docs drift |
| Low | 4 | допустимые для прототипа, но хрупкие локальные решения |

Статус `TURN-001` — `Inference`: алгоритмическая дыра доказана, но production-reachability снятия именно активного врага требует функционального теста. Статус `LOAD-001` — `Hypothesis`: 23 `LoadSynchronous()` и hard-reference closure доказаны, но hitch до cold-cache packaged trace не доказан. Отсутствие loading overlay (`UX-001`) при этом является фактом и не зависит от гипотезы о fog: в текущем логе сам fog bake занимал около 85–95 мс, а многосекундная готовность определяется совокупностью streaming/spawn/camera.

## Четыре наиболее серьёзные проблемы

1. **`MOVE-001 — High`.** `AUnitAIController::OnMoveCompleted` объединяет success и failed/aborted route в общий settlement, после чего публикует `Movement.Settled`, гасит tutorial destination и для AI списывает AP. GDD требует отдельного technical-failure результата и возврата AP при engine-stuck у anchor.
2. **`UI-001 — High`.** `WBP_TacticalHUD` привязывает `EnemyCountText` к `GetAliveEnemyCount`, раскрывая число скрытых врагов. Безопасный `GetVisibleEnemyCount` уже существует, то есть исправление локально и не требует новой подсистемы.
3. **`TURN-001 — High, Inference`.** Вражеская очередь продвигается только callback-ом текущего AI. Снятие/деактивация текущего элемента не завершает activation token и может либо заморозить фазу, либо пропустить следующий элемент после позднего callback.
4. **`UX-001 — High`.** Между запросом сценария и `Scenario.Ready` нет readiness-driven overlay, хотя streaming, fog preparation, encounter spawn и camera staging выполняются до первого хода и пауза уже видима пользователю.

## Сильные стороны, которые следует сохранить

- Атака имеет явный `ActionId`, `FireCommit`, watchdog и единый `UTacticsCombatStatics::ResolveShot`; механика не спрятана в Blueprint.
- `UGameInstance` владеет межкартовым сценарием/save/run generation, а `UWorldSubsystem` — только состоянием текущего мира. `ATacticalScenarioDirector::EndPlay` очищает timers/delegates/quest runtime.
- Fog rules dirty-driven и throttled; grid не пересчитывается без изменения источников, есть Insights scopes.
- UI root хранит слабую ссылку на мир и корректно переживает travel; subtitle и pause services имеют явную очистку.
- Модульный граф из 7 C++-модулей и 5 локальных dependency edges не имеет циклов; editor→runtime направление plugin-модулей корректно.
- Полный Asset Registry дал 2664/2664 проектных packages, 0 redirectors; все 620 packages `/Game/XRU1Game` были опрошены по hard/soft dependencies без ошибок.
- Сетевой код сознательно отсутствует и это соответствует GDD; mid-combat save также намеренно вне scope.
- Семь существующих pure AI scoring tests реально прошли. Проблема — узкий scope, а не ложные тесты.

## Рекомендуемая целевая архитектура

Сейчас рекомендован **вариант A: минимальная эволюция**.

- Сохранить один runtime-модуль `XRU1`.
- Добавить только `XRU1Editor` для Widget/StateTree authoring и editor dependencies.
- Внутри runtime зафиксировать логические направления: `Contracts → Tactics/Scenario/Presentation`, не разрешая обратный `Tactics → concrete UI`.
- Оставить `ATacticalPlayerController` и `AUnitAIController` façade, но после contract tests извлечь cohesive collaborators: command/selection/move-preview/camera/tutorial и activation/route-executor/pure scoring.
- Ввести захватываемый `FScenarioRunHandle { ScenarioId, RunId }`, typed movement/activation result и `FCombatStarted/FCombatResult` DTO.
- Загружать presentation assets экранными/сценарными bundles под loading overlay, но только после packaged profiling.

Долгосрочный вариант `XRU1Contracts + XRU1Tactics + XRU1Scenario + XRU1Presentation + XRU1 + XRU1Editor` допустим только при измеренном build-time pain, втором consumer, росте числа сценариев или появлении независимых владельцев слоёв. До этих триггеров это over-engineering.

## Первые три действия

1. **Стабилизировать movement truth:** ввести typed move result и закрыть таблицу success/fail/abort тестами.
2. **Закрыть два информационно-lifecycle контракта:** переключить HUD на visible count; добавить activation token/watchdog и атомарное завершение текущего врага при unregister/deactivate.
3. **Создать минимальный validation contour:** headless `XRU1.AI + XRU1.Smoke`, Data Validation, non-unity Editor/Game builds, Win64 cook/stage и clean-folder Hub→Tutorial→Mission01 smoke. Только после этого начинать module moves или controller decomposition.

## Полнота и ограничения

Покрытие высокое для статической архитектуры и проектного content-layer: проиндексированы все 440 `.h/.cpp`, вручную перепроверены ключевые flows и build descriptors, прочитаны все 12 документов `docs/`, структурно опрошены все 46 XRU Blueprint-family assets и dependencies всех 620 XRU packages. Семь reviewer-профилей и отдельный critic работали независимо.

Не выполнялись UBT non-unity Game/Shipping, cook/stage/package, clean-machine smoke, Unreal Insights/Memory Insights и exhaustive semantic review 2044 сторонних packages вне `/Game/XRU1Game`. Поэтому cook failure, runtime hitch, RAM residency, production-reachability enemy-turn race и точные streaming flags части legacy sublevels обозначены как `Unknown`/`Hypothesis`, а не выданы за факты.

Полные результаты находятся в этом каталоге; реестр findings — в [04_FINDINGS.md](04_FINDINGS.md), машинная версия — в [findings.json](findings.json), доказательства — в [10_EVIDENCE_INDEX.md](10_EVIDENCE_INDEX.md).
