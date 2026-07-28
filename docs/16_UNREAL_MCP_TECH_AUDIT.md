# 16 — Глобальный аудит Unreal, Blueprint-графов и боевой анимации

> **Снимок:** 2026-07-28, commit `14ad3ae`, Unreal Engine
> `5.7.4-51494982+++UE5+Release-5.7`, UnrealClaude подключён.
> **Статус E1:** шаги E1.6–E1.8 физически собраны в ассетах, но **не приняты**:
> глобальный аудит подтвердил блокирующие гонки действия, движения и анимации.
> До исправления §6 чеклист E1.10 остаётся открытым.
>
> **Implementation snapshot 2026-07-28:** C++-пакеты action context,
> mechanics-only `FireCommit`, stable active cover, target-aware StepOut,
> action/reaction barriers, death ownership и общий enemy route собраны дважды
> (`XRU1Editor Win64 Development`, success). `ABP_Solider` исправлен и сохранён.
> Сохранённые `BP_GA_Attack`/`BP_GA_Overwatch`, два montage notify и callbacks
> пяти `BP_Unit_*` ещё не мигрированы. Поэтому текущий runtime timeout-отменяет
> выстрел и возвращает AP. Ручной источник истины —
> [17_MANUAL_EDITOR_CHECKLIST.md](17_MANUAL_EDITOR_CHECKLIST.md).

Этот файл — рабочий источник истины для исправления проблем выстрела из укрытия,
поворотов, смерти и движения AI. Он фиксирует не только симптомы, но и проверенный
путь данных от C++ до Blueprint/AnimBlueprint.

---

## 1. Что и как проверено

Проверка выполнена четырьмя независимыми слоями:

1. История последних восьми коммитов и документы `01`, `03`, `04`, `08`, `09`,
   `14`, `15`.
2. Семантический граф C++: вызовы способностей, выстрела, движения, укрытий,
   `VisualState` и поворота.
3. Живой Unreal Editor через UnrealClaude: реестр всех `/Game`-ассетов,
   зависимости, CDO пяти юнитов, все Blueprint-графы, AnimBlueprint state
   machines и Output Log.
4. Официальные материалы Epic UE 5.7, руководство/SDK XCOM 2 и отдельно
   помеченные вторичные наблюдения сообщества.

### Границы проверки

- Реестр содержит **689 ассетов**. Найдены и прочитаны **все 36 Blueprint-ассетов**,
  их **94 графа / 2438 узлов**. Ошибок чтения и неизвестных узлов MCP не вернул.
- `ABP_Solider` проверен командой `validate_blueprint`: `UpToDate`, без compile
  errors/warnings; это доказывает структурную корректность, но не визуальное
  качество перехода в PIE.
- Активный мир редактора во время аудита — `Untitled_0`, не `Lvl_TopDown`.
  Уровень намеренно не переключался, чтобы не затронуть несохранённую сессию.
- Dedicated MCP API не отдаёт содержимое Montage Notify Track, Slot Track,
  sections и часть preview-настроек. Поэтому наличие будущего `FireCommit` и
  точный кадр нужно проверять в Montage Editor/Animation Insights. Выполнение
  скрытого произвольного Python/console API ради этого не использовалось: оно
  требует отдельного интерактивного разрешения и имеет существенно больший риск.

---

## 2. Полная карта возможностей UnrealClaude MCP

### 2.1 Что доступно агенту напрямую — 16 инструментов

| Инструмент | Возможности |
|---|---|
| `unreal_status` | связь, проект и версия UE |
| `unreal_get_project_context` | C++-классы, структура Source, текущий уровень, сводка ассетов; счётчики могут быть неполными, поэтому для аудита используется Asset Registry |
| `unreal_get_ue_context` | локальная справка UE 5.7 по animation, Blueprint, Slate, actors, assets, replication, Enhanced Input, character, material и core API |
| `unreal_asset_search` | поиск ассетов по пути, классу и имени с пагинацией |
| `unreal_asset_dependencies` | hard/soft-зависимости ассета |
| `unreal_asset_referencers` | кто ссылается на ассет |
| `unreal_blueprint_query` | список/inspect, графы и узлы, переменные, функции, pins, поиск узлов и ссылок |
| `unreal_ue` | единый роутер операций §2.2 |
| `unreal_get_level_actors` | чтение акторов активного уровня и transforms |
| `unreal_spawn_actor` | создание актора/Blueprint instance |
| `unreal_move_actor` | абсолютный/относительный transform |
| `unreal_set_property` | свойства актора и его компонентов по property path |
| `unreal_delete_actors` | удаление выбранных акторов; необратимая операция, сначала обязательна инвентаризация |
| `unreal_open_level` | список шаблонов, открыть/создать/save-as уровень |
| `unreal_get_output_log` | чтение и фильтрация Output Log |
| `unreal_capture_viewport` | снимок editor/PIE viewport |

### 2.2 Операции роутера `unreal_ue`

| Домен | Поддерживаемые операции |
|---|---|
| `blueprint` | create; add/remove variable; add/remove function; add/add-many/delete node; connect/disconnect pins; set pin value; list/inspect/get graph/nodes/variables/functions/node pins; search nodes; find references. Modify-команды автоматически компилируют BP |
| `anim` | info/validate/compile; список state machines/states/transitions/conduits; create state machine; add/remove state и transition; entry state; duration/priority; condition nodes и connections; connect state machine to output; назначить animation state; найти анимации; inspect pins; defaults; comparison chains; batch |
| `character` | список/информация/компоненты/config персонажей; get/set movement params; assign AnimBP; CRUD Character Data; CRUD таблицы статов; apply character data |
| `enhanced_input` | create/query/list actions и contexts; add/remove mapping; triggers; modifiers; action info |
| `material` | create material instance; параметры; назначение материала skeletal mesh/actor; material info |
| `asset` | info/list; set property/save; duplicate/rename/move/delete/reimport |

### 2.3 Внутренний HTTP-набор плагина — 28 tools

Плагин регистрирует:

`spawn_actor`, `get_level_actors`, `set_property`, `run_console_command`,
`delete_actors`, `move_actor`, `get_output_log`, `execute_script`,
`cleanup_scripts`, `get_script_history`, `capture_viewport`, `blueprint_query`,
`blueprint_modify`, `anim_blueprint_modify`, `asset_search`,
`asset_dependencies`, `asset_referencers`, `enhanced_input`, `character`,
`character_data`, `material`, `asset`, `open_level`, `task_submit`,
`task_status`, `task_result`, `task_list`, `task_cancel`.

Bridge объединяет семь больших modify-доменов в `unreal_ue`; девять опасных или
служебных операций (`run_console_command`, три script tools и пять task-queue
tools) не публикуются как обычные callable tools. `execute_script` по умолчанию
требует модального подтверждения в редакторе (`bAutoApproveScripts=false`).
Это не дефект аудита, а сознательная safety-граница плагина.

### 2.4 Что MCP пока не умеет доказать полностью

- runtime-порядок callbacks, фактический path following, collision/stuck и
  визуальный blend без PIE/trace;
- точное содержимое Montage Notify/Section/Slot tracks;
- корректность retarget pose, root motion, foot sliding и penetration на кадре;
- наличие несохранённых изменений в другом неактивном уровне;
- semantic correctness графа: Blueprint может успешно компилироваться и всё
  равно иметь race, как `BP_GA_Attack` сейчас.

---

## 3. Глобальная инвентаризация ассетов и графов

### 3.1 Реестр `/Game`

| Класс | Количество |
|---|---:|
| `Texture2D` | 186 |
| `AnimSequence` | 113 |
| `StaticMesh` | 86 |
| `MaterialInstanceConstant` | 70 |
| `StaticMeshActor` (external actors включены) | 63 |
| `Blueprint` | 20 |
| `Material` | 19 |
| `InputAction` | 18 |
| `MaterialFunction` | 16 |
| `WidgetBlueprint` | 12 |
| `Skeleton` / `SkeletalMesh` | 8 / 8 |
| `ObjectRedirector` | 8 |
| `AnimMontage` | 5 |
| `ControlRigBlueprint` / `BlendSpace` | 3 / 3 |
| `World` | 4 |
| `AnimBlueprint` / `AimOffsetBlendSpace` / `InputMappingContext` | 1 / 1 / 1 |

Остаток — level external actors, Data Assets, physics/nav/landscape и другие
единичные классы. Крупнейший пакет — `/Game/XRU1Game/Units` (381 ассет).

### 3.2 Все Blueprint-ассеты

- Core: `BP_TacticalCameraPawn`, `BP_MoveRangeVisualizer`,
  `BP_TacticalPlayerController`, `GM_Tactics`, `BP_TacticsGameInstance`.
- Abilities: `BP_GA_Attack`, `BP_GA_Overwatch`.
- Units/weapons: пять `BP_Unit_*`, `BP_AssaultRifle_Default`,
  `BP_Sniper_Default`, `BP_SMG_Default`, `BP_LMG_Default`.
- UI: `WBP_PrimaryGameLayout`, `WBP_UnitPortrait`, `WBP_TacticalHUD`,
  `WBP_UnitHUD`, `WBP_UnitCoverIcon`, `WBP_UnitHealthBar`, `WBP_UnitAPPips`,
  `WBP_Settings`, `WBP_PauseMenuWidget`, `WBP_AboutMenuWidget`,
  `WBP_DifficultySelect`, `WBP_MainMenu`.
- Animation: `ABP_Solider`, `CR_Mannequin_FootIK`,
  `CR_Mannequin_Procedural`, `CR_Mannequin_Body`.
- Оставшиеся template/content BP: `BP_BakeLandscapeLayers`, `BP_WobbleTarget`,
  `BP_JumpPad`, `BP_DoorFrame`.

Контрольный реестр ниже получен отдельным `inspect(include_graphs, variables)`
для каждого из 36 ассетов. Сумма — **94 графа / 2438 узлов**; крупные
`ControlRig`-функции включены, а не скрыты общей цифрой.

| Blueprint | Parent | Графы | Узлы | Vars |
|---|---|---:|---:|---:|
| `ABP_Solider` | `AnimInstance` | 3 | 112 | 21 |
| `BP_AssaultRifle_Default` | `Actor` | 2 | 4 | 0 |
| `BP_BakeLandscapeLayers` | `Actor` | 2 | 9 | 3 |
| `BP_DoorFrame` | `Actor` | 3 | 165 | 12 |
| `BP_GA_Attack` | `GA_Attack` | 1 | 53 | 5 |
| `BP_GA_Overwatch` | `GA_Overwatch` | 1 | 50 | 5 |
| `BP_JumpPad` | `Actor` | 2 | 24 | 3 |
| `BP_LMG_Default` | `Actor` | 2 | 4 | 0 |
| `BP_MoveRangeVisualizer` | `MoveRangeVisualizer` | 2 | 4 | 0 |
| `BP_SMG_Default` | `Actor` | 2 | 4 | 0 |
| `BP_Sniper_Default` | `Actor` | 2 | 4 | 0 |
| `BP_TacticalCameraPawn` | `TacticalCameraPawn` | 2 | 4 | 0 |
| `BP_TacticalPlayerController` | `TacticalPlayerController` | 2 | 3 | 0 |
| `BP_TacticsGameInstance` | `TacticsGameInstance` | 1 | 0 | 0 |
| `BP_Unit_Assault` | `Unit_Assault` | 2 | 18 | 0 |
| `BP_Unit_Marauder` | `UnitBase` | 2 | 18 | 0 |
| `BP_Unit_Medic` | `Unit_Healer` | 2 | 18 | 0 |
| `BP_Unit_Sniper` | `Unit_Sniper` | 2 | 18 | 0 |
| `BP_Unit_Tank` | `Unit_Tank` | 2 | 18 | 0 |
| `BP_WobbleTarget` | `Actor` | 2 | 8 | 0 |
| `CR_Mannequin_Body` | `ControlRig` | 36 | 1409 | 11 |
| `CR_Mannequin_FootIK` | `ControlRig` | 2 | 49 | 6 |
| `CR_Mannequin_Procedural` | `ControlRig` | 1 | 176 | 0 |
| `GM_Tactics` | `TacticsGameMode` | 2 | 3 | 0 |
| `WBP_AboutMenuWidget` | `AboutMenuWidget` | 1 | 3 | 0 |
| `WBP_DifficultySelect` | `DifficultySelectWidget` | 1 | 3 | 0 |
| `WBP_MainMenu` | `MainMenuWidget` | 1 | 3 | 0 |
| `WBP_PauseMenuWidget` | `PauseMenuWidget` | 1 | 3 | 0 |
| `WBP_PrimaryGameLayout` | `PrimaryGameLayout` | 1 | 3 | 0 |
| `WBP_Settings` | `SettingsMenuWidget` | 1 | 3 | 0 |
| `WBP_TacticalHUD` | `TacticalHUDWidget` | 1 | 99 | 2 |
| `WBP_UnitAPPips` | `APPipsWidget` | 1 | 3 | 0 |
| `WBP_UnitCoverIcon` | `CoverIconWidget` | 1 | 3 | 0 |
| `WBP_UnitHealthBar` | `HealthBarWidget` | 1 | 3 | 0 |
| `WBP_UnitHUD` | `UnitHUDWidget` | 1 | 3 | 0 |
| `WBP_UnitPortrait` | `CommonUserWidget` | 3 | 134 | 1 |

У пяти юнитов подтверждены один `ABP_Solider`, классы `BP_GA_Attack` /
`BP_GA_Overwatch`, пять монтажей и соответствующий weapon BP. У всех одинаковые
основные CDO movement-параметры: capsule `34 × 88`, `MaxWalkSpeed=600`,
`MaxWalkSpeedCrouched=300`, `MaxAcceleration=2048`, `GroundFriction=8`.

### 3.3 `ABP_Solider`

- 21 переменная, 112 узлов: EventGraph 101, AnimGraph 9, OnUpdate 2.
- AnimGraph: Locomotion/pose → cached pose → `Main States` →
  `Inertialization` → `DefaultSlot` → Control Rig → Output. Шаг E1.6 собран.
- 4 state machines, 19 states, 42 transitions:
  `Locomotion` 9/30, `Main States` 6/8, `SM_CrouchCover` 2/2,
  `SM_HighCover` 2/2.
- `Dead` играет `MM_Death_Front_01`, а каждый `BP_Unit_*::OnDied` одновременно
  запускает `AM_Death`. Это подтверждённое двойное владение смертью.
- `HighCover::Loop` — standing `MF_Rifle_Idle_ADS`, но `HighCover::Look`
  выбирает full-body `anim_CoverDown_Look_*`. Поэтому юнит при прицеливании у
  высокой стены закономерно садится.
- В `Main States` найдены два одинаковых перехода
  `MM_Rifle_Jump_Fall_Land → Locomotion` с blend 0.4 и 0.5. Удалить дубликат.
- В EventGraph сохранилась переменная `CoverEnterAnim`, хотя Enter-state удалён;
  она обновляется каждый кадр. Все 21 переменная помечены instance-editable —
  лишняя поверхность случайных overrides.
- `FrozenPeekSide` записывается C++, но не входит в `FUnitVisualState` и не
  читается ABP. Фактически сторона продолжает пересчитываться во время поворота.

### 3.4 Монтажи и зависимости

- Созданы `AM_Fire_Open`, `AM_Fire_OverCover`, `AM_HitReact`, `AM_Death`,
  `AM_Overwatch_Enter`; назначение пяти юнитам подтверждено.
- `AM_Fire_Open` зависит не только от `MM_Rifle_Fire`, но и от
  `MF_Rifle_Idle_ADS`; причину нужно проверить вручную в Montage Editor.
- `AM_Fire_OverCover` использует `MM_Rifle_DryFire`, как записано в плане.
- `AM_HitReact` видит 8, `AM_Death` — 6, `AM_Overwatch_Enter` — Equip + Reload.
- Dedicated MCP не подтвердил ни одного gameplay notify. До внедрения §6.1
  считать, что `FireCommit` отсутствует.

### 3.5 Дополнительные проблемы контента/лога

1. Output Log содержит **13 missing package/load ошибок** старых ссылок
   `/Game/InfimaGames/ModernGunsBundle/...` у Sniper/SMG/LMG: silencers, grips,
   lasers, sights и material instance пули. Это реальные битые зависимости.
2. В `Lvl_TopDown` зарегистрированы один `NavMeshBoundsVolume`, но **два external
   actor `RecastNavMesh`**. В логе есть `RegistrationFailed_AgentNotValid` и
   повторяющееся `Unable to find RecastNavMesh instance` для CrowdManager.
   Проверить после открытия/сохранения именно `Lvl_TopDown`; лишний nav actor не
   удалять вслепую.
3. В `/Game/XRU1Game/UI/Icons` осталось 8 `ObjectRedirector`; после проверки
   referencers выполнить штатный Fix Up Redirectors.
4. Player unit BP всё ещё имеют hard dependency на legacy
   `Anim/Unarmed/BS_Idle_Walk_Run`, хотя используется `ABP_Solider`.
5. Есть stale preview-ссылка на отсутствующий `SKM` под
   `/Game/OtherAssets/FreeAnimationLibrary/...`.
6. `DefaultGame.ini` не задаёт `GameplayCueNotifyPaths`, поэтому GAS сканирует
   весь `/Game`; это не функциональный баг, но лишнее время загрузки.
7. В `BP_GA_Attack`/`BP_GA_Overwatch` рабочие transient-поля помечены
   instance-editable. В обоих оставлены пустые template events
   `ActivateAbility`/`OnEndAbility`; они не ломают runtime, но создают шум.

### 3.6 Последние восемь коммитов

| Commit | Что добавил | Вывод аудита |
|---|---|---|
| `14ad3ae` | 5 montage assets, `BP_GA_Attack`, `BP_GA_Overwatch`, `OnHitReact`, E1.7–E1.8 | Ассеты/хуки реально существуют, но docs-чекбоксы не обновлены; post-hit hook запустил latent presentation после раннего `ResolveShot/EndAbility` |
| `17e08b2` | pose/cover state machines, smooth hug/turn/idle-peek, death/ragdoll flow | Здесь сосредоточены конфликтующие writers yaw/side; `FrozenPeekSide` остался write-only, а state-side смерти позднее был продублирован montage из `14ad3ae` |
| `ed877e2` | оружие в unit BP, переменные EventGraph ABP | Dependencies подтверждают назначения, а E1.1 в docs остался полностью пустым |
| `dfe8126` | реорганизация Content, переход юнитов на `ABP_Solider` | Старые `ABP_Unarmed`/`ABP_Soldier` инструкции в 04/05/08/09 стали ложными |
| `85ce69b` | большой смешанный пакет: animation prep, Crowd/cover/camera/Overwatch | Native post-move cover flow и позднее BP `HugCover` стали дублироваться; большой разнородный commit усложнил bisect |
| `a347b48` | аудит рекурсии, AP и dead code | Подтверждает, что successful build без PIE/graph trace не является приёмкой gameplay flow |
| `c329acd` | utility AI и физическая модель cover/flank | Решение AI улучшено, но общий player/enemy route executor не появлялся |
| `b74a8e4` | более ранний боевой flow поверх cover Ф2–Ф6 | Базовая точка до нового animation/action слоя; найденные семь симптомов относятся к последующим интеграциям |

---

## 4. Корневая причина семи наблюдаемых проблем

| # | Подтверждённая причина | Почему это происходит |
|---|---|---|
| 1 | StepOut — latent Blueprint-цепочка вне жизненного цикла action; `OutFiringEyeLocation` ошибочно используется как nav goal корня/капсулы; возврат идёт прямым `AI Move To` в точный anchor с radius 5, ошибки игнорируются; общий `OnMoveCompleted` принимает исходящий StepOut за обычный tactical move и вызывает `HugCover` | Ability уже завершена, eye point не равен root point, route/action id не сохранён, capsule 34 см режет угол, служебное и обычное движение не различаются |
| 2 | Игрок использует `PlanMoveTo → MoveAlongRoute`, enemy `MoveWithBudget` всё ещё использует `GetPointAlongPathBudget → MoveToLocation` | Стоящие союзники не входят в navmesh-path; Detour local avoidance не заменяет общий occupancy-aware route planner |
| 3 | `Pose == Dead` запускает death sequence в AnimSM, `OnDied` всех пяти BP запускает `AM_Death` через тот же full-body output | Два независимых владельца одной разовой анимации |
| 4 | `HighCover::Loop` standing, `HighCover::Look` — full-body crouched CoverDown | При `bShouldPeek=true` граф сам переводит high cover в присед |
| 5 | `PendingTurnYaw`, `bShouldPeek`, current actor rotation и cover pose меняются параллельно; TurnInPlace конкурирует с cover transitions. При новом малом повороте `FaceTowardsSmooth` мгновенно меняет yaw, но не очищает старые `bTurningInPlace/TurnTargetYaw/PendingTurnAmount` | На следующем Tick старый turn может развернуть бойца обратно; анимация одновременно догоняет несколько источников ориентации |
| 6 | `FindPeekEdgeSide` выбирает ближайший край стены без target, а `GetFiringPositions` строит side относительно текущей цели; `PeekSideLocal` каждый rebuild берётся из текущего local Y; `FrozenPeekSide` не читается | У системы нет неизменяемого контекста выбранного края/цели, и знак может смениться прямо во время разворота |
| 7 | `UGA_Attack`: `ResolveShot → OnShotFired → EndAbility`; montage запускается только в BP после урона. Overwatch делает то же | Target получает HitReact/Death до кадра выстрела; notify не управляет gameplay commit |

### 4.1 Проверенный race выстрела

```text
UGA_Attack::ActivateAbility
  → CommitAbility / списать AP
  → ResolveShot                         // урон уже применён
      → мгновенный FaceActorTowards
      → target OnHitReact / OnDied
  → BP OnShotFired
      → AI Move To StepOut              // latent
      → FaceActorTowards
      → PlayMontageAndWait
      → AI Move To Home                 // latent
      → HugCover
  → EndAbility                          // выполняется сразу после запуска BP event
```

`UTacticalAbility` — `InstancedPerActor`; её mutual-exclusion тег существует
только пока ability активна. После раннего `EndAbility` ход, AP и другая команда
могут продвинуться, а latent graph persistent ability продолжает менять того же
юнита. Это не локальная ошибка монтажа, а нарушение границы транзакции действия.

Дополнительно `GetFiringStance` возвращает **eye/LOS point**, а BP проецирует её
и передаёт `AI Move To` как позицию capsule root. Проверка кандидата выполнялась
не полным capsule sweep + route. `ProjectPointToNavigation` возвращает точку на
nav surface (**nav-foot**), не transform actor root: нельзя ни сохранять
исходный eye/raw candidate, ни напрямую ставить root в `Projected.Location`.
Нужно восстановить root с capsule floor offset, затем подтвердить capsule sweep
и route. Эти ошибки нельзя лечить одним увеличением acceptance radius.

Во время физического `AI Move To` юнит считается in-transit и исключается из
обычного набора unit obstacles. Значит заявленный инвариант «домашняя occupancy
остаётся занятой» сам по себе не выполняется — coordinator обязан держать
отдельную reservation на `CoverAnchor` до return/abort.

### 4.2 Несколько писателей rotation/state

Сейчас yaw или цель поворота меняют:

- hover-target в `ATacticalPlayerController` → `FaceTowardsSmooth`;
- idle peek в `AUnitBase::UpdateCoverPeek`;
- `AUnitBase::HugCover`;
- `AUnitBase::Tick` через `SetActorRotation`;
- `UTacticsCombatStatics::ResolveShot` через мгновенный `FaceActorTowards`.

При этом `RebuildVisualState` вычисляет `PeekSideLocal` из уже меняющейся actor
rotation. Поэтому зеркалирование клипа в ABP симптом не исправит: на другом
target знак снова может измениться.

В штатной timed-ветке `UpdateCoverPeek` возврат корректно защищён
`bTurnBodyOnPeek`. Дефект уже: при **принудительном сбросе** активного peek,
когда `bCanPeek` стал false, вызывается `FaceCoverWall` и при
`bTurnBodyOnPeek=false`. Это создаёт лишний turn, хотя тело для клипа не
вращалось; gated штатный return удалять нельзя.

Отдельно проверить классификацию: текущий `GetFiringStance` может вернуть
`OverCover` при центральном LOS даже для `Full` cover. По GDD `OverCover`
принадлежит HalfCover; для Full допустимы target-aware lean/StepOut.

---

## 5. Целевая архитектура

### 5.1 Одна транзакция `CoverShot`

```text
InCoverIdle
  → SelectPeek
  → AlignToPeek
  → ExitCover
  → AimReady
  → FirePending
  → FireCommitted
  → Recover
  → ReturnToAnchor
  → InCoverIdle

Death и Cancel имеют явную политику по фазе: до `FireCommitted` отменяют
выстрел без урона; после commit урон не откатывают, а переводят действие в
безопасный cleanup/return либо terminal death.
```

Нужен один C++-контекст действия, например `FCoverActionContext`:

- `ActionId`, `Phase`, weak `Shooter/Target`;
- `CoverAnchor`, `CoverNormal`, `CoverTangent`, неизменяемый `PeekSide`;
- `FiringStance`, `PeekTransform`, версионируемые пути выхода/возврата;
- `MoveRequestId`, `MontageInstanceId`, `bShotCommitted`;
- результат движения/монтажа и причина cancel.

После выбора target фиксируются `ActionId`, firing solution и сторона. Их читают
movement executor, AnimBP и ability; ни один consumer не вычисляет сторону
заново. Route/`MoveRequestId` — изменяемая часть контекста: при блокировке путь
можно пересчитать с новой версией, не меняя identity действия и firing side.

### 5.2 Правила владения

1. **Ability/action coordinator** владеет фазой, AP, cancel и единственным
   вызовом `ResolveShot`.
2. **Movement executor** владеет actor/capsule rotation и path request во время
   действия. `OnMoveCompleted` различает `TacticalMove`, `StepOut`,
   `ReturnToAnchor`, а не вызывает общий post-move flow для всех request.
3. **AnimBP** только проецирует immutable visual snapshot в pose; он не выбирает
   gameplay-side и не запускает урон.
4. **Montage Notify** только сообщает `FireCommit` активной транзакции.
5. **Death lifecycle XRU1**: `Alive → Dying → Dead`. В `Dying` играет один
   full-body montage. `Completed`, `Interrupted` и при необходимости
   `BlendOut` обрабатываются раздельно: `BlendOut` начинается до полного
   завершения и не равен success. Terminal Dead pose/ragdoll — выбранный нами
   контракт; Death state sequence одновременно не запускается.

Для обычной `UGA_Attack` ability живёт до return/abort. `UGA_Overwatch` —
долгоживущая parent ability до следующего хода: каждая реакция создаёт вложенный
`ReactionShot` subaction с собственным `ActionId`, montage/move callbacks и
commit, не завершая parent раньше её штатного лимита/снятия состояния.

`ResolveShot` также нужно разделить: mechanics-функция получает явный
`FShotContext`/frozen firing solution и **не** поворачивает actor и не управляет
камерой. Coordinator отдельно делает presentation; на commit revalidate LOS/
target, затем mechanics ровно один раз применяет roll/damage/noise/outcome.

Контракт preview → commit для XRU1: при подтверждении замораживаются target id,
`FiringEyeLocation`, stance, выбранная сторона, cover modifiers и показанный HUD
hit chance. На `FireCommit` проверяются тот же `ActionId`, живость target/shooter
и LOS **из frozen origin**; нельзя молча выбрать другую сторону, origin или
пересчитать шанс из текущего actor transform после StepOut. Если target/геометрия
сделали frozen solution невалидной, это явный pre-commit abort по GDD §5.2;
roll выполняется с тем шансом, который видел игрок. Overwatch создаёт такой же
snapshot при старте reaction subaction, хотя предварительного HUD у него нет.
Нельзя заново выводить shot origin/side из текущего actor transform.

### 5.3 Сторона укрытия

Предлагаемый для XRU1 алгоритм — выбирать `Left/Right/Over` не по знаку в
текущем local space, а по кандидатам:

1. capsule sweep до peek transform;
2. reachability/nav path;
3. LOS к выбранной цели;
4. score distance/turn/exposure;
5. freeze side до завершения/cancel действия.

При смене цели на противоположную сторону текущий align/aim отменяется, создаётся
новый `ActionId` и новый context. Для обычного idle-look нужна отдельная
косметическая сторона; она не должна менять активный firing context.

### 5.4 Общее движение игрока и AI

Игрок и enemy должны вызывать один route planner/executor с одинаковыми
occupancy disks, capsule/acceptance правилами и завершением. Для dynamic
avoidance выбрать **один** механизм: Detour Crowd или RVO. Detour — исходный
кандидат для текущей navmesh-тактики, но его нужно подтвердить матрицей XRU1;
RVO одновременно не включать. Проектные требования XRU1:

- reservation конечной точки;
- stuck detector по прогрессу вдоль route;
- один controlled replan, затем wait/alternate point;
- AP/abort строго по GDD §5.2, а не по факту случайного callback.

---

## 6. План исправления без точечных костылей

### A16-P0 — зафиксировать диагностику и контракты

- [x] Глобальный static/MCP-аудит и синхронизация docs завершены 2026-07-28.
- [x] Зафиксировать AP/cancel contract обычной атаки, движения и Overwatch в
  GDD §5.2; до `FireCommit` технический abort возвращает snapshot обычной атаки,
  после commit отката нет, Overwatch entry AP не возвращается.
- [ ] Добавить коррелированный лог: `ActionId`, phase, target, `CoverAnchor`,
  `PeekSide`, `MoveRequestId/result`, montage instance, notify,
  `bShotCommitted`.
- [ ] Снять до исправления один Animation Insights/Rewind trace каждого кейса
  из §7; сохранить команды/скриншоты в журнале проверки.

### A16-P1 — атомарный выстрел и смерть

- [~] Введён `FTacticalFireActionContext` со frozen target/chance/origin/
  stance/home/presentation/cover snapshot и guards; route/request-id часть ещё
  исполняется BP и требует документа 17. Полный route/request-id snapshot
  остаётся последующим усилением; eye и capsule-root уже разделены.
- [~] C++ action/reaction coordinator и terminal API готовы; latent
  StepOut/fire/return ещё нужно переподключить в `BP_GA_*` по документу 17.
  Долгосрочно можно вынести сами latent tasks в C++:
  coordinator/AbilityTask; обычная attack ability живёт до return/abort,
  Overwatch создаёт вложенный reaction subaction.
- [x] Разделить назначения `OnMoveCompleted` по request purpose; исходящий
  StepOut не вызывает обычные `EvaluateSurroundings/HugCover/NotifyMoveFinished`.
- [x] Возврат завершать по capsule/root transform и acceptance radius, а не по
  eye point. В `HugCover` трактовать `Projected.Location` как **nav-foot** и
  восстановить root (`nav-foot + capsule floor offset`) перед sweep/route; не
  запускать одновременно новый smooth turn и nudge, способные повторно вжать
  capsule во внутренний угол после успешного return.
- [!] Добавить вручную в оба fire montage один `FireCommit` Montage Notify с
  `Montage Tick Type = Branching Point` на кадре muzzle/recoil.
- [x] Убрать ранний `ResolveShot` из activation; разделить mechanics и
  presentation. По notify coordinator revalidate target/LOS и вызывает
  mechanics ровно один раз с guard
  `ActionId + MontageInstance + !bShotCommitted`; mechanics не пишет rotation/
  camera и использует explicit `FShotContext`.
- [~] C++ contract готов: до notify abort возвращает AP, после notify не
  откатывает commit; BP montage/move callbacks ещё подключить по документу 17.
- [x] Зафиксировать preview → commit: action snapshot получает показанные
  HUD chance/cover/stance и frozen origin; commit только валидирует target и LOS
  из этого origin. Никакого пересчёта от физически смещённого actor после StepOut.
- [x] Watchdog отсутствующего `FireCommit`: диагностировать montage/action id,
  безопасно отменить без урона и выполнить return; timeout не должен сам
  подменять gameplay commit.
- [!] Развести вручную BP callbacks `Completed`, `Interrupted`, `Cancelled`, `BlendOut`;
  устаревший callback с чужим `ActionId` игнорировать.
- [x] Ввести общий барьер `IsActionInProgress(ActionId)`: при `AP=0`
  `HandleSelectedUnitAPChanged`/`TryAutoEndTurn`, `TurnManager` и AI не меняют
  активного юнита/фазу и не начинают следующий decision до terminal
  `ReturnToAnchor/Abort/Death` активного subaction.
- [~] C++ single-owner DeathMontage/terminal ragdoll/watchdog готов; callbacks
  пяти `BP_Unit_*` ещё подключить вручную по документу 17. При назначенном
  montage `Dead` state больше не запускает параллельную sequence; штатный
  terminal должен прийти из callback фактически сыгранного montage instance.
- [x] `AUnitAIController::ExecuteDecision` ждёт completion обычного attack
  subaction и только потом `ScheduleNextStep`; сейчас следующий AI-шаг ставится
  сразу после успешного старта GA.
- [ ] Запуск AI action возвращает явный `Accepted/Rejected + ActionId`, а
  результат — terminal callback. Текущий AP delta сразу после gameplay event не
  считается ни подтверждением принятого action, ни фактом состоявшегося выстрела;
  attack throttle обновляется только по предусмотренной action phase/commit.
- [~] Reaction subaction Overwatch единолично владеет паузой mover/camera в
  C++; сохранённый BP ещё мигрировать по документу 17. Mover и presentation
  закрываются ровно один раз по completion/abort; timeout — только watchdog.
- [x] Убрать прямые обходы: missing `AttackAbilityClass` у AI — configuration
  error без аварийного урона; tutorial forced hit/miss проходит тот же action
  pipeline, а результат roll задаётся override в `FShotContext`.

### A16-P2 — единый cover/orientation context

- [x] Расширить action snapshot active cover/stance/root данными для
  hover/idle/aim и сохранить immutable `PeekSide` на время действия.
- [x] Frozen peek side применяется на время действия; активная сторона больше
  не пересчитывается по каждой цели. Удалить вычисление активной стороны из текущего
  `CoverDirectionLocal.Y`; либо реально публиковать frozen side, либо удалить
  мёртвое поле `FrozenPeekSide`.
- [x] Ввести постоянный `ActiveCoverAnchor/WallId` с hysteresis: удерживать его
  до tactical move или физической invalidity, а не выбирать заново по правилу
  `Full > Half > nearest` на каждом evaluate. Смена direction/anchor обязана
  иметь отдельное событие даже при неизменном типе `Full → Full`.
- [ ] Оставить одного владельца yaw; hover только задаёт desired target.
- [x] В `FaceTowardsSmooth` при немедленном малом повороте атомарно очистить
  старые `bTurningInPlace`, `TurnTargetYaw` и `PendingTurnAmount`; callback/tick
  предыдущего turn не должен разворачивать юнита назад.
- [ ] Развести cosmetic idle-look и gameplay aim/step-out.
- [x] Для HighCover использовать standing look/lean или upper-body additive;
  full-body `CoverDown_Look_*` оставить только HalfCover.
- [x] Исправить `GetFiringStance`: FullCover при работающей стене никогда не
  классифицируется как `OverCover`; central LOS ведёт к target-aware
  standing lean/StepOut, а при фактическом фланге — `Open`.
- [x] В `UpdateCoverPeek` менять orientation только в корректной ветви:
  gated return при `bTurnBodyOnPeek=true` сохранить; в принудительном сбросе
  `bCanPeek=false` не звать `FaceCoverWall`, если тело для peek не вращалось.
- [x] Удалить duplicate transition и stale `CoverEnterAnim`; сократить
  instance-editable variables ABP.

### A16-P3 — унифицировать движение AI

- [~] Добавлен read-only `PlanMoveForUnit` поверх того же occupancy field;
  окончательное извлечение model из visualizer остаётся архитектурным долгом.
- [x] Перевести `MoveWithBudget` на общий route + `MoveAlongRoute` executor.
- [ ] Reservation, stuck/replan и точный GDD §5.2 AP/result contract: failure у
  anchor возвращает reserve, после реального выхода из anchor AP потрачен.
- [ ] Проверить/починить `Lvl_TopDown` RecastNavMesh/CrowdManager до оценки
  avoidance.

### A16-P4 — санитарная чистка ассетов и документов

- [ ] Починить 13 missing weapon dependencies.
- [ ] Проверить `AM_Fire_Open` и все Montage Notify/Slot/Section tracks вручную;
  `MM_Rifle_DryFire` не принимать как live `OverCover` shot, пока не подтверждены
  muzzle/recoil frame, weapon socket и визуальный результат в PIE.
- [ ] Fix Up Redirectors после проверки referencers.
- [ ] Удалить stale legacy Unarmed/preview dependencies только после impact
  analysis.
- [ ] Задать узкий `GameplayCueNotifyPaths`, если Gameplay Cues остаются.
- [ ] После C++-изменений: короткая сборка UE 5.7, restart Editor,
  переиндексация codebase-memory и полный §7.

---

## 7. Обязательная матрица PIE-приёмки

| Кейс | Ожидаемый результат |
|---|---|
| Half/High × target Left/Right | правильная сторона фиксирована до конца; High не приседает |
| FullCover + central LOS | не `OverCover`: target-aware standing lean/StepOut; `Open` только если стена реально не защищает от target |
| Смена target слева направо во время aim | старый action отменён, нет разворота от цели и двойного montage |
| StepOut у внутреннего/внешнего угла | валидный путь выхода и возврата; capsule не режет угол; anchor достигнут |
| Путь возврата временно занят союзником | wait/replan/abort по контракту, без вечного зависания |
| Два союзника в узком проходе, игрок и enemy | оба используют одинаковое огибание/reservation |
| Target invalid / technical interrupt до `FireCommit` обычной атаки | урона/шума нет; после return AP восстановлены к pre-action snapshot |
| Interrupt после `FireCommit` | урон ровно один, затем безопасный cleanup |
| Смерть до/после `FireCommit` | ровно одна death-анимация, cover/aim/fire отменены |
| Overwatch interrupt до/после `FireCommit` | entry AP никогда не возвращается; до commit quota не увеличена, после — увеличена; один MovementId не вызывает retry-loop |
| Обычная атака сжигает последние AP до конца montage/return | выбор юнита и конец фазы не меняются, пока активный `ActionId` не завершён |
| AI начал attack/reaction subaction | `ScheduleNextStep` не вызывается до terminal completion этого subaction |
| AI gameplay event принят/отклонён без немедленного AP delta | решение идёт по `Accepted/Rejected + ActionId`, нет ложного fire/throttle |
| Overwatch остановил движущуюся цель | mover возобновлён ровно один раз по completion/abort; нет двойного camera/noise event и штатной зависимости от fixed timer |
| Death Montage с random section/изменённой play rate | terminal pose/ragdoll начинается по callback выбранного instance, а не по длине всего asset |
| Preview у укрытия → физический StepOut → `FireCommit` | target, stance, origin, cover modifiers и hit chance совпадают с подтверждённым HUD; invalid LOS даёт явный pre-commit abort, не скрытый пересчёт |
| Угол между двумя FullCover normals, 30 повторных evaluate | `ActiveCoverAnchor/WallId` не скачет; смена direction происходит один раз по hysteresis/invalidity и публикует событие |
| 20 быстрых смен цели/hover | нет накопления latent callbacks и дёрганья yaw |

Инструменты: Animation Insights для montage/notifies/state weights, Rewind
Debugger для проблемного кадра, AI Debugging (`'` и NavMesh/path data) для
маршрута. Одной визуальной проверки недостаточно — сверять коррелированный лог.

---

## 8. Сверка с Epic и XCOM

Официальный XCOM 2 manual фиксирует игровые понятия low/high cover, flank и LOS,
но не раскрывает технический LOS/step-out алгоритм. SDK QuickStart подтверждает,
что XCOM 2 SDK поставляет UnrealScript gameplay source; индекс depot-файлов
позволяет проверить отдельные `X2Action_*`. Наличие отдельных действий
`ExitCover`, `Fire`, `EnterCover` **согласуется** с декомпозицией XRU1, но само
по себе не доказывает обязательный runtime-порядок. Порядок
`ExitCover → Fire → EnterCover` показан только во вторичном фрагменте
`BuildVisualization` и используется как ориентир, не спецификация.

Материалы сообщества о точном `BuildVisualization` и наблюдаемом step-out ниже
помечены как вторичные, а не как спецификация.

### Источники

- Epic: [Animation Montages](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-montage-in-unreal-engine?application_version=5.7),
  [Slots](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-slots-in-unreal-engine?application_version=5.7),
  [Animation Notifies](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-notifies-in-unreal-engine?application_version=5.7),
  [FBranchingPointNotifyPayload](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FBranchingPointNotifyPayload?application_version=5.7),
  [State Machines](https://dev.epicgames.com/documentation/en-us/unreal-engine/state-machines-in-unreal-engine?application_version=5.7).
- Epic: [Root Motion](https://dev.epicgames.com/documentation/en-us/unreal-engine/root-motion-in-unreal-engine?application_version=5.7),
  [Motion Warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-warping-in-unreal-engine?application_version=5.7),
  [Aim Offset](https://dev.epicgames.com/documentation/en-us/unreal-engine/aim-offset-in-unreal-engine?application_version=5.7),
  [Pose Warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/pose-warping-in-unreal-engine?application_version=5.7).
- Epic: [Avoidance](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-avoidance-with-the-navigation-system-in-unreal-engine?application_version=5.7),
  [AI Debugging](https://dev.epicgames.com/documentation/en-us/unreal-engine/ai-debugging-in-unreal-engine?application_version=5.7),
  [Animation Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-insights-in-unreal-engine?application_version=5.7),
  [Rewind Debugger](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-rewind-debugger-in-unreal-engine?application_version=5.7).
- 2K: [XCOM 2 Extended Manual](https://downloads.2kgames.com/xcom2/manuals/asia/XCOM_2_Extended_Manual_%28English%29_XB1%28For_SG.HK.KR%29.pdf),
  [XCOM 2 SDK QuickStart](https://downloads.2kgames.com/xcom2/uploads/pdfs/XCOM2_SDK_QuickStart.pdf).
- Индекс SDK depot: [XCOM 2 SDK manifest](https://steamdb.info/depot/299991/).
- Вторичные: [порядок actions в SDK-подобном BuildVisualization](https://forums.nexusmods.com/topic/3933095-creating-multitarget-abilities/),
  [наблюдение step-out](https://www.reddit.com/r/Xcom/comments/mls9rz/how_do_cover_and_line_of_sight_actually_work_xcom2/),
  [community case report о Detour Crowd + MoveTo](https://forums.unrealengine.com/t/detour-crowds-move-to-produces-bad-pathfinding/330181).
