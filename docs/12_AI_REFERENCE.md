# 12 — AI XCOM 1/2: полный референс механик (источник правды для реализации)

> **Что это.** Выжимка того, как реально устроен тактический AI в XCOM: Enemy
> Unknown/Within и XCOM 2/WotC — по декомпилированным исходникам `XComGame`
> (UnrealScript), по конфигу `XComAI.ini` и по разборам моддерского сообщества.
> Документ — **инструкция к реализации**, а не пересказ: каждая механика
> заканчивается строкой «**У НАС:**» с решением, берём/не берём и почему.
>
> **Зачем отдельный файл.** [11_COVER_AND_ENEMY_PLAN.md](11_COVER_AND_ENEMY_PLAN.md)
> — про геометрию укрытий (что видно, откуда стреляем). Этот — про **принятие
> решений**. Ф9 из 11-го документа («AI на тех же правилах») — это точка стыка:
> она даёт AI правильную геометрию, а дальше AI живёт по правилам отсюда.
> План реализации — [13_AI_STATE_MACHINE_PLAN.md](13_AI_STATE_MACHINE_PLAN.md).

## Оглавление

- [0. Главный вывод: у решения ТРИ слоя, и стейт-машина — только один из них](#0-главный-вывод-у-решения-три-слоя-и-стейт-машина--только-один-из-них)
- [Часть I — Архитектура принятия решений](#часть-i--архитектура-принятия-решений)
  - [I.1 Три слоя](#i1-три-слоя)
  - [I.2 Слой 1: тревога (alert level) — это ЗНАНИЕ, а не поведение](#i2-слой-1-тревога-alert-level--это-знание-а-не-поведение)
  - [I.3 Слой 2: дерево поведения — выбор ДЕЙСТВИЯ](#i3-слой-2-дерево-поведения--выбор-действия)
  - [I.4 Слой 3: утилити-скоринг — выбор ТОЧКИ и ЦЕЛИ](#i4-слой-3-утилити-скоринг--выбор-точки-и-цели)
  - [I.5 Порядок «сначала действие, потом точка» — и почему это важно](#i5-порядок-сначала-действие-потом-точка--и-почему-это-важно)
- [Часть II — Память и знание о противнике](#часть-ii--память-и-знание-о-противнике)
  - [II.1 AlertData: единый формат «что я знаю»](#ii1-alertdata-единый-формат-что-я-знаю)
  - [II.2 Скоринг alert-записей](#ii2-скоринг-alert-записей)
  - [II.3 Как AI «сохраняет свою цель»](#ii3-как-ai-сохраняет-свою-цель)
- [Часть III — Выбор цели (target selection)](#часть-iii--выбор-цели-target-selection)
- [Часть IV — Выбор точки перемещения (destination scoring)](#часть-iv--выбор-точки-перемещения-destination-scoring)
  - [IV.1 Метрики тайла](#iv1-метрики-тайла)
  - [IV.2 Формула итогового скора](#iv2-формула-итогового-скора)
  - [IV.3 Таблица весовых профилей (verbatim из XComAI.ini)](#iv3-таблица-весовых-профилей-verbatim-из-xcomaiini)
  - [IV.4 Глобальные константы (verbatim)](#iv4-глобальные-константы-verbatim)
- [Часть V — Команда: поды, роли, координация](#часть-v--команда-поды-роли-координация)
- [Часть VI — Ограничители честности и темпа](#часть-vi--ограничители-честности-и-темпа)
- [Часть VII — Действия AI: каталог и разбор Наблюдения](#часть-vii--действия-ai-каталог-и-разбор-наблюдения)
  - [VII.1 Каталог действий](#vii1-каталог-действий)
  - [VII.2 Наблюдение (Overwatch) — правила XCOM 2](#vii2-наблюдение-overwatch--правила-xcom-2-)
- [Часть VIII — Что берём в XRU1: итоговая спецификация](#часть-viii--что-берём-в-xru1-итоговая-спецификация)
- [Часть IX — Источники](#часть-ix--источники)
- [Часть X — Журнал аудита документа](#часть-x--журнал-аудита-документа)

---

## 0. Главный вывод: у решения ТРИ слоя, и стейт-машина — только один из них

> **Уточнение формулировки (аудит 2026-07-24).** Ранняя редакция этого раздела
> называлась «XCOM — это не стейт-машина» и читалась как «стейт-машины —
> устаревший инструмент». Это неверно и не то, что имелось в виду. Точная
> формулировка ниже. Выбор архитектуры для нашего проекта — отдельный документ:
> [13_AI_STATE_MACHINE_PLAN §ADR-1](13_AI_STATE_MACHINE_PLAN.md#adr-1--архитектура-принятия-решений-ai).

Ошибка не в том, чтобы использовать стейт-машину. Ошибка — использовать **одну**
стейт-машину **для всего**: `Patrol → Investigate → Combat → Flanking →
Retreating → Suppressing`, где в каждом состоянии зашито поведение. Именно так
устроен наш сегодняшний `AUnitAIController`, и именно это ломается при росте.

В XCOM решение разложено на три слоя, и **у каждого свой инструмент**:

| Слой | Вопрос | Инструмент XCOM | Стейт-машина здесь? |
|---|---|---|---|
| **Знание** | что я знаю о враге | alert level + журнал наблюдений | **ДА** — и это правильно: состояний мало, переходы осмысленны |
| **Действие** | что я делаю сейчас | Behavior Tree | **НЕТ** — выбор из N вариантов, а не переход между состояниями |
| **Позиция / цель** | куда встать, в кого стрелять | утилити-скоринг | **НЕТ** — чистая функция от мира |

Почему на слое **действия** стейт-машина плоха — конкретно, без вкусовщины:

1. **Она отвечает не на тот вопрос.** FSM отвечает «в каком я состоянии и куда
   могу перейти». Нужен ответ на «какой из 6 доступных мне вариантов сейчас
   лучший». Это сравнение, а не переход.
2. **Переходы растут квадратично.** Добавить гранату в FSM = новое состояние +
   переходы из каждого существующего и в каждое существующее. Шесть действий —
   30 переходов; девять — 72. Добавить гранату в утилити = один новый
   оценщик, **ноль правок в существующих**.
3. **Состояние переживает ход, а решение — нет.** Дерево XCOM гоняется **заново
   на каждую активацию**: второе очко действия получает свежий прогон с уже
   изменившимся миром. «Я в состоянии Flanking, поэтому и вторым AP иду во
   фланг» — как раз то поведение, из-за которого AI выглядит тупым, когда
   обстановка сменилась.

**Что при этом остаётся за стейт-машиной и обязано за ней остаться:**

- **знание/тревога** (4 состояния, §I.2) — классический и уместный FSM;
- **фаза хода** (`ETurnPhase`) — уже так и сделано;
- **режим взаимодействия игрока** (`EPlayerTargetingMode`) — уже так и сделано;
- **визуальное состояние юнита для анимации** (`EUnitStance`) — Anim Graph
  State Machine, см. [13_AI_STATE_MACHINE_PLAN §II.3](13_AI_STATE_MACHINE_PLAN.md#ii3-ключевое-разделение-поза--действие).

То есть стейт-машин в проекте будет **четыре**, и все на своих местах. Спор
идёт ровно про один слой — выбор действия, — и там нужен скоринг.

> **Практический вывод.** «Полная стейт-машина AI» = тонкая FSM знания
> (`EUnitAlertState`, уже есть) + **утилити-выбор действия** (нужно построить) +
> скоринг цели и позиции (`FindCoverPoint`, нужно расширить).

---

# Часть I — Архитектура принятия решений

## I.1 Три слоя

```
                    ┌─────────────────────────────────────────┐
   ЗНАНИЕ           │  Alert level (Green/Yellow/Orange/Red)  │  ← что я знаю
                    │  + AlertData[] (журнал наблюдений)      │
                    └────────────────┬────────────────────────┘
                                     ↓ выбирает ветку
                    ┌─────────────────────────────────────────┐
   ДЕЙСТВИЕ         │  Behavior Tree (Selector/Sequence/…)    │  ← что я делаю
                    │  → (ability, target) | (move, profile)  │
                    └────────────────┬────────────────────────┘
                                     ↓ параметризует
                    ┌─────────────────────────────────────────┐
   ГДЕ / ПО КОМУ    │  Утилити-скоринг тайлов и целей         │  ← куда и по кому
                    │  веса профиля × метрики позиции         │
                    └─────────────────────────────────────────┘
```

Один прогон = одно действие. Юнит с 2 AP гоняет всё это дважды.

## I.2 Слой 1: тревога (alert level) — это ЗНАНИЕ, а не поведение

В XCOM 2 — статистика юнита `eStat_AlertLevel`:

| Уровень | Значение | Что означает | Поведение по умолчанию |
|---|---|---|---|
| **Green** | 0 | врага не видел, ничего подозрительного | патруль по маршруту группы; **шагом** |
| **Yellow** | 1 | есть подозрение: услышал звук, нашёл труп, засёк движение | идёт **разведать точку** AlertData; **бегом**; может применять пассивные способности |
| **Orange** | — | союзник видит врага, я — нет («знаю с чужих слов») | сближается на позицию, где сам увидит |
| **Red** | 2 | **сам видит** врага | полный боевой цикл |

Ключевые тонкости, которые легко упустить:

1. **Переход вниз есть.** Красная тревога спадает до жёлтой, если цель потеряна
   из виду, и жёлтая — до зелёной по истечении срока годности записи
   (`RemoveAlertDataOlderThanAge=10` ходов). У нас понижение red→yellow есть, а
   yellow→green делается по прибытии в точку — это допустимое упрощение.
2. **Оранжевая тревога — отдельный слой, а не оттенок жёлтой.** Она существует
   ради **командной работы**: «Я не вижу врага, но мой сосед видит — иду туда,
   откуда увижу и я». Без неё враги не подтягиваются к бою и дерутся поодиночке.
   В дереве это отдельная ветка `GenericOrangeMovement` → `TrySelectOrangeAlertAction`.
3. **Тревога у XCOM 2 — свойство ЮНИТА, но выставляется ГРУППОЙ.** Когда под
   активируется, alert поднимается **всем** его членам сразу.

> **У НАС:** `EUnitAlertState` (Patrol/Investigate/Combat) = green/yellow/red.
> **Не хватает Orange** — это главная дыра в командной игре: сейчас враг, не
> видящий цель лично, честно уходит в Investigate к «последней известной точке»,
> а точки может не быть вовсе. Добавляем 4-е состояние. Понижение по возрасту
> записи — добавляем (сейчас запись живёт вечно).

## I.3 Слой 2: дерево поведения — выбор ДЕЙСТВИЯ

`XComAI.ini`, синтаксис:

```
Behaviors=(BehaviorName=<имя>, NodeType=<тип>, Child[0]=<имя>, Param[0]=<число>, …)
```

**Типы узлов, реально используемые в XCOM 2:**

| NodeType | Семантика |
|---|---|
| `Selector` | OR: первый успешный ребёнок → успех; все провалились → провал |
| `Sequence` | AND: идём по детям, первый провал → провал |
| `Condition` | лист-предикат, резолвится по имени в C++ |
| `Action` | лист-действие (выбрать способность / найти точку / записать переменную) |
| `Inverter` | инвертирует результат ребёнка |
| `Successor` | всегда успех, чем бы ребёнок ни кончился |
| `RandSelector` | случайный выбор ребёнка по весам `Param[N]` (в процентах) |
| `RandFilter` | вероятностный «пропустить/не пропустить» — рандомизация поведения |
| `RepeatUntilFail` | цикл (используется для перебора целей/alert-записей) |
| `StatCondition` / `TargetStatCondition` | сравнение статов свой/цели |

**Корень:**

```
Behaviors=(BehaviorName=GenericAIRoot, NodeType=Selector,
  Child[0]=TryNonAggressiveBehavior,   ; лимит атакующих исчерпан → «изображаем занятость»
  Child[1]=TryMindControlledRoot,      ; я под контролем
  Child[2]="::CharacterRoot",          ; ← основная ветка, своя у каждого типа врага
  Child[3]=SkipMove)                   ; ничего не смог → пропуск
```

Обратите внимание на `Child[3]=SkipMove`: **у дерева всегда есть терминальный
фолбэк**. Ход юнита не может «зависнуть», если ни одно правило не сработало.

**Характерные листья-условия:** `IsRedAlert`, `IsOrangeAlert`, `IsYellowAlert`,
`ShouldPatrol`, `IsFlanked`, `IsFlankingTarget`, `HasGoodShotTarget`,
`HasKillShot`, `IsInDangerousArea`, `HasAmmo`, `IsLastActionPoint`,
`NotLastActionPoint`, `HasPriorityTarget`, `SafeToMove`, `IsGroupLeader`,
`IsFollower`, `IsMyJob-Soldier|Flanker|Leader|Support|Terrorist`,
`IsAbilityAvailable-<Ability>`, `HasHitAttackLimit`.

**Характерные листья-действия:** `SelectAbility-StandardShot`,
`SelectAbility-Overwatch`, `SelectAbility-StandardMove`, `FindDestination-<Profile>`,
`SetBTVar`, `AddToTargetScore_<N>`, `SetNextAlertData`, `DeleteCurrentAlertData`,
`UpdateBestAlertData`, `SkipMove`.

**Показательные готовые узлы (verbatim):**

```
Behaviors=(BehaviorName=ShootIfAvailable, NodeType=Sequence,
   Child[0]=IsAbilityAvailable-StandardShot, Child[1]=SelectTargetForStandardShot, Child[2]=SelectAbility-StandardShot)

Behaviors=(BehaviorName=TryOverwatch, NodeType=Sequence,
   Child[0]=IsAbilityAvailable-Overwatch, Child[1]=SelectAbility-Overwatch)

Behaviors=(BehaviorName=MoveDefensive,  NodeType=Sequence, Child[0]=SafeToMove, Child[1]=MoveDefensiveUnsafe)
Behaviors=(BehaviorName=MoveFlanking,   NodeType=Sequence, Child[0]=SafeToMove, Child[1]=MoveFlankingUnsafe)
Behaviors=(BehaviorName=MoveAggressive, NodeType=Sequence, Child[0]=SafeToMove, Child[1]=MoveAggressiveUnsafe)
Behaviors=(BehaviorName=FallBack,       NodeType=Sequence, Child[0]=SafeToMove, Child[1]=FallBackUnsafe)

Behaviors=(BehaviorName=NonAggressiveBehaviorFirstAction, NodeType=RandSelector,
   Child[0]=TryOverwatch, Param[0]=33, Child[1]=MoveFlankingOrDefensive, Param[1]=67)
```

Три вещи, которые стоит из этого выписать:

1. **`SafeToMove` — общий предохранитель перед ЛЮБЫМ перемещением.** Это
   BT-переменная, которую AI выставляет, оценив, сколько овервотчей/подавлений
   на него нацелено. XCOM не бегает под овервотч бездумно.
2. **Порядок в `Sequence` — это порядок проверок.** «Есть способность» → «есть
   цель» → «выбрать способность». Никогда не наоборот.
3. **Рандомизация встроена в дерево** (`RandSelector` с `Param` в процентах,
   `RandFilter`). Без неё AI читается наизусть со второго боя.

> **У НАС:** полноценный BT (движковый `UBehaviorTree` + Blackboard) — **избыточен**
> для курсовой на 5 классов и 1 архетип врага, и он плохо стыкуется с пошаговым
> «одно действие на активацию». Берём **идею**, а не движок: приоритетный список
> правил в C++, каждое правило = `Sequence` из проверок + выбор действия, с
> обязательным терминальным фолбэком. Это ровно то, что уже наметилось в
> `StepCombat`, — надо вынести из `switch` в явный список и добавить рандомизацию.

## I.4 Слой 3: утилити-скоринг — выбор ТОЧКИ и ЦЕЛИ

Дерево не решает, **куда** бежать и **в кого** стрелять. Оно решает
«отступаю по профилю `Fallback`» / «стреляю, цель выбрать скорингом», а числа
считает утилити.

Два независимых скоринга:

- **Скоринг тайлов** (Часть IV) — куда встать. Профиль весов задаёт дерево.
- **Скоринг целей** (Часть III) — по кому стрелять. Считается **сложением
  бонусов**, каждый бонус — узел дерева `AddToTargetScore_<N>`. То есть таблица
  приоритетов целей — тоже данные, а не код.

## I.5 Порядок «сначала действие, потом точка» — и почему это важно

В XCOM 2 (`XGAIBehavior`) последовательность одной активации такая:

1. `StartRunBehaviorTree()` → `InitBehaviorTree()`;
2. `StepProcessBehaviorTree()` — тикает дерево **порциями по кадрам** (не за один
   кадр: перебор тайлов дорогой, `BT_StepProcessDestinations()` идёт итеративно);
3. дерево кладёт результат в поля: `m_strBTAbilitySelection` (имя способности),
   `m_kBTCurrTarget` (цель), `m_vBTDestination` + `m_bBTDestinationSet` (точка);
4. `BTExecuteAbility()` — исполняет ровно то, что выбрано;
5. `OnBehaviorTreeRunComplete()` — сброс дерева; если AP остались, всё заново.

Провал дерева → `SkipTurn()`.

> **У НАС:** такая же дисциплина уже частично есть: `AdvanceTurnStep` вызывается
> повторно, пока есть AP. Чего нет — **явного разделения «решение» и
> «исполнение»**: сейчас `StepCombat` и решает, и сразу двигает/стреляет. Для
> отладки и для анимаций (Ф10) это стоит разделить: сначала получить структуру
> «решение», залогировать её, потом исполнить. См. `FAIDecision` в
> [13_AI_STATE_MACHINE_PLAN.md](13_AI_STATE_MACHINE_PLAN.md).

---

# Часть II — Память и знание о противнике

Это ответ на вопрос «как AI сохраняет свою цель». Ответ неожиданный: **XCOM
хранит не цель, а наблюдения**.

## II.1 AlertData: единый формат «что я знаю»

У каждого AI-юнита есть **массив записей AlertData**. Одна запись = одно
наблюдение, и в ней:

- **тип** — чем вызвана;
- **позиция** (тайл) — где это случилось;
- **возраст** — сколько ходов назад;
- **абсолютность знания** — «я вижу цель прямо сейчас» или «я знаю, что она
  была здесь»;
- **тег** — `"Defend"` / `"Advance"` (подсказка, как реагировать).

Типы записей (verbatim имена условий дерева):

| Условие | Причина | Обычная реакция |
|---|---|---|
| `AlertDataIsType-SeesSpottedUnit` | вижу помеченного врага | **red** |
| `AlertDataIsType-TookDamage` | по мне попали | red, если есть LOS к источнику |
| `AlertDataIsType-DetectedNewCorpse` | нашёл труп | **yellow** |
| `AlertDataIsType-DetectedSound` | услышал шум | **yellow** |
| `AlertDataWasSoundMade`, `AlertDataWasSoundScary` | оценка «насколько страшно» по радиусу | приоритет записи |
| `AlertDataWasEnemyThere` | «враг был здесь» | разведка |
| `AlertDataIsAbsoluteKnowledge` | вижу прямо сейчас | red |
| `AlertDataTileIsVisible` | точка записи уже просматривается | запись можно удалять |

Действия дерева над журналом: `SetAlertDataStack` (взять журнал в работу),
`SetNextAlertData` (следующая запись), `UpdateBestAlertData` (запомнить лучшую),
`DeleteCurrentAlertData` / `DeleteAlertDataIfValid` / `PurgeAlertDataIfNotScary`
(чистка). Глобальный срок годности: `RemoveAlertDataOlderThanAge=10`.

**Почему это лучше поля «последняя известная точка».** Мы сейчас держим один
`LastKnownThreatLocation`. Разница:

- два выстрела с разных сторон → у нас второй затирает первый, у XCOM обе записи
  живут и конкурируют по скору;
- дошёл до точки, никого нет → у нас состояние обнуляется, у XCOM удаляется
  **одна запись**, остальные остаются;
- «страшный» шум рядом важнее старого трупа вдалеке — это выражается скором, а
  не порядком присваивания.

## II.2 Скоринг alert-записей

Записи не берутся по очереди — они **сортируются скором**, куда входят:
близость, возраст, «страшность», видима ли уже точка (тогда запись
обесценивается), и `ScoreAlert_FormerKnowledge` — бонус записям о месте, где
враг уже был замечен раньше. Дерево делает `RepeatUntilFail` по журналу,
скорит каждую и выбирает лучшую (`UpdateBestAlertData`).

## II.3 Как AI «сохраняет свою цель»

Разделяем **три разных срока жизни** — их часто путают, и путаница даёт
метания:

| Срок жизни | Что хранится | Где в XCOM 2 |
|---|---|---|
| **на одно действие** | выбранная способность и её цель | `m_strBTAbilitySelection`, `m_kBTCurrTarget`, `m_arrBTTargetStack` |
| **на ход** | флаги хода: «уже двигался», «ход небезопасен» | BT-переменные `SafeToMove`, `NoMove` (через `SetBTVar`) |
| **между ходами** | знание о противнике, тревога, кэш известных врагов | `AlertData[]`, `eStat_AlertLevel`, `CachedKnownUnitRefs` |

Сама «цель» **между ходами не сохраняется**. Сохраняется знание, и следующий
ход цель выбирается заново скорингом. Это принципиально: цель, которая ушла в
укрытие/умерла/отошла, не «залипает».

Но есть три исключения, дающие ощущение упорства:

1. **Priority target** — `HasPriorityTarget` / `TargetIsPriorityUnit` / `+60` к
   скору. Это внешняя пометка (провокация, задание, эффект), а не память.
2. **Marked target** — `TargetAffectedByEffect-MarkedTarget` → `+45`.
3. **`PreselectedAbility`** — способность, «заряженная» на следующий ход
   (аналог нашего Overwatch).

> **У НАС:** провокация танка (`State.Taunting` + `TauntPriorityRadius`) — это
> ровно «priority target» XCOM. Правильно. Дополнительно нужно:
> **журнал AlertData вместо одиночной точки** и **удержание НАМЕРЕНИЯ, а не
> цели** — начатый манёвр не бросаем (у нас это `bManeuverInProgress`, механика
> верная, оставляем).

---

# Часть III — Выбор цели (target selection)

Цель выбирается сложением бонусов. Verbatim из `XComAI.ini`:

```
Behaviors=(BehaviorName=TargetScoreHitChance, NodeType=Selector,
   Child[0]=TargetScoreHitChanceUnlikely, Child[1]=TargetScoreHitChanceProbable, Child[2]=AddToTargetScore_40)
Behaviors=(BehaviorName=TargetScoreHitChanceUnlikely, NodeType=Sequence,
   Child[0]=TargetHitChanceLow,  Child[1]=AddToTargetScore_10)
Behaviors=(BehaviorName=TargetScoreHitChanceProbable, NodeType=Sequence,
   Child[0]=TargetHitChanceHigh, Child[1]=AddToTargetScore_70)

Behaviors=(BehaviorName=ScoreIfKillShot,        NodeType=Sequence, Child[0]=TargetIsKillable,                   Child[1]=AddToTargetScore_15)
Behaviors=(BehaviorName=ScoreTargetIfFlanked,   NodeType=Sequence, Child[0]=IsFlankingTarget, Child[1]=TargetIsEnemy, Child[2]=AddToTargetScore_50)
Behaviors=(BehaviorName=ScoreTargetIfMarked,    NodeType=Sequence, Child[0]=TargetAffectedByEffect-MarkedTarget, Child[1]=AddToTargetScore_45)
Behaviors=(BehaviorName=ScoreTargetIfPriority,  NodeType=Sequence, Child[0]=TargetIsPriorityUnit,               Child[1]=AddToTargetScore_60)
```

Сводная таблица бонусов:

| Слагаемое | Баллы | Комментарий |
|---|---|---|
| Шанс попадания высокий (`TargetHitChanceHigh`) | **+70** | доминирующий фактор |
| Шанс попадания средний | **+40** | ветка по умолчанию |
| Шанс попадания низкий (`TargetHitChanceLow`) | **+10** | стрелять всё ещё можно |
| Приоритетная цель | **+60** | внешняя пометка |
| Цель фланкирована мной | **+50** | ← стык с Ф8 |
| Цель помечена (Marked) | **+45** | эффект |
| Выстрел добивает (`TargetIsKillable`) | **+15** | XCOM ценит **добивание** ниже, чем шанс попасть |
| Цель на малом HP | +10 | |
| Цель ранена | +5 | |
| Цель на полном HP | +20 | только для пси-целей |
| Оппортунист-профиль по неудобной цели | **−20** | «не мой профиль» |
| Цель уже паникует/связана | **−1000** | «не добивай безобидного, если есть альтернатива» |
| Лёгкие сложности | +20 к «плохим» решениям | намеренное оглупление |

**Три вывода, которые важнее самих чисел:**

1. **Шанс попадания перевешивает всё остальное** (70 против 50 за фланг и 15 за
   добивание). AI XCOM в первую очередь **не мажет**, и уже потом умничает.
   Отсюда и жалобы игроков «AI всегда бьёт самого открытого» — это не баг.
2. **Добивание стоит дёшево (+15).** Не «убей, если можешь» — а «немного
   предпочти». Иначе AI фокусит одного бойца до смерти и игра ломается.
3. **−1000 за бессмысленную цель.** Так в аддитивной системе выражается
   «никогда, если есть выбор»: не спецветка, а очень большой штраф. Приём
   стоит скопировать.

> **У НАС сейчас:** `FindVisibleTarget` = «провоцирующий, иначе БЛИЖАЙШИЙ». Это
> самая слабая часть всего AI: расстояние не коррелирует ни с шансом попадания
> (у нас уже есть модификатор дистанции и укрытия), ни с угрозой. **Заменяем на
> аддитивный скоринг** с таблицей выше, адаптированной под наши статы.

---

# Часть IV — Выбор точки перемещения (destination scoring)

## IV.1 Метрики тайла

`struct ai_tile_score` (XCOM 2, `XGAIBehavior`):

| Поле | Смысл | Диапазон |
|---|---|---|
| `fCoverValue` | усреднённое качество укрытия против **всех** врагов | −4…1.1 |
| `fDistanceScore` | близость к **идеальной дистанции** оружия | 0…1 |
| `fPriorityDistScore` | дистанция до приоритетной цели | |
| `fFlankScore` | 1, если с тайла фланкирую хоть кого-то | 0/1 |
| `fEnemyVisibility` | сколько врагов видно (растёт с числом) | −1…1 |
| `fEnemyVisibilityPeak1` | максимум при **одном** видимом враге | −1…1 |
| `fAllyVisibility` | вижу ли союзников (плато) | 0…1 |
| `bCloserThanIdeal` | ближе ли идеальной дистанции | bool |
| `bWithinSpreadMin` | не слишком ли близко к своим | bool |
| `SpreadMultiplier` | множитель-штраф за кучность | |
| `fHeightScore` | нормированное превышение | 0…1 |

**Укрытие считается ПРОТИВ ВСЕХ врагов сразу, усреднением:**

```cpp
fCoverValue = (nFlanked*CALC_NO_COVER_FACTOR + nMidCover*CALC_MID_COVER_FACTOR
             + nHighCover*FullCoverFactor) / (nFlanked + nMidCover + nHighCover);
```

При `CALC_NO_COVER_FACTOR = −4.0` открытость **против одного** врага
перевешивает укрытие против двух других. Это и есть «AI боится флангов» —
никакого спецкода, просто резко отрицательный коэффициент.

**Видимость врагов — две разные метрики.** `fEnemyVisibility` растёт с числом
видимых врагов (для агрессоров: чем больше вижу — тем лучше);
`fEnemyVisibilityPeak1` **максимальна ровно при одном** видимом и падает
дальше (для осторожных: хочу видеть одного, не хочу оказаться под перекрёстным
огнём). Один и тот же профиль пользуется либо одной, либо другой.

**Дистанция считается от ИДЕАЛЬНОЙ дальности оружия, а не «чем ближе, тем
лучше»:**

```cpp
fDistanceScore = 1 - (abs(fDist - fIdealRange) / CALC_RANGE_LINEAR_DENOM);   // bCALC_RANGE_LINEAR=true
```

## IV.2 Формула итогового скора

Два принципиальных момента: скорится **дельта относительно текущей клетки**, и
близко/далеко модифицируется отдельно.

```cpp
// 1) дельта каждой метрики
kDiffScore.fCoverValue      = RawTileData.fCoverValue      - m_kCurrTileData.fCoverValue;
kDiffScore.fFlankScore      = RawTileData.fFlankScore      - m_kCurrTileData.fFlankScore;
kDiffScore.fEnemyVisibility = RawTileData.fEnemyVisibility - m_kCurrTileData.fEnemyVisibility;
kDiffScore.fAllyVisibility  = RawTileData.fAllyVisibility  - m_kCurrTileData.fAllyVisibility;

// 2) дистанция: свой множитель для «ближе идеала» и «дальше идеала»
fNewDistScore  = RawTileData.bCloserThanIdeal      ? fDistanceScore*fCloseModifier : fDistanceScore*fFarModifier;
fCurrDistScore = m_kCurrTileData.bCloserThanIdeal  ? …;
fDistanceScore = fNewDistScore - fCurrDistScore;

// 3) взвешенная сумма
fTotalScore = kTileDiffScore.fCoverValue           * fCoverWeight
            + fDistanceScore                       * fDistanceWeight
            + kTileDiffScore.fFlankScore           * fFlankingWeight
            + kTileDiffScore.fEnemyVisibility      * fEnemyVisWeight
            + kTileDiffScore.fEnemyVisibilityPeak1 * fEnemyVisWeightPeak1
            + kTileDiffScore.fAllyVisibility       * fAllyVisWeight
            + fPriorityDistScore                   * fPriorityDistWeight
            + kTileDiffScore.fAllyAbilityRangeScore* AllyAbilityRangeWeight
            + kTileDiffScore.fHeightScore          * fHeightWeight;

// 4) штраф за кучность — множителем и только к положительному скору
if (fTotalScore > 0 && RawTileData.bWithinSpreadMin) fTotalScore *= RawTileData.SpreadMultiplier;
```

Плюс `CURR_TILE_LINGER_PENALTY=0.75` — **бонус за то, чтобы остаться на месте**
(точнее, скор текущей клетки домножается, что делает переезды дороже). Это
лечит «AI дёргается ради +0.1».

> **У НАС уже есть** ровно этот приём — `RelocateBias` («переезжаю, только если
> лучше на столько»). Механика эквивалентна, менять не надо.

## IV.3 Таблица весовых профилей (verbatim из `XComAI.ini`)

```ini
m_arrMoveWeightProfile=(Profile=Fallback,        fCoverWeight=3.0f, fDistanceWeight=0.0f, fFlankingWeight=0.0f,  fEnemyVisWeight=0.0f,  fEnemyVisWeightPeak1=2.0,  fAllyVisWeight=1.0f, fCloseModifier=0.9f, fFarModifier=1.1f)
m_arrMoveWeightProfile=(Profile=Defensive,       fCoverWeight=2.0f, fDistanceWeight=2.0f, fFlankingWeight=0.5f,  fEnemyVisWeight=0.0f,  fEnemyVisWeightPeak1=2.0,  fAllyVisWeight=4.0f, fCloseModifier=1.0f, fFarModifier=1.0f)
m_arrMoveWeightProfile=(Profile=Standard,        fCoverWeight=1.8f, fDistanceWeight=4.0f, fFlankingWeight=1.0f,  fEnemyVisWeight=0.0f,  fEnemyVisWeightPeak1=1.0,  fAllyVisWeight=1.0f, fCloseModifier=1.0f, fFarModifier=1.0f)
m_arrMoveWeightProfile=(Profile=Aggressive,      fCoverWeight=1.7f, fDistanceWeight=5.0f, fFlankingWeight=2.0f,  fEnemyVisWeight=1.0f,  fEnemyVisWeightPeak1=0.0,  fAllyVisWeight=1.0f, fCloseModifier=1.1f, fFarModifier=0.9f)
m_arrMoveWeightProfile=(Profile=Fanatic,         fCoverWeight=0.0f, fDistanceWeight=5.0f, fFlankingWeight=2.0f,  fEnemyVisWeight=1.0f,  fEnemyVisWeightPeak1=0.0,  fAllyVisWeight=0.0f, fCloseModifier=1.1f, fFarModifier=0.9f)
m_arrMoveWeightProfile=(Profile=Hunting,         fCoverWeight=2.0f, fDistanceWeight=4.0f, fFlankingWeight=2.0f,  fEnemyVisWeight=6.0f,  fEnemyVisWeightPeak1=0.0,  fAllyVisWeight=1.0f, fCloseModifier=1.0f, fFarModifier=1.0f)
m_arrMoveWeightProfile=(Profile=AdvanceCover,    fCoverWeight=2.0f, fDistanceWeight=5.0f, fFlankingWeight=2.0f,  fEnemyVisWeight=0.5f,  fEnemyVisWeightPeak1=1.5,  fAllyVisWeight=1.0f, fCloseModifier=1.1f, fFarModifier=0.9f)
m_arrMoveWeightProfile=(Profile=Flanking,        fCoverWeight=1.0f, fDistanceWeight=1.0f, fFlankingWeight=10.0f, fEnemyVisWeight=0.0f,  fEnemyVisWeightPeak1=3.0,  fAllyVisWeight=1.0f, fCloseModifier=0.9f, fFarModifier=1.1f)
m_arrMoveWeightProfile=(Profile=Melee,           fCoverWeight=0.1f, fDistanceWeight=1.0f, fFlankingWeight=1.0f,  …, fPriorityDistWeight=1.0f, bPrioritizeClosest=1, bIsMelee=1)
m_arrMoveWeightProfile=(Profile=MeleeDefensive,  fCoverWeight=0.2f, fDistanceWeight=1.0f, fFlankingWeight=0.0f,  fEnemyVisWeight=-0.5f, fEnemyVisWeightPeak1=2.0, …)
m_arrMoveWeightProfile=(Profile=CivilianGreen,   fCoverWeight=1.0f,  …, fRandWeight=7.0f)
m_arrMoveWeightProfile=(Profile=CivilianRed,     fCoverWeight=10.0f, fDistanceWeight=1.0f, …, fCloseModifier=0.1f, fFarModifier=1.5f)
m_arrMoveWeightProfile=(Profile=RandomNoCover,   fCoverWeight=0.0f,  …, fRandWeight=2.0f)
```

Что читается из таблицы:

- **Профиль — это «настроение», а не состояние.** Одно и то же дерево, разные
  веса. Хотим «трус» — `Fallback`; «фанатик» — `Fanatic`; «обходчик» —
  `Flanking` с весом фланга **10.0** при укрытии 1.0.
- **`fRandWeight` — легальный шум.** Гражданские бегают хаотично не спецкодом,
  а весом случайности 7.0.
- **`fAllyVisWeight=4.0` у `Defensive`** — «держись своих». Это единственный
  вес, который делает командную игру без явной координации.

## IV.4 Глобальные константы (verbatim)

```ini
DefaultIdealRange=10.0f                       ; тайлов (тайл XCOM 2 ≈ 96 см → ≈ 9.6 м)
CALC_RANGE_NUMERATOR=10
CALC_RANGE_DENOM_ADDEND=10
CALC_RANGE_DENOM_FACTOR=1
bCALC_RANGE_LINEAR=true
CALC_RANGE_LINEAR_DENOM=16
CURR_TILE_LINGER_PENALTY=0.75
CALC_NO_COVER_FACTOR=-4.0f
CALC_MID_COVER_FACTOR=1.0f
CALC_FULL_COVER_FACTOR=1.1f
CALC_FULL_COVER_FACTOR_POD_LEADER=2.5f
DEFAULT_AI_MIN_SPREAD_DISTANCE=6.0f
DEFAULT_AI_SPREAD_WEIGHT_MULTIPLIER=0.2f
MIN_SURPRISED_SCAMPER_PATH_LENGTH=2
MAX_SURPRISED_SCAMPER_PATH_LENGTH=6
MaxEngagedEnemies[0]=4   ; Rookie
MaxEngagedEnemies[1]=6   ; Veteran
MaxEngagedEnemies[2]=6   ; Commander
MaxEngagedEnemies[3]=-1  ; Legend
RemoveAlertDataOlderThanAge=10
```

Обратите внимание: **`CALC_FULL_COVER_FACTOR = 1.1` против `CALC_MID_COVER_FACTOR = 1.0`.**
Полное укрытие для AI лишь чуть-чуть ценнее половинчатого — потому что из
полного **труднее стрелять**. А для лидера пода — **2.5**: лидер бережётся.
Это тонкий, но очень «взрослый» штрих, который стоит скопировать.

## IV.5 СВЕРКА КОДА С VERBATIM (2026-07-25) ⭐

Построчная сверка того, что реально стоит в
[UnitAIController.h](../Source/XRU1/Tactics/UnitAIController.h), с числами выше.
Делалась впервые: до этого числа переносились «по смыслу», и часть разошлась.

| Наш параметр | У нас | XCOM verbatim | Вердикт |
|---|---|---|---|
| `OpenCoverFactor` | −4.0 | `CALC_NO_COVER_FACTOR=-4.0` | ✅ точно |
| `HalfCoverFactor` | 1.0 | `CALC_MID_COVER_FACTOR=1.0` | ✅ точно |
| `FullCoverFactor` | 1.1 | `CALC_FULL_COVER_FACTOR=1.1` | ✅ точно |
| `IdealRangeFalloff` | 1500 см | `CALC_RANGE_LINEAR_DENOM=16` тайлов ≈ 1536 см | ✅ совпало |
| `MinSpreadDistance` | ~~250~~ → **576 см** | `DEFAULT_AI_MIN_SPREAD_DISTANCE=6.0` тайлов ≈ 576 см | 🔴 **было в 2.3 раза меньше** — штраф за кучу почти не срабатывал. Исправлено |
| `SpreadPenaltyMultiplier` | ~~0.4~~ → **0.2** | `DEFAULT_AI_SPREAD_WEIGHT_MULTIPLIER=0.2` | 🔴 **было вдвое мягче**, хотя комментарий ссылался на эту же константу. Исправлено |
| `AllyVisibilityWeight` | ~~нет~~ → **10** | `fAllyVisWeight` 0.5–4.0 по профилям | 🔴 **члена не было вовсе.** У AI работал только анти-кучный штраф: отряд умел разбегаться, но не умел держать линию. Добавлен |
| лимит атакующих | ~~нет~~ → **4 / 6 / −1** | `MaxEngagedEnemies` 4/6/6/−1 | 🔴 не было. Добавлен (A8) |
| `TargetScoreHitChance*` | 70 / 40 / 10 | те же | ✅ точно |
| `TargetScoreFlanked` | 50 | 50 | ✅ точно |
| `TargetScoreKillShot` | 15 | 15 | ✅ точно |
| `TargetScoreWounded` | 5 | 5 | ✅ точно |
| `TargetScoreTaunting` | 1000 | 60 | ⚠️ осознанное отклонение §VIII.D п.7 (GDD требует ПРИКАЗ, а не предпочтение) |
| `MaxScoredThreats` | 4 | `MAX_EXPECTED_ENEMY_COUNT` 4 | ✅ точно |
| `RelocateBias` | 10 (стоять выгоднее) | `CURR_TILE_LINGER_PENALTY=0.75` (двигаться выгоднее) | ⚠️ **противоположный знак, осознанно.** Главная претензия к нашему боту по логам — «кровожадно бежит вперёд», а не «стоит столбом». Перенос множителя усилил бы ровно тот дефект, от которого мы уходили |
| профили весов | одна «настройка» | 17 именованных профилей | ⏭ фаза **A6**. При ОДНОМ архетипе врага выигрыш почти нулевой; таблица §IV.3 лежит готовой |
| `CALC_FULL_COVER_FACTOR_POD_LEADER` 2.5 | нет | есть | ⏭ подов у нас нет (§VIII.D п.2) |
| `fRandWeight` | нет | 2.0–7.0 у гражданских | ⏭ гражданских нет |

**Вывод сверки.** Три числа из «скопированных» на деле не были скопированы
(`MinSpreadDistance`, `SpreadPenaltyMultiplier`, `fAllyVisWeight`), и все три
касались ОДНОГО — расстановки отряда. Это объясняет наблюдение из логов «боты
жмутся друг к другу и лезут в одно укрытие» лучше, чем что-либо ещё: механизм
разведения был вдвое слабее задуманного, а механизма сплочённости не было
вообще, поэтому скоринг вообще не имел мнения о том, как отряд стоит.

---

# Часть V — Команда: поды, роли, координация

## V.1 Под (pod) — единица, а не юнит

Враги в XCOM живут **группами по 2–4** (`XGAIGroup`). Группа имеет:

- общий патрульный маршрут (двигаются вместе);
- **общую тревогу**: обнаружили одного — активируется весь под;
- **лидера** (`IsGroupLeader` / `IsFollower`), который ценит полное укрытие в
  2.5 раза выше остальных.

## V.2 Scamper — обязательная механика активации

Момент активации пода — отдельное поведение:

- под, активированный **в ход игрока**, получает бесплатный «рывок в укрытие»
  (scamper) длиной `MIN_SURPRISED_SCAMPER_PATH_LENGTH=2` …
  `MAX_SURPRISED_SCAMPER_PATH_LENGTH=6` тайлов, **без атаки**;
- под, активированный **в ход пришельцев**, может огрызнуться на ходу;
- поды, не участвующие в бою, работают по **упрощённому AI** — это осознанная
  оптимизация Firaxis, а не лень.

Зачем это существует: *«scamper существовал, чтобы пришельцев не расстреливали
в упор из укрытия при каждом столкновении»* — то есть это **защита от
альфа-страйка игрока**, а не «глупость AI».

## V.3 Роли (AI Jobs)

`IsMyJob-Soldier`, `-Flanker`, `-Leader`, `-Support`, `-Terrorist`. Роль
переключает и **веса выбора цели**, и **профиль перемещения**:

```
ScoreHitIfSoldier=(Sequence, Child[0]=IsMyJob-Soldier, Child[1]=TargetScoreHitChanceUnlikely)
ScoreHitIfFlanker=(Sequence, Child[0]=IsMyJob-Flanker, Child[1]=TargetScoreHitChanceOPPORTUNIST)
```

То есть «фланкер» и «солдат», глядя на одну и ту же цель, ставят ей **разный
скор**. Разнообразие боя достигается ролями внутри одного дерева, а не
отдельными деревьями на каждого врага.

## V.4 Оранжевая тревога как механизм подтягивания

Единственный «настоящий» механизм координации в бою — оранжевая тревога:
союзник видит врага → я иду туда, откуда увижу сам. Никакого общего планировщика
на группу нет. Это важно: **координация XCOM эмерджентна**, а не централизована.

> **У НАС:** поды в чистом виде не нужны (одна карта, немного врагов), но
> **три вещи брать обязательно**: (1) общая тревога по группе, (2) scamper при
> первом обнаружении — он уже помечен как P2-улучшение в
> [01_GDD.md §8](01_GDD.md), (3) оранжевая тревога. Роли — берём в облегчённом
> виде: у нас один архетип врага, но `EUnitRole` уже есть, и веса можно
> раскладывать по нему.

---

# Часть VI — Ограничители честности и темпа

Секция, которую почти всегда забывают, а она определяет ощущение от игры.

1. **Лимит атакующих за ход** (`MaxEngagedEnemies`, `HasHitAttackLimit`). На
   Rookie одновременно атакуют максимум **4** врага, на Veteran/Commander — 6,
   на Legend — без лимита. Превысившие лимит уходят в `NonAggressiveBehavior`:
   овервотч (33%) или перемещение (67%) — **выглядят занятыми, но не бьют**.
   Это главный регулятор сложности XCOM 2, и он честнее, чем правка чисел урона.
2. **`SafeToMove` / `NoMove`** — AI считает, сколько овервотчей на него
   нацелено, и не идёт под них бездумно.
3. **Рандомизация решений** — `RandSelector`/`RandFilter` прямо в дереве
   (овервотч в 33% случаев, «50–75% фильтры» на разные ветки).
4. **Оглупление на низких сложностях — явное**: `+20` к скору заведомо плохих
   решений, «occasionally just flat out not taking their turn with a Unit».
5. **AI XCOM не читерит знанием**: юнит в green alert **не знает** позиций
   отряда игрока. Всё знание проходит через AlertData/видимость.

> **У НАС:** пункт 5 уже соблюдён и явно записан в GDD. Пункт 1 — очень дешёвый
> и очень полезный регулятор: у нас есть `EDifficultyLevel`, лимит ложится в него
> идеально. Пункт 2 — надо, у нас Overwatch есть у игрока. Пункт 3 — надо,
> иначе бой читается со второго раза.

---

# Часть VII — Действия AI: каталог и разбор Наблюдения

## VII.1 Каталог действий

Что вообще AI может выбрать (по `SelectAbility-*` и `FindDestination-*`),
в порядке «сколько нам это нужно»:

| Действие | XCOM | Нужно нам | Комментарий |
|---|---|---|---|
| Выстрел с места | `SelectAbility-StandardShot` | ✅ есть | `GA_Attack` через `Event.Attack` |
| Перемещение к точке | `SelectAbility-StandardMove` + `FindDestination-*` | ✅ есть | `MoveWithBudget` / `FindCoverPoint` |
| Овервотч | `SelectAbility-Overwatch` | ✅ надо | `GA_Overwatch` есть, AI им **не пользуется** |
| Глухая оборона | Hunker | ✅ надо | `GA_HunkerDown` есть, AI им **не пользуется** |
| Отступление | `FindDestination-FallBack` | ✅ есть | `bRetreat` в `FindCoverPoint` |
| Обход во фланг | `FindDestination-Flanking` | ✅ надо | нужен вес фланга (Ф8) |
| Наступление по укрытиям | `FindDestination-AdvanceCover` | ✅ есть | `bAdvance` |
| Разведка точки | движение к AlertData | ✅ есть | `StepInvestigate` |
| Патруль | `GreenAlertActionSelector` | ✅ есть | `StepPatrol` |
| Подавление | Suppression | ⛔ нет способности | не в GDD |
| Гранаты/AoE | `MinTargets=1..3`, `bFailOnFriendlyFire` | ⛔ нет | не в GDD |
| Ближний бой | `FindDestination-Melee*` | ⛔ нет | не в GDD |
| Пропуск хода | `SkipMove` | ✅ надо явно | терминальный фолбэк |

**Вывод:** у нас есть почти все кирпичи. Не хватает не действий, а **правил
выбора между ними**: AI сейчас никогда не встаёт в овервотч и никогда не
уходит в глухую оборону, хотя обе способности ему выданы в `AUnitBase`.

## VII.2 Наблюдение (Overwatch) — правила XCOM 2 ⭐

Раздел добавлен отдельно: наблюдение — единственное действие, которое работает
**в чужой ход**, и потому единственное, у которого правила срабатывания важнее
правил выбора.

### VII.2.1 Кто им пользуется

**Все ADVENT и большинство не-ближнебойных пришельцев.** Это не «фича игрока» —
в XCOM 2 наблюдение доступно вражескому AI наравне с игроком, и AI им активно
пользуется:

- в дереве есть узел `TryOverwatch` = `Sequence(IsAbilityAvailable-Overwatch,
  SelectAbility-Overwatch)`;
- в «не-агрессивной» ветке (лимит атакующих исчерпан, §VI п.1) овервотч берётся
  с вероятностью **33%**: `RandSelector(TryOverwatch 33, MoveFlankingOrDefensive 67)`;
- под, активированный в **зелёной** тревоге, может уйти в наблюдение или глухую
  оборону вместо атаки (шанс зависит от сложности);
- при активации пода реакция «разбежаться и часть встаёт в овервотч» —
  штатное поведение на любой сложности.

### VII.2.2 Что именно вызывает реакционный выстрел

Три условия, и первое — то, которое обычно реализуют неверно:

1. **ДВИЖЕНИЕ видимого врага, а не его появление.** Формально: наблюдающий
   должен увидеть перемещение — *«visibility on two tiles in a row that the
   enemy moves through»*. То есть враг, который **уже был виден** и просто
   пошёл, реакцию **вызывает**. Это принципиально: наблюдение — это «стреляю по
   тому, кто шевельнулся», а не «стреляю по тому, кто выскочил».
2. **Линия видимости** — недостаточно быть в радиусе, нужна LOS.
3. **Враждебность на момент движения** — в скрытности (concealment) реакции нет,
   потому что отряд формально не враждебен.

### VII.2.3 Числа и порядок

| Правило | XCOM 2 |
|---|---|
| Штраф точности | **множитель ×0.7** к финальному шансу; **×0.6** по бегущему (dash) |
| Реакций за ход | обычно 1 на юнита; наблюдение снимается после выстрела |
| Несколько наблюдающих | стреляют **по очереди**, каждый следующий **перевыбирает цель**, если предыдущая убита |
| Стоимость | завершает активацию юнита |
| Снятие | в начале следующего хода своей стороны |

Обратите внимание на **множитель, а не вычитание**: ×0.7 по цели с 80% даёт 56%
(−24), а по цели с 30% — 21% (−9). Плоский штраф вёл бы себя иначе на краях.

> **У НАС — три расхождения, найденные аудитом:**
>
> 1. **Триггер неверный.** `UGA_Overwatch::HandlePerceptionUpdated` реагирует на
>    **вход** врага в зону восприятия (`OnTargetPerceptionUpdated`), и это прямо
>    отмечено в комментарии как ограничение: *«перемещение врага целиком внутри
>    зоны повторного стимула не даёт»*. Для наблюдения игрока это ещё сходит
>    (враги приходят издалека), но **для наблюдения ботов это ломает механику
>    полностью**: бойцы игрока обычно уже видны, когда враг встаёт в овервотч, —
>    значит их перемещение не даст стимула и реакции не будет никогда.
> 2. **Штраф плоский, а не множитель** (`ReactionAimPenalty = 10.f`,
>    вычитается). Зафиксировано в GDD §5.4 — это осознанное упрощение, но
>    записать расхождение надо; переход на множитель стоит одну строку.
> 3. **AI не активирует наблюдение вообще** — способность выдана, но ни одно
>    правило её не выбирает.
>
> Исправления — фазы **A7** и **W1** в
> [13_AI_STATE_MACHINE_PLAN](13_AI_STATE_MACHINE_PLAN.md#часть-iii--план-стейт-машины-ai).

---

# Часть VIII — Что берём в XRU1: итоговая спецификация

Сведение всех «**У НАС:**» в один список требований. Реализация — в
[13_AI_STATE_MACHINE_PLAN.md](13_AI_STATE_MACHINE_PLAN.md).

### A. Слой знания (тревога и память)

| № | Требование | Статус |
|---|---|---|
| A1 | 4 состояния тревоги: Patrol / Investigate / **Alerted (orange)** / Combat | orange — новое |
| A2 | Журнал `FAIAlertRecord[]` вместо одиночной `LastKnownThreatLocation` | новое |
| A3 | Скоринг записей (близость, возраст, «страшность», видима ли точка) | новое |
| A4 | Срок годности записи в ходах (аналог `RemoveAlertDataOlderThanAge=10`) | новое |
| A5 | Общая тревога по группе (увидел один — знают все) | новое |
| A6 | AI не знает позиций отряда без наблюдения | ✅ соблюдено |

### B. Слой действия (правила выбора)

| № | Требование | Статус |
|---|---|---|
| B1 | Явный приоритетный список правил вместо `switch` по состоянию | доработка |
| B2 | Каждое правило = условия + выбор действия + профиль весов | новое |
| B3 | Терминальный фолбэк («пропуск») — ход не может зависнуть | ✅ есть (`FinishUnitTurn`) |
| B4 | Решение отделено от исполнения (структура `FAIDecision`) | новое |
| B5 | AI умеет **овервотч** и **глухую оборону** | новое |
| B5a | **Триггер овервотча — движение видимого врага**, а не его появление (§VII.2.2). Без этого овервотч ботов не работает в принципе | **дефект, чинить** |
| B5b | Реакция перевыбирает цель, если предыдущая убита | новое |
| B6 | Рандомизация: овервотч ~33%, выбор между близкими по скору вариантами | новое |
| B7 | `SafeToMove`: не бежать под известный овервотч | новое |
| B8 | Лимит одновременно атакующих по сложности | новое |

### C. Слой скоринга

| № | Требование | Статус |
|---|---|---|
| C1 | Укрытие точки — против **всех** видимых угроз, с резко отрицательным весом «открыт» | доработка (сейчас против одной) |
| C2 | Дистанция — от **идеальной**, а не «чем ближе, тем лучше» | доработка |
| C3 | Вес фланга (нужен `IsTargetFlankedBy`, Ф8) | новое |
| C4 | Вес высоты (у нас `HeightAdvantageZ` уже влияет на точность) | новое в скоринге |
| C5 | Штраф за кучность (`SpreadMultiplier`) | новое |
| C6 | Порог «переезжаю, только если заметно лучше» | ✅ есть (`RelocateBias`) |
| C7 | Скоринг **дельтой** от текущей позиции | ✅ есть (`BaselineScore`) |
| C8 | Именованные **профили весов** (Fallback/Defensive/Standard/Aggressive/Flanking/AdvanceCover) в Data Asset | новое |
| C9 | Выбор цели — аддитивный скоринг вместо «ближайший» | новое |
| C10 | Штраф −1000 как способ сказать «никогда, если есть выбор» | приём |

### D. Осознанные отклонения от XCOM (фиксируем, чтобы не «чинить»)

1. **Не делаем движковый Behavior Tree.** Приоритетный список правил в C++ —
   проще, отлаживается логом, не требует Blackboard. Дерево оправдано, когда
   типов врагов десятки; у нас один.
2. **Не делаем поды как отдельную сущность.** Группа = сторона + радиус
   оповещения. Общая тревога достигается рассылкой, а не классом `AIGroup`.
3. **Не делаем тайлы.** У нас навмеш и кольцевой сэмплинг; метрики те же,
   дискретизация другая. Формулы скоринга это переживают без изменений.
4. **Идеальная дистанция** у нас должна выводиться из `AimByDistanceCurve`
   (максимум кривой), а не быть отдельным числом, — иначе два источника правды.
5. **Нет подавления/гранат/ближнего боя** — их нет в GDD. ⚠️ «Нет **сейчас**» ≠
   «нельзя добавить»: архитектура обязана принимать их как новый оценщик без
   правок существующих — проверка масштабируемости в
   [13_AI_STATE_MACHINE_PLAN §ADR-1.4](13_AI_STATE_MACHINE_PLAN.md#adr-14--доказательство-масштабируемости).
6. **Штраф реакционного выстрела — плоский (−10), а не множитель ×0.7**
   (GDD §5.4). Осознанно: плоский штраф читаемее в HUD. Если захочется
   XCOM-паритета — менять в `UGA_Overwatch::FireReactionShot` и в GDD
   одновременно.
7. **Провокация танка — ПРИКАЗ, а не предпочтение.** В XCOM priority target даёт
   +60 (сопоставимо с бонусом за высокий шанс), то есть цель можно и не
   выбрать. GDD §7 требует «враг в радиусе провокации **обязан** бить именно
   её», поэтому `TargetScoreTaunting` = 1000 — перебивает любую комбинацию
   остальных слагаемых. Бонус даётся только когда по провоцирующему реально
   есть выстрел. Хотите XCOM-модель — 60 и правка GDD §7.
8. **Фланг определяется ФИЗИКОЙ ВЫСТРЕЛА, а не углом к стене.** XCOM оперирует
   тайлами: укрытие «принадлежит» ребру клетки, и защита формулируется как
   «180 градусов», а при нескольких укрытиях применяется то, что больше
   совпадает с направлением атаки. Это работает там **только потому, что юнит
   физически притянут к тайлу укрытия** и визуально вжат в конкретную стену.
   У нас юниты стоят свободно и притягиваться не будут — значит опираться на
   позу нельзя вообще.

   **Наша модель (2026-07-24, геометрия уточнена 2026-07-25):** укрытие против
   стрелка = толстый луч от цели в сторону **фактической огневой позиции
   стрелка** (с учётом выглядывания) на высотах half/full. Работает стена —
   есть укрытие; луч проходит мимо — фланг. Никаких углов и порогов.

   Два свойства геометрии, без которых модель ломается вплотную (цикл 17):
   - **Юниты не геометрия.** Луч идёт тем же object-query, что и линия огня
     (`GetShotGeometryObjects`: WorldStatic + WorldDynamic). Трейс по КАНАЛУ
     `WorldStatic` упирался в капсулу самого стрелка — вплотную цель всегда
     оказывалась «в полном укрытии». Союзник и труп «укрывали» так же.
   - **Стена должна быть МЕЖДУ.** Длина луча = `min(CoverTraceDistance,
     дистанция до огневой позиции − толщина луча)`. Иначе укрытием цели
     становилась стена за спиной стрелка. Ближний бой при этом сам собой даёт
     фланг — как в XCOM.

   Это даёт три вещи, которых углом добиться нельзя: (1) «стою сбоку от ящика»
   честно перестаёт быть укрытием; (2) **peek flanking** — боец, выглянувший
   из-за угла, обходит укрытие цели, и щит становится жёлтым, как в XCOM;
   (3) логика полностью отвязана от анимации и поз.

   ⚠️ **Разделение слоёв, которое надо держать:** `GatherCoverSides` /
   `BestCoverDirection` / `CoverSides` — это **визуальный/локальный** слой («к
   чему я прижат», для анимации Ф10 и отладки), он **не участвует** в решении
   «фланг или нет». Угловой параметр `CoverArcHalfAngle` и функция
   `BestCoverAgainstDirection` **удалены**: держать ручку, которая ни на что не
   влияет, хуже, чем не иметь её вовсе.
9. **Круговой обзор AI (180° полуугол).** В XCOM так же, но у нас есть
   дополнительная причина: предикат выстрела `HasLineOfSight` поворот юнита не
   учитывает вовсе. Конус зрения делал перцепцию строже правил стрельбы, и враг
   игнорировал цели за спиной, по которым мог стрелять.

## E. Где мы СОЗНАТЕЛЬНО ЛУЧШЕ XCOM 2 (по разбору претензий игроков) ⭐

Разбор обсуждений игроков и модеров (2026-07-25, источники §IX.6–8). Смысл
раздела: копировать XCOM надо не целиком — у его AI есть общепризнанные
дефекты, и часть из них мы не наследуем не по лени, а намеренно.

| # | Претензия к XCOM 2 | Наш статус | Почему так |
|---|---|---|---|
| E1 | **«Враги почти никогда не встают в овервотч, если СЕЙЧАС не видят отряд».** Боец, слышавший стрельбу, продолжает ходить по маршруту вместо удержания направления. Это чинят модами | ✅ **лучше:** `InvestigateOverwatchChance` (0.5) — дойдя до последней известной точки и никого не найдя, бот разворачивается к ней и встаёт в наблюдение | У нас состояние `Investigate` буквально означает «знаю точку, не вижу цель». Данные уже были — не использовать их было бы странно |
| E2 | **Активация пода: «увидел одного — на меня побежали шестеро».** Свободный ход пода после обнаружения воспринимается как читерство, а не как тактика | ✅ **не наследуем:** подов нет вообще (§D п.2), свободного хода при обнаружении нет. Тревога распространяется шумом боя, а решение принимается общим скорингом | Именно эту механику удаляет самый популярный AI-мод («All Pods Active»). Копировать то, что массово вырезают, смысла нет |
| E3 | **Враги «выманивают» игрока под овервотч** — поведение читается и эксплуатируется | ⚠️ частично: наш `UAIEval_Overwatch` рандомизирован (`ActivationChance` 0.33), а не детерминирован | Полностью проблема решается только разнообразием архетипов (**A6**) |
| E4 | **AI кажется «читающим», а не думающим** — одинаковые решения в одинаковых ситуациях | ⚠️ открыто: рандомизация есть только у наблюдения. Общая рандомизация при близких скорах — часть **A8** | Детерминизм полезен для отладки, поэтому шум добавляем адресно и детерминированным зерном, чтобы лог решений оставался честным |
| E5 | **Лишние пачки врагов подтягиваются на звук боя и ломают размен** | ✅ у нас это управляемо: радиус `NotifyCombatNoise` — параметр, а не константа пода | — |

⚠️ **Чего НЕ надо чинить.** Претензия «AI видит сквозь стены» к XCOM 2
относится к его тайловой видимости; у нас видимость и стрельба считаются ОДНИМ
предикатом (`HasLineOfSight`), так что этот класс жалоб к нам неприменим по
построению — и это стоит сохранить при любых будущих правках.

---

# Часть IX — Источники

Все ссылки проверялись 2026-07-24.

1. **`XComAI.ini` (XCOM 2, ванильный конфиг)** — дерево поведения, весовые
   профили, глобальные константы, бонусы скоринга целей:
   [github.com/russgray/xcom2-config](https://github.com/russgray/xcom2-config/blob/master/XComAI.ini)
   — основной первоисточник чисел в этом документе.
2. **`XGAIBehavior.uc` (X2 WotC Community Highlander, декомпилированный
   `XComGame`)** — `ai_tile_score`, `AITileWeightProfile`, `FillTileScoreData`,
   `GetWeightedTileScore`, `ScoreDestinationTile`, цикл прогона дерева:
   [X2WOTCCommunityHighlander/Src/XComGame/Classes/XGAIBehavior.uc](https://github.com/X2CommunityCore/X2WOTCCommunityHighlander/blob/master/X2WOTCCommunityHighlander/Src/XComGame/Classes/XGAIBehavior.uc)
3. **Визуализатор дерева поведения XCOM 2** (структура дерева целиком):
   [sterlingvix.github.io/xcom2ai](http://sterlingvix.github.io/xcom2ai/),
   [Steam-гайд](https://steamcommunity.com/sharedfiles/filedetails/?id=618641962)
4. **UFOpaedia, Customizing LW2** — прикладной разбор AI-конфига моддерами:
   [ufopaedia.org/index.php/Customizing_LW2](https://www.ufopaedia.org/index.php/Customizing_LW2)
5. **Pavonis Interactive (Long War 2), «The Issue with Yellow Alert and
   Engagements»** — семантика зелёной/жёлтой тревоги, назначение scamper,
   упрощённый AI неактивных подов:
   [pavonisinteractive.com forum t=25613](https://www.pavonisinteractive.com/phpBB3/viewtopic.php?t=25613)
6. **UFOpaedia, Mechanics (LW2)** — активация подов и тайминг scamper:
   [ufopaedia.org/index.php/Mechanics_(LW2)](https://www.ufopaedia.org/index.php/Mechanics_(LW2))
7. **GDC 2013, доклад об AI XCOM: EU** — «AI оценивал каждую способность по
   defensive/offensive/intangible пользе», расовые смещения, выбор
   «двигаться или применить способность»:
   [pcgamer.com — GDC 2013: The AI tricks behind XCOM…](https://www.pcgamer.com/gdc-2013-the-ai-tricks-behind-xcom-assassins-creed-3-and-warframe/)
8. **PC Gamer, «How to tweak XCOM 2 .ini files»** — назначение `DefaultAI.ini`,
   «ideal engagement range», «how much the AI values High Cover»:
   [pcgamer.com](https://www.pcgamer.com/how-to-tweak-xcom-2-ini-files-for-fun-and-danger/)
9. **UFOpaedia, Tactical AI** — общая страница по тактическому AI серии:
   [ufopaedia.org/index.php/Tactical_AI](https://www.ufopaedia.org/index.php/Tactical_AI)
   ⚠️ на 2026-07-24 отдаёт 403 автоматическим запросам; открывать вручную.
10. **XCOM Wiki, Overwatch (XCOM 2)** — правила срабатывания реакции,
    множители ×0.7/×0.6, последовательность нескольких наблюдающих,
    доступность овервотча ADVENT и пришельцам:
    [xcom.fandom.com/wiki/Overwatch_(XCOM_2)](https://xcom.fandom.com/wiki/Overwatch_(XCOM_2))
    ⚠️ на 2026-07-24 отдаёт 402 автоматическим запросам; цитаты §VII.2 взяты из
    поисковых сниппетов той же страницы и перепроверены по п. 11.
11. **UFOpaedia, Overwatch (XCOM2)** — независимое подтверждение правила
    «нужно увидеть движение — видимость двух клеток подряд»:
    [ufopaedia.org/index.php/Overwatch_(XCOM2)](https://www.ufopaedia.org/index.php/Overwatch_(XCOM2))
12. **XCOM Wiki, Reaction Shot** — общая механика реакционного огня серии:
    [xcom.fandom.com/wiki/Reaction_Shot](https://xcom.fandom.com/wiki/Reaction_Shot)

Добавлено 2026-07-25 (разбор претензий игроков, §VIII.E):

13. **Steam Community, «Enemies splitting when getting discovered ruins the
    game»** — претензия к активации пода и свободному ходу при обнаружении:
    [steamcommunity.com — обсуждение](https://steamcommunity.com/app/268500/discussions/0/1471967615873145426/)
14. **Pavonis Interactive, «Overwatch Creep and Pod Mechanics»** — разбор того,
    как поды взаимодействуют с овервотчем и почему это эксплуатируется:
    [pavonisinteractive.com forum t=25147](https://www.pavonisinteractive.com/phpBB3/viewtopic.php?t=25147)
15. **Vigaroe, «XCOM 2 Analysis: General Enemy Intro»** — разбор поведения
    врагов, в том числе «враги не встают в овервотч, если не видят отряд»:
    [vigaroe.com](http://www.vigaroe.com/2020/05/xcom-2-analysis-general-enemy-intro.html)

> **Оговорка о достоверности.** Числа Частей III/IV/VI взяты **дословно** из
> конфига и исходников (пп. 1–2) — это самый твёрдый уровень. Формулировки
> Части V (scamper, поды) — из моддерских разборов (пп. 5–6) и согласуются с
> наблюдаемым поведением игры. Часть про XCOM: EU (п. 7) — пересказ доклада в
> прессе, поэтому её выводы использованы только как подтверждающие, а не как
> источник чисел.

---

# Часть X — Журнал аудита документа

Циклическая проверка «выписана ли ВСЯ логика» — по требованию задачи.

| № | Что проверялось | Итог |
|---|---|---|
| 1 | Слои принятия решений (знание / действие / позиция) — разделены и не смешаны | ✅ §I.1 |
| 2 | Типы узлов дерева перечислены полностью, включая редкие (`RandFilter`, `Successor`, `RepeatUntilFail`) | ✅ §I.3 |
| 3 | Корень дерева и терминальный фолбэк описаны | ✅ §I.3 |
| 4 | Порядок «действие → точка», а не наоборот | ✅ §I.5 |
| 5 | Alert-уровни: **четыре**, не три — оранжевый не забыт | ✅ §I.2 |
| 6 | Понижение тревоги (red→yellow→green) и срок годности знания | ✅ §I.2, §II.1 |
| 7 | Память описана как журнал наблюдений, а не «последняя точка» | ✅ §II.1 |
| 8 | Явно отвечен вопрос «как AI сохраняет цель» — три разных срока жизни | ✅ §II.3 |
| 9 | Выбор цели: все слагаемые с числами | ✅ §III |
| 10 | Отмечено, что шанс попадания доминирует (70 > 50 > 15) | ✅ §III |
| 11 | Приём «−1000 вместо ветки исключения» зафиксирован | ✅ §III |
| 12 | Метрики тайла перечислены полностью, включая обе метрики видимости врагов | ✅ §IV.1 |
| 13 | Формула укрытия «против всех, усреднением» с `-4.0` за открытость | ✅ §IV.1 |
| 14 | Дистанция считается от **идеальной**, формула приведена | ✅ §IV.1 |
| 15 | Итоговая формула — **дельтой** от текущей клетки | ✅ §IV.2 |
| 16 | Штраф кучности и `CURR_TILE_LINGER_PENALTY` | ✅ §IV.2 |
| 17 | Профили весов — verbatim, а не пересказ | ✅ §IV.3 |
| 18 | Глобальные константы — verbatim | ✅ §IV.4 |
| 19 | Замечено `FULL_COVER 1.1` vs `MID 1.0` и `2.5` у лидера пода | ✅ §IV.4 |
| 20 | Командная логика: поды, лидер, роли, оранжевая тревога | ✅ §V |
| 21 | Scamper: числа, тайминг, назначение | ✅ §V.2 |
| 22 | Зафиксировано, что координация **эмерджентна**, а не централизована | ✅ §V.4 |
| 23 | Ограничители: лимит атакующих, SafeToMove, рандомизация, оглупление | ✅ §VI |
| 24 | Явно записано «AI не читерит знанием» | ✅ §VI п.5 |
| 25 | Каталог действий сверен с тем, что уже есть в нашем коде | ✅ §VII |
| 26 | Найдено: способности Overwatch/Hunker выданы AI, но никогда не выбираются | ✅ §VII |
| 27 | Каждая механика имеет решение «берём / не берём» | ✅ строки «У НАС» |
| 28 | Осознанные отклонения выписаны отдельно, чтобы их не «чинили» | ✅ §VIII.D |
| 29 | Источники разделены по уровню достоверности | ✅ §IX |
| 30 | Сквозная сверка: числа §III/§IV не противоречат друг другу и §VIII | ✅ согласован |
| 31 | Проверено, что документ не дублирует 11-й (геометрия) и 13-й (реализация) | ✅ согласован |
| 32 | Контрольный проход | ✅ согласован |

**Цикл 2 (2026-07-24, по запросу «ультра-анализ»).** Направления проверки:
формулировки, покрытие механик, соответствие настоящему XCOM, масштабируемость.

| № | Направление | Что нашли | Итог |
|---|---|---|---|
| 33 | **Формулировка §0** | «XCOM — не стейт-машина» читалось как «стейт-машины устарели». Реальный тезис узче: FSM не годится **для слоя выбора действия**, но обязательна на трёх других слоях | §0 переписан: таблица «слой → инструмент → нужна ли FSM», перечислены все 4 уместных FSM проекта |
| 34 | **Полнота каталога действий** | **Наблюдение вообще не было разобрано** — при том, что это единственное действие, работающее в чужой ход, и вопрос игрока был именно про него | Добавлен §VII.2 целиком |
| 35 | **Пользуются ли овервотчем боты XCOM** | Да: доступен всем ADVENT и большинству не-ближнебойных; узел `TryOverwatch`, 33% в не-агрессивной ветке, овервотч/hunker при активации пода в green | Зафиксировано §VII.2.1 |
| 36 | **Правило срабатывания реакции** | XCOM требует **увидеть движение** (видимость двух клеток подряд). У нас реакция вешается на **вход в зону восприятия** — для наблюдения ботов это не работает никогда, т.к. бойцы игрока уже видны | Дефект B5a, чинится фазой W1 |
| 37 | **Числа реакции** | XCOM — **множитель** ×0.7 (×0.6 по бегущему), у нас плоские −10 | Записано как осознанное отклонение §VIII.D п.6 |
| 38 | **Последовательность наблюдающих** | В XCOM 2 стреляют по очереди, каждый **перевыбирает** цель, если предыдущая убита | Требование B5b |
| 39 | **Не потеряны ли механики, отсутствующие в GDD** | Гранаты/перезарядка/подавление помечены «не берём» — но не было сказано, что архитектура обязана их принимать | §VIII.D п.5 дополнен ссылкой на доказательство масштабируемости |
| 40 | Сквозная сверка §0 ↔ §VII.2 ↔ §VIII ↔ 13-й документ | противоречий нет | ✅ согласован |
| 41 | Источники по наблюдению добавлены, помечена недоступность части страниц для автозапросов | ✅ согласован |
| 42 | **Контрольный проход цикла 2** — перечитаны все части подряд | новых расхождений нет | ✅ согласован |
