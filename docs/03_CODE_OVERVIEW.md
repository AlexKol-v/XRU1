# Обзор кодовой базы XRU1

Актуально на 2026-07-29. Документ описывает действующую архитектуру, а не
историю её появления. Игровой модуль — `Source/XRU1`, UE 5.7.

## 1. Границы модулей

| Область | Каталоги | Ответственность |
|---|---|---|
| Тактическое ядро | `Source/XRU1/Tactics/` | ходы, AP, перемещение, укрытия, бой, AI, способности, камера, миссия и save |
| Игровой UI | `Source/XRU1/UI/`, `UI/Menus/` | HUD, unit widgets, CommonUI-стеки и экраны |
| GAS-иерархия | `Characters/` | ASC, атрибуты и базовые combatant-классы, перенесённые из донора |
| Интеракции | `Interaction/` | детектор объектов и prompt |
| PCG | `PCG/` | вспомогательные PCG-ноды для окружения |
| Плагины | `Plugins/` | `STQuestSystem`, `TeamManager`, `GameplayMessageRouter`, editor-only `UnrealClaude` |

Старые `Variant_Strategy`, `Variant_TwinStick` и шаблонные
`XRU1Character/GameMode/PlayerController` не участвуют в тактическом режиме.

## 2. Главные владельцы состояния

| Класс | Что хранит и решает |
|---|---|
| `ATacticsGameMode` | состав сторон, сложность, запуск боя, mission outcome |
| `UTurnManagerSubsystem` | текущая сторона/активация, лимит атакующих, конец боя |
| `ATacticalPlayerController` | выбор, targeting mode, команды, hover/path preview, auto-advance |
| `ATacticalCameraPawn` | тактический yaw/zoom, focus/follow, временный shot framing |
| `AUnitBase` | параметры юнита, GAS, visual state, оружие, cover anchor и BP-события презентации |
| `UActionPointsComponent` | AP и единый контракт затрат |
| `UCoverDetectionComponent` | локальное Half/Full cover, стороны стены и peek edge |
| `AMoveRangeVisualizer` | поле достижимости, маршрут и визуализация зоны хода |
| `AUnitAIController` | тревога, контекст решения, utility evaluators и исполнение маршрута |
| `UAIBehaviorProfileDataAsset` | единый DataAsset тюнинга perception/nav/alert/position/target/evaluators |
| `UFogOfWarSubsystem` | player-facing gameplay visibility и безопасный список видимых врагов |
| `UTacticsGameInstance` | save/UI/cover и выбранный scenario одной общей боевой карты |
| `ATacticalScenarioDirector` | вход в scenario sublevel и запуск квеста после его загрузки |
| `UTacticalQuestEvents` / `ATacticalQuestZone` | подтверждённые доменные события обучения и зоны тактических бойцов |
| `UTacticsCombatStatics` | LOS, шанс, firing positions, cover shield, урон и общие predicates |

Правило владения: механическое состояние находится в C++; Blueprint отображает
состояние, запускает монтаж/VFX/SFX и сообщает строго определённые presentation
сигналы. BP не рассчитывает урон, AP, LOS, укрытие или исход действия.

## 3. Перемещение и укрытия — закрытая база

Игрок и AI используют одни и те же строительные блоки:

1. навигационный путь;
2. бюджет пути на 1/2 AP;
3. учёт живых юнитов как динамической занятости;
4. корректировка конечной точки из чужой капсулы;
5. исполнение ломаной через `MoveAlongRoute`;
6. переоценка укрытия после прибытия.

`UTacticsCombatStatics::GetUnitClearance` и `GetUnitObstacles` — общий контракт
занятости. `PlanMoveTo`/поле достижимости отвечает за допустимость, route
executor — только за исполнение уже принятого маршрута.

Укрытие разделено на два слоя:

- локальный визуальный слой (`CoverSides`, `BestCoverDirection`, anchor,
  `FindPeekEdgeSide`) определяет, к какой стене прижат юнит и как его показать;
- боевой слой (`GetCoverShieldAgainst`, firing positions, LOS) отвечает на
  вопрос, защищает ли геометрия от конкретного стрелка.

Юниты и трупы не считаются стеной. Half/Full определяется геометрией от уровня
пола. Flanked вычисляется реальной линией атаки, а не направлением анимации.

## 4. Канонический action flow выстрела

Обычный выстрел и Overwatch следуют одному принципу синхронизации:

```text
команда/реакция
  → C++ ability создаёт action context и фиксирует target/firing stance
  → BP-событие запускает нужный montage
  → AnimNotify_FireCommit подтверждает конкретный montage instance
  → активная ability один раз вызывает ResolveShotMechanics
  → HitReact или Death presentation
  → StepOut возвращается в сохранённый anchor
  → ability завершает action, после чего разрешён auto-advance
```

Ключевые классы: `UGA_Attack`, `UGA_Overwatch`, `UAnimNotify_FireCommit`,
`FTacticalFireActionContext`, `UTacticsCombatStatics`.

Инварианты:

- AP резервируется/списывается по общему контракту ability; abort не оставляет
  частично выполненное действие;
- урон не применяется до `FireCommit`;
- один action принимает только notify своего montage instance;
- смена юнита и следующий ход запрещены, пока action не достиг terminal state;
- shot camera может быть отменена ручным pan/rotate/zoom, но сама не завершает
  gameplay action;
- StepOut хранит неизменяемые home anchor, сторону выхода и target до возврата.

## 5. Анимации — принятая архитектура

`ABP_Solider` — единственный AnimBP всех пяти тактических BP. Постоянная поза
берётся из `FUnitVisualState`; разовые действия принадлежат montage pipeline.

| Состояние/действие | Владелец |
|---|---|
| locomotion, crouch, Full/Half cover, Overwatch pose | Anim State Machine по `FUnitVisualState` |
| fire, HitReact, Overwatch enter, death | Montage через C++/BP presentation hook |
| yaw/cover anchor/StepOut movement | `AUnitBase` и активная ability |
| фактический урон | `FireCommit` → `UTacticsCombatStatics` |

Death запускается один раз монтажом и заканчивается terminal Dead; параллельной
death sequence в state machine нет. Этот блок, включая cover hug/peek/StepOut,
считается завершённым. Новые симптомы оформляются как отдельные баги.

## 6. Юниты и оружие

| BP | C++ база | Оружие |
|---|---|---|
| `BP_Unit_Assault` | `AUnit_Assault` | `BP_AssaultRifle_Default` |
| `BP_Unit_Sniper` | `AUnit_Sniper` | `BP_Sniper_Default` |
| `BP_Unit_Medic` | `AUnit_Healer` | `BP_SMG_Default` |
| `BP_Unit_Tank` | `AUnit_Tank` | `BP_LMG_Default` |
| `BP_Unit_Marauder` | `AUnitBase` | `BP_AssaultRifle_Default` |

Оружие прикреплено и различается по классам. Будущая косметическая задача —
поддерживать левую руку на цевье/прикладе через IK/Control Rig и socket/effector
оружия; она не должна менять боевой или animation state flow.

## 7. AI

`AUnitAIController` уже содержит `Patrol/Investigate/Combat`, `FAIDecision`,
набор `UAIEval_*`, скоринг позиций против нескольких угроз, SafeToMove,
Overwatch/Hunker и общий route executor. Это рабочая настраиваемая база, но
качество решений ещё не принято. Текущая программа настройки и тестов —
[08_AI.md](08_AI.md).

Введён `UAIBehaviorProfileDataAsset`: один ассет задаёт perception, навигацию,
alert FSM, target/position scoring и при необходимости instanced evaluators.
Вариативность utility/Investigate использует `DecisionSeed`, построенный из
карты, номера хода, стабильного имени юнита и номера решения; FPS и
`GFrameCounter` больше не влияют на выбор.

## 8. Туман войны

`UFogOfWarSubsystem` централизует текущую actor visibility. На него уже
переведён `ATacticalPlayerController::IsVisibleToSquad`, move preview не считает
скрытых врагов, а HUD получил `GetVisibleEnemyCount`. Это F0 — gameplay-основа,
а не готовый визуальный туман.

Следующие слои: actor/UI/VFX gating, CPU-grid `Unknown/Explored/Visible`, один
Render Target и post-process. Полный контракт — [10_FOG_OF_WAR.md](10_FOG_OF_WAR.md).

## 9. Общая карта, сценарии и квесты

`UTacticalScenarioDataAsset` разделяет логические Tutorial/Mission01, не создавая
копий World. `UTacticsGameInstance::StartCombatScenario` сохраняет активный
DataAsset и всегда открывает `SharedCombatLevel`; `ATacticalScenarioDirector`
в persistent level загружает указанный streaming sublevel и только после этого
запускает `UQuestDefinition`. Каждый запуск получает монотонный
`ScenarioRunId`; Director автоматически назначает активный quest tracked и
очищает tracking/active runner при уходе из World. `Scenario.Ready` публикуется
после `PlayerStarted`, а BP-хук открытия Action Gate — ещё через один tick,
после обработки события StateTree. `FinalizeConfiguredScenario(Success)`
образует единую границу исхода;
GameMode ждёт `OnLevelShown`, после stream регистрирует units/bomb/evac и не
пишет campaign save/не показывает result screen при ошибке финализации.
`RestartActiveScenario` очищает quest runtime, увеличивает `ScenarioRunId` и
переоткрывает тот же shared World.

`STQuestSystem` уже был перенесён из донора. Добавлены Asset Manager scan
`/Game/XRU1Game/Quests`, зависимости `STQuestSystem`/`GameplayMessageRuntime`,
нативные каналы `Quest.Event.Tactical.*`, единый broadcaster и
`ATacticalQuestZone`, который распознаёт AI-controlled бойцов стороны игрока.
Состояние квеста становится Active только после валидного `QuestLogic` и
успешного spawn runner. Confirmed turn, player ability/attack, kill,
defuse/evac и scenario-result hooks уже публикуют leaf-события; selection,
camera, move и scripted enemy attack требуют tutorial action/payload gate.
Tactical objectives и specs группы поддерживают exact channel; отдельная
`Quest Wait Outcome` возвращает правильный terminal `Succeeded`/`Failed`.
Content-only objective IDs уже перечислены в `Config/DefaultGameplayTags.ini`.

Production-графы StateTree, два scenario-specific streaming sublevel и
BP-director создаются в Editor по
[11_SHARED_MAP_TUTORIAL.md](11_SHARED_MAP_TUTORIAL.md).

## 10. UI

`UTacticalHUDWidget`, unit/attribute widgets и `UTacticalHUDStyleData` дают
функциональный боевой HUD и единую тему. Остаток относится к presentation и
полноте экранов, а не к переделке боевого ядра. См. [09_UI_HUD.md](09_UI_HUD.md).

## 11. Blueprint API, который считается контрактом

- `OnFireActionStarted` / `OnReactionFireActionStarted` — выбрать и запустить
  монтаж, не наносить урон;
- `OnShotFired` / `OnReactionShot` — косметика после commit;
- `OnHitReact` и death presentation hooks — один montage, без gameplay-логики;
- getters visual state/cover/firing stance — только чтение для AnimBP/UI;
- `UTacticalAbility` и `CanIssueCommand` — единая проверка допустимости команды;
- `BroadcastQuestEvent` вызывается после подтверждённого результата механики,
  не из `OnClicked` и не в момент старта montage.

При добавлении ability: механика в C++, BP-наследник — ассеты презентации и
настройка. Урон всегда проходит через `UTacticsCombatStatics`.

## 12. Диагностика

| Команда | Назначение |
|---|---|
| `xru1.AI.LogCombat 1` | варианты AI, score, принятое действие и причина |
| `xru1.LOS.Debug 1` | firing positions и линия огня |
| `xru1.Cover.Debug 1` | геометрия, засчитанная укрытием |
| `xru1.MoveRange.LogBuildTime 1` | стоимость построения поля хода |

После C++-изменений: короткая сборка UE 5.7 при закрытом редакторе, затем PIE.
После правок BP: Compile, Save, повторное чтение графа через UnrealClaude и PIE.

## 13. Открытый технический backlog

P0/P1 на ближайшие этапы:

- настройка и отладка AI по [08_AI.md](08_AI.md);
- actor gating и renderer тумана по [10_FOG_OF_WAR.md](10_FOG_OF_WAR.md);
- баланс и presentation четырёх классовых способностей;
- завершение HUD/экранов по [09_UI_HUD.md](09_UI_HUD.md);
- Editor-интеграция двух сценариев `Showreel_Scene` по
  [11_SHARED_MAP_TUTORIAL.md](11_SHARED_MAP_TUTORIAL.md).

Отложено:

- общая переработка тактической/action-camera;
- IK второй руки;
- PCG-полировка окружения и косметические эффекты.
