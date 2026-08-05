# 10. Индекс доказательств

Дата среза: 2026-08-04. Commit: `c8edbc8027a607f480307eef21359526f2e18654`.

## 1. Назначение

Этот файл связывает каждый finding с конкретным кодом, config, документацией, asset, dependency edge и выполненной проверкой. Он не заменяет разбор последствий в `04_FINDINGS.md`: здесь важнее воспроизводимость и честное разделение факта, вывода, гипотезы и неизвестного.

Обозначения:

- **Fact** — проверено прямым чтением/запросом;
- **Inference** — вывод из нескольких проверенных фактов;
- **Hypothesis** — ожидаемый runtime-эффект без воспроизведения;
- **Unknown** — нужная проверка не выполнялась.

Уверенность относится к evidence, severity — к возможному влиянию.

## 2. Матрица finding → evidence

| ID | Severity | Confidence | Статус | Основные файлы/классы | Assets / dependency edges | Выполненная проверка |
|---|---|---|---|---|---|---|
| `MOVE-001` | High | High | Fact | `TacticalPlayerController.cpp:1607-1615`; `UnitAIController.cpp:2854-3030` | event edge `movement completion → Movement.Settled → tutorial/quest` | direct control-flow read; negative runtime test не запускался |
| `UI-001` | High | High | Fact | `TacticalHUDWidget.cpp:121-132`; `WBP_TacticalHUD` EventGraph | WBP node GUID `A62206904B9B469926B54983E1BF4B29` | Blueprint graph query всех 46 XRU BP |
| `TURN-001` | High | Medium | Inference | `TurnManagerSubsystem.cpp:119-145`, `:493-529`; `ScenarioActorRegistry.cpp:176-216`; `UnitAIController.cpp:3114-3135` | callback edge `current enemy → HandleEnemyUnitFinished` без token/watchdog | static lifecycle/control-flow audit; runtime deactivation test нужен |
| `UX-001` | High | High | Fact | `FogGridSubsystem.cpp:170-208`, `:356-410`; `docs/04_BACKLOG.md:55-69` | scenario readiness до HUD, loading overlay отсутствует | code/docs read; editor log измерил fog bake 95,2 ms |
| `REL-001` | Medium | High | Fact / process risk | `Build-XRU1.ps1:13`, `:57`; `XRU1.Target.cs:10-13`; `DefaultGame.ini:8-14` | maps/scenario/quest cook closure не зафиксирован | repo/config scan; Game build/cook/package **не** запускались |
| `TEST-001` | Medium | High | Fact | `Tactics/Tests/XRU1AITests.cpp`; `docs/04_BACKLOG.md:121-123`, `:150+`, `:262-282`, `:317-319` | только pure AI scoring seam | `Automation RunTests XRU1.AI`: 7/7 Success; CI отсутствует |
| `MOD-001` | Medium | High | Fact | семь `.Build.cs`; `FogGridSubsystem.cpp:21`, `:944`; plugin public headers; editor authoring libraries | missing/misclassified `RHI`, `LevelEditor`, `DeveloperSettings/Json`, `Engine`, `CoreUObject`; all-Public surface | descriptor + direct include scan; non-unity build не запускался |
| `DEP-001` | Medium | High | Fact | `Source/XRU1` folders; `XRU1.Build.cs:72-84` | 608 local includes, 170 cross-folder, 27 pairs, 6 bidirectional | полный quoted-include scan; это не UE module cycle |
| `CODE-001` | Medium | High | Inference | `TacticalPlayerController` 3 185+869 lines; `UnitAIController` 3 377+1 180 lines | high responsibility fan-out на tactical subsystems | file/definition/include inventory + responsibility clustering |
| `FLOW-001` | Low | High | Fact | `TacticsGameMode.cpp:23-25`, `:490-499`, `:707-719`; `TacticalPlayerController.cpp:100-106` | `GameMode → concrete WBP classes`; root layout создаётся PC | direct include/control-flow read; реальный startup race не воспроизведён |
| `SAVE-001` | Medium | High | Fact | `TacticsGameInstance.cpp:26-60`; `MenuWidgets.cpp:308-327`; `TacticsGameMode.cpp:691-715`; audio `:563-571`; POI `:193-198` | save/load calls без recovery/notification edge | return-value/call-site audit; I/O failure injection не выполнялся |
| `LOAD-001` | Medium | Medium | Hypothesis | 23 `LoadSynchronous` sites; `TacticsAudioSubsystem.h:82-96`; `TacticsGameInstance.h:75-76` | startup closure 59 packages / 82,25 MiB disk; 5 music waves 75,17 MiB | source + Asset Registry; packaged cold trace/RAM profile не выполнялись |
| `RUN-001` | Medium | Medium | Inference | `TacticsGameInstance.h:149-155`; `.cpp:115-175`; `TacticalQuestEvents.cpp:79-84`, `:110-129` | async callback may read new current `RunId` | отсутствие guard/capture — Fact; stale effect требует mid-action retry test |
| `SEC-001` | Medium | High | Fact, editor-only | `DefaultEditor.ini:15-16`; `DefaultEngine.ini:151-166`; `ScriptExecutionManager.cpp:89-103`; MCP server `:39-62` | `UnrealClaude` local HTTP/script surface | config/source read; активность listener зависела от состояния editor; не Shipping |
| `DOC-001` | Medium | High | Fact | `AGENTS.md`, GDD, architecture, code/config comments | ссылки на удалённые docs и старые rules/assets | полный docs inventory + markdown/reference scan |
| `BP-001` | Low | High | Fact | native `UGA_Attack`/`UGA_Overwatch`; BP graphs | `BP_GA_Attack` 115 nodes/8 vars; `BP_GA_Overwatch` 107/8; fan-in 5+5 | Blueprint graph query; combat rules остаются C++ |
| `LIFE-001` | Low | High | Fact | `TacticalPlayerController.cpp:172-233`; `UnitAIController` lifecycle | mapping/delegate subscription cleanup неполон | BeginPlay/EndPlay/bind-unbind audit; leak не воспроизведён |
| `DATA-001` | Low | High | Fact | `TacticsSaveGame.h:19-60` | нет `SaveFormatVersion`/migration dispatcher | schema read; compatibility matrix со старыми слотами не запускалась |

## 3. Evidence cards

### MOVE-001 — Failed/aborted path публикуется как успешный `Movement.Settled`

**Классификация:** High / High / Fact. Critic подтвердил: settlement event семантически неверен всегда для failed/aborted финала; AP refund в GDD нужен только engine-stuck-at-anchor, а не для любого частичного движения.

**Прямые доказательства:**

- Player AP списываются до фактического завершения пути: `TacticalPlayerController.cpp:1607-1615`.
- `AUnitAIController::OnMoveCompleted` отличает success только при переходе к следующему segment, но failed/aborted final segment попадает в общий finalization: `UnitAIController.cpp:2854-2912`.
- `TryFinalizeMovement` не хранит итоговый `FPathFollowingResult`, origin или фактически пройденную дистанцию, после чего публикует `Event_Tactical_Movement_Settled_InCover/Open`: `UnitAIController.cpp:2940-3014`.
- AI AP списываются независимо от result: `UnitAIController.cpp:3023-3030`.
- Контракт противоречит GDD technical stuck refund (`docs/01_GDD.md:165-168`), documented event contract (`docs/03_ARCHITECTURE.md:654-665`) и ещё не закрытым negative tests (`docs/04_BACKLOG.md:226-230`).

**Dependency edge:** path following completion → movement finalizer → quest/tutorial gameplay message. Ложный `Settled` способен продвинуть tutorial objective, даже если destination не достигнут.

**Что не проверено:** runtime fail/abort matrix.

**Верификация исправления:** transaction table `Success`, `Blocked before leaving anchor`, `Partial then failed`, `Aborted by scenario teardown`, `Actor destroyed`; отдельно проверять AP, location tolerance, cover recalculation и отсутствие/тип event.

### UI-001 — HUD раскрывает полное количество скрытых врагов

**Классификация:** High / High / Fact. Critic подтвердил без понижения.

**Прямые доказательства:**

- `/Game/XRU1Game/UI/WBP_TacticalHUD`, EventGraph `OnUnitsStateChanged` вызывает `Get Alive Enemy Count`, node GUID `A62206904B9B469926B54983E1BF4B29`, затем `ToText → SetText EnemyCountText`.
- `UTacticalHUDWidget::GetAliveEnemyCount` возвращает полный alive list TurnManager (`TacticalHUDWidget.cpp:121-125`).
- Visibility-safe `GetVisibleEnemyCount` уже существует (`:128-132`), но WBP его не использует.
- Это противоречит fog/hidden-information rules `docs/01_GDD.md:296-305` и документированной безопасной UI границе `docs/03_ARCHITECTURE.md:920`, `:1087-1092`.

**Dependency edge:** `WBP_TacticalHUD → native full TurnManager count`, обход visibility read model.

**Верификация исправления:** Blueprint structural test запрещает `GetAliveEnemyCount` в HUD; functional test с 1 visible + N unrevealed enemies; смена visibility/death не должна раскрывать N.

### TURN-001 — Enemy phase ждёт единственный callback без liveness token/watchdog

**Классификация:** High / Medium / Inference. Critic подтвердил algorithmic gap, но требует runtime reproduction.

**Прямые доказательства:**

- `TurnManagerSubsystem.cpp:493-512` подписывается на current enemy completion и начинает его activation.
- Единственный normal continuation — `HandleEnemyUnitFinished` (`:515-529`).
- `UnregisterUnit` удаляет current unit, но index корректируется лишь при `RemovedIndex < CurrentEnemyIndex`, не при равенстве (`:119-145`).
- `ScenarioActorRegistry.cpp:176-216` способен отключить tick/controller перед unregister; StateTree path вызывает такую деактивацию (`:391-437`).
- `AUnitAIController` очищает timer/delegate в normal `FinishTurn` (`UnitAIController.cpp:3114-3135`), но собственного `EndPlay`/`OnUnPossess` completion path не найдено.

**Inference:** исчезновение current enemy между activation и completion может оставить phase без continuation либо пропустить индекс. Это не было воспроизведено, поэтому не помечено Fact.

**Верификация исправления:** функциональные тесты destroy/unregister/deactivate current enemy до action, во время move и перед finish; activation token + watchdog; `EndEnemyPhase` должен наступить ровно один раз.

### UX-001 — Readiness bootstrap не имеет видимого loading overlay

**Классификация:** High / High / Fact для отсутствия UX feedback. Critic отделил это от performance: текущий fog bake сам по себе только low/medium cost и не обосновывает async rewrite.

**Прямые доказательства:**

- `docs/04_BACKLOG.md:55-69` фиксирует отсутствие loading/readiness overlay между scenario start и доступностью tactical HUD.
- `FogGridSubsystem.cpp:170-208` синхронно выполняет Reset/Build/Rasterize; per-cell floor/blocker traces/overlaps находятся в `:356-410`.
- `Saved/Logs/XRU1.log:1511` зафиксировал текущий editor sample: 47 040 cells, 95,2 ms. Это не многосекундный bottleneck и не packaged benchmark.
- ScenarioDirector/ATacticsGameMode выполняют streaming/objective/encounter/fog readiness до `StartCombat` и HUD.

**Что не следует из evidence:** нельзя требовать async fog build без профиля; нельзя экстраполировать editor 95,2 ms на clean packaged machine.

**Верификация исправления:** сквозной test start/retry с overlay до confirmed readiness; отсутствие input leakage; измерение phase timings отдельно: stream, fog, quest, encounter.

### REL-001 — Нет автоматизированного release/cook/package gate

**Классификация:** Medium / High / Fact как process gap; фактический cook failure — Unknown. Critic понизил первоначальную оценку до Medium.

**Прямые доказательства:**

- `Build-XRU1.ps1:13`, `:57` собирает `XRU1Editor Win64 Development`.
- Game target существует: `Source/XRU1.Target.cs:10-13`.
- `Config/DefaultGame.ini:8-14` сканирует quest primary assets с `CookRule=Unknown`.
- `ProjectPackagingSettings`, `MapsToCook`, `BuildCookRun`, project CI workflow не найдены; `.github/workflows` пуст.
- Backlog оставляет cook-validation и packaged Dev/Shipping/clean-machine проверки незакрытыми (`docs/04_BACKLOG.md:129-130`, `:317-319`).

**Что не запускалось:** `XRU1` Game target build, cook, package, Shipping и clean-machine smoke. Следовательно, «релиз сломан» не является выводом аудита.

**Верификация:** explicit map/primary asset cook contract, Development packaged smoke, Shipping build/cook, запуск Hub→Tutorial→Mission на clean machine; Windows self-hosted UE 5.7 CI.

### TEST-001 — Regression net покрывает только семь pure AI scoring tests

**Классификация:** Medium / High / Fact. Critic понизил с High: это prototype test gap, не доказанный runtime defect.

**Прямые доказательства:**

- Единственный project gameplay test file: `Source/XRU1/Tactics/Tests/XRU1AITests.cpp`.
- В нём семь `WITH_DEV_AUTOMATION_TESTS` simple tests вокруг `ScorePositionFacts`.
- Команда `Automation RunTests XRU1.AI` реально выполнена в текущем editor.
- `Saved/Logs/XRU1.log:4261-4268` — discovery 7 tests; `:4274-4316` — семь `Result={Success}`; `:4318` — `7 tests performed`.
- Preflight ждал interactive FPS около 600 секунд (`:2787`, `:4245`); это задержка harness, не падение теста.
- Backlog подтверждает узкое покрытие и множество непроверенных acceptance/lifecycle/package paths (`:121-123`, `:150+`, `:262-282`, `:317-319`).

**Не смешивать:** UnrealClaude содержит собственные plugin tests, но они не покрывают XRU1 combat/scenario.

**Верификация:** добавить unit tests transactions, functional tests scenario lifecycle/retry, Blueprint/asset validation и build/cook gates; текущие 7 должны остаться зелёными.

### MOD-001 — Build dependency declarations полагаются на transitives

**Классификация:** Medium / High / Fact. Critic оставил Medium.

**Прямые доказательства:**

- `XRU1.Build.cs:11-50` — 26 all-public runtime deps; private list пуст (`:52`); four editor-private deps (`:60-69`); broad include paths (`:72-84`).
- `FogGridSubsystem.cpp:21`, `:944` использует `RHI.h`/`FUpdateTextureRegion2D`, но `RHI` не объявлен.
- `UnrealClaudeModule.cpp:14`, `:136` использует `LevelEditor`, которого нет в Build.cs.
- Public `UnrealClaudeSettings.h:6`/`ScriptTypes.h:6` экспортируют modules, объявленные private.
- Public `TeamManager/PlayerControllerTeams.h` требует `Engine`, объявленный private.
- Public `GameplayMessageRuntime` header требует `CoreUObject`, объявленный private.

**Что не доказано:** текущий editor build failure. Имеющийся binary/editor запускается; non-unity build не выполнялся.

**Верификация:** привести direct/public/private deps к header surface; build Editor + Game с unity/shared PCH disabled; затем обычный Editor Development.

### DEP-001 — Логические папки не являются направленными boundaries

**Классификация:** Medium / High / Fact. Critic отдельно запретил называть это UE module cycle и признал немедленный runtime split overengineering.

**Прямые доказательства:**

- Полный quoted-include scan `Source/XRU1`: 608 local include statements; 170 cross-top-folder (28,0%); 27 directed pairs.
- Двунаправленные pairs: `UI↔Tactics`, `Tactics↔Audio`, `Tactics↔Subtitles`, `Tactics↔Characters`, `UI↔Subtitles`, `UI↔Hub`.
- Exact examples: `APPipsWidget.cpp→ActionPointsComponent.h`, `MissionPointOfInterest.cpp→POIPopupWidget.h`, `AnimNotify_UnitFootstep.cpp→UnitBase.h`, `GamePauseSubsystem.cpp→TacticsAudioSubsystem.h`, `CSTPlayerController.cpp→GameUIManagerSubsystem.h`, `HealthBarWidget.cpp→TDAttributeSet.h`.
- UE module graph при этом: 7 nodes, 5 local edges, 0 cycles, depth 2.
- Editor authoring libraries находятся внутри runtime `XRU1`, но защищены editor guards.

**Верификация:** automated forbidden-include rules; вынести только `XRU1Editor`; до любого runtime split ввести contracts и разорвать bidirectional pairs.

### CODE-001 — Player/AI controllers стали responsibility hotspots

**Классификация:** Medium / High / Inference на основе подтверждённых size/responsibility facts. Critic оставил Medium и рекомендовал decomposition после correctness transaction tests.

**Прямые доказательства:**

- `TacticalPlayerController.cpp` 3 185 строк, `.h` 869; 48 includes, около 97 definitions, около 45 subsystem accesses.
- Responsibility clusters: lifecycle/UI/input `:81-293`; hover/path `:295-621`; tutorial `:645-1177`; selection `:762-1200`; commands `:1225-2370`; pause `:2423-2452`; camera `:2456-2724`; fog/enemy camera/range/auto-end `:2725-3185`.
- `UnitAIController.cpp` 3 377 строк, `.h` 1 180; 39 includes, около 63 definitions.
- Clusters: perception `:98-509`; executor `:510-775`; decisions `:776-1799`; GAS `:1800-1908`; patrol `:1909-2390`; route execution `:2409-2691`; scripted `:2712-2844`; settlement `:2854-3034`; terminal `:3036-3377`.

Line count не является дефектом сам по себе. Finding основан на числе независимых lifecycle/reason-to-change clusters и fan-out.

**Верификация:** зафиксировать transaction/lifecycle tests, сохранить controllers как UE facade, извлекать selection/preview/route executor/activation context по одному без изменения внешнего Blueprint API.

### FLOW-001 — Gameplay authority напрямую создаёт concrete presentation

**Классификация:** Low / High / Fact для direct coupling. Startup/race consequence не воспроизведена; critic счёл coupling приемлемым для прототипа.

**Прямые доказательства:**

- `TacticsGameMode.cpp:23-25` включает concrete UI headers.
- GameMode push-ит tactical HUD `:490-499` и mission result `:707-719`.
- CommonUI root layout создаётся в `ATacticalPlayerController::BeginPlay` (`TacticalPlayerController.cpp:100-106`).
- Null guards предотвращают crash, но presentation зависит от порядка composition.

**Верификация:** функциональные tests direct map launch/travel/retry; если coupling мешает — GameMode публикует typed start/result DTO/event, UI owner выполняет push. Не рефакторить до появления test seam.

### SAVE-001 — Результаты persistence операций не доведены до recovery path

**Классификация:** Medium / High / Fact.

**Прямые доказательства:**

- `UTacticsGameInstance::{HasSaveGame,StartNewCampaign,SaveCampaign,LoadCampaign}`: `TacticsGameInstance.cpp:26-60`; `SaveCampaign` возвращает `bool`, `LoadCampaign` может вернуть null.
- Menu continue/new campaign flow не формирует полноценный error/recovery feedback: `MenuWidgets.cpp:308-327`.
- Mission completion изменяет save и вызывает `SaveCampaign`, не обрабатывая отрицательный результат: `TacticsGameMode.cpp:691-715`.
- Audio settings save path игнорирует failure: `TacticsAudioSubsystem.cpp:563-571`.
- Hub POI save path: `HubPOIMarker.cpp:193-198`.

**Что не доказано:** реальная потеря прогресса. Disk-full/permission/corrupt-slot injection не выполнялся.

**Верификация:** общее `FSaveOperationResult`, atomic temp/replace или документированный UE slot contract, UI notification/retry; tests create/load/corrupt/read-only/disk failure.

### LOAD-001 — Hard startup closure и synchronous first-use loads

**Классификация:** Medium / Medium / Hypothesis для hitch/RAM impact. Critic потребовал packaged cold trace до async refactor.

**Прямые доказательства:**

- Найдено 23 `LoadSynchronous` call sites; реальных `RequestAsyncLoad`/`FStreamableManager` flows не найдено (есть лишь неиспользуемый include).
- `BP_TacticsGameInstance → DA_TacticsAudio` — hard dependency.
- Music/stinger поля — `TObjectPtr` (`TacticsAudioSubsystem.h:82-96`), audio DA ссылка GI — hard (`TacticsGameInstance.h:75-76`).
- Startup closure: 59 packages / 82,25 MiB disk proxy; пять music/stinger SoundWave — 75,17 MiB.
- First-use sync sites: `MissionVoiceDirector.cpp:190`, `TutorialPresentation.cpp:119`, menu screen `:160`, `:507-509`, `:562`, `:670`, briefing `:43`, `:92`, result `:68`, `:154`.

**Что не доказано:** resident RAM, I/O hitch, frame miss, cook residency. Disk size нельзя подменять memory measurement.

**Верификация:** packaged cold start trace + Unreal Insights bookmarks по screens/scenario; только измеренные hotspots переводить на soft ref/async preload по состоянию.

### RUN-001 — Retry API не защищён terminal phase и не передаёт captured run handle

**Классификация:** Medium / Medium / Inference: отсутствие guard/captured handle — Fact, stale callback effect — Hypothesis.

**Прямые доказательства:**

- `RestartActiveScenario` публичен/BlueprintCallable без terminal-state precondition (`TacticsGameInstance.h:149-155`, `.cpp:172-175`).
- `PrepareScenarioRun` сбрасывает quest state и увеличивает run до `OpenLevel` (`TacticsGameInstance.cpp:115-157`).
- `TacticalQuestEvents.cpp:79-84`, `:110-129` читает текущий GI `RunId` при публикации, а не captured generation action.
- Нормальный result-widget path вызывает retry только после finalization (`MissionResultWidget.cpp:178-191`), поэтому штатный UI безопаснее публичного API.
- Документированный контракт запрещает mid-action retry (`docs/03_ARCHITECTURE.md:793-803`).

**Hypothesis:** stale callback предыдущего мира/action может получить новый RunId. Не воспроизводилось.

**Верификация:** `FScenarioRunHandle {ScenarioId, RunId}` захватывается при старте async action; old callback отбрасывается; tests retry during move, ability montage, quest wait и terminal screen.

### SEC-001 — Локальная editor automation имеет расширенную trust boundary

**Классификация:** Medium / High / Fact. Critic подтвердил только editor/local scope; shipping exposure не утверждается.

**Прямые доказательства:**

- `Config/DefaultEditor.ini:15-16`: `bAutoApproveScripts=True`.
- `ScriptExecutionManager.cpp:89-103`: настройка обходит permission dialog и логирует auto-approval.
- `Config/DefaultEngine.ini:165-166`: Python remote execution включён.
- `UnrealClaudeMCPServer.cpp:39-62`: HTTP router, listeners и execution routes.
- Ранний снимок при активном мосте показывал `127.0.0.1:3000`, не `0.0.0.0`; поздний снимок listener уже не обнаружил. Активность зависит от состояния editor.
- Android file server config содержит development token в `DefaultEngine.ini:151-161`; в отчёте он маскирован как `2693…A23`, `bIncludeInShipping=False`, external shipping start disabled, connection USB-only.
- `docs/agents/AGENT_UNREAL_TOOLING.md:142-156` описывает auto-approval как осознанный компромисс authoring workflow.

**Верификация:** keep editor-only; bind localhost; документировать trusted-repo assumption; секретный scan должен маскировать token; проверить Shipping plugin/module exclusion.

### DOC-001 — Документация и кодовые ссылки дрейфуют

**Классификация:** Medium / High / Fact.

**Прямые доказательства:**

- `AGENTS.md:15-16`, `:78`, `:88` указывает удалённые `03_CODE_OVERVIEW.md`, `04_ROADMAP.md`, `05_EDITOR_GUIDE.md`, `06_CONVENTIONS.md`; фактическая схема docs — `03_ARCHITECTURE.md`, `04_BACKLOG.md`, `05_WORKFLOW.md` и др.
- `AGENTS.md:58` и `docs/03_ARCHITECTURE.md:36-37` говорят о `Variant_Strategy`/`Variant_TwinStick`, которых в текущем source tree нет.
- GDD `:17-20` называет несуществующий `ResolveCoverAgainstDirection`; actual API — `GetCoverAgainst`/`EvaluateCoverAtLocation` (`CoverDetectionComponent.cpp:603`, `:799`).
- GDD `:291-295` говорит fully explored fog; actual scenario defaults/asset values false (`TacticalScenarioDataAsset.h:106-125`, `TacticsGameMode.cpp:466-471`, architecture `:576-579`).
- GDD `:121` помечает `WBP_MissionResult` как ещё не созданный, но asset существует и hard-referenced GameMode.
- GDD `:579` описывает BP `PlaySound2D/AtLocation`, тогда как actual audio централизован subsystem.
- Broken doc references в code/config: `SubtitleOverlay.cpp:121` (`14_SUBTITLES.md`), `FogGridSubsystem.h:22`, `FogOfWarConfigDataAsset.h:14`, `TacticsGameMode.cpp:314`, `UnitBase.cpp:171` (`10_FOG_OF_WAR.md`), `TacticalHUDStyleData.h:622` (`14_SUBTITLES.md`), `TacticalHUDWidget.cpp:433` (`13_LOS_TARGETING.md`), `DefaultEngine.ini:17` (`09_UI_HUD.md`), `DefaultGame.ini:13` (`06_CONVENTIONS.md`).
- `Config/DefaultGame.ini:3` всё ещё имеет `ProjectName=Top Down Game Template`.
- Внутренние relative markdown links в актуальных docs проверены: broken links не найдено; drift в основном в AGENTS/code/config comments и устаревших assertions.

**Верификация:** docs link/reference linter; один index/current-truth map; historical briefs маркировать archived; acceptance checkbox закрывать только после test evidence.

### BP-001 — Attack/Overwatch дублируют presentation coordinator graphs

**Классификация:** Low / High / Fact для graph duplication; maintainability consequence — Inference.

**Прямые доказательства:**

- `/Game/XRU1Game/.../BP_GA_Attack`: 115 graph nodes, 8 variables.
- `/Game/XRU1Game/.../BP_GA_Overwatch`: 107 nodes, 8 variables.
- Оба имеют близкий latent topology fire montage/notify/tracer/outcome; каждый имеет fan-in от пяти unit Blueprint.
- Native mechanics не дублируются: attack проходит через `UTacticalAbility`, `AnimNotify_FireCommit`, `ActionId` и `UTacticsCombatStatics::ResolveShotMechanics` (`GA_Attack.cpp:200-684`, `AnimNotify_FireCommit.cpp:154-209`).

**Inference:** coordinator drift cost существует, но механическая абстракция может ухудшить readability. Сначала нужен shared native/presentation helper только для реально одинакового lifecycle.

**Верификация:** structural diff на два graphs, paired ability tests success/cancel/watchdog/stale notify; после extraction — те же node timings и presentation outcome.

### LIFE-001 — Cleanup subscriptions/input mapping неполон

**Классификация:** Low / High / Fact как absence; leak/duplicate event — не доказан.

**Прямые доказательства:**

- Enhanced Input mapping добавляется `TacticalPlayerController.cpp:219-233`.
- `EndPlay` `:172-216` не вызывает `RemoveMappingContext`.
- Часть tutorial gate/selected-unit delegates не имеет симметричного unbind в controller teardown.
- `AUnitAIController` не объявляет отдельный `EndPlay`/`OnUnPossess`; timers/delegates завершаются normal `FinishTurn` (`:3114-3135`).
- Dynamic multicast/world teardown и уничтожение objects снижают текущий риск.

**Верификация:** repeated travel/retry 20 раз; delegate invocation count = 1; mapping context count не растёт; teardown current AI не оставляет timers.

### DATA-001 — Save schema не имеет явной версии/мигратора

**Классификация:** Low / High / Fact. UE tagged properties снижают текущий риск, поэтому critic/аудит не поднимает severity.

**Прямые доказательства:**

- `TacticsSaveGame.h:19-60` содержит current и legacy fields, но не `SaveFormatVersion`, custom version GUID или migration dispatcher.
- Legacy audio/video fields оставлены, а new settings вынесены в `UTacticsUserSettings`; это ручная совместимость, не formal schema evolution.

**Что не проверено:** чтение save slots от всех предыдущих commits и corrupt/unknown future version.

**Верификация:** golden save fixtures N-1/N-2, versioned migration table, forward-version refusal с user-facing recovery.

## 4. Dependency evidence ledger

### 4.1. UE modules

Источник: все `.Build.cs`, `.uplugin`, `.uproject`, cross-module quoted includes.

```text
XRU1 -> STQuestSystem                    13 includes
XRU1 -> GameplayMessageRuntime           3 includes
STQuestSystem -> GameplayMessageRuntime  2 includes
STQuestSystemEditor -> STQuestSystem      7 includes
GameplayMessageNodes -> GameplayMessageRuntime 1 include
```

Итого: 7 module nodes, 5 local edges, 0 cycles, max depth 2. `GameplayMessageRuntime` fan-in 3; `XRU1` fan-out 2.

### 4.2. Folder includes

Источник: quoted include scan 210 files в `Source/XRU1`, resolution по relative path/уникальному basename.

```text
608 local include statements
170 cross-top-folder statements
27 directed area pairs
6 bidirectional area pairs
```

Полная pair table находится в `03_DEPENDENCY_ANALYSIS.md`. Важно: это не UBT/UE module cycles.

### 4.3. Asset dependencies

Источник: Asset Registry inventory `/Game`, hard/soft dependency query всех `/Game/XRU1Game`, Blueprint graph query всех XRU1 BP.

```text
/Game:             2664 packages
/Game/XRU1Game:     620 packages
internal edges:    1071
query failures:       0
XRU BP:              46
BP->BP edges:         50
BP cycles:             0
redirectors:           0
```

Не строился полный edge closure всех third-party packages; Engine/Script edges отфильтрованы.

## 5. Инвентарные метрики

| Метрика | Значение | Метод |
|---|---:|---|
| Tracked files | 3 259 | `git ls-files` |
| `.uasset` | 2 622 | extension count |
| `.umap` | 42 | extension count |
| `.h` | 229 | extension count |
| `.cpp` | 211 | extension count |
| XRU1 `.h/.cpp` | 210 | `Source/XRU1` |
| Plugin `.h/.cpp` | 230 | `Plugins` |
| `/Game` disk size | ~8,93 GiB | package files; не RAM |
| Code graph nodes/edges | 5 869 / 21 668 | codebase-memory fast re-index |
| Scoped XRU1 nodes/edges | 2 478 / 9 040 | codebase-memory scoped query |
| Explicit always-ticking actor/components | 10 | `bCanEverTick=true` scan |
| Tickable world subsystems | 5 | native inheritance scan |
| `LoadSynchronous` sites | 23 | source search |
| Реальные async streamable load paths | 0 найдено | `RequestAsyncLoad`/streamable usage scan |
| Project gameplay automation tests | 7 | source + Automation discovery |

Tick counts не агрегируются в утверждение «все тикает всегда»: дополнительно существуют два widget `NativeTick`, Slate subtitle tick, player tick и StateTree task ticks с разными activation conditions.

## 6. Фактически выполненные команды и процедуры

Следующие категории проверок действительно выполнялись в ходе аудита:

| Цель | Команда/процедура | Результат |
|---|---|---|
| Repository baseline | `git status --short`; `git rev-parse HEAD`; `git log -15` | исходное дерево clean; HEAD зафиксирован |
| File inventory | `git ls-files`; `rg --files`; extension grouping | 3 259 tracked files; code/content/docs counts |
| Source facts | `rg -n` + прямое чтение line ranges | lifecycle, persistence, loads, config, docs drift |
| UE graph | parse `.Build.cs`, `.uplugin`, `.uproject`; cross-module include scan | 7/5/0/depth 2 |
| Folder graph | PowerShell quoted-include resolution по всем `Source/XRU1` `.h/.cpp` | 608/170/27; exact pairs |
| Code semantic index | codebase-memory fast `index_repository`, graph/search queries | поисковый индекс обновлён; metadata/semantic limits отмечены |
| Asset inventory | открытый UE 5.7 editor, Asset Registry query `/Game` | 2 664 packages; совпало с 2 622+42 files |
| XRU dependency graph | hard/soft dependency query всех 620 XRU packages | 1 071 internal edges, 0 failures |
| Blueprint audit | `unreal_blueprint_query` для 46 XRU Blueprint | parents/graphs/nodes; HUD evidence; no BP inheritance/cycles |
| Automation | editor console `Automation RunTests XRU1.AI` | 7 discovered, 7 Success |
| Local listener | два local socket snapshots порта 3000 | при активном bridge — `127.0.0.1:3000`; позднее listener отсутствовал |
| Secrets/config | repository pattern scan; sensitive token masked | dev Android token/config найден, не раскрыт |
| Markdown links | relative `.md` link resolution | актуальные docs links не сломаны; code/config refs drifted |

`codebase-memory` использовался только как индекс. Его branch metadata оставался stale, generic method names создавали false boundaries, complexity values не использованы.

## 7. Что сознательно не запускалось

Поэтому соответствующие утверждения помечены `Unknown`/`Hypothesis`:

- полный `Build-XRU1.ps1` во время открытого editor;
- Game target Development build;
- non-unity/shared-PCH-off build;
- cook/package/Shipping;
- clean-machine launch;
- full Hub→Tutorial→Mission functional playthrough;
- forced path abort/current enemy destroy/mid-action retry tests;
- save I/O failure/corruption/version compatibility tests;
- Unreal Insights packaged cold start/load/memory trace;
- multiplayer/replication tests;
- семантический аудит всех third-party Blueprint/StateTree/material/animation/ControlRig graphs.

## 8. Независимый critic: recalibration verdict

Critic не участвовал в первичном сборе evidence и проверил формулировки на overclaim. Итог:

| Finding/решение | Verdict |
|---|---|
| `MOVE-001` | upheld `High/High`; не расширять AP refund на любое частичное движение |
| `UI-001` | upheld `High/High` |
| `TURN-001` | algorithmic gap upheld, но runtime reproduction нужен; `High/Medium/Inference` |
| `UX-001` | missing overlay остаётся High; fog bake sample ~95 ms не оправдывает async без профиля |
| `REL-001` | понижен до `Medium/High`: process/release gate gap, не доказанный cook failure |
| `TEST-001` | понижен до `Medium/High`: узкий prototype regression net |
| `DEP-001`, `MOD-001` | Medium; folder cycles не называть UE module cycles |
| `CODE-001` | Medium; decomposition после correctness tests |
| `LOAD-001` | performance impact остаётся hypothesis до packaged cold trace |
| `FLOW-001` | concrete GameMode→UI допустим для прототипа; вред имеет низкую уверенность |
| `SEC-001` | Medium только для editor/local trust boundary; не Shipping |
| Target architecture | минимальный Variant A upheld; немедленный runtime module split признан overengineering |

В итоговых severity/confidence этого индекса эта recalibration уже учтена.

## 9. Правило воспроизводимости

Для закрытия finding недостаточно изменить код или поставить checkbox. Evidence chain должна содержать:

1. ссылку на commit и изменённый contract;
2. автоматизированный negative/positive test либо явно записанную ручную процедуру;
3. результат build/cook/package, если затронута boundary/asset/release часть;
4. до/после trace, если рекомендация мотивирована производительностью;
5. обновлённые GDD/architecture/backlog ссылки;
6. отсутствие regression по зависимому flow из таблицы выше.

Такой порядок не превращает гипотезу в «факт» только потому, что для неё уже написан fix.
