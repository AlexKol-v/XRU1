# 08 — Боевой HUD: пошаговый чеклист для редактора

> **⚠️ АРХИВ (2026-07-21): этап 6 закрыт.** Сборка HUD выполнена, чеклист исторический. Актуальный статус — `12_GAMEPLAY_STATUS.md`.


> Рабочий план этапа 6 (+ HUD над головой из этапа 4). Вся логика уже в C++
> (2026-07-17) — здесь только сборка WBP/ассетов в редакторе. По завершении
> шага — ставь чекбокс; когда всё готово, проставить чекбоксы в `04_ROADMAP.md`
> и удалить этот файл (или перенести остатки в беклог).
>
> Меню (главное/пауза/настройки) — ОТДЕЛЬНЫЙ этап, здесь только минимум,
> без которого HUD не работает (WBP_PrimaryGameLayout).

---

## 0. Что уже сделано в C++ (справка, ничего делать не надо)

| Для чего | Что использовать из C++ |
|---|---|
| Индикатор фазы/хода/таймера | хук `OnPhaseChanged(Phase, TurnNumber, TurnsRemaining)`; TurnsRemaining = -1 → таймера нет, спрятать |
| Рамка выбранного портрета | хук `OnSelectedUnitChanged(Selected)` |
| Панель цели у курсора | хук `OnHoveredUnitChanged(Hovered)` + `GetHitChanceOnTarget(Hovered)` (-1 = стрелять нельзя) |
| Портреты/счётчик врагов | хук `OnUnitsStateChanged()` (стреляет при смерти/ранении/эвакуации ЛЮБОГО юнита и один раз на старте) + `GetSquad()`, `GetAliveEnemyCount()`. ⚠️ Обновлять ПО СОБЫТИЯМ, не биндить Pure-геттеры на тик UMG (то же для `GetAvailableInteraction`/`GetAbilityUsesRemaining` — они не для покадрового опроса) |
| Конец боя | хук `OnCombatFinished(bPlayerWon)` |
| Кнопки действий | `GetTacticalController()` → `RequestOverwatch / RequestHunkerDown / RequestClassAbility / RequestInteract / RequestSkipUnitTurn / RequestEndTurn`, выбор — `SelectUnit(Unit)` |
| Текст/серость кнопки F | `GetAvailableInteraction()` → None / DefuseBomb («Обезвредить (F)») / Evacuate («Эвакуация (F)») |
| Серость кнопок по AP | у юнита `GetActionPoints()` → `CanSpend(1)` + событие `OnActionPointsChanged` |
| Лимит способности (Run&Gun 1/бой) | `Unit->GetAbilityUsesRemaining(Unit->ClassAbilityClass)` (-1 = без лимита) |
| Имя на портрете | `Unit->UnitDisplayName` (задать в BP юнитов: Шприц/Оса/Клин/Молот) |
| Статусы юнита | `IsDowned()/IsEvacuated()/IsDead()`; теги ASC: `State.Overwatch`, `State.HunkeredDown`, `State.Taunting` (нода Has Matching Gameplay Tag) |
| HP на портрете | `Unit->GetHealth()` / `GetMaxHealth()` |
| Пипсы AP над головой | новый виджет C++ `UAPPipsWidget` (сам рисует, подписан на AP) |
| Иконка укрытия над головой | новый виджет C++ `UCoverIconWidget` (текстуры — в BP-наследнике) |
| Прогресс бомбы | `ABombObjective` уровня: `OnDefuseProgress(Current, Required)`, `OnDisarmed`, `GetDefuseProgress()` (найти нодой Get Actor Of Class в Construct) |

Тексты — из `02_LORE_SCRIPT.md §7`: «ВАШ ХОД» / «ХОД ПРОТИВНИКА», «Ход N»,
«До подрыва: N ходов», «Обезвреживание: X/2», «Наблюдение», «Оборона»,
«Тяжело ранен», «ЭВАКУИРОВАН», «Попадание: N%», «Ход противника — ждите».

---

## 1. Иконки (сгенерировать/скачать ДО сборки виджетов)

Список глифов, стиль и промт-обёртка — **`07_CONTENT_PROMPTS.md §5`**
(256×256, PNG-alpha, off-white + cyan glow; туда же добавлены «Завершить ход»
и «Счётчик врагов»). Для HUD минимально нужны: Overwatch, Глухая оборона,
Полевая медицина, Рывок и удар, Провокация, Пропустить ход, Обезвредить,
Эвакуация, Half/Full cover, Downed, Завершить ход, Счётчик врагов (череп).

Варианты получения (в порядке скорости):
- **Сгенерировать нейросетью** по промтам из 07 §5 — единый стиль, рекомендую;
- **game-icons.net** — тысячи плоских белых SVG (CC-BY 3.0: строчка авторства
  в титрах/отчёте); на сайте задать белый цвет + прозрачный фон, скачать PNG;
- **Kenney.nl** («Game Icons», CC0) или **Fab** (поиск "flat icons UI",
  бесплатные) — если генерация недоступна.

Импорт: PNG → `Content/XRU1Game/UI/Icons/T_Icon_<Name>`; в настройках текстуры
**Texture Group = UI**, Compression Settings = **UserInterface2D**.

- [x] Иконки получены и импортированы (2026-07-17; ⚠️ осталось: переименовать
  кириллическую С в `T_Icon_*_Сover` и ужать текстуры — см. 09 §0)

---

## 2. Каркас UI (без него GameMode не запушит HUD)

`Content/XRU1Game/UI/` → Widget Blueprint **WBP_PrimaryGameLayout**,
Parent Class = `PrimaryGameLayout` (наш C++). Иерархия (05_EDITOR_GUIDE §3.2):

```
Overlay (root)
 ├─ CommonActivatableWidgetStack  «GameStack»      (Fill/Fill)
 ├─ CommonActivatableWidgetStack  «GameMenuStack»  (Fill/Fill)
 ├─ CommonActivatableWidgetStack  «MenuStack»      (Fill/Fill)
 └─ CommonActivatableWidgetStack  «ModalStack»     (Fill/Fill)
```

⚠️ Имена стеков — **в точности** как выше (BindWidget, иначе компиляция WBP упадёт).

- [x] WBP_PrimaryGameLayout создан, компилируется (2026-07-17)
- [x] `BP_TacticalPlayerController` → `RootLayoutClass = WBP_PrimaryGameLayout`
      (PauseMenuClass пока пустой — меню делаем позже, Esc просто не сработает)

---

## 3. Под-виджет портрета WBP_UnitPortrait

`Content/XRU1Game/UI/` → WBP, Parent = `CommonUserWidget`.
Переменная **Unit** (тип `Unit Base`, Instance Editable + Expose on Spawn).

Иерархия:
```
SizeBox (~160×90)
 └─ Button «SelectButton» (или CommonButtonBase)
     └─ VerticalBox
         ├─ HorizontalBox
         │   ├─ TextBlock «NameText»        ← Unit.UnitDisplayName
         │   └─ Image     «StatusIcon»      ← см. логику ниже
         ├─ ProgressBar   «HPBar»           ← Unit.GetHealth / Unit.GetMaxHealth
         └─ HorizontalBox «APRow»
             ├─ Image «Pip1»                ← цвет по Unit.GetActionPoints
             └─ Image «Pip2»
 (поверх всего) Border/Image «SelectionFrame» — рамка, видима только у выбранного
```

Логика (Event Construct):
- `SelectButton.OnClicked` → `GetOwningPlayer` → Cast to TacticalPlayerController → `SelectUnit(Unit)`;
- подписаться: `Unit.GetActionPoints().OnActionPointsChanged` → перекрасить пипсы
  (Current >= 1 → Pip1 белый, иначе тёмный; Current >= 2 → Pip2);
- функция **Refresh** (зовёт и родитель по OnUnitsStateChanged):
  HPBar percent = GetHealth/GetMaxHealth; StatusIcon:
  `IsEvacuated` → T_Icon_Evac; `IsDowned` → T_Icon_Downed;
  тег `State.Overwatch` → T_Icon_Overwatch; тег `State.HunkeredDown` → T_Icon_Hunker;
  иначе скрыть. Мёртвый юнит — весь портрет Desaturate/затемнить;
- публичная функция **SetSelected(bool)** — видимость SelectionFrame.

- [x] WBP_UnitPortrait готов (2026-07-17: разметка + граф Refresh/SetSelected/
  клик-выбор/подписка на AP — проверено по скриншотам)

## 4. Главный виджет WBP_TacticalHUD

> 📘 **Подробный пошаговый гайд со всеми якорями/размерами/узлами графа —
> `09_TACTICAL_HUD_GUIDE.md`.** Ниже — краткая выжимка для сверки.

`Content/XRU1Game/UI/` → WBP, **Parent = Tactical HUD Widget** (наш C++).
Обязательных BindWidget-имён нет — имена ниже рекомендованные.

Иерархия (Canvas, full screen):
```
CanvasPanel
 ├─ [верх-центр]  VerticalBox «PhasePanel»
 │    ├─ TextBlock «PhaseText»      («ВАШ ХОД» / «ХОД ПРОТИВНИКА»)
 │    ├─ TextBlock «TurnText»       («Ход 3»)
 │    ├─ TextBlock «BombTimerText»  («До подрыва: 8 ходов», Hidden без таймера)
 │    └─ TextBlock «ObjectiveText»  («Обезвреживание: 1/2», Hidden без бомбы)
 ├─ [верх-право]  HorizontalBox: Image(T_Icon_Skull) + TextBlock «EnemyCountText»
 ├─ [низ-лево]    HorizontalBox «SquadPanel» (портреты добавляются в Construct)
 ├─ [низ-центр]   HorizontalBox «ActionsPanel»
 │    ├─ Button «OverwatchBtn»  (T_Icon_Overwatch, tooltip «Наблюдение (Y)»)
 │    ├─ Button «HunkerBtn»     (T_Icon_Hunker,   «Глухая оборона (X)»)
 │    ├─ Button «AbilityBtn»    (иконка по роли юнита, «Способность (R)»)
 │    ├─ Button «InteractBtn»   (T_Icon_Defuse/Evac, «Обезвредить/Эвакуация (F)»)
 │    └─ Button «SkipBtn»       (T_Icon_Skip, «Пропустить ход (Backspace)»)
 ├─ [низ-право]   Button «EndTurnBtn» («Завершить ход (Enter)», T_Icon_EndTurn)
 ├─ [центр-право] VerticalBox «TargetPanel» (Hidden по умолчанию)
 │    ├─ TextBlock «TargetNameText»
 │    ├─ ProgressBar «TargetHPBar»
 │    └─ TextBlock «HitChanceText» («Попадание: 65%»)
 └─ [центр]       TextBlock «EnemyPhaseHint» («Ход противника — ждите», Hidden)
```

Привязка событий (Event Graph):
- [x] **Event Construct** (2026-07-17): `GetSquad()` → ForEach → Create
  WBP_UnitPortrait(Unit) → Add Child to SquadPanel → Add в массив Portraits;
  Get Actor Of Class (BombObjective) → Is Valid → Bind к `OnDefuseProgress`
  (обновить ObjectiveText) и `OnDisarmed` (ObjectiveText = «Эвакуировать
  отряд») — см. подробности и разбор частой ошибки (провод мимо exec-пина
  `Get Actor Of Class`) в 09 §3;
- [x] **Event OnPhaseChanged** (2026-07-17): PhaseText/TurnText; BombTimerText
  виден при TurnsRemaining >= 0; фаза Enemy → ActionsPanel disabled +
  EnemyPhaseHint видим; фаза Player → наоборот; после смены фазы вызывает
  RefreshButtons — см. подробный разбор 6 шагов в 09 §4.1;
- [x] **Event OnSelectedUnitChanged** (2026-07-17): у всех портретов SetSelected(портрет.Unit ==
  Selected); RefreshButtons;
- [x] **Event OnHoveredUnitChanged** (2026-07-17): Hovered валиден и это враг
  (TeamId 2 / не в GetSquad) → TargetPanel показать: имя, HP-бар,
  `GetHitChanceOnTarget` (>= 0 → «Попадание: N%», иначе «Нет линии огня»);
  nullptr/свой → спрятать; подробный разбор — 09 §4.3;
- [x] **Event OnUnitsStateChanged** (2026-07-17): ForEach портретов → Refresh;
  EnemyCountText = `GetAliveEnemyCount()`; RefreshButtons;
- [x] **Функция RefreshButtons** (2026-07-17, подробный разбор — 09 §5):
  S = GetTacticalController().GetSelectedUnit();
  все кнопки disabled если S невалиден/IsDowned/фаза не Player; иначе
  enabled = S.GetActionPoints().CanSpend(1); AbilityBtn дополнительно:
  `S.GetAbilityUsesRemaining(S.ClassAbilityClass) == 0` → disabled;
  InteractBtn: `GetAvailableInteraction()` → None = disabled, иначе
  иконка/тултип по виду; EndTurnBtn активна всю фазу игрока;
  ~~подписаться на OnActionPointsChanged выбранного юнита, переподписка при
  смене выбора~~ → упрощено: все юниты отряда подписаны на один общий AP-
  делегат ещё в Construct (см. 09 §3), RefreshButtons просто читает свежие
  значения по вызову;
- [x] **Event OnCombatFinished** (2026-07-17): спрятать ActionsPanel/
  EndTurnBtn/TargetPanel/EnemyPhaseHint (экран результата покажет GameMode сам);
- [x] Кнопки → соответствующие Request*-методы контроллера (2026-07-17,
  все 6: Overwatch/Hunker/Ability/Interact/Skip/EndTurn).

**Правки по аудиту 2026-07-18** (Designer-часть внесена агентом):

- [x] Designer: **AttackBtn** создана (первая в ActionsPanel, T_Icon_Attack,
  tooltip) и **TargetCoverIcon** создана (в TargetPanel после имени, Collapsed)
  — агент, 2026-07-18.

**⚡ Пересмотр 2026-07-20 — хрупкая логика перенесена в C++** (см.
03_CODE_OVERVIEW §2.2, `UTacticalHUDWidget`): серость кнопок
(`RefreshActionButtons`), панель цели (`UpdateTargetPanel` — этим же починен
баг «Нет линии огня всегда видна»: BP-цепочка врага висела на выходе ноды
«спрятать»), карточка только выбранного бойца (`UpdateSquadCardVisibility`),
клик AttackBtn → `RequestAttack` (подписка из C++ — **OnClicked в BP для
AttackBtn НЕ добавлять**), стиль/размеры/иконки из DataAsset
`UTacticalHUDStyleData` (+`bAutoSelectUnits` в контроллере). Ручные шаги
2а.0/2а/5а из 09 §4.3 и врезки в RefreshButtons **больше не нужны**. Осталось:

- [x] Пересборка C++ (`.\Build-XRU1.ps1`, редактор закрыт);
- [x] **Чистка WBP-графа от перенесённого** (агент через UnrealClaude MCP,
  готовый план с GUID нод — `docs/10_SONNET_MCP_TASKS.md`, выполнено 2026-07-20):
  удалён обработчик OnHoveredUnitChanged, все вызовы и сама функция
  RefreshButtons, AP-подписка в Construct, переменные `NewVar`/`BoundAPUnit`;
- [x] **DA_TacticalHUDStyle** создан (Data Asset → TacticalHUDStyleData),
  иконки/размеры заполнены, назначен в Class Defaults WBP_TacticalHUD → Style;
- [x] Прочее из C++-фиксов проверяется само в PIE: клик по мёртвому портрету
  не выбирает, смерть/эвакуация выбранного снимает выбор, Tab пропускает
  мёртвых, автоселект бойца в начале фазы, карточка только выбранного.

- [x] WBP_TacticalHUD готов и компилируется
- [x] `GM_Tactics` → `TacticalHUDClass = WBP_TacticalHUD`

**Правки по аудиту 2026-07-20 (вторая волна, после PIE-теста пользователя):**

- [x] **Краш-баг**: `ForEachLoop(Portraits)` в `OnSelectedUnitChanged`/
  `OnUnitsStateChanged` звал `SetSelected`/`Refresh` на элементе массива без
  проверки — при невалидном портрете (стоп PIE, пересборка панели) сыпал
  ошибку `... вне UClass` на каждой итерации. Добавлен `IsValid → Branch`
  перед обоими вызовами (тот же паттерн, что уже используется для бомбы).
- [x] **Разный размер кнопок ActionsPanel**: добавлено поле `ActionButtonPadding`
  в `UTacticalHUDStyleData` (`Source/XRU1/UI/TacticalHUDStyleData.h`),
  `ApplyStyle()` теперь принудительно выставляет единый `NormalPadding`/
  `PressedPadding` на всех 6 кнопках ActionsPanel + EndTurnBtn — раньше
  унифицировался только `PressedPadding` относительно (разного!) `NormalPadding`
  каждой кнопки, отсюда и визуальный разнобой при одинаковых иконках.
  Пересобрано, поле заполнено в `DA_TacticalHUDStyle` (8/8/8/8 — дефолт).

## 5. HUD над головой юнита

1. **WBP-наследники** (в `Content/XRU1Game/UI/UnitHUD/`), разметку НЕ рисовать —
   дерево строит C++, WBP нужен только чтобы задать свойства класса:
   - `WBP_UnitHealthBar` ← Parent = `Health Bar Widget` (стиль/цвета по вкусу);
   - `WBP_UnitAPPips`    ← Parent = `AP Pips Widget` (SpentColor уже норм);
   - `WBP_UnitCoverIcon` ← Parent = `Cover Icon Widget` → задать
     `HalfCoverTexture = T_Icon_CoverHalf`, `FullCoverTexture = T_Icon_CoverFull`;
   - `WBP_UnitHUD`       ← Parent = `Unit HUD Widget` (контейнер, пустой).
2. **DataAsset'ы** (ПКМ → Miscellaneous → Data Asset → `UnitHUDLayoutData`):
   - `DA_UnitHUD_Squad`: DrawSize ≈ (200, 52);
     слот V0/H0 = WBP_UnitHealthBar, Size (120, 12), Color красный;
     слот V1/H0 = WBP_UnitAPPips,   Size (60, 8),  Color жёлто-белый;
     слот V1/H1 = WBP_UnitCoverIcon, Size (16, 16), Color белый;
   - `DA_UnitHUD_Enemy`: то же без слота AP (HP + укрытие).
3. **В BP юнитов** (BP_Unit_Assault, BP_Unit_Marauder, будущие):
   `HUDWidgetClass = WBP_UnitHUD`; `HUDLayout = DA_UnitHUD_Squad` (врагу — Enemy).

- [x] HP/AP/укрытие видны над головами в PIE — обнаружено уже полностью
  собранным (4 WBP + `DA_UnitHUD_Squad`/`DA_UnitHUD_Enemy` + назначение в
  BP_Unit_Assault/Marauder), проверено чтением 2026-07-20

## 6. Добить ввод (хоткеи из GDD §11)

`Content/XRU1Game/Input/`: создать недостающие Input Actions (все Digital bool):
`IA_Overwatch` (Y), `IA_Hunker` (X), `IA_ClassAbility` (R), `IA_Interact` (F),
`IA_SkipTurn` (Backspace), `IA_Pause` (Esc). Добавить маппинги в `IMC_Tactical`
и заполнить соответствующие слоты в `BP_TacticalPlayerController`.

- [x] Хоткеи работают в PIE — IA_*/маппинги IMC_Tactical/слоты контроллера
  обнаружены уже готовыми, проверено чтением 2026-07-20

## 7. Проверка (DoD этапа 6) и синхронизация доков

На Lvl_TopDown, только мышью: выбор через портреты → приказ → пипсы серые →
Overwatch/Оборона с кнопок → наведение на врага показывает шанс → «Завершить
ход» → фаза врага блокирует панель → смерть врага уменьшает счётчик.

- [ ] Всё проходит мышью, без клавиатуры (нужен ручной PIE-прогон пользователя —
  агент проверил состояние ассетов/графа через MCP, но не может водить мышью в PIE)
- [x] Чекбоксы: `04_ROADMAP.md` этап 6 (WBP_TacticalHUD) и этап 4 («HUD юнита»)
- [ ] `05_EDITOR_GUIDE.md` — при расхождениях поправить
- [ ] Удалить диагностические логи `[Unit] …` в `AUnitBase` (см. заметку в ROADMAP)
- [ ] Этот файл удалить/зачистить
