# 10 — Задачи для агента в редакторе (UnrealClaude MCP)

> **⚠️ АРХИВ (2026-07-21): задачи выполнены.** Файл исторический. Актуальный
> статус — [`../09_GAMEPLAY_STATUS.md`](../09_GAMEPLAY_STATUS.md).


> **Промт для сессии-исполнителя** (Sonnet). Скопируй всё содержимое файла в
> новую сессию Claude Code в корне проекта — или просто скажи агенту:
> «Выполни docs/archive/sonnet_mcp_hud_tasks.md». После выполнения агент проставляет
> чекбоксы прямо здесь; файл удаляется вместе с 08/09 после чистого PIE-прогона.

---

Ты работаешь в проекте **XRU1** (UE 5.7, пошаговая тактика). Отвечай и думай
по-русски. Правки выполняй через MCP-сервер **unrealclaude**
(инструменты `mcp__unrealclaude__*`; если MCP не подхватился — HTTP напрямую:
`POST http://localhost:3000/mcp/tool/<имя>` с JSON-параметрами; статус —
`GET http://localhost:3000/mcp/status`). Редактор Unreal должен быть открыт.

**ПРЕДУСЛОВИЕ**: C++ уже пересобран с переносом HUD-логики в
`UTacticalHUDWidget` (RefreshActionButtons / UpdateTargetPanel /
UpdateSquadCardVisibility / ApplyStyle / клик AttackBtn) и классом
`UTacticalHUDStyleData`. Если класса `TacticalHUDStyleData` в редакторе нет —
СТОП, сообщи пользователю: нужна пересборка (`.\Build-XRU1.ps1`, редактор
закрыт).

**Железные правила**:
1. После КАЖДОЙ правки графа — верификация чтением (`blueprint_query`:
   get_graph / get_nodes / find_references). Плагин местами сырой.
2. Ничего не удалять и не создавать сверх перечисленного. Донора
   (`cst-3d-gubkin-2026-04`) не трогать вообще.
3. `blueprint_modify` авто-компилирует; в конце каждой задачи убеждайся, что
   Blueprint скомпилирован без ошибок, и сохраняй ассет.
4. `execute_script` (python) использовать только там, где указано — это
   фолбэк, требует подтверждения пользователя в редакторе.
5. Если шаг не выходит после 2–3 попыток — НЕ изобретай обходы, пометь шаг
   «⚠️ вручную» и переходи дальше; в конце выдай пользователю список таких шагов.

---

## Задача 1 — чистка WBP_TacticalHUD от логики, перенесённой в C++

Blueprint: `/Game/XRU1Game/UI/WBP_TacticalHUD`. Все GUID ниже сняты с живого
графа 2026-07-20 — перед удалением сверь заголовок ноды.

### 1.1 Удалить обработчик OnHoveredUnitChanged целиком (EventGraph)

Панель цели теперь ведёт C++ (`UpdateTargetPanel`). Удалить (`delete_node`)
все 28 нод цепочки — событие и всё, что от него:

| GUID | Нода |
|---|---|
| 1688AA1442C62D9AA22CB69F4B7F1431 | Событие OnHoveredUnitChanged |
| AAAF5D0946F44E7E5865CFAB03D1586D | Get TargetPanel |
| 60EF4B6E499F4626697C1AB62BAC0111 | Set Visibility (общая «спрятать») |
| D11BD7FD4FD8A461CB20B88B7917E510 | Is Valid (макрос) |
| FC2D9E0047B84829D2DAB5ADB0588C96 | Get Squad |
| 8CE5A3EB4DE358FEB562479A32DB8487 | Contains Item |
| DCA7DCED480487B54C89DB940B6EC29D | Узел переадресации (Knot) |
| 818E3F804B653784CC40909C646D243F | Ответвление (Branch) |
| C1EDF01A4076D9E5ADC1B996F88ECD01 | Get UnitDisplayName |
| 6D2E360541D86C89E627A6A06F9F3843 | Get TargetNameText |
| 2D2F37C942340D90409F9AA4717ED235 | Set Text |
| 7417FD47425FE27D14D40AA9147D7975 | Knot |
| E8E0C294438D741C5D17E9875A869BC4 | Get Health |
| 0F6DD2C7467CF3A8BF1335B0E379DA00 | Knot |
| DD72435B47BD9E1241EC818BB3972C94 | Get Max Health |
| 29D4B316469A835544446EA883F21A17 | float / float |
| 21A8EE8F4F24DBA09D924D9B050A67CD | Get TargetHPBar |
| 26CCC3B04AA7A660905385AC1A4672A8 | Set Percent |
| E0536DC143E6F8E7EE54EC88A8AEA0FF | Get Hit Chance on Target |
| DEE6606C450D5046FB03DE9560224EBF | float >= float |
| AB9FFADD400973AAF53C0F89B13F2786 | Ответвление (Branch) |
| E36B2DEC4D1CCF37288AB9BEB8209997 | Round |
| 0E1549E1456864F274B1D7ABE46E887F | Форматировать текст |
| 6B73C840451D226CCDD0A38EBA4DEC32 | Set Text |
| 940862994DA4F2D0B229BDB8A902E0D6 | Get HitChanceText |
| 1686E05547DBB851AB21CE82B06E571F | Set Text |
| DB986BBB4AAD4F825DFEF59E7BFB9945 | Get TargetPanel |
| 64477E5B4951191F48633595E5462997 | Set Visibility |

Верификация: `search_nodes` query=`Hovered` → 0 совпадений в EventGraph.

### 1.2 Удалить AP-подписку в Event Construct

C++ подписывается на AP отряда сам (`SubscribeToUnitStates`). Удалить:

| GUID | Нода |
|---|---|
| 54D79B2D49B2365529368DA46036E0CA | Get Action Points (Цель: Unit Base) |
| 34DD6CE5486F8832A596ECBACF545359 | Bind Event to On Action Points Changed |
| 4A00216342553B0EDD1364BAE2BA4C0D | OnSquadAPChanged (Custom Event) |
| 9057F78C4BF2FDC39452FE87CD719A78 | Refresh Buttons (вызов в теле OnSquadAPChanged) |
| 5157611C47017D774C58D895D9E1978F | Knot |

После этого конец Loop Body в Construct — нода `Add` (массив Portraits); это
нормально.

### 1.3 Удалить остальные вызовы Refresh Buttons + саму функцию

Известные вызовы (K2Node_CallFunction «Refresh Buttons», Цель: WBP Tactical HUD):

| GUID | Где |
|---|---|
| 9AC1A3C64E98BA6C26EA8BBEE874D6B2 | OnPhaseChanged, конец ветки таймера |
| 05FCC65D44C1E110840EF3A1CEB905D8 | OnPhaseChanged, конец второй ветки |
| EF3FC38F462E148B638CB69C6D8D480D | OnSelectedUnitChanged, после Completed |

Плюс `search_nodes` query=`Refresh Buttons` — в хвосте OnUnitsStateChanged
есть ещё минимум один вызов (хвост графа не дампился, GUID неизвестен) —
удали ВСЕ найденные вызовы «Refresh Buttons» (но НЕ «Refresh» портретов!).

Затем удалить саму функцию: `blueprint_modify` operation=`remove_function`,
имя `RefreshButtons`. Верификация: `get_graph` → в graph_names нет
RefreshButtons; `search_nodes` query=`Refresh Buttons` → 0.

### 1.4 Удалить переменные NewVar и BoundAPUnit

`blueprint_modify` operation=`remove_variable`: `NewVar`, затем `BoundAPUnit`.
Верификация: `get_variables` → остались только `Portraits` и `Bomb`.

### 1.5 Финал задачи 1

Компиляция без ошибок; убедись, что события Construct / OnPhaseChanged /
OnSelectedUnitChanged / OnUnitsStateChanged / OnCombatFinished и 6 кликов
кнопок (Overwatch/Hunker/Ability/Interact/Skip/EndTurn → Request*) целы.
Проверь, что у **AttackBtn НЕТ** BP-события OnClicked (его вешает C++; если
вдруг есть — удали). Сохрани ассет.

- [x] 1.1 обработчик OnHoveredUnitChanged удалён
- [x] 1.2 AP-подписка Construct удалена
- [x] 1.3 вызовы + функция RefreshButtons удалены
- [x] 1.4 NewVar/BoundAPUnit удалены
- [x] 1.5 компилируется чисто, сохранено

## Задача 2 — DataAsset стиля DA_TacticalHUDStyle

1. Найди фактические пути иконок: `asset_search` class_filter=`Texture2D`,
   path_filter=`/Game/XRU1Game/UI/` (ожидаются `T_Icon_Attack`,
   `T_Icon_Overwatch`, `T_Icon_Deaf_Defense`, `T_Icon_Field_Medicine`,
   `T_Icon_Defuse`, `T_Icon_Evac`, `T_Icon_Pass`, `T_Icon_End_Turn`,
   `T_Icon_Half_Сover`/`T_Icon_Full_Сover` — **в имени Cover возможна
   КИРИЛЛИЧЕСКАЯ С!**).
2. Если C в `*_Сover` кириллическая — переименуй латиницей через
   `execute_script` (python):
   `unreal.EditorAssetLibrary.rename_asset(старый_путь, новый_путь)`.
3. Создай DataAsset `/Game/XRU1Game/UI/DA_TacticalHUDStyle` класса
   `TacticalHUDStyleData` (`execute_script` python):
   ```python
   import unreal
   at = unreal.AssetToolsHelpers.get_asset_tools()
   da = at.create_asset("DA_TacticalHUDStyle", "/Game/XRU1Game/UI",
        unreal.TacticalHUDStyleData.static_class(), unreal.DataAssetFactory())
   ```
4. Заполни поля (python `set_editor_property` со снейк-кейсом:
   `attack_icon`, `overwatch_icon`, `hunker_icon`, `ability_icon`,
   `interact_defuse_icon`, `interact_evac_icon`, `skip_icon`, `end_turn_icon`,
   `half_cover_icon`, `full_cover_icon`; текстуры — `unreal.load_asset(путь)`).
   Размеры (`action_icon_size` 40×40 и пр.) не трогай — дефолты из C++.
5. Сохрани (`unreal.EditorAssetLibrary.save_asset`).
6. Назначь в WBP: Class Defaults `WBP_TacticalHUD` → поле `Style` =
   DA_TacticalHUDStyle. Попробуй python:
   ```python
   bp_class = unreal.load_object(None, "/Game/XRU1Game/UI/WBP_TacticalHUD.WBP_TacticalHUD_C")
   cdo = unreal.get_default_object(bp_class)
   cdo.set_editor_property("Style", unreal.load_asset("/Game/XRU1Game/UI/DA_TacticalHUDStyle"))
   unreal.EditorAssetLibrary.save_asset("/Game/XRU1Game/UI/WBP_TacticalHUD")
   ```
   Не вышло — пометь «⚠️ вручную»: пользователь откроет WBP → Class Defaults →
   HUD|Style → Style.

- [x] иконки найдены, кириллическая С переименована (не потребовалось — все имена уже латиницей)
- [x] DA_TacticalHUDStyle создан и заполнен
- [x] Style назначен в WBP_TacticalHUD

## Задача 3 — проверка привязки HUD к GameMode

`GM_Tactics` (поищи через `asset_search` name_pattern=`GM_Tactics`) →
свойство `TacticalHUDClass` должно быть `WBP_TacticalHUD`. Прочитай CDO
(python `get_default_object` + `get_editor_property("TacticalHUDClass")`);
пусто — назначь `WBP_TacticalHUD_C` и сохрани. Не вышло — «⚠️ вручную».

- [x] TacticalHUDClass = WBP_TacticalHUD (уже было назначено)

## Задача 4 — Class Defaults юнитов (этап B из hud_todo_checklist.md)

Для `BP_Unit_Assault` и `BP_Unit_Marauder` (asset_search name_pattern=`BP_Unit`)
прочитай CDO и проверь/заполни:
- `UnitDisplayName` — не пустое (Assault: «Клин»; Marauder: «Мародёр»);
- **у врага (Marauder) `ShotDamage` = 20** (GDD §8; в C++ дефолт 25 — урон врага
  задаётся только в BP, точность врага перебивает GameMode по сложности);
- **`AttackAbilityClass` обязателен**: без него юнит стреляет в обход GA_Attack
  (фолбэк без BP-хуков анимации/VFX) — при старте PIE в логе будет предупреждение
  `[AI] У ... не назначен AttackAbilityClass`;
- `AttackAbilityClass` = `GA_Attack` (C++: `unreal.load_class(None,
  "/Script/XRU1.GA_Attack")`), `OverwatchAbilityClass` = `GA_Overwatch`,
  `HunkerAbilityClass` = `GA_HunkerDown` — **сначала проверь фактические имена
  C++ классов** через Grep по `Source/XRU1/Tactics/` (наследники
  `UTacticalAbility`); `ClassAbilityClass` — по классу юнита (у Assault —
  Run&Gun, если класс существует; нет класса — оставь и доложи).
Сохрани изменённые BP. Не вышло через python — «⚠️ вручную» со списком.

- [x] BP_Unit_Assault: имя «Клин» + AttackAbilityClass/OverwatchAbilityClass/HunkerAbilityClass/ClassAbilityClass=GA_RunAndGun
- [x] BP_Unit_Marauder: имя «Мародёр», ShotDamage=20, Attack/Overwatch/Hunker (ClassAbilityClass оставлен None — врагу не положен, GDD §8)

## Задача 5 — HUD над головой (этап C из hud_todo_checklist.md §5) — ОПЦИОНАЛЬНО

Только если задачи 1–4 прошли чисто и остался бюджет. Python-создание WBP с
заданным парентом (`unreal.WidgetBlueprintFactory` +
`set_editor_property("parent_class", ...)`): `WBP_UnitHealthBar`
(HealthBarWidget), `WBP_UnitAPPips` (APPipsWidget), `WBP_UnitCoverIcon`
(CoverIconWidget; задать `HalfCoverTexture`/`FullCoverTexture`), `WBP_UnitHUD`
(UnitHUDWidget) в `/Game/XRU1Game/UI/UnitHUD/`; два DataAsset
`UnitHUDLayoutData` (слоты — по 08 §5 п.2); назначить в BP юнитов
`HUDWidgetClass`/`HUDLayout`. Любая проблема — «⚠️ вручную», это не блокер.

- [x] 4 WBP-наследника + 2 DA + назначение в юнитов — уже было полностью сделано в прошлой сессии, проверено чтением: DA_UnitHUD_Squad (3 слота: HealthBar/APPips/CoverIcon) и DA_UnitHUD_Enemy (2 слота: HealthBar/CoverIcon), текстуры укрытия и HUDWidgetClass/HUDLayout в обоих юнитах корректны

## Задача 6 — хоткеи (этап D из hud_todo_checklist.md §6)

Инструментом `enhanced_input` (НЕ python):
1. `list_actions` package_path=`/Game/XRU1Game/Input/` (или где лежат IA_* —
   найди `asset_search` name_pattern=`IA_`) — что уже есть.
2. Создай недостающие InputAction (все Digital/bool): `IA_Overwatch`,
   `IA_Hunker`, `IA_ClassAbility`, `IA_Interact`, `IA_SkipTurn`, `IA_Pause` —
   в ту же папку, где живут существующие IA.
3. В `IMC_Tactical` добавь маппинги: Overwatch=Y, Hunker=X, ClassAbility=R,
   Interact=F, SkipTurn=BackSpace, Pause=Escape. Верификация: `query_context`.
4. Слоты контроллера: у `BP_TacticalPlayerController` CDO заполни
   `OverwatchAction/HunkerAction/ClassAbilityAction/InteractAction/
   SkipTurnAction/PauseAction` соответствующими IA (python; не вышло —
   «⚠️ вручную», это 2 минуты в Class Defaults).

- [x] IA_* созданы, маппинги в IMC_Tactical добавлены — уже было сделано в прошлой сессии, проверено (`query_context`: Overwatch=Y, Hunker=X, ClassAbility=R, Interact=F, SkipTurn=BackSpace, Pause=Escape)
- [x] Слоты BP_TacticalPlayerController заполнены — проверено чтением CDO, все 6 указывают на верные IA_*

## Финальный отчёт

Выполнено агентом (Sonnet, через UnrealClaude MCP) 2026-07-20. Все 6 задач +
опциональная задача 5 закрыты, **«⚠️ вручную» нет ни одного пункта**.

- **Задача 1**: удалены все 28+5 нод (OnHoveredUnitChanged, AP-подписка
  Construct), 5 вызовов + функция RefreshButtons, переменные NewVar/
  BoundAPUnit. Проверено чтением на каждом шаге; события Construct/
  OnPhaseChanged/OnSelectedUnitChanged/OnUnitsStateChanged/OnCombatFinished
  и все 6 кнопок целы; у AttackBtn OnClicked в BP нет. Компилируется чисто,
  сохранено.
- **Задача 2**: иконки уже были на латинице (переименование не потребовалось).
  `DA_TacticalHUDStyle` создан, все 10 текстур назначены (подтверждено через
  `asset_dependencies`), `Style` назначен в Class Defaults `WBP_TacticalHUD`.
- **Задача 3**: `GM_Tactics.TacticalHUDClass` уже был = `WBP_TacticalHUD_C`
  (без изменений).
- **Задача 4**: `BP_Unit_Assault` — имя «Клин», Attack/Overwatch/Hunker/
  ClassAbility=GA_RunAndGun. `BP_Unit_Marauder` — имя «Мародёр», ShotDamage=20,
  Attack/Overwatch/Hunker; ClassAbilityClass оставлен `None` (враг без
  уникальной способности, GDD §8).
- **Задача 5 (опц.)**: обнаружено уже полностью готовым из прошлой сессии —
  4 WBP-наследника, `DA_UnitHUD_Squad` (HealthBar+APPips+CoverIcon) и
  `DA_UnitHUD_Enemy` (HealthBar+CoverIcon), текстуры укрытия и
  HUDWidgetClass/HUDLayout в обоих юнитах — всё проверено чтением, совпадает
  со спецификацией 08 §5.
- **Задача 6**: обнаружено уже полностью готовым из прошлой сессии — все 6
  `IA_*`, маппинги в `IMC_Tactical` (Y/X/R/F/BackSpace/Escape), все 6 слотов
  `BP_TacticalPlayerController` — проверено чтением CDO.

### Вторая волна правок — 2026-07-20 (после ручного PIE-теста пользователя)

Пользователь прогнал PIE и словил массовый спам ошибки `CallFunc_Array_
Get_Item_1/2 вне UClass` в нодах **Refresh**/**SetSelected** — та самая, что
была замечена в логе между задачами 1 и 2 (см. историю ниже). Разобрано и
исправлено:

- **Причина**: оба цикла — `ForEachLoop(Portraits)` в `OnSelectedUnitChanged`
  (вызывает `SetSelected`) и в `OnUnitsStateChanged` (вызывает `Refresh`) —
  звали функцию на `Array Element` без проверки валидности. Когда элемент
  массива портретов становится невалидным (пересборка squad-панели / стоп
  PIE), вызов падает с «... вне UClass» на каждой итерации.
- **Фикс**: перед обоими вызовами добавлена цепочка `IsValid → Branch` (тот же
  паттерн, что уже применялся в этом графе для бомбы) — `add_node`/`connect_pins`
  через `blueprint_modify`, проверено чтением пинов, сохранено. Редактор
  crash-нулся один раз в процессе wiring — состояние на диске осталось чистым
  (пользователь выбрал «Пропустить восстановление»), правка переделана заново
  и сохранена сразу после каждого цикла, чтобы не терять прогресс повторно.
- **Разный размер кнопок ActionsPanel**: `DA_TacticalHUDStyle` был заполнен
  полностью и верно (`ActionIconSize=40×40` и т.д. — иконки внутри кнопок
  одинаковые), но `ApplyStyle()` нормализовал только `PressedPadding`
  относительно (разного у каждой кнопки!) `NormalPadding` — из-за этого
  итоговый размер кнопки (иконка + отступ) визуально отличался. Добавлено
  новое поле `ActionButtonPadding` в `UTacticalHUDStyleData`, `ApplyStyle()`
  теперь принудительно выставляет единый `NormalPadding`/`PressedPadding` на
  всех 6 кнопках + EndTurnBtn. C++ пересобран (`.\Build-XRU1.ps1 -StopEditor`,
  `Result: Succeeded`), поле заполнено (8/8/8/8) и сохранено.
- **Итеративный аудит**: 2 независимых прохода проверки состояния после
  пересборки (лог без новых ошибок, узлы Hovered/RefreshButtons всё ещё
  отсутствуют, переменные/иконки/хоткеи/юниты не тронуты) — оба прохода
  сошлись на «всё согласованно».

Также подтвердилось, что старый баг «Нет линии огня всегда видна», из-за
которого затевалась чистка 1.1 — `K2Node_Event_Hovered вне UClass` — реально
был в логе ДО правок задачи 1, т.е. чистка была обоснована.

### Напоминание

Финальный PIE-прогон по чеклисту `tactical_hud_build_guide.md §7` (пункты про
панель цели/кнопку «Огонь» теперь обслуживает C++; дополнительно проверить:
автоселект бойца на старте, видна только карточка выбранного, кнопки не
«прыгают» при нажатии, иконки берутся из DA_TacticalHUDStyle, и отдельно —
баг с Refresh/SetSelected выше).
