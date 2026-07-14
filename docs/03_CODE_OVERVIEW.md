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
| `UTurnManagerSubsystem` | фазы, ход AI по юнитам, конец боя, **таймер ходов бомбы** | `StartCombat`, `EndTurn`, `CheckCombatOutcome`, `SetTurnLimit`, `GetTurnsRemaining`, `bAutoWinWhenEnemiesDead`, `IsUnitOnActiveSide`, `GetSideUnits`/`GetOpposingUnits`; делегаты `OnTurnStarted`, `OnCombatEnded`, `OnTurnLimitExpired` |
| `UActionPointsComponent` | AP юнита (2/ход) | `TrySpendActionPoint`, `SpendAllRemaining` (XCOM-правило/скип), `GrantExtraPoints` (Run&Gun), `OnActionPointsChanged` |
| `UCoverDetectionComponent` | укрытия трейсами, GE-теги Cover.*, бонус защиты 20/40 | `EvaluateSurroundings`, `GetCoverAgainst`, `GetDefenseBonusAgainst`, `OnCoverStateChanged` (стойки в ABP) |
| `AUnitBase` | статы (BaseAim/ShotDamage/AttackRange/MoveRange, Squadsight, bCanBeDowned), классы способностей юнита, **смерть/Downed/эвакуация**, PatrolPoints | `SetDowned`/`ReviveFromDowned` (медик + скрипты туториала), `Evacuate`, BP-хуки `OnDied/OnDownedChanged/OnEvacuated`, делегат `OnUnitStateChanged` |
| `UTacticalAbility` (база GA) | AP-кост в CheckCost/ApplyCost, **bConsumesAllRemainingAP** (XCOM «завершает активацию»), **MaxUsesPerMission**, bRequiresTargetActor, запрет в чужую фазу | `GetUsesRemaining` |
| `UGA_Attack` | выстрел по событию Event.Attack (цель в payload): дальность/LOS/**Squadsight** (−10), статы с юнита, 1 AP + сжигает остаток | `CanTargetActor` (static, для HUD), BP-хук `OnShotFired` |
| `UGA_Overwatch` | реакционный выстрел в чужую фазу (штраф −10), тег State.Overwatch, самозавершение | BP-хук `OnReactionShot` |
| `UGA_HunkerDown` / `UGA_Taunt` | стойки до своего след. хода (GE State.HunkeredDown ×2 укрытия / State.Taunting: приоритет цели AI + −50% урона) | база `UGA_SelfBuffUntilNextTurn` |
| `UGA_Heal` | лечение/подъём по Event.Heal (цель в payload): +50 HP или ревив 30 HP, радиус 600, 2 заряда | `CanHealTarget`, BP-хук `OnHealApplied` |
| `UGA_RunAndGun` | +1 AP немедленно, 1 раз/миссию | BP-хук `OnRunAndGun` |
| `UTacticsCombatStatics` | общий выстрел/проверки | `ResolveShot` (форс 100/0 для туториала; шумит врагам), `ComputeHitChance` (укрытие + hunker ×2), `HasLineOfSight`, `SquadHasLineOfSight`, `IsUnitAlive/Downed/Evacuated`, `NotifyCombatNoise`, `GetNavPathLength`, `GetPointAlongPathBudget` |
| `AUnitAIController` | perception (sight) + **XCOM-тревога: Patrol → Investigate (шум/потеря) → Combat**; бюджет пути AI = MoveRange; приоритет Taunt-цели | `ExecuteUnitTurn`, `NotifyNoiseHeard`, `GetAlertState` |
| `ATacticalPlayerController` | управление боем: выбор (ЛКМ/1–4/Tab), движение ПКМ (**валидация по длине пути**: 1 AP ≤ 800, 2 AP ≤ 1600), атака кликом (Event.Attack), хоткеи Y/X/Q/F/Backspace/Enter/Esc, режим цели медика, камера | `SelectUnit(BySlot/Next)`, `GetSquad`, `Request*`, делегат `OnSelectedUnitChanged` |
| `ATacticalCameraPawn` | XCOM-камера: панорама/поворот 45°/зум с интерполяцией | `AddPanInput/AddRotationStep/AddZoomInput`, `FocusOnActor` |
| `AMoveRangeVisualizer` | **зона хода**: заливка полигонов навмеша по path-distance (секция 1 AP + кольцо 2 AP), ProceduralMesh | `ShowForUnit`, `Hide`; материалы зон — в BP |
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
2. Обводка-контур зоны хода (сейчас — заливка полигонов; выглядит достаточно).
3. Всплывающие числа урона / декаль-маркеры подсказок туториала — BP-only.
4. Процедурная раскладка укрытий PCG для L_Mission01 (стретч GDD §12).

Всё остальное из GDD в коде есть. Если при сборке уровней вскроется дыра —
сначала записать сюда, потом чинить.
