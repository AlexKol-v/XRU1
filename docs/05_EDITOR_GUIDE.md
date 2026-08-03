# Постоянный гайд по Unreal Editor

Актуально на 2026-08-03. Это справочник повторяемых операций. Закрытых
пошаговых чек-листов анимаций/HUD здесь нет.

## 1. Основные пути ассетов

| Назначение | Путь |
|---|---|
| Core BP/GameMode/Input | `/Game/XRU1Game/Core`, `/Game/XRU1Game/Input` |
| Юниты и оружие | `/Game/XRU1Game/Units` |
| Анимации | `/Game/XRU1Game/Units/Anim` |
| Abilities | `/Game/XRU1Game/Tactics/Abilities` |
| UI (виджеты, арт, иконки) | `/Game/XRU1Game/UI` |
| **Все Data Assets** | `/Game/XRU1Game/Data/<Core\|Units\|AI\|Missions>` (правило — 06_CONVENTIONS §3) |
| StateTree-графы квестов | `/Game/XRU1Game/Quests` |
| Проектные карты | `/Game/XRU1Game/Maps` |
| Общая карта Tutorial/Mission01 | `/Game/XRU1Game/Maps/Main_Map_Showreel` |

Сторонние паки не перестраивать без необходимости. Новые проектные ассеты
класть в `XRU1Game`; общая боевая карта уже лежит в `XRU1Game/Maps`.
Перенос ассетов между папками — только Move/Rename в редакторе с последующим
Fix Up Redirectors и сохранением ссылающихся карт (порядок и ловушки —
[06_CONVENTIONS §3.2](06_CONVENTIONS.md), [agents/AGENT_UNREAL_TOOLING §5.2.8](agents/AGENT_UNREAL_TOOLING.md)).

## 2. Обязательная настройка тактической карты

Для новой карты проверить:

1. `World Settings → GameMode Override = GM_Tactics`.
2. Один `PlayerStart` внутри игровой области и недалеко от стартового отряда.
3. `NavMeshBoundsVolume` покрывает все доступные секции и перепады высоты.
4. В режиме `P` видимая зелёная область не прерывается на целевых проходах.
5. В persistent остаются общий арт/NavMesh/light/camera bounds и один
   `BP_TacticalScenarioDirector`; gameplay actors лежат в scenario sublevel.
6. `SL_Showreel_Tutorial` и `SL_Showreel_Mission01` не загружены одновременно.
7. Четыре player BP и нужные enemy BP стоят капсулами на NavMesh.
8. У врагов заполнены `PatrolPoints`, если они начинают в Patrol.
9. Укрытия имеют collision для shot geometry; ориентиры дизайна — около 60 см
   для Half и 150 см для Full, высота считается от пола.
10. Mission actors (`ABombObjective`, `AEvacZone`) находятся только в mission
    sublevel; tutorial zones/staged actors — только в tutorial sublevel.

Не размещать `BP_TacticalCameraPawn` вручную: его создаёт `GM_Tactics` как
Default Pawn в точке `PlayerStart`.

## 3. `Main_Map_Showreel`: текущее состояние и камера

Проверено через UnrealClaude MCP 2026-07-29:

- карта уже использовала `GM_Tactics`;
- `NavMeshBoundsVolume` и `RecastNavMesh` присутствовали;
- в карте были два тестовых юнита;
- `PlayerStart` отсутствовал, поэтому camera pawn стартовал в `(0,0,0)` и
  затем летел к отряду, расположенному примерно у `(2500,6100)`. На большой
  showcase-карте это выглядело как самопроизвольное движение.

Добавлен `PlayerStart_Tutorial` в `(2500,6100,400)` и карта сохранена. Если
игровая область будет перенесена, переместить и `PlayerStart` к центру стартовой
группы. Пользователь подтвердил PIE-прогоном 2026-07-29: WASD и поворот камеры
на карте работают нормально; этот баг закрыт.

Дополнительный источник движения — edge scroll: пока курсор находится в
16 пикселях от края окна, `ATacticalPlayerController::UpdateEdgeScroll`
постоянно подаёт pan. Это штатная функция, не World Settings. Для диагностики:

- убрать курсор от края и проверить только WASD;
- проверить, что окно PIE имеет корректный viewport size;
- выключить панораму у края в экране настроек (раздел «КАМЕРА») — это
  пользовательская настройка, а не только свойство BP.

WASD сейчас намеренно движет камеру относительно её yaw (`Forward/Right` от
`SpringArm`), а не по фиксированным World X/Y. Поэтому после Q/E направления
поворачиваются вместе с экраном. Это поведение кода, не ошибка координат карты.

**Переработка камеры выполнена 2026-08-02** (модель XCOM 2 + камера-моды):
свободное вращение удержанием Q/E и Alt+мышью, наклон от зума, этажи обзора
PageUp/PageDown, центрирование C/V, обзор и чувствительность в настройках.
Полный список управления — [01_GDD §11](01_GDD.md), устройство и параметры —
[11 §5.0.23](11_SHARED_MAP_TUTORIAL.md). Тюнинг чисел — в `BP_TacticalCameraPawn`
(категории `Tactics|Camera` и `Tactics|Camera|Shot`).

Текущие отдельные проблемы самой карты из Output Log:

- RecastNavMesh не зарегистрирован для ожидаемого агента — сверить Project
  Settings → Navigation System → Supported Agents и пересобрать Paths;
- `MM_Sky` не компилируется для SM6 — исправить материал или заменить sky;
- `NavData RegistrationFailed_AgentNotValid` — сверить Supported Agents и
  пересобрать Paths.

Закрыто: `SpawnActor` с пустым Class оказался `HUDClass=None` у `GM_Tactics`
(движок спавнил `AHUD` без класса); назначен базовый `/Script/Engine.HUD`.

## 4. Юниты, AnimBP и оружие

Все `BP_Unit_*` должны иметь:

- `Mesh → Anim Class = ABP_Solider`;
- соответствующий weapon child actor/asset;
- `BP_GA_Attack` и `BP_GA_Overwatch`;
- `AM_Fire_Open`, `AM_Fire_OverCover`, `AM_HitReact`, `AM_Death`,
  `AM_Overwatch_Enter`;
- корректный HUD layout (`DA_UnitHUD_Squad` или `DA_UnitHUD_Enemy`).

Текущая раскладка оружия:

| Класс | Weapon BP |
|---|---|
| Assault | `Weapons/AssaultRifle/BP_AssaultRifle_Default` |
| Sniper | `Weapons/Sniper/BP_Sniper_Default` |
| Medic | `Weapons/SMG/BP_SMG_Default` |
| Tank | `Weapons/LMG/BP_LMG_Default` |
| Marauder | `Weapons/AssaultRifle/BP_AssaultRifle_Default` |

В каждом fire montage должен быть ровно один `FireCommit` Branching Point на
кадре выстрела. Montage запускает BP presentation hook; урон напрямую из BP не
вызывать. Death — один montage, без параллельной state-sequence.

## 5. Будущий IK второй руки

Неприоритетная задача: автоматически держать левую руку на цевье/прикладе у
разных моделей оружия.

Рекомендуемый контракт:

1. На каждом weapon BP создать socket/SceneComponent `LeftHandIK` в нужном
   месте и ориентации.
2. В AnimBP получить transform этого effector в component/bone space.
3. Применить `Two Bone IK` или Control Rig к левой руке после базовой позы и до
   финального output; alpha выключать для death/специальных монтажей при
   необходимости.
4. Не хранить координаты руки в `ABP_Solider`: источник истины — конкретное
   оружие, иначе четыре модели потребуют ручных исключений.

Это cosmetic layer: он не должен менять `FUnitVisualState`, cover state или
fire action.

## 6. HUD и UI-тема

Глобальная тема: `/Game/XRU1Game/Data/Core/DA_TacticalHUDStyle`, ссылка —
`BP_TacticsGameInstance.UITheme`; презентация обучения — отдельный
`/Game/XRU1Game/Data/Core/DA_Tutorial_Style` (`BP_TacticsGameInstance.TutorialStyle`).
Новый UI-арт и размеры сначала добавлять в Data Asset/C++ style structure, затем
использовать в WBP. Не дублировать texture/color/padding в каждом виджете. Текущий backlog — [09_UI_HUD.md](09_UI_HUD.md).

## 7. AI, scenario, quest и fog

После первой сборки новых C++ типов:

- AI profile и debug matrix — [08_AI.md](08_AI.md) §5–6;
- `DA_Scenario_*`, streaming sublevels, StateTree A1–D3 и action gate —
  [11_SHARED_MAP_TUTORIAL.md](11_SHARED_MAP_TUTORIAL.md);
- `FogVision`, bounds/profile/reveal, actor gating и post-process —
  [10_FOG_OF_WAR.md](10_FOG_OF_WAR.md) §6–7.

Не переносить donor enemy StateTree, donor Level Blueprint или
`WBP_PrimaryGameLayout`. `STQuestSystem` уже находится в `Plugins/`; создаются
только проектные quest/scenario assets и адаптированный UI.

## 8. Работа через UnrealClaude MCP

Редактор должен быть открыт; статус — `localhost:3000/mcp/status`.

Безопасный порядок:

1. найти ассет через asset search;
2. прочитать BP/Anim/actor state;
3. внести одну логическую правку;
4. Compile/Save;
5. повторно прочитать изменённый граф/актор;
6. проверить в PIE.

Для Montage Notify/Slot/Section tracks MCP может возвращать неполную картину —
их проверять в Persona вручную. Для сохранения уровня использовать level
`save_as`/Save Current Level, а не универсальный `asset.save_asset`: последний
может создать ошибочный `.uasset` рядом с `.umap`.

## 9. Сборка

Редактор должен быть закрыт:

```powershell
.\Build-XRU1.ps1
```

Если требуется принудительно закрыть редактор:

```powershell
.\Build-XRU1.ps1 -StopEditor
```

При `Unable to build while Live Coding is active` закрыть Unreal Editor или
выполнить Live Coding (`Ctrl+Alt+F11`) внутри редактора.

## 10. Частые ошибки

- Нет `PlayerStart`: pawn появляется у origin и заметно летит к отряду.
- Неверный GameMode Override: создаются не тактические Controller/Pawn.
- NavMesh не покрывает участок: AI/игрок не получают маршрут.
- Юнит стоит внутри чужой капсулы: planner вынужден корректировать goal или
  отклоняет его.
- Два источника death/fire: montage и state graph накладываются.
- BP наносит урон до `FireCommit`: возникает рассинхрон анимации и механики.
- Старый override BP перекрывает новый C++ default: Reset to Default и повторная
  настройка Data Asset/BP.
- Missing dependencies оружейных attachment-мешей: вернуть отсутствующий
  optional asset или очистить ссылку; разные базовые модели оружия при этом уже
  назначены корректно.
