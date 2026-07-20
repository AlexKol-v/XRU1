# Обзор кодовой базы и беклог C++

Снимок на 2026-07-17 (после C++-подготовки этапа 6: HUD-хелперы и виджеты
над головой). **Кодовая база закрывает весь GDD** — дальше работа в редакторе
(см. 04_ROADMAP этапы 2+; чеклист сборки HUD — `08_HUD_TODO.md`).

---

## 1. Архитектура: кто за что отвечает

```
ACharacter → ABaseCharacter (TeamId) → AGASCharacter (ASC) → ATDCombatant
(атрибуты Health/Shield + HUD над головой) → AUnitBase (тактический юнит)
   ├─ AUnit_Assault / Sniper / Healer / Tank (классы отряда; статы GDD §7 в C++)
   └─ BP_Unit_Marauder (враг: BP-наследник AUnitBase, TeamId=2)
```

- **Квест-система (STQuestSystem)** используется ВНУТРИ уровней: шаги туториала
  (objectives + notifications) и реплики миссии. **POI хаба** — свой лёгкий
  актор `AMissionPointOfInterest` (не квест-вэйпоинт): выбор миссии, гейт по
  прогрессу сейва, загрузка уровня. Связь между ними — `UTacticsSaveGame::
  CompletedMissions` (туториал пройден → POI миссии разблокирован).

## 2. Классы (Source/XRU1)

### 2.1 Тактическое ядро (`Tactics/`)

| Класс | Ответственность | Ключевые API |
|---|---|---|
| `UTurnManagerSubsystem` | фазы, ход AI по юнитам, конец боя, **таймер ходов бомбы**; действующему врагу снимает навмеш-вырез + пауза (`EnemyActivationDelay`) на латание навмеша/полёт камеры перед `ExecuteUnitTurn` | `StartCombat`, `EndTurn`, `CheckCombatOutcome`, `SetTurnLimit`, `GetTurnsRemaining`, `bAutoWinWhenEnemiesDead`, `IsUnitOnActiveSide`, `GetSideUnits`/`GetOpposingUnits`, `GetPlayerSideUnits`/`GetEnemySideUnits` (сырые списки сторон для подписок HUD), `GetAliveEnemyCount` (критерий «жив» общий с CheckCombatOutcome); делегаты `OnTurnStarted`, `OnCombatEnded`, `OnTurnLimitExpired`, **`OnEnemyUnitActivated`** (фокус камеры на действующем враге), **`OnTurnLimitChanged`** (2026-07-20: лимит сняли посреди боя — HUD гасит «До подрыва» сразу, а не в начале следующего хода) |
| `UActionPointsComponent` | AP юнита (2/ход) | `TrySpendActionPoint`, `SpendAllRemaining` (XCOM-правило/скип), `GrantExtraPoints` (Run&Gun), `OnActionPointsChanged` |
| `UCoverDetectionComponent` | укрытия трейсами, GE-теги Cover.*, бонус защиты 20/40 | `EvaluateSurroundings`, `GetCoverAgainst`, `GetDefenseBonusAgainst`, `OnCoverStateChanged` (стойки в ABP) |
| `AUnitBase` | статы (BaseAim/ShotDamage/AttackRange/MoveRange, Squadsight, bCanBeDowned), классы способностей юнита, **смерть/Downed/эвакуация**, PatrolPoints, **подсветка** (кольцо-декаль `SelectionDecal` + обводка Custom Depth: stencil 1 = отряд, 2 = враг); в конструкторе включён **`bUseAccelerationForPaths`** (AI-движение заполняет Acceleration, иначе стоковые локомоушен-ABP не видят движения); **навмеш юнит НЕ трогает** (капсула не влияет на навигацию — занятость через диски `UTacticsCombatStatics::GetUnitObstacles`) | `SetDowned`/`ReviveFromDowned` (медик + скрипты туториала), `Evacuate`, `SetSelectionHighlight`/`SetHoverHighlight`, BP-хуки `OnDied/OnDownedChanged/OnEvacuated`, делегат `OnUnitStateChanged`; для HUD: `UnitDisplayName` (позывной), `GetAbilityUsesRemaining` (остаток применений способности, -1 = без лимита) |
| `UTacticalAbility` (база GA) | AP-кост в CheckCost/ApplyCost, **bConsumesAllRemainingAP** (XCOM «завершает активацию»), **MaxUsesPerMission**, bRequiresTargetActor, запрет в чужую фазу и **мёртвым/раненым/эвакуированным** (страховка от кликов HUD до обновления серости) | `GetUsesRemaining` |
| `UGA_Attack` | выстрел по событию Event.Attack (цель в payload): дальность/LOS/**Squadsight** (−10), статы с юнита, 1 AP + сжигает остаток | `CanTargetActor` (static, для HUD), **`ComputeAttackHitChance`** (static, 2026-07-20: ЕДИНЫЙ расчёт шанса для HUD-прогноза и выстрела — Squadsight-штраф больше не расходится с панелью цели), BP-хук `OnShotFired` |
| `UGA_Overwatch` | реакционный выстрел в чужую фазу (штраф −10 поверх общего расчёта точности), тег State.Overwatch, самозавершение; право на реакцию — **общий предикат `CanTargetActor`** (2026-07-20: радиус перцепции шире дальности оружия, раньше юнит реагировал на недостижимые цели) | BP-хук `OnReactionShot` |
| `UGA_HunkerDown` / `UGA_Taunt` | стойки до своего след. хода (GE State.HunkeredDown ×2 укрытия / State.Taunting: приоритет цели AI + −50% урона) | база `UGA_SelfBuffUntilNextTurn` |
| `UGA_Heal` | лечение/подъём по Event.Heal (цель в payload): +50 HP или ревив 30 HP, радиус 600, 2 заряда | `CanHealTarget`, BP-хук `OnHealApplied` |
| `UGA_RunAndGun` | +1 AP немедленно, 1 раз/миссию | BP-хук `OnRunAndGun` |
| `UTacticsCombatStatics` | общий выстрел/проверки + **занятость** (навмеш статичен, юниты — диски `UnitObstacleRadius` = 60 см на уровне запросов, как occupancy-клетки XCOM) | `ResolveShot` (форс 100/0 для туториала; шумит врагам), `ComputeHitChance` (укрытие + hunker ×2), `HasLineOfSight`, `SquadHasLineOfSight`, `IsUnitAlive/Downed/Evacuated`, `NotifyCombatNoise`, `GetNavPathLength`, `GetPointAlongPathBudget`; занятость: `GetUnitObstacles` (бегущие исключаются — диск встанет по финишу), `IsLocationBlockedByUnit`, `AdjustGoalOutOfUnits` (выталкивание цели приказа на край диска) |
| `AUnitAIController` | perception (sight) + **XCOM-тревога: Patrol → Investigate (шум/потеря) → Combat**; бюджет пути AI = MoveRange; приоритет Taunt-цели **в радиусе `TauntPriorityRadius` = 1200** (GDD §7; раньше провокация тянула врагов через всю карту); **выстрел AI — через то же событие Event.Attack, что у игрока** (2026-07-20, `TryFireAtTarget`): один предикат прицеливания (`UGA_Attack::CanTargetActor` — раньше AI судил по `LineOfSightTo`, игрок по `HasLineOfSight`, и правила расходились), одна стоимость AP (включая XCOM-сжигание остатка), одни BP-хуки выстрела. Фолбэк прямым `ResolveShot` — только если юниту не назначен `AttackAbilityClass` (о чём предупреждает лог в `OnPossess`); если способность не заплатила AP, шаг завершает ход, а не зацикливается; **Detour Crowd** (`UCrowdFollowingComponent` вместо стокового path following, качество High): бегущие юниты локально огибают стоящих — навмеш о юнитах не знает; по финишу перемещения зовёт `NotifyUnitMoveFinished` контроллера игрока | `ExecuteUnitTurn`, `NotifyNoiseHeard`, `GetAlertState` |
| `ATacticalPlayerController` | управление боем: выбор (ЛКМ/1–4/Tab; **мёртвые/эвакуированные невыбираемы**; **ЛКМ в пустоту**: в XCOM-режиме выбор не снимает, в ручном (`bAutoSelectUnits` выключен) — снимает, иначе снять выбор было бы нечем), движение ПКМ (**валидация по длине пути**: 1 AP ≤ 800, 2 AP ≤ 1600), атака кликом или **кнопкой «Огонь»** (`RequestAttack` → режим прицеливания, следующий ЛКМ по врагу; Event.Attack), хоткеи Y/X/R/F/Backspace/Enter/Esc, режим цели медика (**R гейтится по AP/зарядам** — как серость кнопки), камера; **XCOM-автовыбор `bAutoSelectUnits`** (2026-07-20, отключаемо): ход игрока всегда начинается с бойца, который МОЖЕТ действовать (не только «выбора нет», но и «выбранный погиб/упал раненым/эвакуирован» — иначе после ранения в ход врага новый ход начинался бы с мёртвой панели действий) + переход к следующему при смерти/эвакуации выбранного (в свою фазу; в фазу врага выбор просто снимается — камеру не дёргаем) + при исчерпании AP (`SelectNextUnit`; бегущего не переключаем — PlayerTick доделает по остановке); **`GetSquad` = сторона игрока «как есть»** (включая мёртвых/раненых — портреты HUD и слоты 1–4 стабильны; живость фильтруют места использования); **PlayerTick**: превью пути под курсором (троттлинг 25 см) + **ховер-обводка** юнита под курсором (трейс ECC_Pawn; **в фазу врага свои юниты не подсвечиваются**, враги — всегда); **кольцо выбора** — только в свою фазу (в ход врага прячется, `RefreshSelectionHighlight`); **старт боя**: камера на центр отряда (мгновенно); **ход врага**: камера сопровождает действующего юнита, но ТОЛЬКО если его видит хоть один живой боец (LOS-трейс по каждому, ≤ 2500 — скрытые ходят «за кадром»); **edge scroll** мышью у края экрана (`bEdgeScrollEnabled`/`EdgeScrollMarginPx`; **выключен в ход врага** — иначе курсор у края рвал бы авто-follow); свой бегущий боец тоже сопровождается; **зона хода строится синхронно и сразу** (навмеш статичен; `NotifyUnitMoveFinished` от AIController мгновенно перестраивает зону, когда финишировал другой юнит); точка приказа выталкивается из дисков занятости (`AdjustGoalOutOfUnits`) | `SelectUnit(BySlot/Next)`, `SelectNextAvailableUnit` (автовыбор: с AP → любой живой → nullptr), `GetSquad`, `Request*` (включая `RequestAttack`), `IsTargetingAttack/Ability`, `GetHoveredUnit`, `GetAvailableInteraction` (контекст F: бомба/эвакуация — для кнопки HUD), `NotifyUnitMoveFinished` (зовёт AIController), делегаты `OnSelectedUnitChanged`, `OnHoveredUnitChanged`, **`OnAvailableActionsChanged`** (2026-07-20: выбранный боец добежал — HUD пересчитывает серость кнопок и панель цели; без него кнопка F оставалась серой у самой бомбы, т.к. AP списываются в момент приказа, а не прибытия) |
| `ATacticalCameraPawn` | XCOM-камера: панорама/поворот 45°/зум/**полёт к цели** с интерполяцией (`FocusInterpSpeed`); **следование за актором** (`SetFollowTarget`, ручная панорама разрывает follow); **вешает `OutlineMaterial`** (M_OutlinePP) блендаблом на unbound-`PostProcessComponent` в BeginPlay (PPV на карте не нужен) | `AddPanInput/AddRotationStep/AddZoomInput`, `FocusOnActor`/`FocusOnLocation` (bInstant), `SetFollowTarget`/`ClearFollowTarget` |
| `AMoveRangeVisualizer` | **зона хода**: волновой Dijkstra по сетке сэмплов `CellSize` (октильная метрика; связность рёбер — surface-raycast навмеша `NavMeshRaycast`, уровень — `ProjectPoint` от волны; полоса у порогов уточняется `FindPathSync`) → гладкий контур marching squares с разрывом на обрывах; зоны 1 AP/2 AP не пересекаются, при последнем AP зона жёлтая («рывок»); **кайма зоны** (2026-07-17): хорды границы сшиваются в полилинии, сглаживаются Chaikin (`BorderSmoothIterations`) и рисуются лентой `BorderWidth` (материалы `BorderOne/TwoMaterial`, фолбэк — материалы пути/зон; внутренний порог кольца не дублируется); **диски занятости** других юнитов волна огибает (граница диска — интерполяцией, стены — raycast'ом); **превью пути**: лента к курсору + маркер цели, цвет по стоимости AP, прячется на занятой точке | `ShowForUnit` (**bool**: false = юнит вне навмеша), `Hide`, `UpdatePathPreview`, `HidePathPreview`; материалы зон/пути/каймы — в BP |
| `ABombObjective` | цель «бомба»: 2 действия «Обезвредить» по 1 AP вплотную | `TryDefuse`, `CanDefuse`, делегаты `OnDefuseProgress`, `OnDisarmed` |
| `AEvacZone` | зона эвакуации: активация после цели, «Эвакуация» 1 AP | `ActivateZone`, `TryEvacuate`, `OnUnitEvacuated`, `bActiveFromStart` |
| `ATacticsGameMode` | сбор сторон по TeamId, сложность (HP/aim/таймер по `DifficultyParams`), бомба→эвакуация→победа, результат→сейв | `ActivateEvacuation` (зовёт и скрипт туториала), `WasDefeatByTimeout`; настройки: `MissionId`, `bWinWhenAllEnemiesDead`, `MissionResultWidgetClass`, `TacticalHUDClass` |
| `AMissionPointOfInterest` | POI хаба + **гейт** `RequiredCompletedMission` | `IsLocked`, `SelectPointOfInterest`, BP-хук `OnSelectionDenied` |
| `UTacticsGameInstance` / `UTacticsSaveGame` | кампания: сложность, `CompletedMissions`, travel | `StartNewCampaign`, `Save/LoadCampaign`, `TravelToHub/ToMainMenu` |
| `TacticsGameplayTags` | нативные теги | Cover.Half/Full, State.Overwatch/HunkeredDown/Taunting/Downed, Data.Damage/Heal, Event.Attack/Heal |
| `TacticsGameplayEffects` | C++ GE | UGE_CoverHalf/Full, UGE_ShotDamage, UGE_Heal, UGE_HunkerDown, UGE_TauntShield |

### 2.2 UI (`UI/`, `UI/Menus/`)

| Класс | Ответственность |
|---|---|
| `UPrimaryGameLayout` + `UGameUIManagerSubsystem` | корневые стеки слоёв; пересоздание после смены уровня |
| `UMenuScreenBase` и экраны меню | навигация стека: MainMenu/Difficulty/Settings/About/Pause (см. GDD §4) |
| `UMissionResultWidget` | экран результата: `SetupResult(bVictory, bByTimeout)`, кнопки `RetryMission/GoToHub/GoToMainMenu`, BP-хук `OnResultReady` |
| `UTacticalHUDWidget` | база боевого HUD: сам подписан на TurnManager/контроллер/юнитов обеих сторон; BP-хуки `OnPhaseChanged(фаза, ход, осталось)`, `OnSelectedUnitChanged`, `OnHoveredUnitChanged`, `OnUnitsStateChanged` (портреты/счётчик), `OnCombatFinished`; хелперы `GetSquad`, `GetHitChanceOnTarget`, `GetTargetCoverAgainstSelected`, `GetAliveEnemyCount`. **С 2026-07-20 значительная часть логики — в C++ через `BindWidgetOptional`** (имена виджетов Designer = имена свойств): `RefreshActionButtons` (серость кнопок; BP-версия ловила Accessed None — AND в BP не short-circuit), `UpdateTargetPanel` (панель цели: имя/HP/шанс/щит укрытия; показывается только по врагу при выбранном бойце), `UpdateSquadCardVisibility` (XCOM: карточка только выбранного, `bShowOnlySelectedCard`), `ApplyStyle` (размеры/иконки из `UTacticalHUDStyleData` в `NativePreConstruct`), клик AttackBtn → `RequestAttack` (подписан в C++ — OnClicked в BP для AttackBtn НЕ добавлять) |
| `UTacticalHUDStyleData` | DataAsset единого стиля боевого HUD: размеры иконок кнопок/щита укрытия, текстуры всех иконок, `bUnifyPressedPadding` (кнопка не «прыгает» при нажатии), `ActionButtonPadding` (2026-07-20: единый `Normal/PressedPadding` на всех 6 кнопках ActionsPanel + EndTurnBtn — раньше разный `NormalPadding` каждой кнопки из Designer давал визуально разный размер кнопки при одинаковой иконке). Назначается в Class Defaults WBP_TacticalHUD (поле `Style`, ассет `DA_TacticalHUDStyle`); незаданная текстура не трогает браш из Designer |
| `UAPPipsWidget` / `UCoverIconWidget` | HUD над головой: пипсы AP (подписка на `UActionPointsComponent` через аватара ASC; учёт бонус-AP Run&Gun) и иконка укрытия (подписка на `UCoverDetectionComponent`; текстуры Half/Full — в BP-наследнике). Добавляются слотами в `UUnitHUDLayoutData` |
| `AMenuPlayerController` / `ACSTPlayerController` | контроллеры уровней меню/хаба |

### 2.3 Договорённости для нового кода
- Боевые GA — только от `UTacticalAbility`; урон/лечение — только `ResolveShot`
  / GE с SetByCaller; теги — нативные в `TacticsGameplayTags`.
- Команды: отряд TeamId=1, враги TeamId=2 (`DefaultTeamId` в BP).
- GE-компоненты в конструкторах — `CreateDefaultSubobject`+`GEComponents.Add`.

### 2.4 Архитектурный аудит 2026-07-20 (Tactics/ + UI/)

Прогнан целевой аудит написанного с нуля кода (антипаттерны/костыли, не
стиль). Общий вывод: качество высокое, GAS/подписки/делегаты — без утечек.
Найдено и исправлено:
- `UTurnManagerSubsystem::GetActiveSideUnits()` возвращал `const TArray<AActor*>&`
  на мутируемый member-кэш (аляйзинг при повторном вызове) — и не вызывался
  НИГДЕ (ни C++, ни BP). Приведён к возврату по значению, как у соседних
  геттеров (`GetPlayerSideUnits`/`GetEnemySideUnits`/`GetOpposingUnits`); кэш убран.
- `UCoverDetectionComponent::HalfCoverTag`/`FullCoverTag` — `EditDefaultsOnly`
  свойства, выглядящие настраиваемыми («используется UI/способностями»), но
  реальный тег хардкожен в конструкторах `UGE_CoverHalf`/`UGE_CoverFull`
  (тот же паттерн, что у `State.HunkeredDown`/`State.Taunting`) — свойства
  ничего не делали. Удалены как несогласованный обломок старой итерации.
- `ABombObjective::CanDefuse` считал дистанцию через `FVector::Dist` (3D),
  а `AEvacZone::IsUnitInside` — через `Dist2D`: интеракция с зарядом на
  возвышении (стол/консоль) эффективно теряла в радиусе от разницы по Z.
  `CanDefuse` приведён к `Dist2D` (конвенция зоны эвакуации — она корректна).
- `ATacticsGameMode` — комментарий класса заявлял отсев врагов по
  `MinDifficulty` (концепция не существует в коде, только в этом комментарии)
  — исправлен на факт (сложность правит только статы, не состав врагов);
  добавлен `UE_LOG` Warning, если для активной сложности нет записи в
  `DifficultyParams` (раньше таймер бомбы тихо гас до 0 без единого следа в логе).
- `UTacticsGameInstance::TravelToHub/TravelToMainMenu` — добавлен `UE_LOG`
  Warning на тихий no-op при незаданном `HubLevel`/`MainMenuLevel`.

### 2.5 Достижимость перемещения — единственный источник правды (2026-07-20)

**Правило: «можно ли приказать идти сюда» отвечает ТОЛЬКО поле дистанций.**

Навмеш статичен и о юнитах не знает (капсулы `SetCanEverAffectNavigation
(false)`), занятость решается на уровне запросов дисками
(`UnitObstacleRadius` = 60 см, аналог занятого тайла XCOM). Прямой
navmesh-запрос в чистом поле прокладывает прямую СКВОЗЬ стоящего бойца, и по
нему нельзя отличить «одиночный юнит на пути, обходится» от «коридор
перекрыт тремя, обхода нет». Отличить умеет только волновой Dijkstra
`AMoveRangeVisualizer::BuildDistanceField` — он огибает диски, то есть учитывает
обходы.

Поэтому:
- `AMoveRangeVisualizer::GetMoveCostTo(Goal)` — публичный запрос к полю
  (билинейно, с той же условностью «недостижимый сэмпл = чуть за краем», что
  у marching squares) — **единственный** ответ на «достижимо и за сколько AP»;
- его зовут И валидация клика (`ATacticalPlayerController::TryMoveSelectedUnit`),
  И превью пути — поэтому зона, лента и клик согласованы ПО ПОСТРОЕНИЮ;
- пороги 1AP/2AP живут в одной `UTacticsCombatStatics::GetMoveCostForDistance`
  (раньше — три копии с расхождениями: контроллер `CanSpend(2)`, превью
  `AP>=2`, зона — свои бюджеты);
- `GetNavPathLength` вернулась к роли ЧИСТОЙ навигационной метрики (длина
  пути), без суждений о занятости;
- AI поле не строит (дорого на каждого врага) — `GetPointAlongPathBudget`
  **усекает** путь по `GetTransitClearance` (занятая клетка + радиус капсулы
  бегущего, читается с актора, а не константой): враг подходит насколько
  может, а не теряет ход, упёршись в союзника.

> ⚠️ Осознанный компромисс: `AMoveRangeVisualizer` совмещает модель
> (поле достижимости) и вид (отрисовка контуров). Так сделано намеренно —
> единственный источник правды важнее чистоты слоёв, а поле всё равно нужно
> строить для отрисовки. Идеальный вариант — вынести поле в отдельный
> `FMoveDistanceField` (модель), которым владеет контроллер, а визуализатор
> получает `const&` только для рендера. Внесено в беклог P2 (§3), не сделано
> из-за риска регрессии в рабочем marching squares перед сдачей.

Осознанно НЕ тронуто (не баг, просто на будущее):
- Дублирование паттерна «жив+в радиусе+хватает AP» между `CanDefuse`/
  `CanEvacuate` — рефактор в общую утилиту оправдан только если правила
  разъедутся дальше; сейчас две независимые, но идентичные проверки.
- `ATacticsGameMode::BeginPlay` подписывается на цели миссии ОДНИМ проходом
  `TActorIterator` — если объект (`ABombObjective`/`AEvacZone`) когда-нибудь
  будет спавниться скриптом ПОСЛЕ `BeginPlay` (сейчас все цели — placed in
  level, ничего так не делает), он не подпишется. Не трогали — гипотетический
  риск без текущего триггера (YAGNI).
- `UTacticsSaveGame::SquadRoles` заполняется дефолтным ростером в
  `StartNewCampaign`, но не читается ни в одном C++ файле — не выяснено,
  использует ли его BP-слой меню (экран ростера); не удаляли, т.к. риск
  выше пользы при неподтверждённом дохождении.

## 3. Остаточный беклог C++ (P2, только по остатку времени)

1. AI: выбор точки перемещения С УКРЫТИЕМ от цели (сэмплинг + GetCoverAgainst);
   scamper-рывок при первом обнаружении.
2. ~~Обводка-контур зоны хода~~ — сделано (2026-07-15): гладкий контур marching
   squares по полю дистанций + лента превью пути. Возможная полировка: пунктир
   вместо ленты, анимация материала контура.
3. Всплывающие числа урона / декаль-маркеры подсказок туториала — BP-only.
3a. **Разделить модель/вид зоны хода**: вынести поле дистанций из
   `AMoveRangeVisualizer` в отдельный `FMoveDistanceField` (чистая модель,
   владеет контроллер), визуализатору отдавать `const&` только на отрисовку.
   Сейчас поле совмещено с рендером — см. врезку в §2.5.
4. Процедурная раскладка укрытий PCG для L_Mission01 (стретч GDD §12).

Всё остальное из GDD в коде есть. Если при сборке уровней вскроется дыра —
сначала записать сюда, потом чинить.
