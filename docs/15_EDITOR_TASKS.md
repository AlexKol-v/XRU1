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

## Блок E0 — Структура Content и перенос ассетов ✅ ЗАВЕРШЕНО (2026-07-26)

Реорганизация и перенос из донора выполнены, весь юнитовский контент лежит по
единой чистой структуре. Подробный ход работ — ниже в блоке E1 (шаги
E1.0-ter/E1.0-bis/E1.0 оставлены как есть, историческая ценность: там разбор
конкретных ловушек порядка операций). Итоговое состояние на 2026-07-26:

```
/Game/XRU1Game/Units/
    BP_Unit_Assault / Sniper / Medic / Tank / Marauder      — 5 юнитов, Mesh->Anim Class = ABP_Solider
    Anim/
        ABP_Solider                — создан, назначен всем 5; граф пока НЕ переделан (заготовка)
        Cover/    (6)   anim_CoverDown_Idle_L/R, _Look_L/R, _Loop_L/R
        Crouch/   (31)  anim_Crouch_*, BS_Crouch_new, повороты/лупы — ОТРЕТАРГЕЧЕНО на наш скелет
        Death/    (6)   MM_Death_*
        Revive/   (4)   anim_Downed_Idle_R, anim_Knocked_*, anim_Self_Revive_F_R
        Rifle/    (39)  MM_Rifle_*, AIM/, HitReact/, Jog/, Jump/, Walk/
        Unarmed/  (27)  MM_Idle, BS_Idle_Walk_Run, Jump/ — используется заготовкой ABP_Solider
    Meshes/
        SKM_Manny_Simple, SKM_Quinn_Simple   — тела юнитов (Manny/Quinn), материалы восстановлены
        SK_Mannequin                          — ОСНОВНОЙ скелет (у него же Compatible Skeletons)
        SK_Mannequin_AnimLib                  — скелет анимационной библиотеки (Cover/Revive)
        Materials/, Textures/, Rigs/           — зависимости тел (Control Rig и т.д.)
    Weapons/
        AssaultRifle/ SMG/ LMG/ Sniper/ Common/   — по стволу на класс, ПОЛНОСТЬЮ (меш+материалы+текстуры+BP)
```

Что за этим стоит и что важно знать при дальнейшей работе:

- **Скрипты переноса** `Content/Python/reorganize_assets.py` и `cleanup_step2.py`
  оставлены в проекте как исторический артефакт (не перезапускать — пути-источники
  уже не существуют). Правило на будущее, если понадобится похожая массовая
  операция: **порядок Save All → Fix Up Redirectors, не наоборот** (обоснование —
  врезка в E1.0-ter ниже).
- **Мусор шаблона Top Down удалён** (`/Game/Characters`, `/Game/InfimaGames`,
  `/Game/OtherAssets`, дублирующий `/Game/Cursor`, `BP_TopDownCharacter`,
  `BP_TopDownGameMode`) — перед каждым удалением проверялась зависимость через
  `unreal_asset_dependencies`, что удаляемое не используется реальным уровнем
  `Lvl_TopDown`. **Уровень и его рабочее окружение (`TopDown/LevelPrototyping`,
  `MI_Colorway`) не тронуты.**
- **`Config/DefaultEngine.ini`**: `GlobalDefaultGameMode` переключён с удалённого
  `BP_TopDownGameMode` на `/Game/XRU1Game/Core/GM_Tactics` — это только
  fallback-дефолт движка, `Lvl_TopDown` его не использует (у уровня свой
  `WorldSettings → GameMode Override = GM_Tactics`, проверено).
- 🔴 **Найдена и починена (2026-07-26) битая ссылка на скелет у Cover/Revive.**
  10 анимаций (`Anim/Cover/*` — 6 шт., `Anim/Revive/*` — 4 шт.) после переноса
  скелета библиотеки (`SKELETON_MOVE` в первом скрипте) остались ссылаться на
  СТАРЫЙ путь `/Game/OtherAssets/.../SK_Mannequin`, который к моменту финальной
  чистки был физически удалён — `Skeleton` этих анимаций указывал в никуда
  (`LoadErrors: … зависимый пакет … был недоступен`). Починено переназначением
  `Skeleton` на `/Game/XRU1Game/Units/Meshes/SK_Mannequin_AnimLib` (это тот же
  самый скелет библиотеки, просто по новому адресу) и пересохранением всех 10
  файлов. **Урок:** после массового rename/move всегда проверять не только
  Content Browser (там всё выглядит нормально, пока ассет не открыт), а
  реально загрузить/пересохранить переехавшие ассеты и посмотреть Output Log
  на `LoadErrors` — Asset Registry может годами хранить мёртвую ссылку молча.
- **Наименования 4 BP оружия слегка разнобойные** (память о починке
  «Custom version too new», см. E1.0-bis ниже): `BP_AssaultRifle_Default`,
  `BP_LMG_Default` (переименованы), но `BP_SMG_Default_Example`,
  `BP_Sniper_Default_Example` (остались с суффиксом донора). Работать не
  мешает, косметика; переименовать при желании через Content Browser (rename
  сам поправит ссылки, тут они пока нигде не используются).

---

## Блок E1 — Анимации: сборка `ABP_Solider` ⭐ приоритет

Полный контекст и обоснование схемы — [14_ANIMATION_PLAN.md](14_ANIMATION_PLAN.md).
**C++ готов целиком, дописывать в коде нечего.** Перенос и чистка ассетов
(шаги 1 и 3 ниже) — готовы, блок E0 выше. Актуальный фронт работы — **шаг 2**
(меш оружия в руку) и **шаги 4–11** (граф `ABP_Solider`).

> ### 📋 КОРОТКИЙ ПУТЬ (делать строго по порядку)
>
> | # | Шаг | Где | Готово |
> |---|---|---|---|
> | 1 | Перенести crouch-анимации из `UE-HW` | Content Browser | [x] |
> | 2 | Найти и поставить меш винтовки в руку | `BP_Unit_*` | [ ] ⬅️ ТЫ ЗДЕСЬ |
> | 3 | Создать `ABP_Solider`, назначить 5 юнитам | Content Browser | [x] |
> | 4 | В `Event Update Animation` — один узел `Get Visual State` | `ABP_Solider` | [ ] |
> | 5 | Собрать Blend Space стойки и приседа | Content Browser | [ ] |
> | 6 | Собрать стейт-машину «Поза» (8 состояний) | `ABP_Solider` | [ ] |
> | 7 | Добавить **Default Slot** перед Output Pose | `ABP_Solider` | [ ] |
> | 8 | Создать 5 монтажей | Content Browser | [ ] |
> | 9 | Назначить монтажи в слоты юнитов | `BP_Unit_*` | [ ] |
> | 10 | Повесить монтажи на BP-хуки | `BP_Unit_*` | [ ] |
> | 11 | Проверить в PIE по чеклисту E1.10 | PIE | [ ] |
>
> ⚠️ Шаг 3 выполнен раньше шага 2 (не страшно — они не зависят друг от друга):
> `ABP_Solider` уже существует и назначен, но внутри у него пока граф-заготовка
> от `ABP_Unarmed` (Idle/Walk/Run/Jump без оружия) — шаги 4–7 его полностью
> перестраивают.
>
> Дальше — каждый шаг подробно **(пути уже актуальные, `/Game/XRU1Game/Units/...`)**.

### E1.0-ter ✅ ШАГ 0 — НАВЕСТИ ПОРЯДОК В `Content` — СДЕЛАНО, раздел ниже оставлен как история

> ✅ **Выполнено 2026-07-25/26.** Итоговая структура и найденные по пути
> проблемы — см. блок **E0** в начале файла. Раздел ниже оставлен целиком как
> запись о том, ЧТО именно пошло не так и почему — пригодится, если снова
> понадобится массовый rename/move ассетов. Если ты просто продолжаешь работу
> с шага 2/4 — этот раздел можно пропустить.

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
5. ⚠️ **Ctrl+Shift+S (Save All) — ОБЯЗАТЕЛЬНО ДО следующего шага.**
6. ПКМ по `/Game` → **Fix Up Redirectors in Folder**.
7. Ещё раз Ctrl+Shift+S, закоммитить.

> 🔴 **ПОРЯДОК ШАГОВ 5 И 6 КРИТИЧЕН — проверено на своей шкуре (2026-07-25).**
> Редиректор живёт до тех пор, пока ссылающийся на него ассет не будет
> ПЕРЕСОХРАНЁН. Схлопнешь редиректоры раньше — ссылка повисает навсегда, и
> починить её можно только руками, переназначив ассет.
>
> В первый прогон Save All сделали ПОСЛЕ Fix Up, и повисло три группы ссылок:
> - `MI_Manny_01/02_New` → текстуры тела ⇒ **юниты стали серыми**
>   (`Failed to compile Material Instance … Default Material will be used`);
> - `BS_Crouch_new` → 12 анимаций приседа;
> - 4 BP оружия → меши рамы/цевья/магазина/прицелов.
>
> Ничего не потерялось — всё лежит по новым путям, но переназначать пришлось
> вручную.

**Получилось (см. дерево в блоке E0 выше — совпадает с планом):** мусор шаблона
(`Characters/`, `InfimaGames/`, `OtherAssets/`, дублирующий `Cursor/`) удалён
пачкой после проверки `unreal_asset_dependencies`, что ничего из него не
держит `Lvl_TopDown`.

- [x] Скрипт отработал, Fix Up Redirectors сделан, проект открывается без ошибок.

> 🔴 **Дополнительная находка при финальной проверке (2026-07-26):** сам
> Fix Up Redirectors не покрыл ссылку `Skeleton` у 10 анимаций Cover/Revive —
> подробности и фикс см. блок **E0** («Найдена и починена битая ссылка»). Если
> увидишь `LoadErrors` на `SK_Mannequin` при открытии чего-то в `Anim/Cover`
> или `Anim/Revive` — это должно быть уже почищено; если нет, лечится так же:
> **Skeleton → `/Game/XRU1Game/Units/Meshes/SK_Mannequin_AnimLib` → Save**.

> 🔴 **НАХОДКА (2026-07-26, проверено через MCP `unreal_asset_dependencies`):
> Crouch-анимации привязаны к ЧУЖОМУ скелету.** Игрок перенёс Crouch из
> `UE-HW` вручную (Migrate), ДО того как появился скрипт реорганизации. Migrate
> сохранил их hard-reference `Skeleton` на `/Game/Characters/Mannequins/Meshes/
> SK_Mannequin` — это СТАРЫЙ шаблонный скелет. Подтверждено прямым запросом
> зависимостей `anim_Crouch_Idle` — как минимум 30 файлов держали `Characters/`
> живым.
>
> ✅ **РЕШЕНО (2026-07-25):** выбран вариант 1 — **Retarget Animation Assets →
> Duplicate Anim Assets and Retarget** на `/Game/XRU1Game/Units/Meshes/
> SK_Mannequin`, старые копии удалены. Сейчас все 31 файл `Anim/Crouch/`
> подтверждённо ссылаются на наш `SK_Mannequin` (проверено
> `unreal_asset_dependencies` на `anim_Crouch_Idle` — среди зависимостей
> `SKM_Manny_Simple` и `SK_Mannequin`, `Characters/` нет).
>
> ✅ **Compatible Skeletons у нашего `SK_Mannequin` заполнен** — подтверждено
> запросом зависимостей: в списке есть `SK_Mannequin_AnimLib` (скелет
> анимационной библиотеки, на нём играют Cover/Revive).

✅ **Оба «подводных камня» ниже — тоже закрыты, проверено 2026-07-26:**

1. **`ABP_Solider` не ссылается на `/Game/Characters/`.** Его зависимости —
   только `/Game/XRU1Game/Units/Anim/Unarmed/*` (Idle/BlendSpace/Jump/Fall/Land)
   и `SK_Mannequin`/`SKM_Manny_Simple`/`CR_Mannequin_FootIK` — все свои.
   Граф внутри пока не переделан под оружие/укрытия (это и есть шаги 4–7), но
   ссылок на удалённый шаблон нет — почистить папку `Characters/` было можно
   сразу, что и произошло.
2. **У снайперки `SM_Sniper_Scope` использует Render Target** (`RT_Sniper_Scope`
   через `MI_Sniper_Scope_Render`) — прицел рисует живую картинку каждый кадр.
   Для шутера от первого лица это нужно, для тактической камеры сверху — чистая
   трата кадра. **Назначить на слот прицела любой непрозрачный `MI`** (например
   `MI_Sniper_01`) либо просто не цеплять `SM_Sniper_Scope` к оружию.

### E1.0-bis ✅ ПЕРЕНЕСЕНО и РАЗЛОЖЕНО (2026-07-25/26) — итоговые пути

Разбор `UE-HW/Content/OtherAssets/FreeAnimationLibrary` и
`UE-HW/Content/InfimaGames/ModernGunsBundle`. Скопировано файловой системой
между проектами, затем разложено по чистой структуре реорганизацией (блок E0).

| Что | Где лежит сейчас | Зачем |
|---|---|---|
| **6 анимаций укрытия** `anim_CoverDown_Idle_L/R`, `_Look_L/R`, `_Loop_L/R` | `/Game/XRU1Game/Units/Anim/Cover/` | ⭐ **Это ровно то, чего не хватало.** `Idle_L/R` — прижаться к стене слева/справа, `Look_L/R` — выглянуть |
| **4 анимации раненого** `anim_Downed_Idle_R`, `anim_Knocked_Reviver/Reviving`, `anim_Self_Revive_F_R` | `/Game/XRU1Game/Units/Anim/Revive/` | поза `Downed` и подъём медиком — механики уже есть в коде |
| `SK_Mannequin_AnimLib` (скелет библиотеки, переименован во избежание коллизии имён) | `/Game/XRU1Game/Units/Meshes/` | Skeleton для Cover/Revive (см. Compatible Skeletons выше) |
| **4 ствола ПОЛНОСТЬЮ** (меши + материалы + текстуры) | `/Game/XRU1Game/Units/Weapons/*/` | по стволу на класс |

**Итого перенесено 285 файлов, ~1.6 ГБ** (текстуры позже сжаты до
`Maximum Texture Size = 512`, см. [14_ANIMATION_PLAN.md](14_ANIMATION_PLAN.md)).

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
> открыть **наш** скелет (`/Game/XRU1Game/Units/Meshes/SK_Mannequin` → вкладка
> Skeleton) → **Compatible Skeletons** → добавить
> `/Game/XRU1Game/Units/Meshes/SK_Mannequin_AnimLib`. После этого анимации
> выбираются в нашем ABP напрямую.

- [x] Открыть XRU1, дождаться импорта новых ассетов.
- [x] Добавить совместимость скелетов (см. врезку выше) — подтверждено:
      `SK_Mannequin_AnimLib` есть в зависимостях `SK_Mannequin`.
- [x] Проверить, что `anim_CoverDown_Idle_Right` открывается и играет на нашем
      меше — их `Skeleton` указывает на `SK_Mannequin_AnimLib` (починено
      2026-07-26, см. блок E0 «Найдена и починена битая ссылка»).

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

- [x] Анимации приседа лежат в `/Game/XRU1Game/Units/Anim/Crouch/` (31 файл).
- [x] Открывается `anim_Crouch_Idle`, скелет совпадает с нашим (отретаргечено).

### E1.1 ШАГ 2 — Меш оружия в руке ⬅️ ТЫ ЗДЕСЬ (следующий шаг)

⚠️ **Без этого Rifle-анимации будут играть «с пустыми руками»** — руки сложены
под винтовку, а винтовки нет.

Меши **уже перенесены** (E1.0-bis), с текстурами, брать ничего не надо:

| Класс | Меш | Путь |
|---|---|---|
| Assault, Medic, Tank, **Мародёр** | `SK_AssaultRifle_Frame` | `/Game/XRU1Game/Units/Weapons/AssaultRifle/Meshes/` |
| Sniper | `SK_Sniper_Frame` | `/Game/XRU1Game/Units/Weapons/Sniper/Meshes/` |

> 💡 Если захочется у каждого класса своё оружие визуально (а не общий
> автомат на четверых) — то же самое, но взять `SK_SMG_Frame`
> (`Weapons/SMG/Meshes/`) для Медика и `SK_LightMachineGun_Frame`
> (`Weapons/LMG/Meshes/`) для Танка — они тоже полностью готовы (меш + материал
> + текстуры). Раскладка по классам из E1.0-bis это и предполагала; таблица
> выше — минимальный вариант «на скорую руку» с одним автоматом на всех.

- [ ] В `BP_Unit_*` добавить **`Skeletal Mesh Component`** (меши скелетные, не
      статичные), **Parent Socket = `hand_r`** (сокет есть на скелете Manny).
- [ ] Подправить трансформ в вьюпорте: оружие должно лежать в правой руке
      стволом вперёд. Ориентир — повернуть по Yaw/Roll на 90° и подвинуть на
      несколько см; точные значения подбираются глазом за минуту.
- [ ] Материал уже назначен (текстуры перенесены и сжаты до 512px) — трогать
      не нужно, если только не хочется другой раскраски.
- [ ] `Collision → NoCollision` на компоненте оружия: оно не должно ни резать
      навмеш, ни попадать в трейсы укрытия/линии огня.

### E1.2 ШАГ 3 — Создать `ABP_Solider` ✅ СДЕЛАНО

> ✅ **Выполнено.** `ABP_Solider` существует и назначен `Mesh → Anim Class` у
> всех пяти `BP_Unit_*` (проверено `unreal_asset_dependencies` на каждом BP).
> Внутри — пока не переделанный граф-заготовка от `ABP_Unarmed` (стейт-машины
> `Locomotion`: Idle/Walk-Run и `Main States`: Locomotion/Jump/Fall Loop/Land,
> все анимации безоружные из `Anim/Unarmed/`). Шаги 4–7 ниже полностью
> перестраивают этот граф под оружие/укрытия/присед.
>
> ⚠️ Асcет называется **`ABP_Solider`** (опечатка донора, без второй «d») —
> ссылки в шагах 4–11 ниже используют это же имя.

- [x] Duplicate `ABP_Unarmed` → `/Game/XRU1Game/Units/Anim/ABP_Solider`.
- [x] `BP_Unit_Assault/Sniper/Medic/Tank/Marauder` → компонент `Mesh` →
      **Anim Class = `ABP_Solider`**.

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

- [ ] Один `ABP_Solider` на всех. Разное оружие = разный меш в `BP_Unit_*`.
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
