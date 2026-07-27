# 15 — Задачи для редактора (BP/WBP/ассеты)

> **Для кого:** отдельная сессия агента, работающая через **UnrealClaude MCP**
> при ОТКРЫТОМ редакторе, либо ручная работа дизайнера.
>
> **Зачем отдельный файл.** Всё, что делается кодом, сделано; здесь только то,
> что физически живёт в ассетах: графы Anim Blueprint, свойства в BP, монтажи.
> У кода и у ассетов разный инструмент, разный способ проверки и разная цена
> ошибки — держать их в одном списке вредно.
>
> ⚠️ **Правка BP через MCP верифицируется чтением после КАЖДОЙ операции**
> (`unreal_blueprint_query`) — плагин местами сырой, см. `CLAUDE.md`.
>
> ⚠️ **Перед работой пересобрать C++** (`.\Build-XRU1.ps1` при закрытом
> редакторе): графу нужны поля `PeekSideLocal`, `PendingTurnYaw`, `bShouldPeek`
> из `FUnitVisualState`, через Live Coding новые `UPROPERTY` не появляются.
> Свежий git pull почти наверняка меняет `UnitBase.h`/`.cpp` — пересобери, даже
> если раньше уже собирал на этой машине.

---

## Блок E0 — Структура `Content` ✅ ЗАВЕРШЕНО

Весь юнитовский контент лежит по единой чистой структуре; мусор шаблона Top Down
удалён. Как ассеты сюда попали и на какие грабли наступали по пути — приложение
в конце файла, для текущей работы это не нужно.

```
/Game/XRU1Game/Units/
    BP_Unit_Assault / Sniper / Medic / Tank / Marauder      — 5 юнитов, Mesh->Anim Class = ABP_Solider
    Anim/
        ABP_Solider                — рабочий Anim Blueprint всех пяти юнитов
        Cover/    (6)   anim_CoverDown_Idle_L/R (поза), _Look_L/R (выглядывание) — в графе;
                        _Loop_L/R — клип ХОДЬБЫ, в графе НЕ используется (см. 6.1/6.3)
        Crouch/   (30)  anim_Crouch_*, BS_Crouch, Turn_*, Loop_* — на нашем скелете
        Death/    (6)   MM_Death_*
        Revive/   (4)   anim_Downed_Idle_R, anim_Knocked_*, anim_Self_Revive_F_R
        Rifle/    (39)  MM_Rifle_*, AIM/, HitReact/, Jog/, Jump/, Walk/ + BS_Rifle_Locomotion
        Unarmed/  (27)  шаблонный набор без оружия — в графе больше не участвует
    Meshes/
        SKM_Manny_Simple, SKM_Quinn_Simple   — тела юнитов
        SK_Mannequin                          — ОСНОВНОЙ скелет (у него же Compatible Skeletons)
        SK_Mannequin_AnimLib                  — скелет анимационной библиотеки (Cover/Revive)
        Materials/, Textures/, Rigs/           — зависимости тел
    Weapons/
        AssaultRifle/ SMG/ LMG/ Sniper/ Common/   — по стволу на класс (меш+материалы+текстуры+BP)
```

Что важно помнить при работе с этими ассетами:

- **`Anim/Cover/` и `Anim/Revive/` живут на `SK_Mannequin_AnimLib`**, а играют на
  нашем `SK_Mannequin` через **Compatible Skeletons** (уже настроено). Если после
  каких-то правок увидишь `LoadErrors` на скелет — лечится назначением
  `Skeleton = /Game/XRU1Game/Units/Meshes/SK_Mannequin_AnimLib` и пересохранением.
- **`Anim/Crouch/` отретаргечен** на наш `SK_Mannequin`.
- **Двигать ассеты внутри проекта только средствами редактора** (Move/Rename),
  файловый перенос рвёт ссылки внутри `.uasset`. После массового перемещения:
  сначала **Save All**, только потом **Fix Up Redirectors** (обратный порядок
  оставляет мёртвые ссылки навсегда).
- **`GlobalDefaultGameMode`** в `Config/DefaultEngine.ini` = `/Game/XRU1Game/Core/GM_Tactics`
  (fallback движка; у `Lvl_TopDown` свой `GameMode Override`).

---

## Блок E1 — Анимации: сборка `ABP_Solider` ⭐ приоритет

Схема и обоснование — [14_ANIMATION_PLAN.md](14_ANIMATION_PLAN.md).

> ### 📋 КОРОТКИЙ ПУТЬ
>
> | # | Шаг | Где | Готово |
> |---|---|---|---|
> | 1 | Перенести crouch-анимации из `UE-HW` | Content Browser | [x] |
> | 2 | Поставить меш винтовки в руку | `BP_Unit_*` | [ ] |
> | 3 | Создать `ABP_Solider`, назначить 5 юнитам | Content Browser | [x] |
> | 4 | Переменные ABP из `Get Visual State` | `ABP_Solider` | [x] |
> | 5 | Blend Space стойки, приседа, стейты Jump/Fall/Land | Content Browser | [x] |
> | 6 | Стейт-машина «Поза» (8 поз + выглядывание + доворот) | `ABP_Solider` | [~] почти готово, см. 6.1–6.3 |
> | 7 | Inertialization + **Default Slot** перед Output Pose | `ABP_Solider` | [ ] ⬅️ ТЫ ЗДЕСЬ |
> | 8 | Создать 5 монтажей | Content Browser | [ ] |
> | 9 | Назначить монтажи в слоты юнитов | `BP_Unit_*` | [ ] |
> | 10 | Повесить монтажи на BP-хуки | `BP_Unit_*` | [ ] |
> | 11 | Проверить в PIE по чеклисту E1.10 | PIE | [ ] |
>
> Шаг 2 от графа не зависит — можно закрыть в любой момент, но до PIE-проверки
> он нужен: без меша оружия Rifle-анимации играют «с пустыми руками».

### E1.0 ШАГ 1 — Crouch-анимации ✅

- [x] `/Game/XRU1Game/Units/Anim/Crouch/` — 30 файлов на нашем скелете:
      `anim_Crouch_Idle` + 8 направлений, `BS_Crouch`, 12 `M_Neutral_Crouch_Loop_*`,
      8 `M_Neutral_Crouch_Idle_Turn_045/090/135/180_L/R`.

### E1.1 ШАГ 2 — Меш оружия в руке

Меши уже в проекте, с текстурами (сжаты до `Maximum Texture Size = 512`):

| Класс | Меш | Путь |
|---|---|---|
| Assault, Marauder | `SK_AssaultRifle_Frame` | `/Game/XRU1Game/Units/Weapons/AssaultRifle/Meshes/` |
| Medic | `SK_SMG_Frame` | `/Game/XRU1Game/Units/Weapons/SMG/Meshes/` |
| Tank | `SK_LightMachineGun_Frame` | `/Game/XRU1Game/Units/Weapons/LMG/Meshes/` |
| Sniper | `SK_Sniper_Frame` | `/Game/XRU1Game/Units/Weapons/Sniper/Meshes/` |

- [ ] В `BP_Unit_*` добавить **`Skeletal Mesh Component`**, **Parent Socket = `hand_r`**.
- [ ] Подправить трансформ в вьюпорте: оружие в правой руке стволом вперёд
      (ориентир — поворот по Yaw/Roll на 90° и сдвиг на несколько см).
- [ ] `Collision → NoCollision` на компоненте оружия: оно не должно ни резать
      навмеш, ни попадать в трейсы укрытия и линии огня.
- [ ] Снайперке **не цеплять `SM_Sniper_Scope`** (он рисует Render Target каждый
      кадр) либо назначить на слот прицела непрозрачный `MI_Sniper_01`.

### E1.2 ШАГ 3 — `ABP_Solider` ✅

- [x] `/Game/XRU1Game/Units/Anim/ABP_Solider` создан и назначен `Mesh → Anim Class`
      всем пяти `BP_Unit_*`.

> ⚠️ Ассет называется **`ABP_Solider`** (без второй «d») — это его настоящее имя,
> все ссылки ниже используют его.
>
> ⚠️ После шага 6 папка `Anim/Unarmed/` (27 файлов) не будет использоваться
> нигде: рукопашной боёвки, рывка и wall-jump в GDD нет. Решить — удалить или
> оставить про запас; на граф это не влияет.

### E1.3 ШАГ 4 — Переменные ABP ✅

`Event Blueprint Update Animation`: `TryGetPawnOwner` → `Cast to UnitBase` →
**`Get Visual State`** (один узел) → разложить в переменные:

| Переменная ABP | Из чего | Тип |
|---|---|---|
| `Pose` | `VisualState.Pose` | `EUnitPose` |
| `PeekSide` | `VisualState.PeekSideLocal` | float: −1 стена слева, +1 справа, 0 — юнит не вдоль стены |
| `PendingTurn` | `VisualState.PendingTurnYaw` | float: угол начатого доворота, знак = сторона, 0 — не доворачивается |
| `bMoving` | `VisualState.bMoving` | bool (включает подшаг к стене) |
| `CoverSideY` / `CoverSideX` | `VisualState.CoverDirectionLocal.Y` / `.X` | float, для диагностики |
| `Speed` | `Velocity.Size2D()` пешки | float |
| `Direction` | узел **`Calculate Direction`** (Velocity, Actor Rotation) | float, −180…180 |

⚠️ **Состояние не собирать самому** (теги ASC, `IsDead`, компонент укрытий) —
для этого и существует `FUnitVisualState`; второй источник разойдётся с первым.

⚠️ **`PeekSide` брать для выбора клипа `Left`/`Right`, `CoverSideY` — только для
диагностики.** Формально сейчас `PeekSide` — это и есть знак `CoverSideY` (юнит
стоит вдоль стены, см. 6.3), но `PeekSide` дополнительно учитывает порог
вырожденности (`|Y| > 0.35`), поэтому в графе используй именно его — с ним
поведение не изменится, даже если позже стойку снова поправят.

### E1.4 ШАГ 5 — Blend Space и стейты прыжка ✅

- [x] **`BS_Rifle_Locomotion`** (`Anim/Rifle/`) — 2D Blend Space, ось X =
      `Direction` (−180…180°), ось Y = `Speed` (0…600), **17 сэмплов**:
      центр `MF_Rifle_Idle_ADS`; Speed≈150 — 8 клипов `MF_Rifle_Walk_*`;
      Speed≈450 — 8 клипов `MF_Rifle_Jog_*`.
- [x] **`BS_Crouch`** (`Anim/Crouch/`) — 2D, присед: Idle + 8 направлений.
- [x] **Jump/Fall/Land** — на наборе `Rifle/Jump/`: `MM_Rifle_Jump_Start` →
      `_Start_Loop` → `_Apex` → `_Fall_Loop` → `_Fall_Land`, плюс
      `_RecoveryAdditive` аддитивным слоем поверх приземления.

### E1.5 ШАГ 6 — Стейт-машина «Поза» ⬅️ ТЫ ЗДЕСЬ

Стейт-машина держит **ПОЗУ** (что юнит делает постоянно). Действия — выстрел,
реакция, смерть — играются монтажами через Default Slot (шаг 7), в машине их нет.

#### 6.1 Динамические переменные анимаций (делать ПЕРВЫМ)

Чтобы не плодить по два состояния на каждую сторону, стороны выбираются
переменными, а состояния берут их динамическим `Sequence Player`
(ПКМ по пину `Sequence` → **Expose as Pin**). В `Event Blueprint Update Animation`
через `Select`:

| Переменная (Anim Sequence) | Значение при `PeekSide < 0` | При `PeekSide >= 0` |
|---|---|---|
| `CoverLoopAnim` | `anim_CoverDown_Idle_Left` | `anim_CoverDown_Idle_Right` |
| `CoverLookAnim` | `anim_CoverDown_Look_Left` | `anim_CoverDown_Look_Right` |

⚠️ **`CoverLoopAnim` берёт `Idle_*`, а НЕ `Loop_*`.** `anim_CoverDown_Loop_L/R` —
это клип ХОДЬБЫ (переступание в укрытии), а не поза ожидания; в графе он не
используется и его можно не назначать. `anim_CoverDown_Idle_L/R` — короткая
статичная поза, наклон к краю укрытия — вот это и есть «стоит и ждёт».
`CoverEnterAnim`/переменной под `Enter` больше нет — состояние `Enter` убрано
из обеих машин (см. 6.3).

⚠️ **`PeekSide` теперь значит «где СТЕНА» (не «где край»).** `< 0` — стена
слева, `>= 0` — справа; это прямое чтение `CoverDirectionLocal.Y`, а не отдельный
трейс. Работает только потому, что `HugCover()` ставит юнита ВДОЛЬ стены лицом к
краю укрытия (см. 6.3) — тогда стена гарантированно сбоку и знак однозначен.
Раньше поле означало «где кончается укрытие» и юнит стоял носом в стену — при
такой стойке Y вырождается в шум, чем и объяснялись прошлые перевёрнутые позы.

Ещё одна переменная — `TurnAnim`, выбирается по `PendingTurn` (см. 6.4):
модуль даёт бакет, знак — сторону (`+` вправо → `_R`, `−` влево → `_L`).

| \|PendingTurn\| | Клип |
|---|---|
| 25…67° | `M_Neutral_Crouch_Idle_Turn_045_L/R` |
| 67…112° | `M_Neutral_Crouch_Idle_Turn_090_L/R` |
| 112…157° | `M_Neutral_Crouch_Idle_Turn_135_L/R` |
| 157…180° | `M_Neutral_Crouch_Idle_Turn_180_L/R` |

#### 6.2 Состояния позы

| Состояние | Что играет | Условие входа |
|---|---|---|
| `Idle` | `MF_Rifle_Idle_ADS` | `Pose == Stand` |
| `Locomotion` | `BS_Rifle_Locomotion` (`Speed`, `Direction`) | `Pose == Moving` |
| `CrouchCover` | вложенная пара Loop → Look (см. 6.3) | `Pose == CrouchCover` |
| `HighCover` | та же пара | `Pose == HighCover` |
| `Overwatch` | `MF_Rifle_Idle_ADS` + Aim Offset вверх | `Pose == Overwatch` |
| `Hunkered` | `BS_Crouch` | `Pose == Hunkered` |
| `TurnInPlace` | `TurnAnim` (динамический Sequence Player) | см. 6.4 |
| `Downed` | `anim_Downed_Idle_R` (луп) | `Pose == Downed` |
| `Dead` | `MM_Death_Front_01`, последний кадр | `Pose == Dead` |
| `Jump` / `Fall` / `Land` | `Rifle/Jump/*` | стандартный переход по `IsFalling` |

- [ ] Собрать состояния, переходы позы — по одной переменной `Pose`.
- [ ] Blend time между позами 0.15–0.25 с.
- [ ] Aim Offset `MM_Rifle_Idle_ADS_AO_CU/CD/CC` поверх позы (по питчу к цели).

#### 6.3 Поза укрытия — две анимации (Loop + Look), таймер и сторона считает C++

Внутри `CrouchCover` и `HighCover` — плоская пара, без «заселения»:

1. **Loop** — `CoverLoopAnim` (= `anim_CoverDown_Idle_*`), устойчивая поза
   ожидания. Это entry-состояние машины (состояние `Enter` убрано целиком —
   было лишним: `anim_CoverDown_Idle_*` уже статична, отдельный вход не нужен).
2. **Look** — `CoverLookAnim`, короткое выглядывание за край и обратно в Loop.

Таймер и переключение живут в C++ (`AUnitBase::UpdateCoverPeek`), не в ABP:
пересобирать состояние в графе НЕ нужно, оно уже выставлено через
`Get Visual State`.

- [ ] Переход `Loop → Look`: **`Get bShouldPeek`** (bool, `true` — источник C++).
- [ ] Переход `Look → Loop`: **`NOT Get bShouldPeek`**.
- [ ] Никакого `Current State Time`/таймера в самом графе — раньше он стоял на
      узле времени ВНЕШНЕЙ машины (`Main States`) и никогда не сбрасывался, из-за
      чего юнит уходил в `Look` по расписанию и залипал в цикле «вышел-вернулся».
      C++-таймер этой ошибки не допускает по конструкции.
- [ ] После **Inertialization** (см. E1.6 ниже) добавь **Loop Animation = true** у
      обоих `Sequence Player` (`CoverLoopAnim`, `CoverLookAnim`) — иначе на
      статичном клипе анимация замирает на последнем кадре вместо устойчивой позы.

⚠️ **Ориентация юнита — ВДОЛЬ стены, лицом к краю укрытия, не носом в стену.**
`HugCover()` в C++ разворачивает бойца именно так (направление берёт у
`FindPeekEdgeSide`). Это не декоративный выбор: `anim_CoverDown_*` сняты для
бойца, стоящего боком к укрытию, — при развороте носом в стену стороны
вырождаются, и клип начинает доворачивать всё тело, компенсируя неверную
стойку («юнит развёрнут на 180°, голова назад» — это было ровно оно). Если в
PIE поза читается как надо, проверять здесь нечего — стойку ставит код.

⚠️ **Выглядывание живёт только здесь.** Перед выстрелом его играть не нужно:
выстрел из укрытия начинается сразу (см. E1.8), а «пик» — это физический выбег
за угол, а не анимация. C++ также НЕ доворачивает корпус дополнительно при
входе в `Look` (`bTurnBodyOnPeek = false` по умолчанию) — весь поворот уже
внутри клипа; включать эту ручку не надо, иначе углы складываются вдвое.

⚠️ **Сторона `Left`/`Right` может понадобиться зеркалить.** Если после сборки
в PIE юнит выглядывает не в ту сторону — это значит клипы `Left`/`Right` сняты
в обратной паре относительно `PeekSide`. Правится за минуту без пересборки:
поменять местами `Option 0`/`Option 1` у обоих `Select` (`CoverLoopAnim`,
`CoverLookAnim`). Проверяется одним прогоном PIE, гадать заранее бессмысленно.

⚠️ **`HighCover` собран так же, как `CrouchCover`** (обе — `anim_CoverDown_*`
на верх+низ тела целиком, приседная поза). Если за высокой стеной поза должна
быть «стоя вплотную» без приседа — придётся переделать: базовая поза
`MF_Rifle_Idle_ADS`, а `CoverDown_*` подмешать только на верх тела через
**Layered Blend per Bone** от `spine_01` (см. best practice в §3.2 14-го дока).
Это отдельная задача, в графе пока не сделано.

#### 6.4 Доворот на месте

Когда цель сменилась, а клетка та же, юнит доворачивается плавно (C++ крутит yaw
со скоростью `TurnInPlaceRate`), и `PendingTurnYaw` показывает, на сколько
градусов доворот начат. Анимация просто идёт следом.

- [ ] Состояние `TurnInPlace` с динамическим `Sequence Player` = `TurnAnim`.
- [ ] Вход: `PendingTurn != 0` **и** `bMoving == false` **и** `Pose` ∈
      (`CrouchCover`, `HighCover`, `Hunkered`) — клипы приседные, для стоячих поз
      они не подходят.
- [ ] Выход: `PendingTurn == 0` (C++ обнуляет, когда доворот закончен),
      Blend time 0.1–0.15 с.
- [ ] Свести длительность: при `TurnInPlaceRate = 120` °/с поворот на 90° идёт
      0.75 с. Если клип заметно длиннее или короче — подправить `Play Rate` у
      `Sequence Player` либо `TurnInPlaceRate` в `BP_Unit_*` (Class Defaults →
      `Tactics|Visual`). Довороты меньше `TurnInPlaceMinAngle` (25°) C++ делает
      мгновенно, состояние на них не включается.

### E1.6 ШАГ 7 — Inertialization и Default Slot

- [ ] **Inertialization** — узел `AnimGraph` → ПКМ → `Inertialization`,
      вставить между выходом стейт-машины позы и `Default Slot`. Обязателен:
      без него редактор пишет в PIE `Error: Не найден узел инерциализации для
      запроса с перехода 'Loop' к 'Look'…` и переходы внутри `SM_CrouchCover`/
      `SM_HighCover` дёргаются рывком вместо блендинга.
- [ ] **Default Slot** между Inertialization и Output Pose. Без него монтажи не
      видны вообще — самая частая ошибка на этом этапе.

### E1.7 ШАГ 8 — Монтажи

5 монтажей на 6 C++-слотов (`FireMontageStepOut` остаётся пустым намеренно):

| Монтаж | Из чего собрать | Слот в `BP_Unit_*` |
|---|---|---|
| `AM_Fire_Open` | `MM_Rifle_Fire` | `FireMontageOpen` |
| `AM_Fire_OverCover` | **`MM_Rifle_DryFire`** — снимок из-за укрытия короче и без полной отдачи, он обязан отличаться от открытого выстрела | `FireMontageOverCover` |
| `AM_HitReact` | 8 клипов (`MM_HitReact_Front_Lgt_01..04`, `Front_Med_01..02`, `Front_Hvy_01`, `Back_Med_01`), выбор — явный `Random Integer in Range(0,7)` в BP | `HitReactMontage` |
| `AM_Death` | 6 клипов (`MM_Death_Front_01..03`, `Back_01`, `Left_01`, `Right_01`), тоже явный `Random Integer` | `DeathMontage` |
| `AM_Overwatch_Enter` | два клипа одним монтажом: `MM_Rifle_Equip` → `MM_Rifle_Reload` (вскинул → дослал патрон) | `OverwatchEnterMontage` |

⚠️ **`FireMontageStepOut` оставить ПУСТЫМ.** Выход за угол делается перемещением,
а не анимацией: юнит выбегает в точку пика, доворачивается, стреляет обычным
`AM_Fire_Open` и возвращается. Точку отдаёт `GetFireMontageFor` третьим
параметром. Пустой слот безопасен — C++ подставит `FireMontageOpen`.

- [ ] Монтажи созданы в `Anim/Rifle/` (Fire/OverCover/Overwatch) и рядом с
      исходниками (HitReact/Death).

### E1.8 ШАГИ 9–10 — Слоты и BP-хуки юнита

- [ ] Назначить 5 монтажей в слоты `BP_Unit_*` (Class Defaults →
      `Tactics|Visual|Montages`).
- [ ] `OnShotFired(Target, bHit)`:
      1. `GetFireMontageFor(Target)` → монтаж, `OutStance`, `OutFiringEyeLocation`;
      2. если `OutStance == StepOut`: `AI Move To` в `OutFiringEyeLocation`
         (спроецированную на пол) → `Face Actor Towards` цели → `Play Anim Montage`
         (выстрел) → по окончании `AI Move To` обратно → **`HugCover()`**;
      3. иначе — просто `Play Anim Montage` (стойка уже верная).
- [ ] `OnReactionShot(Target, bHit)` → то же самое.
- [ ] `OnDied` → `DeathMontage` (случайный из 6).
- [ ] Попадание по юниту → `HitReactMontage` (случайный из 8).

⚠️ **Монтаж выстрела играть сразу, ничего перед ним не ждать.** Хук приходит уже
после расчёта попадания: любая задержка означает, что цель дёрнется от попадания
раньше, чем стрелок выстрелит.

⚠️ **Тактическая клетка при выбеге не меняется.** Юнит возвращается, а укрытие,
щит и диск занятости считаются от его домашней точки — инвариант §II.6 п.5 в
[11_COVER_AND_ENEMY_PLAN](11_COVER_AND_ENEMY_PLAN.md).

### E1.9 Разное оружие у классов

Анимация не знает про оружие: Rifle-набор — это поза рук «держу длинноствол»,
автомат и снайперка держатся одинаково. Меняется только меш в сокете `hand_r`.

- [ ] Один `ABP_Solider` на всех, разное оружие = разный меш в `BP_Unit_*`.
- [ ] **Стакинг с приседом работает сам**: присед — ПОЗА (стейт-машина), выстрел —
      ДЕЙСТВИЕ (монтаж поверх через Default Slot). Если верх тела в приседе
      выглядит криво — сделать `AM_Fire_*` аддитивным и/или ограничить слот костью
      `spine_01` через Layered Blend per Bone.

### E1.10 ШАГ 11 — Приёмка в PIE

- [ ] У низкого укрытия боец **приседает**, стреляет из приседа.
- [ ] У высокого и низкого — стоит **вплотную, ВДОЛЬ стены, лицом к краю
      укрытия** (не носом в стену — см. предупреждение в 6.3).
- [ ] К стене юнит **подшагивает** (виден шаг локомоции), а не телепортируется.
- [ ] Стойка `StepOut`: выбегает за угол, доворачивается, стреляет, возвращается.
- [ ] После выбега на пик щит цели и укрытие в HUD **не изменились**.
- [ ] Стоя в укрытии, юнит время от времени **выглядывает** — сторону задаёт
      `Look_*`-клип, C++ дополнительно корпус не доворачивает (у пиллара с обеих
      сторон работает одинаково; у длинной глухой стены не выглядывает вовсе —
      `bHasPeekEdge == false`), и возвращается в ожидание.
- [ ] Выглядывание идёт в ТУ сторону, что нужно (не зеркально). Если нет — см.
      правку `Option 0/1` в 6.3.
- [ ] Долгое ожидание в укрытии не подёргивается и не зацикливается на переходе
      `Loop → Look → Loop` (симптом старого бага — таймер на чужом состоянии).
- [ ] Смена цели без смены клетки — юнит **доворачивается анимацией**, а не
      щёлкает в новый ракурс за кадр.
- [ ] Выстрел из укрытия начинается сразу, без паузы перед монтажом.
- [ ] `OverCover`-выстрел выглядит иначе, чем открытый (короче, резче — `DryFire`).
- [ ] В чистом поле — обычная стойка и обычный выстрел.
- [ ] Наблюдение читается позой; вход в него — **два движения подряд** (вскинул,
      дослал патрон).
- [ ] Смерть и реакция на попадание — каждый раз **разный клип**.
- [ ] Ходьба/бег по всем 8 направлениям: юнит двигается боком и спиной, не
      разворачиваясь к движению передом.
- [ ] Прыжок/падение/приземление играются на Rifle-анимациях (если на карте есть
      перепад высот).

---

## Блок E2 — Свойства в BP

### E2.1 `BP_Unit_*` — настройки поз (категория `Tactics|Visual`)
- [ ] `CoverHugMaxNudge` (45) — насколько юнит подшагивает к стене; 0 отключает
      подшаг, оставляя только разворот.
- [ ] `CoverHugStepSpeed` (150) — скорость подшага. Держать равной скорости Walk
      в `BS_Rifle_Locomotion`, иначе к стене юнит поедет с анимацией бега.
- [ ] `TurnInPlaceRate` (120 °/с) и `TurnInPlaceMinAngle` (25°) — темп доворота и
      порог, ниже которого доворот делается мгновенно.

### E2.2 `BP_TacticalCameraPawn` — блок `Tactics|Camera|Shot`
- [ ] Убедиться, что не осталось «висящих» переопределений удалённых полей
      (`ShotFrameZoom*`, `ShotFramePitch*`, `ShotFrameYawOffset`, `ShotFrameLookHeight`).
- [ ] Дефолты: `ShotFrameBackNear/Far` (85/190), `ShotFrameShoulderNear/Far` (75/95),
      `ShotFrameHeightNear/Far` (45/110), `ShotFrameFovSafety` (0.7); блок
      `…|Shot|Score` — verbatim-веса XCOM, трогать не надо.

### E2.3 `DA_CoverTuning`
- [ ] `CoverTraceChannel` удалён из C++ — поле должно пропасть из ассета.
- [ ] `MinSpreadDistance` искать **не здесь** (он на контроллере AI).

### E2.4 `BP_UnitAIController` (если есть BP-наследник)
- [ ] `Tactics|AI|Weights`: нет старых переопределений `MinSpreadDistance = 250`
      и `SpreadPenaltyMultiplier = 0.4` — они перекроют verbatim-значения 576 / 0.2.
- [ ] Новые поля: `AllyVisibilityWeight` (10), `OverwatchExposurePenalty` (30),
      `InvestigateOverwatchChance` (0.5).

### E2.5 `GM_Tactics` — пресеты сложности
- [ ] У `DifficultyParams` есть поле `MaxAttackersPerTurn`: Easy 4, Medium 6,
      Hard −1. Если BP переопределяет карту целиком — проставить вручную.

---

## Блок E3 — Уровень `Lvl_TopDown`

- [ ] **Уступы разной высоты** — проверить бонус/штраф высоты (±20).
- [ ] Укрытия целевого дизайна: `WorldStatic` 60 см = Half, 150 см = Full
      (высоты считаются от ПОЛА).
- [ ] Длинная глухая стена ≥3 м — проверить, что она честно рвёт LOS.
- [ ] Узкий пиллар — проверить peek с обеих сторон.

---

## Блок E4 — Разное

- [ ] `WBP_TacticalHUD`: жёлтый щит (`Flanked`) виден и в панели цели, и над головой.
- [ ] `Mesh → Anim Class = ABP_Solider` у всех пяти BP юнитов.

---

## Приложение — как ассеты попали в проект (справка)

Нужно только если снова придётся переносить или массово двигать контент.

- **Источники:** `UE-HW/Content/OtherAssets/FreeAnimationLibrary` (6 анимаций
  укрытия, 4 анимации раненого, скелет библиотеки) и
  `UE-HW/Content/InfimaGames/ModernGunsBundle` (4 ствола целиком) — 285 файлов,
  ~1.6 ГБ; текстуры затем ужаты до 512 px через **Bulk Edit via Property Matrix**.
  Crouch-набор перенесён отдельно через **Migrate** и отретаргечен на наш скелет.
- **Раскладку по чистой структуре** делал скрипт `Content/Python/reorganize_assets.py`
  (Tools → Execute Python Script, сначала с `DRY_RUN = True`).
- **Порядок операций, который нельзя менять:** Save All → Fix Up Redirectors →
  Save All. Редиректор жив, пока ссылающийся ассет не пересохранён; схлопнешь
  раньше — ссылка повиснет навсегда и чинится только руками.
- **Fix Up Redirectors не чинит ссылку `Skeleton`** у анимаций — её проверять
  отдельно, загрузив ассеты и посмотрев Output Log на `LoadErrors`.
- **`ABP_Unarmed_Modify`** (в `UE-HW`) — рабочий пример графа с приседом, полезен
  как образец.

## Чего в этом файле НЕТ

Кодовые задачи ведутся в [13_AI_STATE_MACHINE_PLAN.md](13_AI_STATE_MACHINE_PLAN.md)
(фазы A/W/S) и [11_COVER_AND_ENEMY_PLAN.md](11_COVER_AND_ENEMY_PLAN.md) (фазы Ф).
Открытые кодовые: **A6** (профили весов), рандомизация при близких скорах,
scamper, **Ф11** (превью peek), **Ф12** (крит от фланга).
