# 14 — Анимации: схема и инвентаризация

> **Статус:** подготовка завершена, сборка графа `ABP_Solider` в работе —
> пройдены шаги 4–5 (переменные ABP, Blend Space), стейт-машина позы (шаг 6)
> почти собрана: осталось добавить **Inertialization** и проверить сторону
> `Left`/`Right` в PIE (см. 15-й док §6.3/E1.6). Текущий фронт — шаг 7.
> Пошаговая инструкция для редактора — [15_EDITOR_TASKS.md](15_EDITOR_TASKS.md),
> блок E1.
>
> Этот документ отвечает на два вопроса: **почему граф собираем свой**, а не
> тянем готовый проект, и **что во что идёт** — судьба каждого анимационного
> ассета в проекте. Источники по движку — §5.

---

## 0. Короткий ответ

**Достраиваем свой ABP. Скачивать ничего не надо — все нужные анимации уже
лежат в проекте.**

1. **Систему движения не трогаем.** `ACharacter` + `CharacterMovementComponent`
   + `UCrowdFollowingComponent` + навмеш — на этом держатся зона хода, бюджет
   пути, диски занятости и весь AI. Любой «готовый проект» тянет свою систему
   движения и ломает это целиком.
2. **Анимации есть.** В `Units/Anim/` — 114 ассетов (`AnimSequence`/`BlendSpace`),
   включая полный Rifle-набор (idle ADS, fire, dry fire, equip, reload,
   aim-офсеты, walk/jog по 8 направлений, прыжок), 8 hit-react, 6 смертей,
   присед с доворотами, укрытие, revive. Полная раскладка — §2.
3. **Схема ABP — гибрид**, стандартный для тактик: стейт-машина держит
   локомоцию и ПОЗУ, монтажи через Default Slot играют ДЕЙСТВИЯ (выстрел,
   реакция на попадание, смерть, вход в наблюдение).
4. **C++ готов.** `FUnitVisualState`, `GetVisualState()`, `GetFireMontageFor()`,
   `HugCover()`, `FaceTowardsSmooth()`, слоты монтажей — §3. В редакторе
   остаётся собрать граф.

---

## 1. Почему не «взять готовый проект целиком»

| Вариант | Что даёт | Почему не подходит |
|---|---|---|
| **GASP как основа проекта** | 500+ анимаций, Motion Matching, мгновенно «дорого выглядит» | Построен вокруг Motion Matching + Chooser, а в 5.7 ещё и вокруг плагина **Mover**, который ЗАМЕНЯЕТ `CharacterMovementComponent`. У нас на CMC завязаны path following, Detour Crowd, диски занятости и вся зона хода. Это не интеграция, это переписывание ядра движения |
| **Lyra** | продакшн-каркас, GAS уже внутри | Сетевой шутер. Тактического движения по навмешу нет вообще, объём чужого кода несопоставим с курсовой |
| **Платные паки «Rifle Shooter Pro» и подобные** | готовые cover/crouch/aim-наборы | Деньги, и всё равно ретаргет + раскладка по стейтам руками |

⚠️ **Главное:** Motion Matching решает задачу «естественно двигаться в
произвольном направлении по вводу игрока». У нас юнит идёт по посчитанному
маршруту навмеша, один за ход, камера смотрит сверху. Задачи, которую решает
Motion Matching, у нас нет — а цена (замена системы движения) максимальная.

Для пошаговых игр с далёкой камерой тонкость переходов локомоции почти не
читается: вложение в неё окупается хуже всего.

---

## 2. Инвентаризация: что во что идёт

Все анимации и меши — под `/Game/XRU1Game/Units/`. Скелет — наш UE5 Manny
(`Units/Meshes/SK_Mannequin`): Rifle/Death ретаргета не требуют, Crouch
отретаргечен, Cover/Revive играют через **Compatible Skeletons** со своим
`SK_Mannequin_AnimLib`.

| Группа | Ассеты | Куда идёт в графе |
|---|---|---|
| Rifle/Idle | `MF_Rifle_Idle_ADS` (1) | центр `BS_Rifle_Locomotion` (Speed=0), стейты `Idle` и `Overwatch` |
| Rifle/Walk, 8 направлений | `MF_Rifle_Walk_{Fwd,Bwd,Left,Right,Fwd_Left,Fwd_Right,Bwd_Left,Bwd_Right}` (8) | сэмплы `BS_Rifle_Locomotion` на Speed≈150 |
| Rifle/Jog, 8 направлений | `MF_Rifle_Jog_*` (8) | сэмплы `BS_Rifle_Locomotion` на Speed≈450 |
| Rifle/Fire | `MM_Rifle_Fire` (1) | `AM_Fire_Open` |
| Rifle/DryFire | `MM_Rifle_DryFire` (1) | `AM_Fire_OverCover` — короче и без полной отдачи: снимок из-за укрытия обязан отличаться от выстрела в полный рост |
| Rifle/Equip + Reload | `MM_Rifle_Equip`, `MM_Rifle_Reload` (2) | оба в `AM_Overwatch_Enter`, два бита одного монтажа (вскинул → дослал патрон) |
| Rifle/AIM | `AO_Rifle`, `MM_Rifle_Idle_ADS_AO_CU/CD/CC` (4) | Aim Offset поверх позы |
| Rifle/HitReact | 8 | `AM_HitReact`, выбор явным `Random Integer` |
| Rifle/Jump | `Start`, `Start_Loop`, `Apex`, `Fall_Loop`, `Fall_Land`, `RecoveryAdditive` (6) | стейты `Jump`/`Fall`/`Land` |
| Cover/Idle_L/R | 2 | **поза ожидания в укрытии** (не «вход» — состояние `Enter` в графе убрано, эти клипы статичны и используются как устойчивая поза, entry-состояние `Loop`) |
| Cover/Loop_L/R | 2 | ⚠️ **не используются** — это клип ХОДЬБЫ (переступание), не поза; в графе не назначены, см. §3.2 |
| Cover/Look_L/R | 2 | выглядывание из ожидания и обратно; сторона — `PeekSideLocal` (см. §3.2, смысл поля переопределён) |
| Crouch/Idle + 8 направлений | `anim_Crouch_Idle` + 8 | сэмплы `BS_Crouch` — поза `Hunkered` |
| Crouch/Turn-in-place (8) | `M_Neutral_Crouch_Idle_Turn_045/090/135/180_L/R` | стейт `TurnInPlace`: доворот на цель без смены клетки, бакет по `PendingTurnYaw` |
| Crouch/Loop-лупы (12) | `M_Neutral_Crouch_Loop_*` | ⚠️ не участвуют — см. §2.1 |
| Revive | 4 | стейт `Downed` + монтажи Reviver/Reviving/Self-Revive |
| Death | 6 | `AM_Death`, выбор явным `Random Integer` |
| Weapon meshes | 4 ствола с текстурами | `Units/Weapons/*/Meshes/`, сокет `hand_r` |
| **Unarmed/\* (27)** | Idle, `BS_Idle_Walk_Run`, Walk×8, Jog×8, Jump/Fall/Land×3, Attack×4, Dash, WallJump | ⚠️ без назначения — см. §2.1 |

### 2.1 Что остаётся без применения

**27 файлов `Anim/Unarmed/`** — шаблонный набор, от которого продублирован
`ABP_Solider`. После перестройки графа точек входа у них не останется:
локомоция и прыжок заменяются Rifle-версиями (юнит всегда с оружием),
`MM_Attack_01..03`/`MM_ChargedAttack` — рукопашная боёвка, которой в GDD нет,
`MM_Dash`/`MM_WallJump` — способности платформера, которых в тактике на навмеше
тоже нет.

**12 файлов `M_Neutral_Crouch_Loop_*`** — направленные лупы приседа. Заполненный
`BS_Crouch` их не использует (у него свои сэмплы `anim_Crouch_*`).

Оба набора — не «забыли подключить», а балласт без механики под него. Варианты:
удалить папки, когда граф перестанет на них ссылаться, либо сознательно
оставить про запас. Это решение дизайнера, не техническая недоработка.

⚠️ **Лицензию перенесённых ассетов проверить перед публикацией.** Шаблонные
анимации UE5 Manny вопросов не вызывают (часть движка); анимации из
`UE-HW`/`ModernGunsBundle` — по условиям соответствующего пака.

⚠️ **Текстуры оружия сжаты, не удалены:** 4K-PBR перенесены полностью, но
выставлен `Maximum Texture Size = 512` через Bulk Edit via Property Matrix — для
тактической камеры, где оружие занимает ~30 пикселей, этого достаточно, а вес в
git-LFS меньше на порядок.

⚠️ **Ассет называется `ABP_Solider`** (без второй «d»). Переименовать можно
(F2 в Content Browser поправит ссылки в пяти BP юнитов), но это косметика.

---

## 3. Схема ABP

```
                 ┌─────────────── Anim Graph ───────────────┐
  Стейт-машина   │  Idle / Locomotion / Jump-Fall-Land      │
  «ПОЗА»         │  CrouchCover, HighCover (Enter→Loop→Look)│
                 │  Overwatch / Hunkered / TurnInPlace      │
                 │  Downed / Dead                           │
                 └──────────────────┬───────────────────────┘
                                    │
                            [ Default Slot ]   ← сюда бьют МОНТАЖИ
                                    │
                                 Output Pose
```

**Стейт-машина держит ПОЗУ** (что юнит делает постоянно), **монтажи — ДЕЙСТВИЕ**
(что происходит один раз). Смешивать их нельзя: разовое действие в стейт-машине
даёт залипшие состояния.

### 3.1 Что даёт C++

| Что зовём из BP | Что отдаёт | Зачем |
|---|---|---|
| **`GetVisualState()`** → `FUnitVisualState` | `Pose` (Stand/Moving/CrouchCover/HighCover/Overwatch/Hunkered/Downed/Dead), `Cover`, `CoverDirection`, `CoverDirectionLocal`, **`PeekSideLocal`**, **`PendingTurnYaw`**, `bMoving`, `bPlayerSide` | ЕДИНЫЙ вход стейт-машины: одно чтение вместо опроса шести источников (теги ASC, флаги смерти, компонент укрытий, path following), которые расходятся между собой |
| **`GetFireMontageFor(Target, OutStance, OutEye)`** | `UAnimMontage*` + `EFiringStance` + точка, откуда реально идёт выстрел | BP не разбирает стойку сам: и монтаж, и геометрия берутся у того же кода, что решает бой |
| **`HugCover()`** | — | прижимание к укрытию: разворот **ВДОЛЬ стены лицом к краю укрытия** (не носом в стену — см. правило 3 в §3.2) + **подшаг** вплотную, ограниченный навмешем. И то и другое плавно, через `CharacterMovement`, — у юнита есть настоящая `Velocity`, поэтому шаг играет обычная локомоция, а `bMoving` на это время = true |
| **`FaceTowardsSmooth(Location)`** | — | плавный доворот на месте; полная дельта кладётся в `PendingTurnYaw`, по ней ABP выбирает клип `Turn_045/090/135/180` и сторону. Зовётся при наводке на цель |
| Слоты монтажей на `AUnitBase` | `FireMontageOpen`, `FireMontageOverCover`, `FireMontageStepOut`, `HitReactMontage`, `DeathMontage`, `OverwatchEnterMontage` | дизайнер назначает ассеты в BP юнита, C++ выбирает нужный |
| `OnShotFired`, `OnReactionShot`, `OnDied`, `OnDownedChanged`, `OnEvacuated`, `OnCoverStateChanged` | BP-хуки | точки запуска монтажей |
| `NotifyUnitStateChanged` | — | пересобирает `VisualState` и шлёт `OnUnitStateChanged`; зовут все системы после изменения состояния |

⚠️ **Приоритет позы совпадает с приоритетом иконки статуса в HUD**
(`GetStatusForUnit`): Dead → Downed → Hunkered → Overwatch → Moving → Full →
Half → Stand. Один порядок на иконку и на анимацию — иначе игрок видит в HUD
«глухая оборона», а в кадре бег.

### 3.2 Три правила, на которых держится схема

1. **Выглядывание — часть ПОЗЫ, а не пролог выстрела.** `Look_L/R` играются из
   ожидания в укрытии и возвращаются в него же. Перед выстрелом их играть
   нельзя: `OnShotFired` приходит уже после расчёта попадания, и задержка
   означала бы, что цель дёргается от попадания раньше, чем стрелок выстрелил.
2. **Пик — это перемещение, а не анимация.** При `EFiringStance::StepOut` юнит
   выбегает в точку из `OutFiringEyeLocation`, стреляет обычным `AM_Fire_Open` и
   возвращается. Поэтому `FireMontageStepOut` остаётся пустым (C++ подставит
   `FireMontageOpen`). Тактическая клетка при этом не меняется: укрытие, щит и
   диск занятости считаются от домашней точки — инвариант §II.6 п.5 в 11-м доке.
3. **Стойка в укрытии — ВДОЛЬ стены, а не носом в неё.** `anim_CoverDown_*`
   сняты для бойца, стоящего боком к укрытию и смотрящего на угол, из-за
   которого он выглядывает — это единственная стойка, при которой у клипов
   совпадают ожидаемая и фактическая ориентация. `HugCover()` разворачивает
   именно так: направление берёт `UCoverDetectionComponent::FindPeekEdgeSide`
   (шаг вдоль стены с `PeekEdgeStep`, пока трейс находит стену — тот же приём,
   что у огневых позиций), при отсутствии края — старый разворот к стене
   (глухая стена, выглядывать всё равно некуда).

   При такой стойке `PeekSideLocal` (−1 слева, +1 справа) можно снова читать
   напрямую из `CoverDirectionLocal.Y` — стена гарантированно сбоку. Это НЕ
   то же самое, что было раньше: первая редакция ставила юнита носом в стену
   (`CoverDirectionLocal ≈ (1,0,0)`, Y — шум) и вычисляла сторону отдельным
   трейсом до края; после того как стойку поменяли на «вдоль стены», этот
   отдельный трейс стал не нужен для выбора клипа — `PeekSideLocal` снова прямое
   чтение `Y`, а `FindPeekEdgeSide` используется только для ориентации `HugCover`
   и для флага «есть куда выглядывать» (`bHasPeekEdge`, если он равен false —
   стена глухая, `Look`-клип не запускается).

   ⚠️ C++ **не доворачивает корпус дополнительно** при входе в `Look`
   (`bTurnBodyOnPeek = false` по умолчанию) — весь поворот к краю уже заложен в
   сам клип. Если довернуть ещё и актором, углы складываются, и боец оказывается
   развёрнут почти на 180° — с этого и начинались «вывернутые» позы.

### 3.3 Набор монтажей (5 ассетов на 6 слотов)

1. `AM_Fire_Open` — выстрел стоя, `MM_Rifle_Fire`.
2. `AM_Fire_OverCover` — привстать из-за низкого укрытия, выстрел, сесть;
   источник `MM_Rifle_DryFire`.
3. `AM_HitReact` — реакция на попадание, случайный выбор из 8.
4. `AM_Death` — смерть, случайный выбор из 6.
5. `AM_Overwatch_Enter` — `MM_Rifle_Equip` → `MM_Rifle_Reload` одним монтажом.
6. `FireMontageStepOut` — **пустой намеренно** (см. правило 2 выше).

---

## 4. Приёмка

- У низкого укрытия боец **приседает**, при выстреле привстаёт и садится обратно.
- У высокого и низкого — стоит **вплотную, ВДОЛЬ стены, лицом к краю укрытия**
  (не носом в стену); к стене он **подшагивает** (виден шаг локомоции), а не
  телепортируется.
- В чистом поле — обычная стойка и обычный выстрел.
- Стойка `StepOut`: выбегает за угол, доворачивается, стреляет, возвращается;
  укрытие, щит в HUD и диск занятости при этом **не меняются**.
- Стоя в укрытии, юнит время от времени **выглядывает** (`Look_*`) и
  возвращается в ожидание; у длинной глухой стены не выглядывает
  (`bHasPeekEdge == false`).
- Долгое ожидание идёт на позе `anim_CoverDown_Idle_*`, не дёргается и не
  зацикливается на переходе в `Look` и обратно.
- Смена цели без смены клетки — юнит **доворачивается анимацией**
  (`Turn_045/090/135/180`), а не щёлкает в новый ракурс за кадр.
- Огонь из-за низкого укрытия (`OverCover`) визуально отличается от открытого
  (короче, без полной отдачи).
- Наблюдение читается позой, вход в него — два движения подряд.
- Ходьба/бег — по всем 8 направлениям, включая движение спиной и боком без
  разворота модели.

---

## 5. Источники

Проверялись 2026-07-25.

1. **Game Animation Sample Project, документация Epic** — состав, скелет
   UEFN_Mannequin, требование Motion Matching/Chooser, перенос через
   Migrate/Export, ретаргет через IK Retargeter:
   [dev.epicgames.com — Game Animation Sample Project](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine)
2. **Epic, анонс проекта** — «500+ game-ready анимаций, рантайм-ретаргет на
   скелет UE5 Mannequin»:
   [unrealengine.com/blog/game-animation-sample](https://www.unrealengine.com/blog/game-animation-sample)
3. **Epic, обновление GASP в 5.7** — переход на плагин **Mover**, smart objects,
   locomotion style (главный аргумент «не брать как основу»):
   [unrealengine.com/tech-blog — updates to GASP in UE 5.7](https://www.unrealengine.com/tech-blog/explore-the-updates-to-the-game-animation-sample-project-in-ue-5-7)
4. **Motion Matching, документация** — что это и для какой задачи:
   [dev.epicgames.com — Motion Matching](https://dev.epicgames.com/documentation/unreal-engine/motion-matching-in-unreal-engine)
5. **State Machines, документация** — стейт-машина как инструмент состояний
   движения:
   [dev.epicgames.com — State Machines](https://dev.epicgames.com/documentation/en-us/unreal-engine/state-machines-in-unreal-engine)
6. **Форумы Epic, «State Machine vs Anim Montage»** — гибрид «стейт-машина +
   Default Slot для монтажей» как рекомендуемая практика:
   [forums.unrealengine.com — advice on state machine versus montage](https://forums.unrealengine.com/t/advice-on-when-to-choose-to-use-a-state-machine-versus-an-animation-montage-help/138994),
   [forums.unrealengine.com — when to use State Machine or Anim Montage](https://forums.unrealengine.com/t/when-to-use-state-machine-or-anim-montage/138686)
