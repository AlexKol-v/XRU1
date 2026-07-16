# Обзор кодовой базы и беклог C++

Снимок на 2026-07-14 (после этапа 1: P0+P1 реализованы). **Кодовая база
закрывает весь GDD** — дальше работа в редакторе (см. 04_ROADMAP этапы 2+).

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
| `UTurnManagerSubsystem` | фазы, ход AI по юнитам, конец боя, **таймер ходов бомбы**; действующему врагу снимает навмеш-вырез + пауза (`EnemyActivationDelay`) на латание навмеша/полёт камеры перед `ExecuteUnitTurn` | `StartCombat`, `EndTurn`, `CheckCombatOutcome`, `SetTurnLimit`, `GetTurnsRemaining`, `bAutoWinWhenEnemiesDead`, `IsUnitOnActiveSide`, `GetSideUnits`/`GetOpposingUnits`; делегаты `OnTurnStarted`, `OnCombatEnded`, `OnTurnLimitExpired`, **`OnEnemyUnitActivated`** (фокус камеры на действующем враге) |
| `UActionPointsComponent` | AP юнита (2/ход) | `TrySpendActionPoint`, `SpendAllRemaining` (XCOM-правило/скип), `GrantExtraPoints` (Run&Gun), `OnActionPointsChanged` |
| `UCoverDetectionComponent` | укрытия трейсами, GE-теги Cover.*, бонус защиты 20/40 | `EvaluateSurroundings`, `GetCoverAgainst`, `GetDefenseBonusAgainst`, `OnCoverStateChanged` (стойки в ABP) |
| `AUnitBase` | статы (BaseAim/ShotDamage/AttackRange/MoveRange, Squadsight, bCanBeDowned), классы способностей юнита, **смерть/Downed/эвакуация**, PatrolPoints, **подсветка** (кольцо-декаль `SelectionDecal` + обводка Custom Depth: stencil 1 = отряд, 2 = враг); в конструкторе включён **`bUseAccelerationForPaths`** (AI-движение заполняет Acceleration, иначе стоковые локомоушен-ABP не видят движения) и **капсула-препятствие навмеша** (`bDynamicObstacle`+NavArea_Null: пути/зоны огибают юнита; у действующего снимается) | `SetDowned`/`ReviveFromDowned` (медик + скрипты туториала), `Evacuate`, `SetNavObstacleEnabled`, `SetSelectionHighlight`/`SetHoverHighlight`, BP-хуки `OnDied/OnDownedChanged/OnEvacuated`, делегат `OnUnitStateChanged` |
| `UTacticalAbility` (база GA) | AP-кост в CheckCost/ApplyCost, **bConsumesAllRemainingAP** (XCOM «завершает активацию»), **MaxUsesPerMission**, bRequiresTargetActor, запрет в чужую фазу | `GetUsesRemaining` |
| `UGA_Attack` | выстрел по событию Event.Attack (цель в payload): дальность/LOS/**Squadsight** (−10), статы с юнита, 1 AP + сжигает остаток | `CanTargetActor` (static, для HUD), BP-хук `OnShotFired` |
| `UGA_Overwatch` | реакционный выстрел в чужую фазу (штраф −10), тег State.Overwatch, самозавершение | BP-хук `OnReactionShot` |
| `UGA_HunkerDown` / `UGA_Taunt` | стойки до своего след. хода (GE State.HunkeredDown ×2 укрытия / State.Taunting: приоритет цели AI + −50% урона) | база `UGA_SelfBuffUntilNextTurn` |
| `UGA_Heal` | лечение/подъём по Event.Heal (цель в payload): +50 HP или ревив 30 HP, радиус 600, 2 заряда | `CanHealTarget`, BP-хук `OnHealApplied` |
| `UGA_RunAndGun` | +1 AP немедленно, 1 раз/миссию | BP-хук `OnRunAndGun` |
| `UTacticsCombatStatics` | общий выстрел/проверки | `ResolveShot` (форс 100/0 для туториала; шумит врагам), `ComputeHitChance` (укрытие + hunker ×2), `HasLineOfSight`, `SquadHasLineOfSight`, `IsUnitAlive/Downed/Evacuated`, `NotifyCombatNoise`, `GetNavPathLength`, `GetPointAlongPathBudget` |
| `AUnitAIController` | perception (sight) + **XCOM-тревога: Patrol → Investigate (шум/потеря) → Combat**; бюджет пути AI = MoveRange; приоритет Taunt-цели | `ExecuteUnitTurn`, `NotifyNoiseHeard`, `GetAlertState` |
| `ATacticalPlayerController` | управление боем: выбор (ЛКМ/1–4/Tab), движение ПКМ (**валидация по длине пути**: 1 AP ≤ 800, 2 AP ≤ 1600), атака кликом (Event.Attack), хоткеи Y/X/Q/F/Backspace/Enter/Esc, режим цели медика, камера; **PlayerTick**: превью пути под курсором (троттлинг 25 см) + **ховер-обводка** юнита под курсором (трейс ECC_Pawn; **в фазу врага свои юниты не подсвечиваются**, враги — всегда); **кольцо выбора** — только в свою фазу (в ход врага прячется, `RefreshSelectionHighlight`); **старт боя**: камера на центр отряда (мгновенно), юнит НЕ выбирается (игрок берёт сам); **ход врага**: камера сопровождает действующего юнита, но ТОЛЬКО если отряд его видит (LOS-гейт — скрытые ходят «за кадром»); **edge scroll** мышью у края экрана (`bEdgeScrollEnabled`/`EdgeScrollMarginPx`; **выключен в ход врага** — иначе курсор у края рвал бы авто-follow); свой бегущий боец тоже сопровождается | `SelectUnit(BySlot/Next)`, `GetSquad`, `Request*`, делегат `OnSelectedUnitChanged` |
| `ATacticalCameraPawn` | XCOM-камера: панорама/поворот 45°/зум/**полёт к цели** с интерполяцией (`FocusInterpSpeed`); **следование за актором** (`SetFollowTarget`, ручная панорама разрывает follow); **вешает `OutlineMaterial`** (M_OutlinePP) блендаблом на unbound-`PostProcessComponent` в BeginPlay (PPV на карте не нужен) | `AddPanInput/AddRotationStep/AddZoomInput`, `FocusOnActor`/`FocusOnLocation` (bInstant), `SetFollowTarget`/`ClearFollowTarget` |
| `AMoveRangeVisualizer` | **зона хода**: волновой Dijkstra по сетке сэмплов `CellSize` (октильная метрика; связность рёбер — surface-raycast навмеша `NavMeshRaycast`, уровень — `ProjectPoint` от волны; полоса у порогов уточняется `FindPathSync`) → гладкий контур marching squares с разрывом на обрывах; зоны 1 AP/2 AP не пересекаются, при последнем AP зона жёлтая («рывок»); **превью пути**: лента к курсору + маркер цели, цвет по стоимости AP | `ShowForUnit`, `Hide`, `UpdatePathPreview`, `HidePathPreview`; материалы зон/пути — в BP |
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
| `UTacticalHUDWidget` | база боевого HUD: сам подписан на TurnManager/контроллер; BP-хуки `OnPhaseChanged(фаза, ход, осталось)`, `OnSelectedUnitChanged`, `OnCombatFinished`; хелперы `GetSquad`, `GetHitChanceOnTarget` |
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
