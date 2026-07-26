# 15 — Задачи для редактора (BP/WBP/ассеты)

> **Для кого:** отдельная сессия агента, работающая через **UnrealClaude MCP**
> при ОТКРЫТОМ редакторе, либо ручная работа дизайнера.
>
> **Зачем отдельный файл.** Всё, что можно было сделать кодом, уже сделано —
> C++ собирается (`Result: Succeeded`). Осталось то, что физически живёт в
> ассетах: графы Anim Blueprint, назначения свойств в BP, монтажи. Держать это
> в одном списке с кодовыми задачами вредно: у них разный инструмент, разный
> способ проверки и разная цена ошибки.
>
> ⚠️ **Правка BP через MCP верифицируется чтением после КАЖДОЙ операции**
> (`unreal_blueprint_query`) — плагин местами сырой, см. `CLAUDE.md`.

---

## Блок E1 — Анимации: сборка `ABP_Soldier` ⭐ приоритет

Полный контекст и обоснование схемы — [14_ANIMATION_PLAN.md](14_ANIMATION_PLAN.md).
**C++ готов целиком, дописывать в коде нечего.**

> ### 📋 КОРОТКИЙ ПУТЬ (делать строго по порядку)
>
> | # | Шаг | Где | Готово |
> |---|---|---|---|
> | 1 | Перенести crouch-анимации из `UE-HW` | Content Browser | [ ] |
> | 2 | Найти и поставить меш винтовки в руку | `BP_Unit_*` | [ ] |
> | 3 | Создать `ABP_Soldier`, назначить 5 юнитам | Content Browser | [ ] |
> | 4 | В `Event Update Animation` — один узел `Get Visual State` | `ABP_Soldier` | [ ] |
> | 5 | Собрать Blend Space стойки и приседа | Content Browser | [ ] |
> | 6 | Собрать стейт-машину «Поза» (8 состояний) | `ABP_Soldier` | [ ] |
> | 7 | Добавить **Default Slot** перед Output Pose | `ABP_Soldier` | [ ] |
> | 8 | Создать 5 монтажей | Content Browser | [ ] |
> | 9 | Назначить монтажи в слоты юнитов | `BP_Unit_*` | [ ] |
> | 10 | Повесить монтажи на BP-хуки | `BP_Unit_*` | [ ] |
> | 11 | Проверить в PIE по чеклисту E1.9 | PIE | [ ] |
>
> Дальше — каждый шаг подробно.

### E1.0-ter ⭐ ШАГ 0 — НАВЕСТИ ПОРЯДОК В `Content` (делать ПЕРВЫМ)

Всё перенесённое легло по «чужим» путям (`/Game/OtherAssets/…`,
`/Game/InfimaGames/…`, `/Game/Characters/…`). Работать так можно, но потом
нельзя будет отличить нужное от мусора шаблона и почистить проект.

⚠️ **Переносить внутри проекта ФАЙЛАМИ НЕЛЬЗЯ.** `.uasset` хранит зависимости
строками вида `/Game/OtherAssets/…/anim_CoverDown_Idle_Right` — файловый перенос
внутри проекта рвёт все ссылки. (Перенос МЕЖДУ проектами по тому же
относительному пути безопасен — именно так сюда и попали новые ассеты.)
Внутри проекта двигать можно только средствами редактора: они правят ссылки и
оставляют редиректоры.

**Готовый скрипт:** `Content/Python/reorganize_assets.py`

1. Edit → Plugins → включить **Python Editor Script Plugin**, перезапустить редактор.
2. **Сделать git-коммит** (операция массовая, откатываться проще git-ом).
3. Tools → Execute Python Script… → выбрать скрипт.
   Первый запуск идёт в режиме проверки и только печатает план в Output Log.
4. Убедиться, что план разумный → открыть скрипт, поставить `DRY_RUN = False` → запустить снова.
5. ПКМ по `/Game` → **Fix Up Redirectors in Folder**.
6. Ctrl+Shift+S (сохранить всё), закоммитить.

**Что получится:**

```
/Game/XRU1Game/Units/
    BP_Unit_Assault / Sniper / Medic / Tank / Marauder
    Meshes/   SK_Mannequin, SK_Mannequin_AnimLib
    Anim/     Cover/ Crouch/ Revive/ Rifle/ Death/   (+ ABP_Soldier, Montages/)
    Weapons/  AssaultRifle/ SMG/ LMG/ Sniper/ Common/
```

После этого всё, что осталось вне `/Game/XRU1Game/` и `/Game/TopDown/`, — мусор
шаблона, и его можно удалять пачкой (сначала Reference Viewer на всякий случай).

- [ ] Скрипт отработал, Fix Up Redirectors сделан, проект открывается без ошибок.

### E1.0-bis ⭐ УЖЕ ПЕРЕНЕСЕНО АГЕНТОМ (2026-07-25) — только подключить

Разбор `UE-HW/Content/OtherAssets/FreeAnimationLibrary` и
`UE-HW/Content/InfimaGames/ModernGunsBundle`. Скопировано файловой системой с
сохранением относительных путей (оба проекта UE 5.7, ассеты самодостаточны —
ссылаются только внутрь своих папок, проверено чтением ссылок в `.uasset`).

| Что | Куда легло | Зачем |
|---|---|---|
| **6 анимаций укрытия** `anim_CoverDown_Idle_L/R`, `_Look_L/R`, `_Loop_L/R` | `/Game/OtherAssets/FreeAnimationLibrary/Animations/Cover/` | ⭐ **Это ровно то, чего не хватало.** `Idle_L/R` — прижаться к стене слева/справа, `Look_L/R` — выглянуть |
| **4 анимации раненого** `anim_Downed_Idle_R`, `anim_Knocked_Reviver/Reviving`, `anim_Self_Revive_F_R` | `.../Animations/Revive/` | поза `Downed` и подъём медиком — механики уже есть в коде |
| `SK_Mannequin` (скелет библиотеки, 0.19 МБ) | `.../Demo/Characters/Mannequins/Meshes/` | без него анимации не откроются |
| **4 ствола ПОЛНОСТЬЮ** (меши + материалы + текстуры) | `/Game/InfimaGames/ModernGunsBundle/` | по стволу на класс |

**Итого 285 файлов, 1.6 ГБ.**

**Раскладка оружия по классам:**

| Класс | Оружие | Меш |
|---|---|---|
| Штурмовик (`BP_Unit_Assault`) | автомат | `ModernAssaultRifle/Meshes/SK_AssaultRifle_Frame` |
| Медик (`BP_Unit_Medic`) | компактный ПП | `ModernSMG/Meshes/SK_SMG_Frame` |
| Танк (`BP_Unit_Tank`) | пулемёт | `ModernLightMachineGun/Meshes/SK_LightMachineGun_Frame` |
| Снайпер (`BP_Unit_Sniper`) | снайперская | `ModernSniper/Meshes/SK_Sniper_Frame` |
| Мародёр (`BP_Unit_Marauder`) | автомат (переиспользуем) | `SK_AssaultRifle_Frame` |

> 💡 **Текстуры 4K — сжать в редакторе.** Выделить всё в
> `Weapons/*/Textures` → ПКМ → **Asset Actions → Bulk Edit via Property Matrix**
> → выставить `Maximum Texture Size = 512` (для тактической камеры за глаза) и
> `Compression Settings` по типу карты. Ассеты на диске останутся прежними, но в
> билд пойдут ужатыми; если надо ужать и на диске — после этого **Resave**.
> Это заметно облегчит git-LFS.

> ⚠️ **Анимации библиотеки привязаны к СВОЕМУ `SK_Mannequin`**, а не к нашему.
> Кости те же (UE5 Manny), поэтому ретаргет не нужен — достаточно совместимости:
> открыть **наш** скелет (`/Game/Characters/Mannequins/Meshes/SK_Mannequin` →
> вкладка Skeleton) → **Compatible Skeletons** → добавить
> `/Game/OtherAssets/FreeAnimationLibrary/Demo/Characters/Mannequins/Meshes/SK_Mannequin`.
> После этого анимации выбираются в нашем ABP напрямую. Если совместимость не
> сработает — `Retarget Animation Assets` (штатная процедура).

- [ ] Открыть XRU1, дождаться импорта новых ассетов.
- [ ] Добавить совместимость скелетов (см. врезку выше).
- [ ] Проверить, что `anim_CoverDown_Idle_Right` открывается и играет на нашем меше.

### E1.0 ШАГ 1 — Перенести присед из проекта `UE-HW` ✅ СДЕЛАНО ИГРОКОМ

Единственное, чего нет в нашем проекте. Источник проверен:
`D:/Unrial_Projects/UE-HW/Content/Characters/Animation/Crouch/`.

Что там лежит (всё на том же скелете UE5 Manny — **ретаргет не нужен**):

| Файл | Что это |
|---|---|
| `anim_Crouch_Idle` | стойка приседа — **минимально необходимое** |
| `anim_Crouch_Fwd/Bwd/Fwd_Left/Fwd_Right/Bwd_Left/Bwd_Right` | ходьба в приседе |
| `BS_Crouch_new` | готовый Blend Space приседа |
| `1/M_Neutral_Crouch_Loop_*` (12 шт.) | направленные лупы |
| `1/M_Neutral_Crouch_Idle_Turn_045..180_L/R` | повороты на месте |
| `ABP_Unarmed_Modify` | пример готового графа с приседом — **посмотреть как образец** |

**Как перенести:**
1. Открыть `UE-HW` вторым редактором (или закрыть XRU1 и открыть UE-HW).
2. Content Browser → папка `Characters/Animation/Crouch` → ПКМ → **Migrate**.
3. В диалоге указать `D:/Unrial_Projects/XRU1/Content`.
4. Migrate сам утянет зависимости (скелет, кривые). Если предложит скопировать
   `SK_Mannequin` — **отказаться нельзя**, но после переноса убедиться, что
   анимации ссылаются на НАШ скелет; иначе Retarget Animation Assets.
5. Заодно перенести `ABP_Unarmed_Modify` — он не нужен в игре, но полезен как
   рабочий пример графа с приседом.

- [ ] Анимации приседа лежат в `/Game/XRU1Game/Units/Anim/Crouch/`.
- [ ] Открывается `anim_Crouch_Idle`, скелет совпадает с нашим.

### E1.1 ШАГ 2 — Меш оружия в руке ⬅️ ТЫ ЗДЕСЬ

⚠️ **Без этого Rifle-анимации будут играть «с пустыми руками»** — руки сложены
под винтовку, а винтовки нет.

Меши **уже перенесены** (E1.0-bis), брать ничего не надо:

| Класс | Меш | Путь |
|---|---|---|
| Assault, Medic, Tank, **Мародёр** | `SK_AssaultRifle_Frame` | `/Game/InfimaGames/ModernGunsBundle/ModernAssaultRifle/Meshes/` |
| Sniper | `SK_Sniper_Frame` | `/Game/InfimaGames/ModernGunsBundle/ModernSniper/Meshes/` |

- [ ] В `BP_Unit_*` добавить **`Skeletal Mesh Component`** (меши скелетные, не
      статичные), **Parent Socket = `hand_r`** (сокет есть на скелете Manny).
- [ ] Подправить трансформ в вьюпорте: оружие должно лежать в правой руке
      стволом вперёд. Ориентир — повернуть по Yaw/Roll на 90° и подвинуть на
      несколько см; точные значения подбираются глазом за минуту.
- [ ] Материал: меши загрузятся серым (текстуры не переносили — см. E1.0-bis).
      Назначить любой тёмный `MI` из проекта либо оставить как есть.
- [ ] `Collision → NoCollision` на компоненте оружия: оно не должно ни резать
      навмеш, ни попадать в трейсы укрытия/линии огня.

### E1.2 ШАГ 3 — Создать `ABP_Soldier`

В проекте **один** Animation Blueprint — `/Game/Characters/Mannequins/Anims/
Unarmed/ABP_Unarmed`, то есть БЕЗОРУЖНЫЙ шаблонный. Он и стоит у юнитов —
отсюда «бойцы бегают без оружия».

- [ ] Duplicate `ABP_Unarmed` → `/Game/XRU1Game/Units/Anim/ABP_Soldier`.
- [ ] `BP_Unit_Assault/Sniper/Medic/Tank/Marauder` → компонент `Mesh` →
      **Anim Class = `ABP_Soldier`**.

### E1.3 ШАГ 4 — Переменные ABP (Event Blueprint Update Animation)

Из `TryGetPawnOwner` → Cast to `UnitBase` → **`Get Visual State`** (один узел!)
разложить в переменные ABP:

| Переменная ABP | Из чего | Тип |
|---|---|---|
| `Pose` | `VisualState.Pose` | `EUnitPose` |
| `CoverSideY` | `VisualState.CoverDirectionLocal.Y` | float |
| `CoverSideX` | `VisualState.CoverDirectionLocal.X` | float |
| `bMoving` | `VisualState.bMoving` | bool |
| `Speed` | `Velocity.Size2D()` пешки | float |

⚠️ **Не собирать состояние самому** (теги ASC, `IsDead`, компонент укрытий) —
именно ради этого написан `FUnitVisualState`. Любой второй источник разойдётся.

### E1.4 ШАГ 5 — Blend Space стойки и приседа

- [ ] `BS_Rifle_Locomotion` (1D, ось Speed 0…600): 0 = `MF_Rifle_Idle_ADS`,
      ~150 = `MF_Rifle_Walk_Fwd`, ~450 = `MF_Rifle_Jog_Fwd`.
- [ ] Присед — использовать перенесённый `BS_Crouch_new` либо просто
      `anim_Crouch_Idle` (юнит в укрытии стоит на месте, ходьба в приседе нам
      не нужна).

### E1.5 ШАГ 6 — Стейт-машина «Поза»

⚠️ **«Вжаться в высокое укрытие» отдельной анимацией НЕ делаем** (решение
2026-07-25). Вместо неё C++ по прибытии зовёт `AUnitBase::HugCover()`: юнит
разворачивается **лицом к стене по её нормали** и подтягивается к ней
вплотную. Поза за укрытием читается сама, если боец стоит вплотную и лицом к
стене, — набор cover-анимаций для этого не нужен.

| Состояние | Анимация | Переход по |
|---|---|---|
| `Idle` | `MF_Rifle_Idle_ADS` | `Pose == Stand` |
| `Locomotion` | `BS_Rifle_Locomotion` по `Speed` | `Pose == Moving` |
| `CrouchCover` | **`anim_CoverDown_Idle_Left` / `_Right`** по знаку `CoverSideY` | `Pose == CrouchCover` |
| `HighCover` | **`anim_CoverDown_Idle_Left` / `_Right`** (та же пара; юнит уже прижат и развёрнут кодом) | `Pose == HighCover` |
| `Overwatch` | `MF_Rifle_Idle_ADS` + Aim Offset вверх | `Pose == Overwatch` |
| `Hunkered` | `anim_Crouch_Idle` (глубокий присед, читается как «закрылся») | `Pose == Hunkered` |
| `Downed` | **`anim_Downed_Idle_R`** (луп) | `Pose == Downed` |
| `Dead` | `MM_Death_Front_01`, последний кадр | `Pose == Dead` |

⚠️ **Выбор стороны прижимания — по `CoverSideY`.** Это `VisualState.CoverDirectionLocal.Y`
(стена в осях юнита): `> 0` → стена справа → `anim_CoverDown_Idle_Right`,
`< 0` → `_Left`. Ровно ради этого поле и считается в C++.

💡 `anim_CoverDown_Look_Left/Right` — «выглянуть из укрытия». Пригодятся, если
захочешь дополнить выбег на пик (E1.8) короткой анимацией выглядывания перед
выстрелом. Не обязательны.

- [ ] Собрать машину, переходы — по одной переменной `Pose`.
- [ ] Blend time между позами 0.15–0.25 с.
- [ ] Aim Offset из `MM_Rifle_Idle_ADS_AO_CU/CD/CC` поверх (по питчу к цели).

### E1.6 ШАГ 7 — Default Slot

- [ ] **Default Slot** между стейт-машиной и Output Pose — обязательно, иначе
      монтажи не будут видны вообще. Это самая частая ошибка на этом этапе.

### E1.7 ШАГ 8–10 — Монтажи (создать из существующих секвенций)

**Монтажей ПЯТЬ, а не шесть** — `FireMontageStepOut` больше не нужен отдельной
анимацией (см. ниже).

| Монтаж | Из чего собрать | Куда назначить |
|---|---|---|
| `AM_Fire_Open` | `MM_Rifle_Fire` | `BP_Unit_* → FireMontageOpen` |
| `AM_Fire_OverCover` | `MM_Rifle_Fire` (тот же; отличается тем, что играется из позы приседа) | `FireMontageOverCover` |
| `AM_HitReact` | `MM_HitReact_Front_Lgt_01` (или рандом из 8) | `HitReactMontage` |
| `AM_Death` | `MM_Death_Front_01` (или рандом из 6) | `DeathMontage` |
| `AM_Overwatch_Enter` | `MM_Rifle_Equip` | `OverwatchEnterMontage` |

⚠️ **`FireMontageStepOut` оставить ПУСТЫМ.** Решение 2026-07-25: выглядывание
делается не анимацией, а физическим перемещением — юнит **выбегает в точку
пика, доворачивается на врага, стреляет обычным `AM_Fire_Open` и возвращается**.
Точку отдаёт `GetFireMontageFor` третьим параметром (`OutFiringEyeLocation`).
Если слот пуст, C++ сам подставит `FireMontageOpen` — ломаться нечему.

### E1.8 Запуск монтажей и выбег на пик — из BP юнита

- [ ] `OnShotFired(Target)`:
      1. `GetFireMontageFor(Target)` → даёт монтаж, `OutStance`, `OutFiringEyeLocation`.
      2. Если `OutStance == StepOut`: `AI Move To` в `OutFiringEyeLocation`
         (спроецированную на пол) → по прибытии `Face Actor Towards` цели →
         `Play Anim Montage` → по окончании `AI Move To` обратно в исходную
         точку → `HugCover()`.
      3. Иначе просто `Play Anim Montage` (стойка уже верная).
- [ ] `OnReactionShot(Target, bHit)` → то же самое.
- [ ] `OnDied` → `DeathMontage`.
- [ ] Попадание по юниту → `HitReactMontage`.

⚠️ **Тактическая клетка при этом не меняется.** Юнит возвращается, а укрытие,
щит и диск занятости считаются от его домашней точки. Это тот же инвариант
§II.6 п.5 в [11_COVER_AND_ENEMY_PLAN](11_COVER_AND_ENEMY_PLAN.md), только
теперь «визуальный сдвиг» реализован реальным перемещением с возвратом, а не
Root Motion.

### E1.9 Разное оружие у классов (автомат / снайперка)

Вопрос: «будет ли анимация адаптивна и как стакается с приседом».

**Ответ: да, потому что анимация не знает про оружие.** Rifle-набор — это поза
рук «держу длинноствол»; и автомат, и снайперка держатся одинаково. Меняется
только МЕШ в сокете `hand_r`.

- [ ] Один `ABP_Soldier` на всех. Разное оружие = разный меш в `BP_Unit_*`.
- [ ] Если захочется отличать визуально сильнее — добавить в ABP переменную
      `WeaponType` и подменять Idle/Fire на уровне стейта. **Для курсовой не
      нужно**: на тактической камере разницы стойки не видно, а лишний набор
      анимаций надо где-то взять.
- [ ] **Стакинг с приседом работает сам:** присед — это ПОЗА (стейт-машина),
      выстрел — ДЕЙСТВИЕ (монтаж через Default Slot). Монтаж накладывается
      поверх текущей позы, поэтому «выстрел из приседа» получается без
      отдельной анимации. Если верх тела в приседе выглядит криво — сделать
      `AM_Fire_*` аддитивным и/или ограничить слот костью `spine_01` через
      Layered Blend per Bone.

### E1.10 Приёмка E1

- [ ] У низкого укрытия боец **приседает**, стреляет из приседа.
- [ ] У высокого — стоит **вплотную и лицом к стене** (это делает `HugCover`).
- [ ] Стойка `StepOut`: **выбегает за угол, доворачивается, стреляет,
      возвращается** в укрытие.
- [ ] В чистом поле — обычная стойка и обычный выстрел.
- [ ] Наблюдение читается позой (оружие вскинуто).
- [ ] Смерть и реакция на попадание проигрываются.
- [ ] ⚠️ После выбега на пик щит цели и укрытие в HUD **не изменились** —
      юнит вернулся в свою клетку.

---

## Блок E2 — Проверить/поправить свойства в BP

### E2.1 `BP_TacticalCameraPawn` — блок `Tactics|Camera|Shot` заменён дважды
Старые поля (`ShotFrameZoom*`, `ShotFramePitch*`, `ShotFrameYawOffset`,
`ShotFrameLookHeight`) удалены из C++ — переопределения в BP отпали.

- [ ] Убедиться, что в BP не осталось «висящих» переопределений.
- [ ] Проверить новые дефолты и при желании подстроить:
      `ShotFrameBackNear/Far` (85/190), `ShotFrameShoulderNear/Far` (75/95),
      `ShotFrameHeightNear/Far` (45/110), `ShotFrameFovSafety` (0.7),
      блок `Tactics|Camera|Shot|Score` — verbatim-веса XCOM, трогать не надо.

### E2.2 `DA_CoverTuning`
- [ ] `CoverTraceChannel` удалён из C++ — поле должно пропасть из ассета.
- [ ] Проверить `MinSpreadDistance` **не здесь** (он на контроллере AI).

### E2.3 `BP_UnitAIController` (если есть BP-наследник)
- [ ] Категория `Tactics|AI|Weights`: убедиться, что нет старых
      переопределений `MinSpreadDistance = 250` и `SpreadPenaltyMultiplier = 0.4`
      — они перекроют новые verbatim-значения 576 / 0.2.
- [ ] Новые поля: `AllyVisibilityWeight` (10), `OverwatchExposurePenalty` (30),
      `InvestigateOverwatchChance` (0.5).

### E2.4 `GM_Tactics` — пресеты сложности
- [ ] У `DifficultyParams` появилось поле `MaxAttackersPerTurn`.
      Ожидание: Easy 4, Medium 6, Hard −1. Если BP переопределяет карту целиком —
      проставить вручную.

---

## Блок E3 — Уровень `Lvl_TopDown`

- [ ] **Уступы разной высоты** — проверить бонус/штраф высоты (±20).
- [ ] Укрытия целевого дизайна: `WorldStatic` 60 см = Half, 150 см = Full
      (высоты считаются от ПОЛА).
- [ ] Длинная глухая стена ≥3 м — проверить, что она честно рвёт LOS.
- [ ] Узкий пиллар — проверить peek с обеих сторон.

---

## Блок E4 — Разное

- [ ] `WBP_TacticalHUD`: проверить, что жёлтый щит (`Flanked`) виден и в панели
      цели, и над головой.
- [ ] Проверить `Mesh → Anim Class` у всех пяти BP юнитов (см. E1.0).
- [ ] Оружие в руках: есть ли `SK_Rifle`/сокет `hand_r`. Если нет меша оружия —
      анимации Rifle будут играть «с пустыми руками».

---

## Чего в этом файле НЕТ

Кодовые задачи ведутся в [13_AI_STATE_MACHINE_PLAN.md](13_AI_STATE_MACHINE_PLAN.md)
(фазы A/W/S) и [11_COVER_AND_ENEMY_PLAN.md](11_COVER_AND_ENEMY_PLAN.md) (фазы Ф).
Открытые кодовые: **A6** (профили весов), рандомизация при близких скорах,
scamper, **Ф11** (превью peek), **Ф12** (крит от фланга).
