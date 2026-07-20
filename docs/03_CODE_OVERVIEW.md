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
| `UTurnManagerSubsystem` | фазы, ход AI по юнитам, конец боя, **таймер ходов бомбы**; действующему врагу снимает навмеш-вырез + пауза (`EnemyActivationDelay`) на латание навмеша/полёт камеры перед `ExecuteUnitTurn` | `StartCombat`, `EndTurn`, `CheckCombatOutcome`, `SetTurnLimit`, `GetTurnsRemaining`, `bAutoWinWhenEnemiesDead`, `IsUnitOnActiveSide`, `GetSideUnits`/`GetOpposingUnits`, `GetPlayerSideUnits`/`GetEnemySideUnits` (сырые списки сторон для подписок HUD), `GetAliveEnemyCount` (критерий «жив» общий с CheckCombatOutcome); делегаты `OnTurnStarted`, `OnCombatEnded`, `OnTurnLimitExpired`, **`OnEnemyUnitActivated`** (фокус камеры на действующем враге) |
| `UActionPointsComponent` | AP юнита (2/ход) | `TrySpendActionPoint`, `SpendAllRemaining` (XCOM-правило/скип), `GrantExtraPoints` (Run&Gun), `OnActionPointsChanged` |
| `UCoverDetectionComponent` | укрытия трейсами, GE-теги Cover.*, бонус защиты 20/40 | `EvaluateSurroundings`, `GetCoverAgainst`, `GetDefenseBonusAgainst`, `OnCoverStateChanged` (стойки в ABP) |
| `AUnitBase` | статы (BaseAim/ShotDamage/AttackRange/MoveRange, Squadsight, bCanBeDowned), классы способностей юнита, **смерть/Downed/эвакуация**, PatrolPoints, **подсветка** (кольцо-декаль `SelectionDecal` + обводка Custom Depth: stencil 1 = отряд, 2 = враг); в конструкторе включён **`bUseAccelerationForPaths`** (AI-движение заполняет Acceleration, иначе стоковые локомоушен-ABP не видят движения); **навмеш юнит НЕ трогает** (капсула не влияет на навигацию — занятость через диски `UTacticsCombatStatics::GetUnitObstacles`) | `SetDowned`/`ReviveFromDowned` (медик + скрипты туториала), `Evacuate`, `SetSelectionHighlight`/`SetHoverHighlight`, BP-хуки `OnDied/OnDownedChanged/OnEvacuated`, делегат `OnUnitStateChanged`; для HUD: `UnitDisplayName` (позывной), `GetAbilityUsesRemaining` (остаток применений способности, -1 = без лимита) |
| `UTacticalAbility` (база GA) | AP-кост в CheckCost/ApplyCost, **bConsumesAllRemainingAP** (XCOM «завершает активацию»), **MaxUsesPerMission**, bRequiresTargetActor, запрет в чужую фазу и **мёртвым/раненым/эвакуированным** (страховка от кликов HUD до обновления серости) | `GetUsesRemaining` |
| `UGA_Attack` | выстрел по событию Event.Attack (цель в payload): дальность/LOS/**Squadsight** (−10), статы с юнита, 1 AP + сжигает остаток | `CanTargetActor` (static, для HUD), BP-хук `OnShotFired` |
| `UGA_Overwatch` | реакционный выстрел в чужую фазу (штраф −10), тег State.Overwatch, самозавершение | BP-хук `OnReactionShot` |
| `UGA_HunkerDown` / `UGA_Taunt` | стойки до своего след. хода (GE State.HunkeredDown ×2 укрытия / State.Taunting: приоритет цели AI + −50% урона) | база `UGA_SelfBuffUntilNextTurn` |
| `UGA_Heal` | лечение/подъём по Event.Heal (цель в payload): +50 HP или ревив 30 HP, радиус 600, 2 заряда | `CanHealTarget`, BP-хук `OnHealApplied` |
| `UGA_RunAndGun` | +1 AP немедленно, 1 раз/миссию | BP-хук `OnRunAndGun` |
| `UTacticsCombatStatics` | общий выстрел/проверки + **занятость** (навмеш статичен, юниты — диски `UnitObstacleRadius` = 60 см на уровне запросов, как occupancy-клетки XCOM) | `ResolveShot` (форс 100/0 для туториала; шумит врагам), `ComputeHitChance` (укрытие + hunker ×2), `HasLineOfSight`, `SquadHasLineOfSight`, `IsUnitAlive/Downed/Evacuated`, `NotifyCombatNoise`, `GetNavPathLength`, `GetPointAlongPathBudget`; занятость: `GetUnitObstacles` (бегущие исключаются — диск встанет по финишу), `IsLocationBlockedByUnit`, `AdjustGoalOutOfUnits` (выталкивание цели приказа на край диска) |
| `AUnitAIController` | perception (sight) + **XCOM-тревога: Patrol → Investigate (шум/потеря) → Combat**; бюджет пути AI = MoveRange; приоритет Taunt-цели; **Detour Crowd** (`UCrowdFollowingComponent` вместо стокового path following, качество High): бегущие юниты локально огибают стоящих — навмеш о юнитах не знает; по финишу перемещения зовёт `NotifyUnitMoveFinished` контроллера игрока | `ExecuteUnitTurn`, `NotifyNoiseHeard`, `GetAlertState` |
| `ATacticalPlayerController` | управление боем: выбор (ЛКМ/1–4/Tab; **мёртвые/эвакуированные невыбираемы**, смерть/эвакуация выбранного **снимает выбор** — подписка на его `OnUnitStateChanged`), движение ПКМ (**валидация по длине пути**: 1 AP ≤ 800, 2 AP ≤ 1600), атака кликом или **кнопкой «Огонь»** (`RequestAttack` → режим прицеливания, следующий ЛКМ по врагу; Event.Attack), хоткеи Y/X/R/F/Backspace/Enter/Esc, режим цели медика, камера; **`GetSquad` = сторона игрока «как есть»** (включая мёртвых/раненых — портреты HUD и слоты 1–4 стабильны; живость фильтруют места использования); **PlayerTick**: превью пути под курсором (троттлинг 25 см) + **ховер-обводка** юнита под курсором (трейс ECC_Pawn; **в фазу врага свои юниты не подсвечиваются**, враги — всегда); **кольцо выбора** — только в свою фазу (в ход врага прячется, `RefreshSelectionHighlight`); **старт боя**: камера на центр отряда (мгновенно), юнит НЕ выбирается (игрок берёт сам); **ход врага**: камера сопровождает действующего юнита, но ТОЛЬКО если отряд его видит (LOS-гейт — скрытые ходят «за кадром»); **edge scroll** мышью у края экрана (`bEdgeScrollEnabled`/`EdgeScrollMarginPx`; **выключен в ход врага** — иначе курсор у края рвал бы авто-follow); свой бегущий боец тоже сопровождается **зона хода строится синхронно и сразу** (навмеш статичен; `NotifyUnitMoveFinished` от AIController мгновенно перестраивает зону, когда финишировал другой юнит); точка приказа выталкивается из дисков занятости (`AdjustGoalOutOfUnits`) | `SelectUnit(BySlot/Next)`, `GetSquad`, `Request*` (включая `RequestAttack`), `IsTargetingAttack/Ability`, `GetHoveredUnit`, `GetAvailableInteraction` (контекст F: бомба/эвакуация — для кнопки HUD), `NotifyUnitMoveFinished` (зовёт AIController), делегаты `OnSelectedUnitChanged`, `OnHoveredUnitChanged` |
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
| `UTacticalHUDWidget` | база боевого HUD: сам подписан на TurnManager/контроллер/юнитов обеих сторон; BP-хуки `OnPhaseChanged(фаза, ход, осталось)`, `OnSelectedUnitChanged`, `OnHoveredUnitChanged` (панель цели), `OnUnitsStateChanged` (портреты/счётчик), `OnCombatFinished`; хелперы `GetSquad`, `GetHitChanceOnTarget` (-1 и при пустом выборе — панель цели показывать только при выбранном бойце), `GetTargetCoverAgainstSelected` (иконка щита цели, как в XCOM), `GetAliveEnemyCount` |
| `UAPPipsWidget` / `UCoverIconWidget` | HUD над головой: пипсы AP (подписка на `UActionPointsComponent` через аватара ASC; учёт бонус-AP Run&Gun) и иконка укрытия (подписка на `UCoverDetectionComponent`; текстуры Half/Full — в BP-наследнике). Добавляются слотами в `UUnitHUDLayoutData` |
| `AMenuPlayerController` / `ACSTPlayerController` | контроллеры уровней меню/хаба |

### 2.3 Договорённости для нового кода
- Боевые GA — только от `UTacticalAbility`; урон/лечение — только `ResolveShot`
  / GE с SetByCaller; теги — нативные в `TacticsGameplayTags`.
- Команды: отряд TeamId=1, враги TeamId=2 (`DefaultTeamId` в BP).
- GE-компоненты в конструкторах — `CreateDefaultSubobject`+`GEComponents.Add`.

## 3. Остаточный беклог C++ (P2, только по остатку времени)

1. AI: выбор точки перемещения С УКРЫТИЕМ от цели (сэмплинг + GetCoverAgainst);
   scamper-рывок при первом обнаружении.
2. ~~Обводка-контур зоны хода~~ — сделано (2026-07-15): гладкий контур marching
   squares по полю дистанций + лента превью пути. Возможная полировка: пунктир
   вместо ленты, анимация материала контура.
3. Всплывающие числа урона / декаль-маркеры подсказок туториала — BP-only.
4. Процедурная раскладка укрытий PCG для L_Mission01 (стретч GDD §12).

Всё остальное из GDD в коде есть. Если при сборке уровней вскроется дыра —
сначала записать сюда, потом чинить.
