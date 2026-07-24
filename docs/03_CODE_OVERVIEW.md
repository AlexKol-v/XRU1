# Обзор кодовой базы и беклог C++

Снимок на 2026-07-23. **Основной каркас GDD реализован**, но C++ ещё не
закончен: обязательные разрывы укрытий, firing positions/peek и AI перечислены
в `09_GAMEPLAY_STATUS.md` §B.2–B.3 и разбиты на фазы в
`11_COVER_AND_ENEMY_PLAN.md`. Параллельно продолжается работа в редакторе по
`04_ROADMAP.md`; историю сборки HUD см. `archive/`.

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
| `UCoverDetectionComponent` | укрытия трейсами, GE-теги Cover.*, бонус защиты 20/40. **Ф2 (2026-07-23):** высоты меряются от точки ПОЛА (`Base = ActorLocation − CapsuleHalfHeight`), а не от центра капсулы. **Ф3:** геометрия/числа укрытия переехали в `UCoverTuningDataAsset` (поля компонента удалены), чтение — `GetTuning()` (`TuningOverride` → глобальный → CDO) | `EvaluateSurroundings`, `GetCoverAgainst`, `GetDefenseBonusAgainst`, `EvaluateCoverAtLocation` (Base = пол), static `TraceCoverAtLocation`, `GetTuning`, `OnCoverStateChanged` (стойки в ABP) |
| `UCoverTuningDataAsset` (Ф3, `DataAsset`) | **единый тюнинг укрытий/LOS/выглядывания/высоты** (по образцу `UTacticalHUDStyleData`): дистанция/высоты/канал детекта, защита 20/40, `HunkerDownMultiplier`, `EyeHeightOffset`/`LosSphereRadius`/`LosPeekOffset`, peek `PeekEdgeStep`/`PeekEdgeMaxDistance`/`PeekOutwardOffset`, высота `HeightAdvantageZ`/`Bonus`/`bSymmetricHeightPenalty`. Дефолты = прежним числам. Ассет `DA_CoverTuning` → `BP_TacticsGameInstance.CoverTuning`; фолбэк на CDO | резолвер — `UTacticsCombatStatics::GetCoverTuning(World)` |
| `AUnitBase` | статы (`BaseMaxHealth`/BaseAim/ShotDamage/AttackRange/MoveRange, Squadsight, bCanBeDowned), классы способностей юнита, **смерть/Downed/эвакуация**, PatrolPoints, **подсветка** (кольцо-декаль `SelectionDecal` + обводка Custom Depth: stencil 1 = отряд, 2 = враг); `PostInitializeComponents` переносит классовый `BaseMaxHealth` в GAS `Health/MaxHealth` **до** StartupEffects и создания HUD (Assault/Sniper/Healer/Tank = 100/80/90/150); нативные defaults дают Attack/Overwatch/Hunker даже новому BP; в `BeginPlay` реальный `AUnit_*` нормализует `UnitRole` и восстанавливает пустой/скопированный позывной (`Клин/Оса/Шприц/Молот`); в конструкторе включён **`bUseAccelerationForPaths`**; **навмеш юнит НЕ трогает** | `SetDowned`/`ReviveFromDowned`, `Evacuate`, `SetSelectionHighlight`/`SetHoverHighlight`, BP-хуки `OnDied/OnDownedChanged/OnEvacuated`; `NotifyUnitStateChanged` централизованно обновляет HUD после life-state, Hunker/Taunt/Overwatch и start/finish движения; для HUD: `UnitDisplayName`, `GetAbilityUsesRemaining` |
| `UTacticalAbility` (база GA) | AP-кост в CheckCost/ApplyCost, **bConsumesAllRemainingAP** (XCOM «завершает активацию»), **MaxUsesPerMission**, bRequiresTargetActor, запрет в чужую фазу и **мёртвым/раненым/эвакуированным**; targeted-GA сама объявляет `TargetedActivationEventTag` и предикат `IsValidTargetActor`, поэтому контроллер не знает конкретный Heal/Event; все наследники имеют asset-tag `Ability.TacticalAction` и блокируют параллельный запуск другой tactical GA штатным `BlockAbilitiesWithTag`; базовый `EndAbility` обновляет состояние юнита только **после** снятия GAS-block | `GetUsesRemaining`, `IsValidTargetActor` |
| `UGA_Attack` | выстрел по событию Event.Attack (цель в payload): дальность/LOS/**Squadsight** (−10), статы с юнита, 1 AP + сжигает остаток | `CanTargetActor` (static, для HUD), **`ComputeAttackHitChance`** (static, 2026-07-20: ЕДИНЫЙ расчёт шанса для HUD-прогноза и выстрела — Squadsight-штраф больше не расходится с панелью цели), BP-хук `OnShotFired` |
| `UGA_Overwatch` | реакционный выстрел в чужую фазу (штраф −10 поверх общего расчёта точности), тег State.Overwatch, самозавершение; право на реакцию — **общий предикат `CanTargetActor`** (2026-07-20: радиус перцепции шире дальности оружия, раньше юнит реагировал на недостижимые цели) | BP-хук `OnReactionShot` |
| `UGA_HunkerDown` / `UGA_Taunt` | стойки до своего след. хода (GE State.HunkeredDown ×2 укрытия / State.Taunting: приоритет цели AI + −50% урона) | база `UGA_SelfBuffUntilNextTurn` |
| `UGA_Heal` | лечение/подъём по Event.Heal (цель в payload): +50 HP или ревив 30 HP, радиус 600, 2 заряда | `CanHealTarget`, BP-хук `OnHealApplied` |
| `UGA_RunAndGun` | +1 AP немедленно, 1 раз/миссию | BP-хук `OnRunAndGun` |
| `UTacticsCombatStatics` | общий выстрел/проверки + **занятость** (навмеш статичен, юниты — диски `UnitObstacleRadius` = 60 см на уровне запросов, как occupancy-клетки XCOM) | `ResolveShot` (форс 100/0 для туториала; шумит врагам), `ComputeHitChance` (укрытие + hunker ×2), `HasLineOfSight`, `SquadHasLineOfSight`, `IsUnitAlive/Downed/Evacuated`, `NotifyCombatNoise`, `GetPointAlongPathBudget`; занятость: **`GetUnitClearance`** (просвет = занятая клетка + радиус капсулы бегущего — единая величина на «встать» и «пройти», см. §2.5), `GetUnitObstacles` (бегущие исключаются — диск встанет по финишу; «бегущий» = `IsUnitInTransit`, по статусу path following, НЕ по velocity), `AdjustGoalOutOfUnits` (выталкивание цели приказа на край диска). **Ф3–Ф5 (2026-07-23):** `GetCoverTuning(World)` (тюнинг с GameInstance/CDO); `HasLineOfSightFromLocation(..., Shooter=nullptr)` — быстрый путь (как раньше) + запасной (огневые позиции у краёв); **`GetFiringPositions`** (единый источник позиций выглядывания, зовётся дважды с переставленными аргументами — стрелок и цель, §III.1/§III.2; всегда непустой, порядок центр→step-out→края); **`GetFiringStance`** (Open/OverCover/StepOut + точка выстрела, для Ф10/Ф11); enum `EFiringStance` в `CoverTypes.h`; диагностика — CVar **`xru1.LOS.Debug`** (лог + `DrawDebugSphere`) |
| `AUnitAIController` | perception (sight) + **XCOM-тревога: Patrol → Investigate (шум/потеря) → Combat**; бюджет пути AI = MoveRange; приоритет Taunt-цели в радиусе `TauntPriorityRadius` = 1200 (GDD §7); **выстрел AI — через то же событие Event.Attack, что у игрока** (`TryFireAtTarget`): один предикат прицеливания (`UGA_Attack::CanTargetActor`), одна стоимость AP (включая XCOM-сжигание остатка), одни BP-хуки выстрела; фолбэк прямым `ResolveShot` — только если юниту не назначен `AttackAbilityClass` (лог-предупреждение в `OnPossess`); если способность не заплатила AP, шаг завершает ход, а не зацикливается; **Detour Crowd** (`UCrowdFollowingComponent`, качество High) — бегущие юниты локально огибают стоящих (навмеш о юнитах не знает); по финишу перемещения зовёт `NotifyUnitMoveFinished` контроллера игрока | `ExecuteUnitTurn`, `NotifyNoiseHeard`, `GetAlertState` |
| `ATacticalPlayerController` | управление боем: выбор (ЛКМ/1–4/Tab; **мёртвые/эвакуированные невыбираемы**; **ЛКМ в пустоту**: в XCOM-режиме выбор не снимает, в ручном (`bAutoSelectUnits` выключен) — снимает), движение ПКМ, атака через модальный targeting, хоткеи и камера. **`CanIssueCommand(ETacticalPlayerCommand)` — единый арбитр** для Request*-методов и серости HUD: фаза/юнит/движение/targeting/AP/заряды/цели/GAS-block; другая команда не запускается внутри Attack/Ability. XCOM-автовыбор, hover, edge scroll, синхронная зона и занятость сохранены | `CanIssueCommand`, `SelectUnit(BySlot/Next)`, `SelectNextAvailableUnit`, `GetSquad`, `Request*`, `IsTargetingAttack/Ability`, `GetHoveredUnit`, `GetAvailableInteraction`, `NotifyUnitMoveFinished`; делегаты `OnSelectedUnitChanged`, `OnHoveredUnitChanged`, `OnAvailableActionsChanged` |
| `ATacticalCameraPawn` | XCOM-камера: панорама/поворот 45°/зум/**полёт к цели**; `TacticalYaw/TacticalZoom` — постоянный пользовательский ракурс, `TargetYaw/TargetZoom` — временная цель action-camera. Выбор/фокус меняют только позицию, aim/shot после себя возвращают глобальный ракурс; `IsPlayingShotFrame` отличает конечный выстрел от бессрочного aim-frame. Следование за актором и unbound outline PP сохранены | `AddPanInput/AddRotationStep/AddZoomInput`, `FocusOnActor`/`FocusOnLocation`, `SetFollowTarget`/`ClearFollowTarget`, `FrameShot`, `IsPlayingShotFrame` |
| `AMoveRangeVisualizer` | **зона хода**: волна **any-angle (Theta\*)** по сетке сэмплов `CellSize` с хранением родителей (проходимость отрезка — `NavMeshRaycast` + просвет до дисков; уровень — `ProjectPoint` от волны) → гладкий контур marching squares с разрывом на обрывах; зоны 1 AP/2 AP не пересекаются, при последнем AP зона жёлтая («рывок»); **кайма зоны** (2026-07-17): хорды границы сшиваются в полилинии, сглаживаются Chaikin (`BorderSmoothIterations`) и рисуются лентой `BorderWidth` (материалы `BorderOne/TwoMaterial`, фолбэк — материалы пути/зон; внутренний порог кольца не дублируется); **диски занятости** других юнитов волна огибает (граница диска — интерполяцией, стены — raycast'ом); **превью пути**: лента по цепочке родителей поля + маркер цели, цвет по стоимости AP | **`PlanMoveTo`** (единый план приказа: достижимость + длина + AP + приведённая цель + маршрут — см. §2.5), `GetMoveCostTo` (короткая форма для BP), `ShowForUnit` (**bool**: false = юнит вне навмеша), `Hide`, `UpdatePathPreview`, `HidePathPreview`, `IsFieldBuiltFor`; материалы зон/пути/каймы — в BP |
| `ABombObjective` | цель «бомба»: 2 действия «Обезвредить» по 1 AP вплотную | `TryDefuse`, `CanDefuse`, делегаты `OnDefuseProgress`, `OnDisarmed` |
| `AEvacZone` | зона эвакуации: активация после цели, «Эвакуация» 1 AP | `ActivateZone`, `TryEvacuate`, `OnUnitEvacuated`, `bActiveFromStart` |
| `ATacticsGameMode` | сбор сторон по каноническим `TacticsTeamIds`, сложность (HP/aim/таймер по `DifficultyParams`), бомба→эвакуация→победа, результат→сейв | `ActivateEvacuation` (зовёт и скрипт туториала), `WasDefeatByTimeout`; настройки: `MissionId`, `bWinWhenAllEnemiesDead`, `MissionResultWidgetClass`, `TacticalHUDClass` |
| `AMissionPointOfInterest` | POI хаба + **гейт** `RequiredCompletedMission` | `IsLocked`, `SelectPointOfInterest`, BP-хук `OnSelectionDenied` |
| `UTacticsGameInstance` / `UTacticsSaveGame` | кампания: сложность, `CompletedMissions`, travel; глобальные ссылки `UITheme` и **`CoverTuning`** (Ф3, `DA_CoverTuning`) | `StartNewCampaign`, `Save/LoadCampaign`, `TravelToHub/ToMainMenu`, `GetUITheme` |
| `TacticsGameplayTags` | нативные теги | Cover.Half/Full, State.Overwatch/HunkeredDown/Taunting/Downed, Data.Damage/Heal, Event.Attack/Heal |

`AUnitBase::HandleHealthChanged` отправляет `NotifyUnitStateChanged` и при
обычном уроне/лечении с HP выше нуля, поэтому `WBP_UnitPortrait.HPBar`
обновляется сразу. Переходы в Downed/Death/Revive не дублируют событие: итоговый
refresh для них отправляет соответствующий life-state метод.
| `TacticsGameplayEffects` | C++ GE | UGE_CoverHalf/Full, UGE_ShotDamage, UGE_Heal, UGE_HunkerDown, UGE_TauntShield |

### 2.2 UI (`UI/`, `UI/Menus/`)

| Класс | Ответственность |
|---|---|
| `UPrimaryGameLayout` + `UGameUIManagerSubsystem` | корневые стеки слоёв; пересоздание после смены уровня |
| `UMenuScreenBase` и экраны меню | навигация стека: MainMenu/Difficulty/Settings/About/Pause; `GetUITheme` даёт всем WBP общую тему из GameInstance. `UIntroPlayerWidget::FinishIntro` завершает/пропускает ролик; `UDifficultySelectWidget` после создания кампании пушит `IntroScreenClass` и только затем идёт в хаб (fallback без класса — сразу хаб) |
| `UMissionResultWidget` | экран результата: `SetupResult(bVictory, bByTimeout)`, кнопки `RetryMission/GoToHub/GoToMainMenu`, BP-хук `OnResultReady` |
| `UTacticalHUDWidget` | база боевого HUD: сам подписан на TurnManager/контроллер/юнитов обеих сторон; BP-хуки `OnPhaseChanged(фаза, ход, осталось)`, `OnSelectedUnitChanged`, `OnHoveredUnitChanged`, `OnUnitsStateChanged` (портреты/счётчик), `OnCombatFinished`; хелперы `GetSquad`, `GetHitChanceOnTarget`, `GetTargetCoverAgainstSelected`, `GetAliveEnemyCount`, `GetUITheme`. Значительная часть логики — в C++ через `BindWidgetOptional`: `RefreshActionButtons`, `UpdateTargetPanel`, `UpdateSquadCardVisibility`, `ApplyStyle`. `ApplyStyle` задаёт state tint + foreground кнопок и применяет icon/size + `EnemyCounterBackground` из общей темы; `ApplyPortraitCardLayout` применяет status padding и создаёт отсутствующий `CoverSizeBox → CoverIcon` |
| `UTacticalHUDStyleData` | **единая UI-тема проекта** (`DA_TacticalHUDStyle`): action/status/class icons, 5 портретов, экранный арт, intro, CommonUI styles, палитры и независимые layout. `GetStatusForUnit` централизует приоритет Downed → Evacuated → Taunt → HunkerDown → Overwatch → Moving; `GetCoverIcon*` централизуют Half/Full. Для прозрачных overhead-status и enemy-counter PNG тема хранит независимые background texture/color/internal padding; validation запрещает отрицательный padding и предупреждает о слишком прозрачной подложке. Глобально назначается в `UTacticsGameInstance::UITheme`; локальный WBP Style — только fallback/preview |
| `UAPPipsWidget` / `UCoverIconWidget` / `UUnitStatusIconWidget` | HUD над головой: AP, локальное укрытие и независимый главный tactical status. Оба glyph берут texture/внутренний размер из общей UITheme. `UUnitStatusIconWidget` программно строит `UBorder StatusBadgeBackground → UImage StatusIcon`: Border даёт стабильный контраст поверх произвольного 3D-фона и использует solid fallback, если background texture в теме не задана. `UUnitHUDLayoutData` строит слоты и, если `bShowTacticalStatusIcon=true`, автоматически добавляет нативный status-widget справа от последнего слота последней строки; у статуса отдельно настраиваются class, V/H, slot size, внешний padding и color |
| `AMenuPlayerController` / `ACSTPlayerController` | контроллеры уровней меню/хаба |

### 2.3 Договорённости для нового кода
- Боевые GA — только от `UTacticalAbility`; урон/лечение — только `ResolveShot`
  / GE с SetByCaller; теги — нативные в `TacticsGameplayTags`.
- Команды: `TacticsTeamIds::Player = 1`, `TacticsTeamIds::Enemy = 2`;
  `DefaultTeamId` в BP обязан соответствовать этим константам.
- GE-компоненты в конструкторах — `CreateDefaultSubobject`+`GEComponents.Add`.

### 2.4 Архитектурный аудит 2026-07-20 (Tactics/ + UI/)

Целевой аудит написанного с нуля кода на антипаттерны/костыли (не стиль):
качество высокое, GAS/подписки/делегаты — без утечек. Найденные мелкие
несоответствия (мёртвый аляйзящий геттер в `UTurnManagerSubsystem`,
неиспользуемые свойства-обломки в `UCoverDetectionComponent`, 3D- vs
2D-дистанция в `ABombObjective::CanDefuse`, недостающие `UE_LOG` Warning на
тихие no-op в `ATacticsGameMode`/`UTacticsGameInstance`) — исправлены.

### 2.5 Достижимость перемещения — единственный источник правды (2026-07-20, переработано)

**Правило: зона, лента превью и приказ пользуются ОДНИМ планом —
`AMoveRangeVisualizer::PlanMoveTo(Goal, FMoveOrderPlan&)`.**

Не «три места зовут одну функцию» (так было раньше, и они всё равно
разъезжались), а один результат на всех: план возвращает достижимость, длину,
стоимость в AP, **приведённую цель** (спроецирована на навмеш + вытолкнута из
дисков занятости) и **точки маршрута**. Превью рисует его линию, контроллер
списывает его AP и отдаёт приказ в его `Goal`.

Навмеш статичен и о юнитах не знает (капсулы `SetCanEverAffectNavigation
(false)`), занятость решается на уровне запросов дисками
(`UnitObstacleRadius` = 60 см, аналог занятого тайла XCOM). Прямой
navmesh-запрос в чистом поле прокладывает прямую СКВОЗЬ стоящего бойца и не
отличает «одиночный юнит обходится» от «коридор перекрыт тремя». Отличает
только волна `BuildDistanceField`, огибающая диски.

Волна — **any-angle (Theta\*)**: релаксируя соседа, сначала пробует прямую от
РОДИТЕЛЯ текущей ячейки (`HasClearLine` = `ARecastNavMesh::NavMeshRaycast` +
просвет до дисков). Отсюда два свойства, ради которых всё и сделано:

1. длина в поле = длина настоящей ломаной, а не октильная оценка (+8%). Прежний
   костыль «уточнять спорную полосу у порогов честным `FindPathSync`» удалён —
   он смешивал в одном поле ДВЕ метрики, и граница дрожала;
2. цепочка родителей — это уже маршрут. Лента рисуется ПО НЕЙ, а не отдельным
   navmesh-запросом «для картинки», который шёл своей дорогой.

**Ключевая гарантия:** каждый отрезок ломаной проверен рэйкастом, значит она
целиком проходима, значит её длина — ВЕРХНЯЯ оценка кратчайшего navmesh-пути.
Боец не может пробежать больше, чем обещала зона. Ошибка всегда в
консервативную сторону.

#### Просвет: препятствие раздувается на радиус агента

`UTacticsCombatStatics::GetUnitClearance(Mover)` = `UnitObstacleRadius` (60,
занятая клетка стоящего) + **радиус капсулы самого Mover** (с актора, не
константой). Это стандартное «раздувание препятствия на радиус агента»: растим
препятствие на того, кто едет, и дальше считаем агента точкой.

Радиус **один и тот же** для «встать» и для «пробежать мимо» — сознательно.
Прежде правила были разные (встать 60, пройти 60+радиус), и это давало
неразрешимое противоречие: клетка, куда встать можно, а выйти из неё нельзя.
Вдобавок волна судила о проходе по МЕНЬШЕМУ радиусу и рисовала маршрут в щель
между двумя бойцами, куда третий физически не лезет.

Единственное послабление — **«выйти из тесноты»**: отрезок разрешён, если
ближайшая к диску его точка — это НАЧАЛО, т.е. просвет был нарушен уже на
старте, а отрезок только удаляется. Иначе боец, оказавшийся вплотную к
союзнику, не смог бы сдвинуться вообще. Войти в теснину или пройти сквозь неё
это не разрешает: там минимум приходится на середину отрезка.

Радиус применяется в одном месте — `HasClearLine` (и волна, и достройка
последнего отрезка, и спрямление), плюс `AdjustGoalOutOfUnits` выталкивает цель
ровно на него же (иначе поле отклоняло бы собственную приведённую цель).

#### Разрешение сетки: чем оно управляет, а чем нет (замерено 2026-07-20)

Проверено в редакторе: капсула юнита **34**, значит просвет `C` = 60 + 34 = **94**,
а физически бойцу нужно 68 (две капсулы).

Щель между двумя бойцами шириной `G` оставляет свободное «горлышко» `G − 2C`;
чтобы волна его нашла, туда должен попасть сэмпл, то есть **`G_min ≈ 2C + CellSize`**:

| `CellSize` | Проходимая щель | Точность границы | Сэмплов (при `MoveRange`=800) |
|---|---|---|---|
| 50 (было) | ≈ 238 см | ±25 см | 4 761 |
| **35 (сейчас)** | ≈ 223 см | ±17 см | 9 216 (×1.9) |
| 25 | ≈ 213 см | ±12 см | 17 689 (×3.7) |

**Вывод, который стоит помнить:** на узкие проходы разрешение влияет слабо —
доминирует `2C` = 188, а не шаг сетки. Хотите пропускать щели уже ~220 см —
это про `UnitObstacleRadius` (сейчас 60), а не про `CellSize`. Понижать его
можно до ~40 (тогда `C` = 74, щель от ~183), но это ослабит правило «занятой
клетки» и вернёт риск исходной жалобы «линия строится там, где не пролезть».
Не трогаем без явного запроса: текущее поведение подтверждено ручным прогоном.

Разрешение поднято с 50 до 35 ради **точности границы зоны** (±25 → ±17 см),
а не ради щелей. Цена квадратична — замерять
`xru1.MoveRange.LogBuildTime 1` (пишет размер сетки, время волны, время
контура и сумму; контур масштабируется так же, поэтому меряется целиком).

#### Исполнение приказа: боец идёт ПО ломаной, а не «в точку»

`AUnitAIController::MoveAlongRoute(RoutePoints)` ведёт бойца от вершины к
вершине; следующий отрезок запрашивается из `OnMoveCompleted` (движок это
разрешает явно — статус сбрасывается в `Idle` ДО оповещения наблюдателей,
«they can request another move»).

Почему не `MoveToLocation` в конечную цель: навмеш о юнитах не знает и
прокладывает к цели ПРЯМУЮ — сквозь стоящих бойцов. Detour Crowd объезжает их
локально, но в тесноте просто упирается: боец стоит, а AP уже списано. И
игрок при этом видел совсем другую линию. Внутри отрезка ломаной навмеш даёт
ту же прямую (отрезок проверен полем и свободен от занятых клеток), поэтому
бежит ровно как нарисовано.

Детали:
- план перед выдачей **спрямляется** (string pulling — выкидываем вершину,
  если её соседи видят друг друга), иначе боец тормозил бы на каждом
  остаточном изломе сетки. Отбрасывать вершины «за близостью», без проверки
  рэйкастом, нельзя: срезанный угол уводит в союзника;
- `RouteCornerAcceptance` (25 см) — **не косметика**. Радиус приёмки это ровно
  то, на сколько path following срежет угол, а поворотные вершины стоят у
  занятых клеток: просвет 94, физически нужно ~68, запас ~26 см. Радиус больше
  запаса — и срезанный угол заводит бойца в союзника;
- `IsMoving()` включает паузы между отрезками, иначе на каждом повороте боец
  «финишировал» бы (перестройка зоны, автопереход выбора). Выбывший боец
  «не движется» всегда — иначе смерть посреди маршрута оставила бы флаг
  висеть навсегда (диск не ставится, зона не перестраивается).

Прочее:
- пороги 1AP/2AP — в одной `UTacticsCombatStatics::GetMoveCostForDistance`;
- дистанция до произвольной точки = продолжение волны: минимум по ближайшим
  сэмплам от «дойти до сэмпла + прямая до цели», прямая проверяется тем же
  `HasClearLine`. Недостижимость = отсутствие кандидатов;
- поле хранит СНИМОК дисков (`FieldObstacles`) — запрос судит по тем же дискам,
  что и волна, иначе сдвинувшийся юнит развёл бы подсветку с ответом;
- отрисовка и запрос берут дистанцию сэмпла через общий `SampleDistance`
  (недостижимый = `FieldUnreachableDistance`);
- отдельной «чистой навигационной метрики» больше нет: длину меряет поле, и
  только оно (`GetNavPathLength` удалена — см. врезку ниже);
- AI поле не строит (дорого на каждого врага) — `GetPointAlongPathBudget`
  **усекает** путь по `GetUnitClearance`: враг подходит насколько может, а
  не теряет ход, упёршись в союзника. **Асимметрия осознанная**: враг идёт
  funnel-путём и по ломаной поля НЕ ведётся. Если станет заметно (враги
  толкаются в проходах) — строить поле и для них, см. беклог §3.

#### История: две волны багфиксов (2026-07-20, свёрнуто)

Прежняя версия поля достижимости прошла две волны отладки — сначала базовые
баги построения (недостижимость на границе сетки считалась за 1 AP, контур
и запрос расходились кламп'ом, лента превью строилась отдельным funnel-путём
вместо самого поля, приказ шёл `MoveToLocation` напрямую вместо ломаной, из-за
чего боец утыкался в союзников), затем более тонкие расхождения, найденные
повторными аудитами (позиция сэмпла vs узел сетки, интерполяция границы через
препятствие, порядок LOS-проверок, срезание угла при недостаточном
`RouteCornerAcceptance`, дедуп вершин без проверки проходимости, залипание
`bFollowingRoute` при смерти в движении). Все перечисленные дефекты
исправлены; текущее поведение описано выше и гарантируется правилами §2.5
целиком (план — единственный источник правды, каждый отрезок проверен
рэйкастом). Если новый симптом похож на один из старых (зона показывает не
то, что кликается; боец бежит не по нарисованной линии) — велик шанс, что
сломано именно то место, которое чинили тогда: `PlanMoveTo`, `HasClearLine`
или снимок `FieldObstacles`.

**Почему не канонический путь UE** (капсулы как `DynamicObstacle` +
`NavArea_Obstacle` + Runtime Generation Dynamic — то, что советуют форумы Epic):
он требует асинхронных перестроек тайлов навмеша, а это ровно та
недетерминированность, от которой проект ушёл (зона и валидация приказа должны
считаться синхронно и точно, в тот же кадр). Вдобавок `NavArea_Obstacle` —
«проходить можно, если другого пути нет», то есть при перекрытом коридоре AI
всё равно пошёл бы сквозь бойцов. Поэтому занятость решается на уровне
ЗАПРОСОВ (диски + раздувание на радиус агента), а навмеш остаётся статичным.

> ⚠️ Осознанный компромисс: `AMoveRangeVisualizer` совмещает модель
> (поле достижимости) и вид (отрисовка контуров). Так сделано намеренно —
> единственный источник правды важнее чистоты слоёв, а поле всё равно нужно
> строить для отрисовки. Идеальный вариант — вынести поле в отдельный
> `FMoveDistanceField` (модель), которым владеет контроллер, а визуализатор
> получает `const&` только для рендера. Внесено в беклог P2 (§3), не сделано
> из-за риска регрессии в рабочем marching squares перед сдачей.

### 2.6 Флоу атаки v2 и модификаторы точности (2026-07-21)

**Единый источник правды о режиме взаимодействия (2026-07-21).**
`EPlayerTargetingMode { None, Attack, Ability }` в контроллере заменил два
независимых булевых флага (`bAwaitingAttackTarget/AbilityTarget`) с ручными
условиями входа/выхода в каждой точке — из-за них «камера зависала» (один из
путей выхода забывал её вернуть). Теперь ЛЮБОЙ переход идёт через
`SetTargetingMode(New)` → `ExitTargetingMode(old)` + `EnterTargetingMode(new)`:
- `Exit(Attack)` централизованно снимает подсветку цели, баннер и **возвращает
  камеру — но только если она держит кадр ПРИЦЕЛА** (`IsHoldingAimFrame`);
  если играет кадр ВЫСТРЕЛА (выход из-за подтверждения), не трогает — тот сам
  вернёт ракурс по таймеру. Один выход закрывает и отмену, и выстрел;
- откат побочных эффектов физически нельзя пропустить — его делает Exit, а не
  вызывающий. Новое состояние = новый `case` в двух switch'ах, а не новый флаг
  с россыпью условий по классу;
- фаза боя (Player/Enemy) и «юнит в движении» СЮДА НЕ входят — их источник
  `TurnManager` и `AIController` (истину не дублируем).

**Арбитр команд + GAS-взаимоисключение (2026-07-23).**
`CanIssueCommand(ETacticalPlayerCommand)` обслуживает и `Request*`, и
`UTacticalHUDWidget::RefreshActionButtons`. В Attack/Ability-targeting другая
команда получает `false`, кнопка сразу disabled, хоткей ничего не активирует.
Уже запущенные `UTacticalAbility` независимо защищены общим asset-tag
`Ability.TacticalAction` + `BlockAbilitiesWithTag`. Это два разных слоя:
контроллер владеет предактивационным UI-mode, GAS — временем жизни способности.
GAS-block входит в общий пролог арбитра и запрещает также Move/Interact/Skip.
Базовый `UTacticalAbility::EndAbility` сначала снимает block через `Super`, затем
уведомляет контроллер/HUD — одинаково для мгновенных и длительных GA; обработчик
состояния также заново строит зону хода.

Targeted-способность хранит событие активации в своём CDO
(`TargetedActivationEventTag`) и предоставляет `IsValidTargetActor`.
Контроллер сначала фиксирует действующего юнита, проверяет CDO и отправляет
Gameplay Event этому юниту, и только затем закрывает targeting. То же правило
порядка действует для атаки. Синхронные делегаты UI/AP поэтому не могут сменить
`SelectedUnit` между проверкой и исполнением; финальная GA повторяет проверку до
`CommitAbility`, а GAS-block не допускает параллельную tactical GA.

**Фокус клавиатуры принадлежит игре, не кнопкам HUD (2026-07-21).** UMG-кнопка
после клика оставляет фокус себе, и пробел/Enter «нажимают» её повторно
средствами Slate — мимо Enhanced Input. Это давало баг «пробел выполняет
последнее нажатое действие» (завершить ход, глухую оборону…).
`UTacticalHUDWidget::EnsureButtonsDontStealFocus` рекурсивно (включая
вложенные UserWidget'ы — портреты) вешает на каждый UButton возврат фокуса
вьюпорту после клика; повторный вызов безопасен (AddUniqueDynamic), зовётся
на конструкции и после пересборки портретов.

**Камера: постоянный и временный ракурс разделены.** `TacticalYaw/TacticalZoom`
меняются только ручным вводом; action-camera пишет в `TargetYaw/TargetZoom`.
`ClearShotFraming` возвращает пользовательский ракурс и позицию до кадра,
`AbandonShotFraming` возвращает пользовательский ракурс без старой позиции,
потому новый focus/follow уже задал другую. Поэтому выбор бойца, смена фазы и
пассивный edge scroll не могут «продвинуть» временный yaw прицела в глобальный.

**Прицеливание — двухтактное, как в XCOM 2.** «Огонь» → режим прицеливания:
контроллер держит `CurrentAttackTarget` (ближайшая цель или под курсором),
подсвечивает её ховер-обводкой (не гаснет при уходе курсора — гейт в
`UpdateHoverHighlight`), камера — кадр «из-за плеча» (`FrameShot`). Tab/Q/E
листают `GetAttackTargets()` (отсортированы по дальности), ЛКМ по врагу
переводит прицел (НЕ стреляет), подтверждение — «Огонь» повторно / ЛКМ по уже
выбранной цели
(`ConfirmAttack`), отмена — ПКМ/Esc/клик мимо (`CancelTargeting`).

**Поворот к цели**: `UTacticsCombatStatics::FaceActorTowards` (yaw к точке) —
ОДИН хелпер для взятия цели на прицел (`SetAttackTarget`, читаемость наводки)
и для выстрела (`ResolveShot`, не стрелять «в спину»). Повороты прицела и
выстрела не расходятся по построению.

**Камера**: `FrameShot` (держится) / `FrameShotForDuration` (кадр выстрела,
сам истекает) запоминают позицию/наклон один раз, а yaw/zoom берут из отдельного
пользовательского ракурса. Новый focus/follow/manual-pan бросает старую позицию,
но возвращает tactical yaw/zoom. Автопереход бойца при AP=0 **отложен только
до конца таймерного кадра выстрела** (`IsPlayingShotFrame`), не aim-frame
(`bPendingAutoAdvance` в PlayerTick) — иначе смена выбора рвала кадр в тот же
тик. Кадр показывается и на выстрелах ВРАГА (вызов из `ResolveShot` →
`NotifyShotFired`), там же стрелок разворачивается лицом к цели.

**LOS (XCOM):** `HasLineOfSight[FromLocation]` — сферические лучи 15 см
(щели на стыках мешей не пропускают выстрел), step-out ±70 см (стрельба
из-за угла укрытия), 2 точки цели (глаза/корпус), только WorldStatic/Dynamic
(юниты не блокируют). Одна функция на игрока, AI, Overwatch и план AI.

**Точность (GDD §5.4):** всё в `UGA_Attack::ComputeEffectiveAim` — BaseAim +
дистанция (`AimByDistanceCurve` юнита или встроенная винтовка +10/0/−15) +
высота (≥1.5 м → +20) − Squadsight; укрытие/hunker — в `ComputeHitChance`.
Через `ComputeEffectiveAim` идут выстрел игрока, AI, Overwatch (−10 поверх)
и HUD-прогноз — расходиться негде.

**AI боевых решений — утилити-скоринг с весами (2026-07-21, XCOM-подход).**
Firaxis в XCOM гоняет дерево ОДИН раз на активацию юнита и выбирает позицию
скорингом (укрытие/фланг/дистанция) — не реалтайм-стейтами. У нас так же:
FSM тревоги (Patrol/Investigate/Combat) остаётся источником правды «что юнит
знает» (перцепция живёт между ходами), а ВНУТРИ Combat — приоритетный список
с взвешенной оценкой позиций. Веса — UPROPERTY `Tactics|AI|Weights`
(CoverDefenseWeight, LineOfFireBonus/LoseLineOfFirePenalty, TravelCostPerCm,
OverextendPenaltyPerCm, RelocateBias, RetreatHealthFraction,
RetreatRewardPerCm) — новое поведение = новый вес/слагаемое, не новый флаг.

Порядок решений `StepCombat`: продолжение начатого манёвра → **отступление**
(HP < 35% и открыт: укрытие ПОДАЛЬШЕ от угрозы, бюджет все AP, потеря LOS не
штрафуется) → **манёвр в укрытие с линией огня** (1 AP из 2, выстрел вторым)
→ **рывок к дальнему укрытию** (оба AP, когда рядом стрелять неоткуда) →
**выстрел** → сближение. Манёвр длиннее 1 AP продолжается второй ногой на
следующем шаге (`PendingManeuverPoint`); выбор манёвра — один на ход. Срыв
старта манёвра проваливается вниз к выстрелу, а не завершает ход.

`FindCoverPoint(PathBudget, bRetreat)`: кольцевой сэмплинг (4 радиуса × 12
углов) в переданном бюджете; каждая точка — ТЕМИ ЖЕ правилами боя: укрытие
`EvaluateCoverAtLocation` (общее ядро с трейсом стоящего юнита: план = факт),
линия огня `HasLineOfSightFromLocation`, занятость/достижимость — общие
хелперы. Манёвр только при выигрыше > RelocateBias против ТЕКУЩЕЙ позиции.
Диагностика решений: `xru1.AI.LogCombat 1` (печатает выбранную ветку).

> Почему НЕ StateTree, как у донора (`ANPCStateTreeAIController` + ST-таски):
> у донора реалтайм-NPC (погоня/патруль каждый тик) — там ST уместен. Наши
> решения одноразовые на активацию в пошаговом ходе; ST дал бы ассет-оверхед
> без выгоды. Если AI дорастёт до стай/перезарядок — утилити-опции переносятся
> в ST-таски 1:1 (оценка уже отделена от исполнения).

Осознанно НЕ тронуто (не баг, просто на будущее):
- ~~`GetNavPathLength` и `IsLocationBlockedByUnit`~~ — **удалены 2026-07-20**.
  После перевода на единый план C++-вызовов не осталось; BP-вызовы проверены
  через MCP по всем 24 блюпринтам проекта — нет. Метод проверки предварительно
  валидирован на заведомо используемой функции. Грань, о которую легко
  споткнуться: имена нод в UE разбиты пробелами (`Get Nav Path Length`), поиск
  по слитному имени даёт ложный ноль; грепом по `.uasset` не проверяется вовсе.
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
