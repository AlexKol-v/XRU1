# 09 — WBP_TacticalHUD: подробный гайд сборки (для новичка)

> Пошаговая инструкция этапа 6: главный боевой HUD. Виджет `WBP_TacticalHUD`
> уже создан в `Content/XRU1Game/UI/` — здесь описано, каким он должен стать:
> вся разметка с якорями/размерами и весь Event Graph узел за узлом.
> Родственные файлы: чеклист этапа — `hud_todo_checklist.md`, состав HUD — GDD §9,
> тексты — `02_LORE_SCRIPT.md §7`.

> ## ⚡ ПЕРЕСМОТР 2026-07-20 — часть графа переехала в C++
>
> Хрупкие куски WBP-логики перенесены в `UTacticalHUDWidget` (C++, через
> `BindWidgetOptional` по именам виджетов): серость кнопок
> (`RefreshActionButtons`), панель цели (`UpdateTargetPanel` — заодно починен
> баг из красного блока §4.3), карточки отряда «только выбранный»
> (`UpdateSquadCardVisibility`), клик AttackBtn → `RequestAttack` (подписан из
> C++ — **OnClicked в BP для AttackBtn НЕ добавлять**, будет двойной выстрел),
> стиль из DataAsset `UTacticalHUDStyleData` (`ApplyStyle`).
>
> Поэтому **УСТАРЕЛИ и НЕ выполняются**: Шаги 2а.0/2а/5а из §4.3, врезка
> AttackBtn в §5 п.3, строка AttackBtn в §6. Вместо них — **чистка графа от
> перенесённого** (план с GUID нод — `sonnet_mcp_hud_tasks.md`): удалить
> обработчик OnHoveredUnitChanged целиком, все вызовы и функцию RefreshButtons,
> AP-подписку в Construct (§3 п.1, подшаг «Подписка на AP» — C++ подписывается
> сам), переменные `NewVar`/`BoundAPUnit`. Разметка (§1), Construct-портреты
> (§3), OnPhaseChanged (§4.1), OnSelectedUnitChanged (§4.2), OnUnitsStateChanged
> (§4.4, без хвостового RefreshButtons), OnCombatFinished (§4.5) и клики
> остальных кнопок (§6) — остаются в BP как есть.

---

## 0. Перед стартом (5 минут наведения порядка)

- [ ] **Parent Class**: открой WBP_TacticalHUD → кнопка **Graph** → вверху
  **Class Settings** → панель Details → Class Options → **Parent Class** должен
  быть **Tactical HUD Widget**. Если там `UserWidget` — смени через выпадающий
  список (это безопасно, иерархия виджетов не потеряется).
- [ ] **Иконки укрытий**: в `UI/Icons/` файлы `T_Icon_Full_Сover` и
  `T_Icon_Half_Сover` содержат **кириллическую С** в слове Cover. ПКМ по ассету
  → Rename → перепечатай имя латиницей (`T_Icon_Full_Cover`). Иначе потом
  замучают поиск и битые референсы.
- [ ] **Сжатие иконок** (они сейчас по ~1.4 МБ): выдели все текстуры в
  `UI/Icons/` → ПКМ → Asset Actions → **Bulk Edit via Property Matrix** →
  справа найди и выставь: `Compression Settings = UserInterface2D`,
  `Texture Group = UI`, `Maximum Texture Size = 256`. Сохрани всё.

---

## 1. Разметка (вкладка Designer)

Корень — **Canvas Panel** (создаётся по умолчанию). Каждый блок ниже — прямой
ребёнок Canvas. Для каждого указано: **Anchor** (кнопка-«цветок» в Details →
пресеты), **Alignment**, **Position**, **Size** (или галочка **Size To
Content**). Позиции — стартовые, двигай по вкусу.

> Якорь и Alignment должны совпадать по смыслу: прижат к низу — якорь снизу и
> Alignment.Y = 1. Тогда HUD не разъедется на другом разрешении.

### 1.1 PhasePanel — фаза и ход (верх-центр)

`Vertical Box`, имя **PhasePanel**.
Anchor: **top-center**; Alignment `(0.5, 0)`; Position `(0, 25)`;
**Size To Content = ✔**. Дети (у всех Horizontal Alignment = Center):

| Виджет | Имя | Текст-заглушка | Font Size | Цвет |
|---|---|---|---|---|
| Text | **PhaseText** | ВАШ ХОД | 28 | белый |
| Text | **TurnText** | Ход 1 | 16 | серый (0.7) |
| Text | **BombTimerText** | До подрыва: 8 ходов | 18 | оранжевый (1, 0.5, 0.1) |
| Text | **ObjectiveText** | Обезвреживание: 0/2 | 16 | белый |

У **BombTimerText** и **ObjectiveText** сразу поставь в Details →
Behavior → **Visibility = Collapsed** (покажем из графа, когда есть бомба).

### 1.2 Счётчик врагов (верх-право)

`Horizontal Box` (имя не важно). Anchor: **top-right**; Alignment `(1, 0)`;
Position `(-30, 25)`; Size To Content = ✔. Дети:

- `Image` — Brush → Image = `T_Icon_Enemy_Count`, Image Size `(28, 28)`;
- `Text` **EnemyCountText**, заглушка «4», Font Size 22, отступ слева
  (Padding Left = 6).

### 1.3 SquadPanel — портреты (низ-лево)

`Horizontal Box`, имя **SquadPanel**. Anchor: **bottom-left**; Alignment
`(0, 1)`; Position `(30, -30)`; Size To Content = ✔. Детей в дизайнере НЕ
добавляем — портреты создаст Event Construct. (Чтобы увидеть вид в дизайнере,
можно временно накидать WBP_UnitPortrait и удалить перед сохранением.)

### 1.4 ActionsPanel — кнопки действий (низ-центр)

`Horizontal Box`, имя **ActionsPanel**. Anchor: **bottom-center**; Alignment
`(0.5, 1)`; Position `(0, -30)`; Size To Content = ✔.

Шесть кнопок, каждая устроена одинаково:

> ✅ **AttackBtn уже создана агентом через UnrealClaude (2026-07-18):** первая
> в ActionsPanel, внутри Image «AttackIcon» (T_Icon_Attack, 40×40), tooltip
> задан. В Designer её создавать НЕ нужно — осталась только граф-логика
> (§5 п.3 и §6).

```
Button «AttackBtn»   (Padding между кнопками: слот HBox → Padding = 4)
 └─ Image  (Brush → Image = иконка, Image Size = (40, 40))
```

| Кнопка | Иконка | Tool Tip Text (Details → Behavior) |
|---|---|---|
| **AttackBtn** (первая слева) | T_Icon_Attack | Огонь: выстрел по врагу (1 AP, завершает ход бойца) |
| **OverwatchBtn** | T_Icon_Overwatch | Наблюдение (Y) |
| **HunkerBtn** | T_Icon_Deaf_Defense | Глухая оборона (X) |
| **AbilityBtn** | T_Icon_Field_Medicine* | Способность класса (R) |
| **InteractBtn** | T_Icon_Defuse | Обезвредить / Эвакуация (F) |
| **SkipBtn** | T_Icon_Pass | Пропустить ход (Backspace) |

\* иконка AbilityBtn статичная — заглушка; смена по роли юнита — полировка потом.

У **InteractBtn** внутренней картинке дай имя **InteractIcon** (иконку будем
менять из графа: обезвредить/эвакуация).

### 1.5 EndTurnBtn (низ-право)

`Button`, имя **EndTurnBtn**. Anchor: **bottom-right**; Alignment `(1, 1)`;
Position `(-30, -30)`; Size To Content = ✔. Внутри — HBox: `Image`
(T_Icon_End_Turn, 32×32) + `Text` «Завершить ход», Font Size 18.
Tool Tip Text: «Завершить ход (Enter)».

### 1.6 TargetPanel — инфо о цели (центр-право)

`Vertical Box`, имя **TargetPanel**. Anchor: **right-center**; Alignment
`(1, 0.5)`; Position `(-40, 0)`; Size X = 240 (сними Size To Content, задай
размер `(240, 120)`). **Visibility = Collapsed** (по умолчанию скрыта). Дети:

| Виджет | Имя | Заглушка | Настройки |
|---|---|---|---|
| Horizontal Box | — | — | дети ниже: имя + щит цели |
| ├─ Text | **TargetNameText** | Мародёр | Font 18 |
| └─ Image | **TargetCoverIcon** | T_Icon_Half_Cover | Image Size (18, 18); Padding Left 6; Visibility Collapsed |
| Progress Bar | **TargetHPBar** | Percent 0.7 | Fill Color красный; слот VBox → Padding Top 4 |
| Text | **HitChanceText** | Попадание: 65% | Font 20, жёлтый (1, 0.85, 0.2) |

TargetCoverIcon — иконка укрытия цели ПРОТИВ выбранного стрелка (как в XCOM 2):
щит виден только когда укрытие реально работает; фланкирована — иконки нет.

> ✅ **TargetCoverIcon уже создана агентом через UnrealClaude (2026-07-18):**
> вторая строка TargetPanel (после имени), 28×28 (брашь скопирован с иконки
> счётчика врагов), Visibility = Collapsed. В Designer создавать НЕ нужно —
> осталась только граф-логика (Шаг 5а в §4.3). Иерархия фактически проще, чем
> в таблице выше: не HBox с именем, а отдельная строка VBox — так и оставить.

### 1.7 EnemyPhaseHint (центр)

`Text`, имя **EnemyPhaseHint**, «Ход противника — ждите», Font Size 26, серый.
Anchor: **center**; Alignment `(0.5, 0.5)`; Position `(0, 200)` (чуть ниже
центра, чтобы не закрывать бой). **Visibility = Collapsed**.

### 1.8 Галочки Is Variable

Пройдись по дереву и включи **Is Variable** (Details, самый верх) у:
`PhaseText`, `TurnText`, `BombTimerText`, `ObjectiveText`, `EnemyCountText`,
`SquadPanel`, `ActionsPanel`, `InteractIcon`, `TargetPanel`, `TargetNameText`,
`TargetCoverIcon`, `TargetHPBar`, `HitChanceText`, `EnemyPhaseHint`,
`EndTurnBtn`. (Кнопки и так переменные по умолчанию.)

---

## 2. Переменные графа (My Blueprint → Variables → +)

| Имя | Тип | Зачем |
|---|---|---|
| **Portraits** | `WBP Unit Portrait` → **Object Reference**, затем в типе кликни иконку и выбери **Array** | список созданных портретов для Refresh/SetSelected |
| **Bomb** | `Bomb Objective` → Object Reference | ссылка на бомбу уровня (если есть) |

> ~~BoundAPUnit~~ — в первой версии гайда тут была третья переменная для
> ручной переподписки на AP при смене выбора. Оказалось проще подписать
> **все 4 юнита сразу на старте** (см. §3, доп. шаг в цикле портретов) — тогда
> переподписка вообще не нужна. Если уже создал `BoundAPUnit` — можешь смело
> удалить, она больше нигде не используется.
>
> 🧹 Аудит 2026-07-18: в живом WBP лежат **`BoundAPUnit`** и случайная
> **`NewVar`** — обе не используются (проверено find_references), удалить
> обе: My Blueprint → ПКМ по переменной → Delete.

---

## 3. Event Construct — старт HUD

HUD создаётся ПОСЛЕ StartCombat (пушит GameMode), поэтому отряд уже известен.

Цепочка от `Event Construct` (все узлы последовательно по exec):

**⚠️ Ключевое правило ниже:** ноду для добавления/подписки всегда ищи, потянув
провод **от переменной-владельца** (`SquadPanel`, `Bomb`), а НЕ от результата
`Create Widget` — иначе нужной функции просто не будет в списке поиска (это
функция панели/бомбы, а не создаваемого виджета).

1. **Портреты.** `Get Squad` (функция нашей C++ базы — ПКМ по холсту, набери
   «Get Squad») → из массива тяни **For Each Loop**. В Loop Body (цепочка по
   экзеку, один за другим):
   - `Create Widget` → Class = **WBP Unit Portrait**; появившийся пин **Unit**
     (он есть благодаря Expose on Spawn) соедини с `Array Element`;
     Owning Player можно не заполнять;
   - перетащи переменную **SquadPanel** на холст → **Get SquadPanel** → от
     ЭТОЙ ноды (не от Create Widget!) потяни провод и найди в поиске
     `Add Child To Horizontal Box` (Target подставится сам = SquadPanel); в
     её вход **Content** подай `Return Value` от `Create Widget`;
   - перетащи переменную **Portraits** → **Get Portraits** → от неё найди
     узел `Add`; в `New Item` подай тот же `Return Value` от `Create Widget`.
   - **Подписка на AP этого юнита** (новый шаг, добавь В КОНЕЦ цепочки Loop
     Body, после `Add`): от **`Array Element`** (НЕ от Create Widget!) потяни
     провод → в поиске `Get Action Points` → поставь. От её `Return Value`
     потяни провод → в поиске напиши `On Action Points Changed`:
     - **на первой итерации мысленно** (на графе это ОДИН узел — неважно,
       сколько раз цикл прогонит его в игре) выбери **Assign On Action
       Points Changed**. Появится `Bind Event to On Action Points Changed`
       (Target уже = `Get Action Points`) и рядом красный Custom Event —
       назови его понятно (ПКМ по заголовку → Rename) в **OnSquadAPChanged**.
     - в теле `OnSquadAPChanged` (пины `NewCurrent`/`Max` не трогай, они не
       нужны) — вызови функцию **RefreshButtons** (создай пустую заглушку
       сейчас же: My Blueprint → Functions → **+** → `RefreshButtons`, без
       входов — тело заполним в §5).
     - Экзек: `Add` (Portraits) → `Bind Event to On Action Points Changed`
       (это и есть последний узел цикла).

   Так все 4 юнита отряда сразу подписаны на один и тот же `OnSquadAPChanged`
   — при трате AP ЛЮБЫМ бойцом `RefreshButtons` пересчитает кнопки текущего
   выбранного. Простая и надёжная схема — никакой переподписки при клике по
   портрету не требуется (см. §4.2 ниже, он из-за этого стал короче).
2. После Completed цикла — **бомба**: узел `Get Actor Of Class` → Actor Class =
   **Bomb Objective** → результат запиши в переменную (`Set Bomb`).

   ⚠️ **Частая ошибка**: `Get Actor Of Class` в этой версии движка — НЕ Pure-
   нода, у неё есть свои exec-пины (▷ слева и справа), и их обязательно нужно
   включить в цепочку! Если провод от `Completed` пойдёт мимо неё сразу в
   `Set Bomb` (а `Return Value` при этом всё равно подключишь в `Set Bomb`
   как значение) — нода `Get Actor Of Class` НИКОГДА не выполнится (Unreal
   покажет жёлтую плашку **WARNING!** прямо на ней), `Bomb` навсегда останется
   пустым, и вся ветка ниже (обезвреживание/эвакуация в HUD) не заработает.
   Правильно: `Completed → (exec) → Get Actor Of Class → (exec) → Set Bomb`
   — то есть сначала зайти В саму ноду, а не просто взять из неё значение.
3. `Is Valid` (Branch-версия, у неё сразу ветки Is Valid/Is Not Valid) — тяни
   от выхода `Set Bomb`:
   - **Is Valid** → показать `ObjectiveText` (`Set Visibility = Visible`) →
     затем два бинда (снова: тянуть от **Get Bomb**, не от Create Widget):
     - `Get Bomb` → провод → поиск **Assign On Defuse Progress** — создастся
       красный Custom Event с пинами `Current`, `Required`. От него:
       `Format Text` со строкой `Обезвреживание: {Current}/{Required}` (пины
       появятся сами после ввода фигурных скобок) → `Set Text` у
       **ObjectiveText**;
     - `Get Bomb` → провод → поиск **Assign On Disarmed** — в его Custom
       Event: `Set Text` ObjectiveText = «Эвакуировать отряд»;
     (узлы `Bind Event to...`, которые Assign создаёт сама, по экзеку идут
     сразу после `Set Visibility`).
   - **Is Not Valid** → ничего (ObjectiveText остаётся Collapsed).

> Начальные значения фазы/счётчика заполнятся сами: C++ база при создании HUD
> дергает OnPhaseChanged и OnUnitsStateChanged (см. ниже).

## 4. События C++ базы

Эти события объявлены в C++ (`UTacticalHUDWidget`) — в графе они добавляются
так: **ПКМ по пустому месту → набери имя события** (например «Phase Changed»)
→ выбери **Event On Phase Changed**.

### 4.1 Event On Phase Changed (Phase, TurnNumber, TurnsRemaining)

Создание события: ПКМ по пустому месту холста → в поиске напиши `Phase
Changed` → выбери **Event On Phase Changed** (красный узел с тремя выходными
пинами: `Phase`, `TurnNumber`, `TurnsRemaining` + белый exec).

Заранее подготовь функцию-заглушку **RefreshButtons** (она понадобится в конце
этого события, а её нутро заполним в §5): My Blueprint → Functions → **+** →
назови `RefreshButtons`, входов не добавляй, просто оставь пустой — Compile
это не сломает.

#### Шаг 1 — определить фазу

1. От пина **Phase** (выход события) потяни провод, отпусти в пустом месте,
   в поиске набери `Equal` — появится нода сравнения (её точное название
   зависит от версии движка, но в описании будет видно, что она сравнивает
   именно тип `ETurnPhase`; если такой не находится, ищи `==`).
2. У этой ноды два входа: `A` (уже подключён от Phase) и `B` — на пине `B`,
   если он ничем не подключён, кликни по нему один раз ЛКМ — появится
   выпадающий список значений enum'а — выбери **Player Turn**.
3. От выходного пина сравнения (bool) потяни провод → в поиске `Branch` →
   поставь. Соедини экзек: событие → `Branch`.

#### Шаг 2 — True (наш ход)

Все ноды ниже — «Get ИмяВиджета» тяни из My Blueprint (см. §1.8 — они уже
переменные), затем от Get-ноды ищи нужную функцию:

1. `Get PhaseText` → `Set Text`; в пин `In Text` впиши литералом (прямо в
   поле ноды, без проводов): `ВАШ ХОД`.
2. `Get ActionsPanel` → в поиске `Set Is Enabled` → на пине `In Enabled`
   поставь галочку (✔, это литерал bool прямо на ноде).
3. `Get EndTurnBtn` → `Set Is Enabled` → галочка ✔.
4. `Get EnemyPhaseHint` → `Set Visibility` → `Collapsed`.
5. Соедини все четыре по экзеку друг за другом (в любом порядке, главное —
   цепочкой): `Branch` (True) → 1 → 2 → 3 → 4.

#### Шаг 3 — False (ход противника)

Тот же список, другие значения:

1. `Get PhaseText` → `Set Text` = `ХОД ПРОТИВНИКА`.
2. `Get ActionsPanel` → `Set Is Enabled` → галочку **сними** (✘).
3. `Get EndTurnBtn` → `Set Is Enabled` → ✘.
4. `Get EnemyPhaseHint` → `Set Visibility` → `Visible`.
5. Цепочка: `Branch` (False) → 1 → 2 → 3 → 4.

#### Шаг 4 — номер хода (не зависит от True/False)

1. Перетащи `TurnText` → `Get TurnText`.
2. Поставь ноду `Format Text` (ПКМ, поиск `Format Text`); в поле `Format`
   впиши `Ход {TurnNumber}` — после ввода `}` у ноды сам появится новый
   входной пин с именем `TurnNumber`.
3. От пина **TurnNumber** события (то самое значение из шапки Event) потяни
   провод в этот новый пин `TurnNumber` на `Format Text`.
4. От `Result` (выход Format Text) → в `In Text` ноды `Set Text` (Target =
   Get TurnText).
5. Экзек в эту цепочку подай **из обеих веток разом**: и от последней ноды
   шага 2 (True), и от последней ноды шага 3 (False) — просто протяни от
   каждой ещё по одному белому проводу в `Format Text`. Так это выполнится
   ровно один раз, какая бы фаза ни началась.

#### Шаг 5 — таймер бомбы

1. От пина **TurnsRemaining** события потяни провод → в поиске `>=`
   (Integer >= Integer) → на пине `B` впиши `0`.
2. От результата → `Branch` (вторая, отдельная от первой).
3. Экзек в эту `Branch` подай от конца шага 4 (`Set Text` у TurnText).
4. **True** (таймер есть):
   - `Get BombTimerText` → `Format Text`, строка `До подрыва: {TurnsRemaining}
     ходов` (пин появится сам) ← подключи `TurnsRemaining` события;
   - `Result` → `Set Text` (Target = Get BombTimerText);
   - следом (по экзеку) → `Get BombTimerText` → `Set Visibility` = `Visible`.
5. **False** (таймера нет): `Get BombTimerText` → `Set Visibility` =
   `Collapsed`.

#### Шаг 6 — обновить кнопки

От концов ОБЕИХ веток шага 5 (True и False) протяни экзек в один и тот же
узел вызова функции **RefreshButtons** (перетащи её из My Blueprint —
Functions, или начни печатать её имя в поиске на холсте).

Это всё для одного события — да, узлов много, но каждый простой; после
Compile проверь, что нет ни одной красной/жёлтой пометки на нодах.

### 4.2 Event On Selected Unit Changed (Selected)

Благодаря подписке на AP всех юнитов сразу в Construct (см. §3) это событие
получилось короткое — только подсветка портретов + пересчёт кнопок.

Создание: ПКМ по холсту → в поиске `Selected Unit Changed` → **Event On
Selected Unit Changed** (один выходной пин **Selected**, тип `Unit Base`).

1. Перетащи переменную **Portraits** → **Get Portraits** → от её выхода
   потяни провод → в поиске `For Each Loop` → поставь. Экзек: событие →
   `For Each Loop`.
2. В `Loop Body`:
   - от пина **Array Element** потяни провод → в поиске напиши `Unit` →
     выбери **Get Unit** (это переменная-ссылка внутри самого портрета,
     которую ты делал в §2 гайда по WBP_UnitPortrait);
   - от выхода `Get Unit` потяни провод → в поиске `Equal` (или `==`) →
     поставь **Equal (Object)**; во второй вход (`B`) подай пин **Selected**
     из события (просто протяни провод от Selected прямо в B);
   - от результата (bool) потяни провод → в поиске `Set Selected` — появится
     НАША функция из WBP_UnitPortrait (не путай с обычным «Set» переменной);
     у неё `Target` подставь = **Array Element**, а сам bool-вход (`Selected`
     параметр функции) подключи из Equal;
   - Экзек: `Loop Body` → `Set Selected`.
3. После `Completed` цикла → вызов функции **RefreshButtons** (та же пустая
   заглушка из §3 — сейчас можно оставить пустой, заполним в §5).

### 4.3 Event On Hovered Unit Changed (Hovered)

Создание: ПКМ по холсту → в поиске `Hovered Unit Changed` → **Event On
Hovered Unit Changed** (один выход **Hovered**, тип Unit Base).

Тут НЕСКОЛЬКО ранних выходов («панель не показываем — спрятать»), которые
удобно свести в ОДНУ общую ноду `Set Visibility(TargetPanel) = Collapsed` —
сделай её один раз, а все Branch'и по exec подключишь к ней.

> ⚠️ **Семантика (аудит 2026-07-18):** панель цели показывается ТОЛЬКО при
> выбранном бойце (панель прицеливания принадлежит активному бойцу, как в
> XCOM). Без этого в начале боя (выбор пуст по дизайну) панель писала бы
> «Нет линии огня» по любому врагу — ложь: линия огня ни при чём, просто
> некому стрелять. Отсюда Шаг 2а ниже.

> 🔴 **Найденная ошибка ЖИВОГО графа (чтение через UnrealClaude, 2026-07-18):**
> вся цепочка «врага» (Шаги 3–6) сейчас подключена к **выходному exec-пину
> общей ноды `Set Visibility (Collapsed)`** из Шага 0, а ветка **«это враг»
> (нижний выход Branch'а с Contains) ПУСТАЯ**. Из-за этого ветки «спрятать
> панель» (нет ховера / свой юнит) после скрытия ПРОДОЛЖАЮТ исполнять цепочку
> врага и в конце СНОВА показывают панель — отсюда «панель всегда видна»,
> «Нет линии огня» по своим бойцам и варнинги «обращение к None» при Stop PIE
> (в §8 они ошибочно списаны на teardown — на самом деле это ходы с Hovered =
> null по цепочке врага). **Починка — Шаг 2а.0 ниже.**

#### Шаг 0 — общая нода «спрятать панель»

`Get TargetPanel` → `Set Visibility` → `In Visibility = Collapsed`. Просто
поставь её пока в стороне на холсте — подключим exec с двух сторон позже.

#### Шаг 1 — Hovered вообще есть?

1. От пина **Hovered** потяни провод → в поиске `Is Valid` → возьми
   Branch-версию (сразу с ветками `Is Valid`/`Is Not Valid`).
2. Экзек: событие → `Is Valid`.
3. Ветка **Is Not Valid** → веди сразу в exec-вход ноды из Шага 0
   (`Set Visibility = Collapsed`). Больше в этой ветке ничего.

#### Шаг 2 — это свой юнит или враг?

(Продолжение от ветки **Is Valid**.)

1. ПКМ по холсту → `Get Squad` (Pure, наша функция).
2. От её `Return Value` потяни провод → в поиске `Contains Item` → поставь;
   в её вход `Item` подай пин **Hovered** из события.
3. От результата (bool) → `Branch` (вторая, отдельная от первой).
4. Экзек: `Is Valid`(ветка Is Valid) → эта `Branch`.
5. Ветка **True** («это свой», Squad содержит Hovered) → тоже веди в exec-вход
   ноды из Шага 0. Больше здесь ничего.
6. Ветка **False** («это враг») → дальше, шаг 2а.

#### Шаг 2а.0 — отцепить цепочку врага от ноды «спрятать» (ПОЧИНКА)

1. Найди общую ноду `Set Visibility` (Collapsed, Target = TargetPanel) из
   Шага 0 — в неё входят три exec-провода (от Is Valid и двух веток Branch).
2. **Alt+клик по её ВЫХОДНОМУ exec-проводу** (он сейчас ведёт в `Set Text`
   имени цели) — разорви. Выход этой ноды должен остаться ПУСТЫМ (тупик:
   спрятали панель — и всё).

#### Шаг 2а — выбран ли боец? (добавлено после аудита)

1. ПКМ по холсту → `Get Tactical Controller` (Pure, C++ база) → от него
   `Get Selected Unit`.
2. От результата → `Is Valid` (Branch-версия).
3. Экзек: **нижний выход «это враг» Branch'а из Шага 2** (тот, что после
   починки 2а.0 остался пустым; у Branch с Contains верхний выход «свой» уже
   ведёт в Collapse) → этот `Is Valid`.
4. **Is Not Valid** (боец не выбран — стрелять некому) → в общую ноду Шага 0
   (`Collapsed`). **Is Valid** → в `Set Text` имени цели (начало Шага 3) —
   тот самый вход, который освободился в Шаге 2а.0.

#### Шаг 3 — имя цели

1. От пина **Hovered** потяни провод → в поиске `Unit Display Name` (это
   свойство юнита, ищется как обычный Get) → подключится напрямую.
2. `Get TargetNameText` → `Set Text`, в `In Text` подай `Unit Display Name`.
3. Экзек: `Is Valid`(ветка Is Valid) из Шага 2а → эта `Set Text`.

#### Шаг 4 — полоса HP цели

1. От **Hovered** → `Get Health`; от **Hovered** ещё раз → `Get Max Health`
   (оба Pure, BlueprintPure с ATDCombatant).
2. Узел `float / float` (Divide) — Health в A, Max Health в B.
3. `Get TargetHPBar` → `Set Percent`, в `In Percent` подай результат деления.
4. Экзек: конец шага 3 → эта `Set Percent`.

#### Шаг 5 — шанс попадания

1. ПКМ по холсту → в поиске `Get Hit Chance On Target` (функция C++ базы,
   Pure); в её вход `Target` подай **Hovered**.
2. От результата (float) потяни провод → в поиске `>=` → `Float >= Float`,
   во второй вход впиши `0`.
3. От bool-результата → `Branch` (третья).
4. Экзек: конец шага 4 → эта `Branch`.
5. **True** (можно стрелять):
   - от `Get Hit Chance On Target`'а (переиспользуй тот же провод/ноду, не
     создавай заново) → в поиске `Round` (float→int) → поставь;
   - `Format Text`, строка `Попадание: {Percent}%` (после `}` появится пин
     `Percent`) ← подключи в него выход `Round`;
   - `Get HitChanceText` → `Set Text` ← `Result` от Format Text.
6. **False** (нельзя стрелять): `Get HitChanceText` → `Set Text`, в `In Text`
   впиши литералом `Нет линии огня`.

#### Шаг 5а — иконка укрытия цели (щит, как в XCOM)

1. ПКМ по холсту → `Get Target Cover Against Selected` (Pure, C++ база);
   в её вход `Target` подай **Hovered**.
2. От результата (enum ECoverType) потяни провод → в поиске `Switch on
   ECoverType` → поставь.
3. Экзек: оба конца шага 5 (True и False) → этот `Switch`.
4. Ветка **None** → `Get TargetCoverIcon` → `Set Visibility` = `Collapsed`.
5. Ветка **Half** → `Get TargetCoverIcon` → `Set Brush from Texture`
   (T_Icon_Half_Cover) → `Set Visibility` = `Not Hit-Testable (Self Only)`.
6. Ветка **Full** → то же с T_Icon_Full_Cover.

#### Шаг 6 — показать панель

1. `Get TargetPanel` → `Set Visibility`, `In Visibility` = **Not Hit-Testable
   (Self Only)** (то же значение, что и для рамки выбора портрета — панель не
   должна перехватывать клики мыши по миру).
2. Экзек подведи из всех ТРЁХ концов шага 5а (None/Half/Full) в эту ноду.

Итого у события два «выхода» по итогу: либо сразу прячем панель (Шаг 0, если
Hovered невалиден или это свой), либо доходим до конца и показываем её с
данными (Шаг 6).

### 4.4 Event On Units State Changed

Самое короткое событие — портреты просто перерисовываются, плюс обновляется
счётчик врагов.

Создание: ПКМ → в поиске `Units State Changed` → **Event On Units State
Changed** (без выходных пинов с данными, только exec).

1. Перетащи **Portraits** → **Get Portraits** → от неё `For Each Loop`.
   Экзек: событие → `For Each Loop`.
2. В `Loop Body`: от пина `Array Element` потяни провод → в поиске напиши
   `Refresh` → найдётся **наша** функция из WBP_UnitPortrait (без параметров,
   Target подставится = Array Element сам). Экзек: `Loop Body` → `Refresh`.
3. От `Completed` цикла: ПКМ → в поиске `Get Alive Enemy Count` (C++ база,
   Pure) → поставь.
4. От её `Return Value` (int) потяни провод → в поиске `To Text (int)` →
   поставь (обычная конвертация числа в текст, плюс проценты не нужны).
5. `Get EnemyCountText` → `Set Text` ← результат `To Text`. Экзек: `Completed`
   → эта `Set Text`.
6. От её exec-выхода → вызов **RefreshButtons**.

### 4.5 Event On Combat Finished (bPlayerWon)

Самое простое событие — просто прячем весь боевой интерфейс, экран результата
дальше покажет сам GameMode.

Создание: ПКМ → в поиске `Combat Finished` → **Event On Combat Finished**
(есть выходной пин `bPlayerWon`, но в этом событии он не нужен — просто не
трогай его).

1. `Get ActionsPanel` → `Set Visibility` = `Collapsed`.
2. `Get EndTurnBtn` → `Set Visibility` = `Collapsed`.
3. `Get TargetPanel` → `Set Visibility` = `Collapsed`.
4. `Get EnemyPhaseHint` → `Set Visibility` = `Collapsed`.
5. Соедини все четыре по экзеку цепочкой одна за другой: событие → 1 → 2 →
   3 → 4. Порядок между собой не важен, главное чтобы шли друг за другом.

С этим событием С++-хуки (§4) закончены — остаётся §5 (тело функции
RefreshButtons, которую мы весь этот раздел вызывали пустой заглушкой), §6
(клики кнопок) и §7 (подключение + проверка в PIE).

## 5. Функция RefreshButtons

Functions → + → **RefreshButtons** (без входов). Внутри:

1. `Get Tactical Controller` (C++ база) → `Get Selected Unit` → сохрани в
   **локальную переменную** `S` (в функции: панель слева → Local Variables → +,
   тип Unit Base).
2. Считаем «можно действовать»: узел **AND** из трёх условий:
   - `Is Valid` (зелёный, возвращает bool) от S;
   - **NOT** ← `Is Downed` (от S);
   - `Can Spend` (от S → `Get Action Points`, Cost = 1).
   Результат → локальная bool `bCanAct`.
   (Фазу проверять не надо: в фазу врага вся ActionsPanel уже disabled из
   OnPhaseChanged, а disabled-панель гасит детей.)
3. `Set Is Enabled` с `bCanAct`: **AttackBtn**, **OverwatchBtn**,
   **HunkerBtn**, **SkipBtn**.
   > Для AttackBtn (добавляется к готовому графу): врежь `Set Is Enabled`
   > (Target = `Get AttackBtn`, Is Enabled ← та же развязка `bCanAct`) в
   > exec-цепь МЕЖДУ двумя узлами `Set bCanAct` (обычным и литеральным из
   > Is-Valid-защиты — оба их выхода сейчас ведут в Set Is Enabled
   > OverwatchBtn) и `Set Is Enabled (OverwatchBtn)`: оба провода перекинь
   > на новый узел, его выход — в OverwatchBtn.
4. **AbilityBtn**: `bCanAct` **AND** (**NOT** (`Get Ability Uses Remaining`
   (Target = S, Ability Class = от S → `Get Class Ability Class`) `== 0`))
   → `Set Is Enabled`.
5. **InteractBtn**: `Get Tactical Controller` → `Get Available Interaction` →
   узел `Switch on EInteractionKind`:
   - **None** → `Set Is Enabled` InteractBtn = ✘ (⚠️ ЛИТЕРАЛ, галочка снята
     руками, БЕЗ провода от bCanAct — иначе кнопка F останется включённой
     по AP, даже когда рядом нет ни бомбы, ни зоны эвакуации);
   - **Defuse Bomb** → Is Enabled = `bCanAct`; `Set Brush from Texture`
     **InteractIcon** = T_Icon_Defuse;
   - **Evacuate** → Is Enabled = `bCanAct`; InteractIcon = T_Icon_Evac.

> ВАЖНО: RefreshButtons вызывается только из событий (фаза/выбор/AP/статусы).
> НИКАКИХ биндингов-Pure на тик в Designer (поле Bind у Is Enabled) — это
> опрашивало бы мир каждый кадр.

## 6. Клики кнопок

Для каждой кнопки: Designer → выбери кнопку → Details → Events → **+ On
Clicked**. В графе от события:

`Get Tactical Controller` → вызов:

| Кнопка | Функция |
|---|---|
| AttackBtn | `Request Attack` (режим прицеливания: следующий ЛКМ по врагу — выстрел; прямой ЛКМ по врагу работает и без кнопки) |
| OverwatchBtn | `Request Overwatch` |
| HunkerBtn | `Request Hunker Down` |
| AbilityBtn | `Request Class Ability` |
| InteractBtn | `Request Interact` |
| SkipBtn | `Request Skip Unit Turn` |
| EndTurnBtn | `Request End Turn` |

(`Get Tactical Controller` — Pure-функция C++ базы, каст не нужен.)

## 7. Подключение и проверка

- [ ] **Compile + Save** WBP_TacticalHUD (жёлтых/красных ошибок нет).
- [ ] Открой `Core/GM_Tactics` → Details → категория **Tactics|UI** →
  **Tactical HUD Class = WBP_TacticalHUD**.
- [ ] У `BP_TacticalPlayerController` заполнен `RootLayoutClass =
  WBP_PrimaryGameLayout` (уже сделано ранее — проверь).

### Чеклист PIE (Lvl_TopDown)

- [ ] HUD появился после старта боя: «ВАШ ХОД», «Ход 1», портреты отряда.
- [ ] Клик по портрету выбирает бойца (рамка + кольцо в мире).
- [ ] Потратил AP (пробежал) — пипсы на портрете погасли, кнопки посерели
  после второго действия.
- [ ] **Панель цели скрыта, пока боец не выбран** (навёл на врага БЕЗ выбора —
  ничего; никакого «Нет линии огня» на старте боя).
- [ ] Выбрал бойца, навёл на врага — панель цели: имя, HP, «Попадание: N%»,
  щит укрытия цели (если есть); увёл курсор — скрылась.
- [ ] Враг за стеной без LOS — «Нет линии огня» (при выбранном бойце).
- [ ] **Кнопка «Огонь»** доступна только если есть хоть одна цель по дальности+LOS
  (иначе серая); нажал → клик по врагу — выстрел; клик по земле/своему —
  режим тихо снялся.
- [ ] **Прямой ЛКМ по врагу БЕЗ кнопки «Огонь» НЕ стреляет** (правка
  2026-07-20 — до этого стрелял, не по-XCOM'овски); прогноз по ховеру виден
  всё равно.
- [ ] Навёл на врага дальше `AttackRange`, но с чистой LOS — текст **«Слишком
  далеко»**, не «Нет линии огня» (разные причины, правка 2026-07-20).
- [ ] «Завершить ход» → «ХОД ПРОТИВНИКА», панель действий серая, подсказка
  «Ход противника — ждите»; ход вернулся — всё ожило.
- [ ] Убил врага — счётчик у черепа уменьшился, портреты обновились.
- [ ] Погибший боец: портрет затемнён, клик по нему НЕ выбирает; если был
  выбран — выбор снялся сам (кольцо/зона исчезли).
- [ ] Overwatch/Оборона нажимаются, у юнита появляется иконка статуса на
  портрете (сработает после создания BP_GA_Overwatch/Hunker, этап 5).
- [ ] **Все 6 кнопок ActionsPanel + EndTurnBtn одинакового размера**
  (`ActionButtonPadding` в `DA_TacticalHUDStyle`, фикс 2026-07-20 —
  до этого разный `NormalPadding` каждой кнопки давал визуальный разнобой).
- [ ] В Output Log нет спама `... вне UClass` при выборе бойца/смерти юнита/
  остановке PIE (краш-баг `Refresh`/`SetSelected` по `Portraits`, фикс
  2026-07-20 — добавлен `IsValid`-guard в оба `ForEachLoop`).

Готово (исторически) → чекбоксы стояли в `hud_todo_checklist.md` (шаг 4) и
`../04_ROADMAP.md` (этап 6, пункт WBP_TacticalHUD).

## 8. Траблшутинг (частые грабли)

- **HUD в PIE схлопнут в левый-верхний угол, хотя в Designer правильный.**
  Причина не в якорях панелей HUD (они ок, раз Designer верный), а в
  **WBP_PrimaryGameLayout**: стеки в Overlay должны быть **Fill/Fill**. Если
  `GameStack` (и др.) не Fill — `CommonActivatableWidgetStack` берёт «желаемый
  размер» HUD'а (а у Canvas Panel он считается только по элементам с якорем
  (0,0), т.е. верх-лево), и весь HUD жмётся в угол. Фикс: WBP_PrimaryGameLayout
  → каждый стек → Slot (Overlay Slot) → Horizontal/Vertical Alignment = **Fill**.

- **Варнинги в логе «свойство S / K2Node_Event_Hovered … обращение к None».**
  Варнинги про `S` при Stop — teardown-косметика (защита `Is Valid(S)` в
  RefreshButtons уже стоит). Варнинги про `K2Node_Event_Hovered` имели
  РЕАЛЬНУЮ причину — неправильный провод в OnHoveredUnitChanged (цепочка
  врага исполнялась с Hovered = null; см. красный блок в §4.3, починка —
  Шаг 2а.0). После починки должны исчезнуть.
