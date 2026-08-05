# Область и покрытие архитектурного аудита

Дата среза: **2026-08-04**. Проект: **XRU1**, Unreal Engine **5.7**, Windows.

Этот документ фиксирует не только просмотренные области, но и границу доказательств. «Обнаружено статически» здесь не означает «воспроизведено в packaged build», а полная инвентаризация ассетов не означает полную интерпретацию внутренней семантики каждого материала, StateTree или стороннего Blueprint.

## 1. Git baseline и неизменность проекта

Аудит начат на следующем baseline:

| Поле | Значение |
|---|---|
| Branch | `main` |
| Commit | `c8edbc8027a607f480307eef21359526f2e18654` |
| Commit date | `2026-08-04T10:30:13+03:00` |
| Subject | `Рефракт документации` |
| Worktree на старте | чистый (`git status --short` не вернул строк) |
| Изменения в рамках аудита | только файлы отчёта в `docs/audit/global-architecture/` |

Ни исходный код, ни конфигурация, ни `.uasset`/`.umap`, ни соседний репозиторий-донор не изменялись. В процессе совместной подготовки отчёта в worktree могли появляться другие файлы итогового аудита; они не являются изменениями исследуемого baseline.

## 2. Прочитанная документация

Рекурсивно прочитаны все **12** Markdown-файлов, существовавших в `docs/` на baseline: всего **5 310 строк**.

| Документ | Строк | Роль в аудите |
|---|---:|---|
| `docs/README.md` | 83 | актуальная карта документации и сводка состояния |
| `docs/01_GDD.md` | 617 | игровые правила и числа |
| `docs/02_LORE_SCRIPT.md` | 398 | сценарии, реплики, ожидаемый порядок прохождения |
| `docs/03_ARCHITECTURE.md` | 1 371 | заявленная текущая архитектура и контракты |
| `docs/04_BACKLOG.md` | 321 | открытая работа и непройденная приёмка |
| `docs/05_WORKFLOW.md` | 324 | сборка, контент, Git и definition of done |
| `docs/06_COURSE_HOMEWORK.md` | 176 | границы учебной сдачи |
| `docs/07_CONTENT_PROMPTS.md` | 220 | происхождение и назначение контента |
| `docs/08_AI.md` | 896 | фактическая модель AI, дефекты и тест-матрица |
| `docs/agents/AGENT_UNREAL_TOOLING.md` | 615 | доступный editor/MCP tooling |
| `docs/agents/BRIEF_AI_Behavior_Fix.md` | 172 | исторический контекст последней починки AI |
| `docs/agents/BRIEF_AI_Refactor.md` | 117 | исторический бриф и незакрытая ручная приёмка |

Также прочитаны `AGENTS.md`, `MIGRATION_NOTES.md`, `.cbmignore`, проектные конфиги и сборочные дескрипторы. Документация рассматривалась как заявленный источник намерений, но каждое существенное утверждение перепроверялось по коду, конфигурации, Asset Registry или Blueprint-графу. Расхождения вынесены в [09_DOCUMENTATION_DRIFT.md](09_DOCUMENTATION_DRIFT.md).

## 3. Инвентаризация репозитория

### 3.1 Файлы и код

| Метрика | Результат | Метод |
|---|---:|---|
| Tracked files | **3 259** | `git ls-files` |
| C++ headers/sources (`.h` + `.cpp`) | **440** | фильтр по `git ls-files` |
| C++ в игровом модуле `XRU1` | **210** файлов | инвентаризация `Source/XRU1` |
| C++ в project plugins | **230** файлов | инвентаризация `Plugins/*/Source` |
| Сборочные дескрипторы | **14 из 14** | `.uproject`, `.uplugin`, `.Build.cs`, `.Target.cs` |

В 14 дескрипторов входят `XRU1.uproject`, два Target-файла, семь `Build.cs` и четыре `uplugin`. Все они проверены напрямую; зависимости модулей не выводились только из include-графа.

### 3.2 Модули и плагины

Подтверждены **7 C++-модулей**:

1. `XRU1` — Runtime;
2. `STQuestSystem` — Runtime;
3. `STQuestSystemEditor` — Editor;
4. `GameplayMessageRuntime` — Runtime;
5. `GameplayMessageNodes` — UncookedOnly;
6. `TeamManager` — Runtime;
7. `UnrealClaude` — Editor.

Проверены **4 project plugins**: `GameplayMessageRouter`, `STQuestSystem`, `TeamManager`, `UnrealClaude`. Последний не перечислен в `XRU1.uproject`, но включён через `EnabledByDefault` в собственном `.uplugin`; это учтено при инвентаризации.

Из engine plugins как архитектурно значимые проверены явно включённые `StateTree`, `GameplayStateTree`, `GameplayAbilities`, `CommonUI`, `PCG` и editor-only `ModelingToolsEditorMode`. Транзитивные plugin/module edges восстановлены из дескрипторов, а критичные include-зависимости перепроверены по исходникам.

### 3.3 Ассеты и Blueprint

Asset Registry был прочитан для всего `/Game`, а не только для одной папки проекта.

| Область | Покрытие | Результат |
|---|---|---:|
| Все пакеты `/Game` | полная инвентаризация | **2 664** packages |
| Сверка с файловой системой | полная | **2 622** `.uasset` + **42** `.umap` = 2 664 |
| `/Game/XRU1Game` | все пакеты и direct hard/soft dependencies | **620** packages, **1 071** внутренних edges, 0 ошибок запроса |
| Blueprint-family во всём `/Game` | классовая инвентаризация | **92** (`Blueprint`, `WidgetBlueprint`, `AnimBlueprint`, `ControlRigBlueprint`) |
| Blueprint-family в `/Game/XRU1Game` | родители, графы и узлы каждого | **46 из 46** |
| XRU Blueprint dependency subgraph | полный в пределах 46 XRU Blueprint | **50** Blueprint→Blueprint edges, циклов не найдено |

Для всех 46 XRU Blueprint прочитаны native parent, список графов и узлов. Это позволило проверять реальные EventGraph-связи, а не только имена ассетов. Для всех 620 XRU-пакетов получены direct hard и soft dependencies. Полный Asset Registry также дал классы и package paths стороннего контента, но графы остальных 46 Blueprint-family ассетов за пределами `/Game/XRU1Game` не разбирались по узлам.

Asset Registry и Blueprint query не видят все возможные runtime-created references, строковые пути, созданные кодом, и семантику произвольных сериализованных данных. Поэтому формулировка покрытия — «полный реестр пакетов и полный XRU direct dependency/Blueprint-graph срез», а не «полностью доказано поведение каждого ассета».

## 4. Граф кода и прямые проверки

Использован локальный `codebase-memory-mcp`:

- выполнен fast re-index текущего пути репозитория;
- получен граф **5 869 nodes / 21 668 edges** по индексируемому коду;
- для `Source/XRU1` получен срез **2 478 nodes / 9 040 edges**;
- граф использован как навигационный индекс, после чего серьёзные связи проверялись по исходникам и дескрипторам.

У инструмента обнаружено ограничение: первоначальный индекс и metadata указывали на старый commit `a9f16dd`, тогда как baseline — `c8edbc8`; fast re-index обновил поисковый граф исходников, но branch/revision metadata оставалась непоследовательной. Кроме того, parser создаёт ложные межмодульные связи на распространённых именах вроде `Get` и `IsValid`, а его метрика complexity для UE-кода местами нулевая. Поэтому:

- цифры nodes/edges не используются как доказательство UE module boundaries;
- module/plugin graph построен по 14 дескрипторам;
- cross-folder include graph построен прямым разбором `#include`;
- критичные вызовы, lifetime и ownership проверены по конкретным строкам C++.

Прямой include-анализ игрового модуля нашёл **608** внутримодульных include edges, из них **170** пересекают верхнеуровневые каталоги `Source/XRU1`. Эти данные описывают физическую связанность внутри единственного runtime-модуля, но не подменяют UBT module graph.

## 5. Выполненные команды и инструменты

Ниже перечислены категории реально выполненных read-only действий. В [10_EVIDENCE_INDEX.md](10_EVIDENCE_INDEX.md) они привязаны к findings.

| Инструмент/команда | Назначение |
|---|---|
| `git status --short`, `git rev-parse HEAD`, `git log`, `git ls-files` | baseline, история и количественный реестр |
| `rg`, `rg --files`, PowerShell `Get-ChildItem`/`Get-Content` | поиск символов, ссылок, тестов, конфигов и line evidence |
| прямой разбор `.uproject`, `.uplugin`, `.Build.cs`, `.Target.cs` | точный module/plugin dependency graph |
| прямой разбор `#include` | cross-folder edges и двунаправленные связи |
| `codebase-memory-mcp` (`index_repository`, architecture/search/graph queries) | семантическая навигация и граф кода |
| Unreal Editor Asset Registry через установленный editor bridge | полный реестр `/Game`, class/dependency queries |
| Blueprint query | родители, графы, узлы и связи всех 46 XRU Blueprint |
| editor console `Automation RunTests XRU1.AI` | фактический прогон project automation tests |
| просмотр `Saved/Logs/XRU1.log` | подтверждение discovery, имён и результата тестов |

Не запускались команды, меняющие исходный контент или Blueprint. Донор использовался только как read-only контекст происхождения.

## 6. Проверка тестов

Единственный обнаруженный gameplay-набор проекта — семь `WITH_DEV_AUTOMATION_TESTS` в `Source/XRU1/Tactics/Tests/XRU1AITests.cpp`. Он запущен в уже открытом редакторе командой `Automation RunTests XRU1.AI`.

- discovery: `Saved/Logs/XRU1.log:4261-4268` — найдено 7 тестов;
- execution: `Saved/Logs/XRU1.log:4274-4314` — все семь завершились `Result={Success}`;
- итог: `Saved/Logs/XRU1.log:4318` — `7 tests performed`;
- editor preflight/discovery занял около **600 секунд** при низком editor FPS;
- само выполнение семи тестов заняло около **3 секунд** (`08:00:14.665`–`08:00:17.606`).

Этот результат доказывает только текущие чистые функции позиционного scoring AI. Он не доказывает прохождение карты, lifecycle боя, навигацию, fog, UI, persistence, cook или package.

## 7. Независимые области review

Первичный аудит был разделён ровно по семи ролям, заданным в исходном запросе. Каждый reviewer должен был вернуть paths/lines/assets/edges, а не только оценку.

| № | Reviewer role | Ограниченная область |
|---:|---|---|
| 1 | **Module and dependency reviewer** | 14 build descriptors, UBT/plugin graph, public/private deps, runtime/editor boundary |
| 2 | **Gameplay and lifecycle reviewer** | scenario bootstrap, turns, movement, shooting, ownership, delegates, teardown/retry |
| 3 | **Blueprint and asset dependency reviewer** | Asset Registry, XRU Blueprint graphs, hard/soft refs, map/data dependencies |
| 4 | **Networking and persistence reviewer** | authority model, отсутствие replication, SaveGame boundaries, schema/error paths |
| 5 | **Performance and async reviewer** | Tick/load sites, asset closure, blocking paths, измеримость performance claims |
| 6 | **Build, test and tooling reviewer** | targets/build scripts, automation, validation, CI/cook/package/tooling risks |
| 7 | **Reusability and target architecture reviewer** | кандидаты на reuse, границы extraction, два целевых варианта и стоимость миграции |

После консолидации отдельный **independent critic reviewer**, не формировавший первичные выводы, попытался опровергнуть findings и проверить калибровку. Его существенные коррекции учтены:

- отсутствие build/cook/functional evidence — это подтверждённый пробел уровня **Medium**, а не доказательство сломанной сборки;
- отсутствие loading UX при известной многостадийной загрузке — риск уровня **High**;
- при этом стоимость fog reset и наличие конкретного async bottleneck не доказаны без Insights, поэтому такие утверждения не повышались до факта.

## 8. Уровни покрытия

### A — исчерпывающее перечисление

- 12/12 текущих документов в `docs/`;
- 3 259/3 259 tracked files по имени и типу;
- 14/14 build descriptors;
- 7/7 project/plugin C++ modules и 4/4 project plugins;
- 2 664/2 664 packages в `/Game` по Asset Registry;
- 620/620 XRU packages по direct hard/soft dependencies;
- 46/46 XRU Blueprint-family ассетов по parent/graph/node структуре;
- 7/7 обнаруженных gameplay automation tests — фактически запущены и прошли.

### B — широкая статическая проверка

- все 440 C++ header/source files доступны инвентаризации и поиску;
- основные subsystems, framework classes, компоненты, Data Assets и runtime flows;
- include graph и наиболее связанные классы;
- C++/Blueprint boundary, Gameplay Tags, delegates, synchronous load sites;
- конфиги input, navigation, Asset Manager, maps/startup и editor tooling.

«Широкая» не означает ручное построчное code review каждой функции из 440 файлов. Детально трассировались архитектурные владельцы и критические потоки.

### C — целевая проверка

- actor contents и настройки ключевых XRU-карт;
- карты/ассеты сторонних паков, попавшие в зависимости XRU;
- внутренности StateTree, AnimBlueprint, Control Rig, материалов и Niagara — только там, где они участвовали в проверяемом контракте;
- plugin tooling internals — security/lifecycle/build seams, а не полный upstream review.

### D — не проверено исполнением

- UHT/UBT build текущего commit;
- non-unity build;
- cook и package Development/Shipping;
- запуск packaged build на чистой машине/папке;
- два сквозных PIE-прогона Hub → Tutorial/Mission01 → retry/abort;
- функциональная приёмка приблизительно 90 пунктов из `docs/04_BACKLOG.md`;
- Unreal Insights, memory capture, render/GPU profiling и hitch trace;
- cold-cache asset timing;
- сетевой прогон: проект документирован и реализован как single-player без replication.

## 9. Ограничения и исключения

1. **Build/cook/package не запускались.** Редактор был открыт, а полный UE build/cook относится к долгим операциям, которые инструкции проекта требуют не запускать автономно. Наличие свежих DLL и старых логов не считается доказательством текущего build.
2. **Нет полного gameplay execution.** Семь unit-like automation tests не создают World и не проверяют critical lifecycle.
3. **Нет performance evidence.** Число `Tick`/`LoadSynchronous` и disk-size closure — индикаторы мест измерения, но не доказанные frame hitches или RAM footprint.
4. **Бинарные данные имеют границу наблюдаемости.** Asset Registry и Blueprint query покрывают package dependencies и XRU-графы; они не раскрывают каждую property/streaming flag и runtime-generated reference.
5. **Сторонний контент не прошёл узловой review.** Все его packages присутствуют в глобальном реестре, но 46 Blueprint-family ассетов вне `/Game/XRU1Game` не разобраны по каждому узлу.
6. **Engine source и upstream plugins не аудировались целиком.** Проверялись только использованные публичные контракты и локальные plugin sources.
7. **`codebase-memory` не является источником истины для UE modules.** Его revision metadata была stale, а семантические edges содержат parser noise; поэтому серьёзные выводы подтверждены другим каналом.
8. **Networking не является активной подсистемой.** Отсутствие replication проверено статически; multiplayer correctness не оценивалась как требование, которого у проекта нет.
9. **Соседний donor repository исключён из mutation и полного повторного аудита.** Его архитектура не включена в метрики XRU1.

Итоговая оценка покрытия: архитектурный и dependency-срез текущего кода и XRU-контента — широкий и доказательный; release readiness, полное runtime-поведение и производительность остаются явно непроверенными.
