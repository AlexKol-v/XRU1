# Расхождения документации и реализации

Дата среза: **2026-08-04**, commit `c8edbc8027a607f480307eef21359526f2e18654`.

Все перечисленные ниже расхождения подтверждены минимум двумя сторонами: утверждением документа/комментария и фактическим файлом, символом, конфигурацией или ассетом. Этот аудит не исправляет исходные документы: он фиксирует, что синхронизировать после принятия решений.

## 1. Как выбирать источник истины при конфликте

У XRU1 есть полезное правило «`docs/` — источник истины», но оно не отменяет необходимость различать **намерение** и **фактическое состояние**.

| Вопрос | Предпочтительный источник | Почему |
|---|---|---|
| Какие файлы, модули, API и ассеты реально существуют | checkout, build descriptors, C++, Asset Registry/Blueprint query | это исполняемая/собираемая поверхность текущего commit |
| Как игра должна работать | `docs/01_GDD.md` | это дизайн-контракт; расхождение с кодом требует решения и правки GDD до изменения правила |
| Как сейчас устроена реализация | `docs/03_ARCHITECTURE.md`, но только после сверки с кодом/ассетами | документ свежий и подробный, однако уже содержит минимум одну удалённую структуру |
| Что открыто и что принято | `docs/04_BACKLOG.md` | он специально разделяет «работа» и «сделано, но не принято» (`:8-16`) |
| Как выполнять работу | `docs/05_WORKFLOW.md` | это текущая замена старых editor/conventions документов |
| Текущее состояние AI | `docs/08_AI.md` + `docs/04_BACKLOG.md` | два брифа в `docs/agents` сохраняют исторический контекст и местами старое состояние |

Если GDD противоречит фактическому ассету, «текущая правда» в таблице означает **что произойдёт на этом commit**, а не автоматическое одобрение реализации. Дизайн-владелец должен либо привести реализацию к GDD, либо сначала явно изменить GDD.

## 2. Сводная таблица drift

| ID | Приоритет | Устаревшее утверждение | Фактическая текущая правда | Доказательство | Рекомендуемая синхронизация |
|---|---|---|---|---|---|
| DOC-001 | **High / P0** | `AGENTS.md` направляет в удалённые `03_CODE_OVERVIEW.md`, `04_ROADMAP.md`, `05_EDITOR_GUIDE.md`, `06_CONVENTIONS.md` | актуальная карта: `03_ARCHITECTURE.md`, `04_BACKLOG.md`, `05_WORKFLOW.md`; отдельного roadmap/conventions файла нет | `AGENTS.md:15-16,78,88`; реестр `docs/README.md:13-23`; все четыре старых файла отсутствуют | сначала исправить agent routing: старый code overview → Architecture, roadmap → Backlog, editor/conventions → Workflow; сохранить GDD как дизайн-контракт |
| DOC-002 | **Medium / P1** | `AGENTS.md` и Architecture утверждают, что `Variant_Strategy/` и `Variant_TwinStick/` ещё лежат в модуле и «могут быть удалены» | каталогов уже нет; остались только корневые template-классы `XRU1Character`, `XRU1GameMode`, `XRU1PlayerController` | `AGENTS.md:58-59`; `docs/03_ARCHITECTURE.md:36-37`; фактический список `Source/XRU1/` | убрать упоминание удалённых каталогов; отдельно решить и задокументировать судьбу трёх оставшихся template-классов |
| DOC-003 | **High / P1** | GDD называет единственной функцией укрытия `UCoverDetectionComponent::ResolveCoverAgainstDirection` | такого символа нет; публичные расчёты — `GetCoverAgainst` и `EvaluateCoverAtLocation`, использующие общую модель окружения | `docs/01_GDD.md:15-21`; определения `Source/XRU1/Tactics/CoverDetectionComponent.cpp:603,799`; актуальное описание `docs/03_ARCHITECTURE.md:103-119` | сохранить дизайн-инвариант «plan = fact», но заменить вымышленное имя на реальные entry points и явно назвать общий внутренний источник данных |
| DOC-004 | **High / P1** | GDD говорит, что профиль `Showreel` стартует полностью разведанным | оба scenario Data Asset имеют `bStartFullyExplored=false`; C++ default и fallback тоже `false`; Architecture уже описывает чёрную карту | `docs/01_GDD.md:286-295`; `Source/XRU1/Tactics/TacticalScenarioDataAsset.h:106-125`; `TacticsGameMode.cpp:466-471`; `docs/03_ARCHITECTURE.md:574-579`; `/Game/XRU1Game/Data/Missions/DA_Scenario_Tutorial`, `DA_Scenario_Mission01` | принять явное дизайн-решение; если текущая чёрная карта верна — исправить GDD, иначе изменить сначала GDD, затем оба ассета и acceptance tests |
| DOC-005 | **Medium / P1** | GDD помечает `WBP_MissionResult` как «ещё создать» | ассет существует и используется `GM_Tactics`; C++ хранит класс и push-ит экран результата | `docs/01_GDD.md:117-124`; `/Game/XRU1Game/UI/Menus/WBP_MissionResult`; Asset Registry edge `GM_Tactics → WBP_MissionResult`; `Source/XRU1/Tactics/TacticsGameMode.h:89-91`, `.cpp:708-719`; `docs/README.md:35-36` | пометить WBP как готовый; оставить отдельными только непройденные outcome/retry acceptance cases |
| DOC-006 | **Medium / P1** | GDD предписывает `PlaySound2D/AtLocation` из Blueprint | текущий контракт централизует 2D/3D/music/settings в `UTacticsAudioSubsystem`; низкоуровневые `UGameplayStatics` вызовы находятся в C++ subsystem | `docs/01_GDD.md:575-583`; `docs/03_ARCHITECTURE.md:1095-1112`; `Source/XRU1/Audio/TacticsAudioSubsystem.h:136-143`; `.cpp:388,422,709` | заменить BP-рецепт на subsystem contract; указать, что Blueprint/feature-код обращается к публичному audio API, а не играет звук напрямую |
| DOC-007 | **High / P0** | комментарии и инструкции ссылаются на восемь legacy-документов, которых больше нет | их содержание консолидировано в Architecture, Backlog и Workflow; внутри самих 12 текущих docs относительные Markdown-ссылки валидны | 24 строки / 27 упоминаний, подробный индекс в §3 | сначала исправить `AGENTS.md` и Config-комментарии, затем механически обновить code comments с проверкой смысла разделов |
| DOC-008 | **Medium / P1** | header `ATacticsGameMode` говорит, что отсев врагов по сложности не реализован и состав фиксирован | `ATacticsGameMode` вызывает `ATacticalEncounter::SpawnForDifficulty`, а encounter считает и создаёт состав по пресету сложности | `Source/XRU1/Tactics/TacticsGameMode.h:48-53`; `.cpp:200-225`; `TacticalEncounter.h:183-207`; `.cpp:325-363` | обновить class comment: GameMode координирует encounter spawn и затем применяет difficulty stats/profile |
| DOC-009 | **Medium / P2** | `BRIEF_AI_Refactor` утверждает, что AI profile assets отсутствуют и сложность меняет только HP/Aim | профили `DA_AI_Easy/Medium/Hard` существуют и назначены; актуальный статус и открытая ручная приёмка уже описаны в `08_AI`/Backlog | `docs/agents/BRIEF_AI_Refactor.md:68-95`; `docs/08_AI.md:342,781-813`; `docs/04_BACKLOG.md:159-178`; `docs/README.md:23` | не переписывать исторический бриф как текущий дизайн; добавить заметный статус «historical input, superseded by 08_AI/04_BACKLOG» и дату закрытия реализации |
| DOC-010 | **Low / P3** | project metadata всё ещё называется `Top Down Game Template` | проект, модуль и продукт называются XRU1 | `Config/DefaultGame.ini:3`; `XRU1.uproject:6-11`; `Source/XRU1.Target.cs:10-13` | перед package заменить display/project metadata на XRU1 и проверить packaged title/settings |

## 3. Индекс устаревших путей документации

Поиск восьми legacy-имён нашёл **24 строки** и **27 упоминаний** в `AGENTS.md`, `Config/` и `Source/XRU1`. Это не означает, что в текущих документах массово сломаны ссылки: отдельная проверка относительных `.md`-ссылок внутри 12 файлов `docs/` не нашла отсутствующих целей. Drift сосредоточен в инструкциях и комментариях после консолидации документации.

### 3.1 Agent routing

| Legacy target | Где упомянут | Текущая замена |
|---|---|---|
| `03_CODE_OVERVIEW.md` | `AGENTS.md:15,88` | `docs/03_ARCHITECTURE.md` + открытые пункты в `docs/04_BACKLOG.md` |
| `04_ROADMAP.md` | `AGENTS.md:15` | `docs/04_BACKLOG.md` |
| `05_EDITOR_GUIDE.md` | `AGENTS.md:16` | `docs/05_WORKFLOW.md` |
| `06_CONVENTIONS.md` | `AGENTS.md:16,78` | `docs/05_WORKFLOW.md`, а архитектурные контракты — `docs/03_ARCHITECTURE.md` |

Это P0 документации, потому что `AGENTS.md` читается до начала каждой агентской задачи. Ошибка маршрутизации заставляет нового исполнителя либо остановиться на отсутствующем файле, либо самостоятельно угадывать замену.

### 3.2 Config-комментарии

| Evidence | Legacy target | Текущая замена |
|---|---|---|
| `Config/DefaultGame.ini:13` | `docs/06_CONVENTIONS.md §3` | `docs/05_WORKFLOW.md` — дерево `Data/` и правила контента |
| `Config/DefaultEngine.ini:17` | `docs/09_UI_HUD.md §5.5` | `docs/03_ARCHITECTURE.md:1038-1061` — единый источник user settings |

Комментарии не влияют на runtime, но находятся рядом с параметрами, которые разработчик меняет вручную, поэтому их следует чинить раньше обычных code comments.

### 3.3 C++ comments

| Legacy документ | Evidence | Текущая замена |
|---|---|---|
| `06_CONVENTIONS` | `Source/XRU1/Tactics/FogGridSubsystem.h:45` | `docs/05_WORKFLOW.md` и принцип «поле только при наличии потребителя» в Architecture |
| `09_UI_HUD` | `Audio/TacticsAudioSubsystem.cpp:319`; `Tactics/GA_Overwatch.cpp:130`; `Tactics/TacticsGameInstance.cpp:68`; `Tactics/TacticsCombatStatics.cpp:339`; `UI/CombatFeedbackSubsystem.h:37`; `UI/Editor/XRU1WidgetAuthoringLibrary.cpp:78`; `UI/Menus/MenuWidgets.cpp:1293`; `UI/Menus/MissionBriefingWidget.h:11`; `UI/TacticalHUDWidget.cpp:1082`; `UI/TacticalHUDWidget.h:256` | `docs/03_ARCHITECTURE.md` §11 UI и §12 звук |
| `10_FOG_OF_WAR.md` | `Tactics/FogOfWarConfigDataAsset.h:14`; `Tactics/FogGridSubsystem.h:22`; `Tactics/TacticsGameMode.cpp:314`; `Tactics/UnitBase.cpp:171` | `docs/03_ARCHITECTURE.md:476-617` (§8) |
| `13_LOS_TARGETING.md` | `UI/TacticalHUDWidget.cpp:433` | `docs/03_ARCHITECTURE.md:125-199` (§4) |
| `14_SUBTITLES.md` | `Subtitles/SubtitleOverlay.cpp:121`; `UI/TacticalHUDStyleData.h:622` | `docs/03_ARCHITECTURE.md:1165-1241` (§13) |

Обновлять эти ссылки можно механически только после сверки смысла: старые номера подразделов (`§5.5`, `§2.6`, `§3.1`) не совпадают с новой структурой.

## 4. Подробный разбор ключевых конфликтов

### DOC-001 — карта документации в `AGENTS.md`

**Fact, High confidence.** `docs/README.md:13-23` содержит восемь актуальных верхнеуровневых документов, но `AGENTS.md:15-16` всё ещё описывает прежнюю схему. Более того, обязательная инструкция «свериться с ROADMAP» невыполнима буквально, потому что `04_ROADMAP.md` отсутствует.

Текущий operational truth:

- этап/открытая работа — `04_BACKLOG.md`;
- устройство кода — `03_ARCHITECTURE.md`;
- сборка/editor/conventions — `05_WORKFLOW.md`;
- правила игры — `01_GDD.md`.

Риск выше обычной битой ссылки: это bootstrap-инструкция для всех будущих агентов.

### DOC-002 — уже удалённые template variants

**Fact, High confidence.** Ни `Source/XRU1/Variant_Strategy`, ни `Source/XRU1/Variant_TwinStick` не существуют. При этом `Source/XRU1/XRU1Character.*`, `XRU1GameMode.*` и `XRU1PlayerController.*` остаются в корне. Документ смешивает уже выполненное удаление каталогов с ещё возможной чисткой отдельных классов.

Правильная формулировка должна перечислять только оставшиеся классы и не предлагать удалить отсутствующие папки.

### DOC-003 — API укрытий

**Fact, High confidence.** Поиск символа `ResolveCoverAgainstDirection` в tracked source не даёт определения или объявления. Реальные entry points:

- `GetCoverAgainst(const AActor*)` — `CoverDetectionComponent.cpp:603`;
- `EvaluateCoverAtLocation(const FVector&, const FVector&)` — `:799`.

При этом более важный инвариант GDD, что preview и факт должны исходить из одной модели стен, остаётся согласован с `docs/03_ARCHITECTURE.md:103-119`. Drift — в вымышленном API-имени и чрезмерном обещании «единственная функция», а не доказанный разрыв механики.

### DOC-004 — старт тумана

**Fact, High confidence.** GDD `:291-295` обещает полностью разведанный Showreel. Фактический контракт `UTacticalScenarioDataAsset` имеет default `false` (`TacticalScenarioDataAsset.h:125`), GameMode передаёт property или `false` (`TacticsGameMode.cpp:466-471`), а Blueprint/asset query подтвердил `false` в обоих Data Asset:

- `/Game/XRU1Game/Data/Missions/DA_Scenario_Tutorial`;
- `/Game/XRU1Game/Data/Missions/DA_Scenario_Mission01`.

Свежая Architecture `:576-579` уже описывает обе миссии с чёрной картой. Это сильный признак, что GDD не синхронизировали после решения, но именно владелец дизайна должен подтвердить выбор.

### DOC-005 — экран результата

**Fact, High confidence.** `WBP_MissionResult.uasset` существует, `GM_Tactics` имеет hard asset dependency на него, а `ATacticsGameMode::HandleCombatEnded` push-ит `MissionResultWidgetClass` (`TacticsGameMode.cpp:708-719`). Следовательно, фраза «WBP ещё создать» устарела. Открыты не создание ассета, а outcome/retry acceptance cases из Backlog.

### DOC-006 — владение звуком

**Fact, High confidence.** GDD сохранил ранний Blueprint-рецепт. Текущая реализация сознательно централизована: `UTacticsAudioSubsystem` — `UGameInstanceSubsystem`, через который идут музыка, stingers, 2D/3D и настройки. Вызовы `UGameplayStatics::PlaySoundAtLocation/PlaySound2D` остаются низкоуровневой реализацией subsystem, а не разрешением каждому BP играть напрямую.

Этот drift опасен тем, что новый контент может обойти SoundClass/settings/logging contract, следуя старому GDD.

### DOC-008 — difficulty composition

**Fact, High confidence.** Комментарий `TacticsGameMode.h:51-53` говорит «отсев не реализован», но `GatherEncounterEnemies` в `.cpp:200-225` вызывает `SpawnForDifficulty`, а `ATacticalEncounter` явно документирует и реализует количество бойцов по сложности (`TacticalEncounter.h:183-207`, `.cpp:325-363`). Это только stale comment; фактический runtime path существует.

### DOC-009 — исторические AI briefs

`BRIEF_AI_Behavior_Fix.md` уже содержит итоговый раздел «что закрыто и что осталось» (`:146-172`) и полезен как история. `BRIEF_AI_Refactor.md:73-74` всё ещё говорит, что трёх profile assets нет, хотя `docs/08_AI.md:342,781-813` и Backlog подтверждают их наличие.

`docs/README.md:23` называет оба файла брифами, оставленными до приёмки, но этого недостаточно, если файл открыт напрямую. Нужен короткий banner в начале каждого:

- статус `Historical brief`;
- baseline/date;
- актуальный источник `08_AI.md`;
- какие checklist-пункты всё ещё активны в `04_BACKLOG.md`.

Не следует переписывать старые симптомы задним числом: они объясняют происхождение решений.

### DOC-010 — project metadata

**Fact, High confidence.** `Config/DefaultGame.ini:3` содержит `ProjectName=Top Down Game Template`. Это не архитектурная ошибка и не влияет на механику, но может попасть в packaged metadata/UI и плохо выглядит на сдаче. Исправлять вместе с release branding, а не смешивать с P0 gameplay changes.

## 5. Что уже согласовано

Чтобы drift-отчёт не создавал впечатление тотальной ненадёжности документации:

- `docs/README.md` правильно перечисляет все 12 актуальных docs и честно отделяет готовую реализацию от непройденной приёмки;
- `docs/03_ARCHITECTURE.md` в целом совпадает с текущими владельцами состояния и основными runtime flows;
- `docs/04_BACKLOG.md:121-130` точно отмечает отсутствие fog functional tests и packaged soft-ref validation;
- `docs/04_BACKLOG.md:262-282,317-319` честно оставляет lifecycle, retry и package/clean-machine checks открытыми;
- `docs/08_AI.md` соответствует текущей реализации лучше исторических briefs;
- относительные Markdown-ссылки внутри текущего набора `docs/` разрешаются; основная проблема — ссылки из `AGENTS.md`, Config и C++ comments на удалённые документы.

## 6. Приоритет синхронизации

1. **P0 — bootstrap:** исправить карту документации в `AGENTS.md` и два Config-комментария. Это уменьшает риск новых неверных изменений.
2. **P1 — правила и публичные контракты:** решить fog start state; синхронизировать cover API, MissionResult, audio ownership и difficulty composition.
3. **P2 — навигация по коду:** заменить legacy-ссылки в C++ comments на реальные разделы Architecture/Workflow; добавить historical banners двум AI briefs.
4. **P3 — release polish:** заменить `ProjectName` и проверить остальные packaged metadata.

После синхронизации полезен простой CI link-check, который проверяет не только Markdown links в `docs/`, но и backtick/comment mentions вида `docs/NN_NAME.md` в `AGENTS.md`, `Config/` и `Source/`. Именно второй класс ссылок пропустила обычная проверка Markdown.
