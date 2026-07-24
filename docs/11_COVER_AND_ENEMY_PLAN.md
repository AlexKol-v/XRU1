# 11 — Укрытия и враг: архитектурный план (блоки B.2 / B.3)

> Рабочий гайд-план для реализации механики укрытий в духе XCOM (EU/EW и XCOM 2),
> общей для игрока и AI. **Документ самодостаточен** — агент, работающий по нему,
> не обязан знать историю обсуждения.
>
> **Источник задачи:** [09_GAMEPLAY_STATUS.md](09_GAMEPLAY_STATUS.md) §B.2 (AI
> врага) и §B.3 (укрытия/peek). Правила боя и числа — [01_GDD.md](01_GDD.md) §5.
> Карта кода — [03_CODE_OVERVIEW.md](03_CODE_OVERVIEW.md).

## Оглавление

| | Раздел | Зачем |
|---|---|---|
| **0** | [Правила работы](#0-правила-работы) | как собирать, что нельзя трогать |
| **I** | [Референс XCOM](#часть-i--референс-xcom) | что должно получиться и почему |
| **II** | [Диагноз: три дефекта](#часть-ii--диагноз-текущей-реализации) | что именно сломано сейчас |
| **III** | [Архитектура](#часть-iii--архитектура-решения) | как чиним, с сигнатурами |
| **IV** | [План по фазам Ф0–Ф12](#часть-iv--план-по-фазам) | что делать, по шагам |
| **V** | [Проверка](#часть-v--проверка) | тест-матрица и инварианты |
| **VI** | [Приложения](#часть-vi--приложения) | источники, журнал аудита |

**С чего начать:** прочитать §0, Часть II (диагноз) и §III.1–III.3, затем идти по
фазам с **Ф0**. Ключевой порядок: **Ф2 → Ф3 → (Ф4 + Ф5)**. Ф4 и Ф5 — две половины
одной задачи, поодиночке эффекта в бою не дают.

---

## 0. Правила работы

- **Язык:** отвечать и рассуждать по-русски; идентификаторы и API UE — на
  английском; комментарии в новом C++ — по-русски (как в существующем коде).
- **Сборка — при ЗАКРЫТОМ редакторе:** пользователь сохраняет работу, сам
  закрывает Editor, затем запускается `.\Build-XRU1.ps1`. Агент не применяет
  `-StopEditor` без отдельной просьбы. Live Coding допустим только для
  body-only правок без новых/изменённых `UCLASS`, `USTRUCT`, `UENUM`,
  `UPROPERTY` и `UFUNCTION`; после reflected/UHT-правок обязателен полный build
  и перезапуск Editor. Проверять текст вывода, а не только exit-code.
- **Донор `cst-3d-gubkin-2026-04` — только чтение.** Не патчить.
- **BP/WBP-правки через UnrealClaude верифицировать чтением** после каждой.
- **Каждую фазу заканчивать:** сборка → PIE-проверка по критерию приёмки →
  чекбокс здесь → короткая заметка в [09_GAMEPLAY_STATUS.md](09_GAMEPLAY_STATUS.md).
- **Не ломать инварианты** — §V.2. Главные: «план == факт» и «правка только
  ДОБАВЛЯЕТ, не отнимает».
- **Отклонение от XCOM/GDD фиксируется в `01_GDD.md` ДО кода.**
- **Ссылки на код здесь — по ИМЕНАМ функций, без номеров строк.** Это сделано
  намеренно: план правит те же файлы, на которые ссылается, поэтому номера строк
  устаревают уже после первой фазы. Ищи символ (`GetCoverAgainst`,
  `TraceCoverInDirection`, `FindCoverPoint`, …), а не строку.

---

# Часть I — Референс XCOM

## I.1 Ядро укрытий (обязательно к реализации)

| # | Механика XCOM | XCOM 1 | XCOM 2 | У нас | Фаза |
|---|---|---|---|---|---|
| 1 | **Укрытие направленное** (работает только со стороны объекта) | ✔ | ✔ | ✅ есть | — |
| 2 | **Half / Full = +20 / +40 к обороне** | ✔ | ✔ | ✅ есть | — |
| 3 | **Фланг обнуляет укрытие** | ✔ | ✔ | ✅ есть | — |
| 4 | **Фланг/открытый = жёлтый щит в UI** | ✔ | ✔ | ❌ нет | **Ф8** |
| 5 | **Peek стрелка: LOS «с соседних клеток» по обе стороны укрытия** | ✔ | ✔ | ⚠️ грубо (±70 от центра) | **Ф4** |
| 6 | **Укрытие цели НЕ делает её невидимой — только даёт защиту** | ✔ | ✔ | ❌ **ломает выбор целей** | **Ф5** ⭐ |
| 7 | **Из half встают и стреляют ПОВЕРХ; из full выходят из-за края** | ✔ | ✔ | ❌ нет понятия стойки | **Ф4 + Ф10** |
| 8 | **Низкое простреливается поверх, высокое — только из-за края** | ✔ | ✔ | ❌ сломано высотами | **Ф2** |
| 9 | **Hunker Down усиливает укрытие и ТРЕБУЕТ укрытия** | ✔ удвоение | ✔ +30 защ./+50 dodge, только в укрытии | ⚠️ удваиваем, но **укрытия не требуем** | **Ф7** |
| 10 | **Высота: +аим стрелку сверху** | ✔ +20 | ✔ (~+20/25%) | ✅ есть | — |
| 11 | **Высота НЕ обнуляет укрытие** (влияет на аим и на перекрытие LOS, но не «делает фланг») | ✔ | ✔ | ✅ так и есть | ⚠️ §II.3 |
| 12 | **Укрытие пересчитывается по факту в момент выстрела** | ✔ | ✔ | ✅ есть | — |
| 13 | **Разрушаемое укрытие → Exposed** | ✔ | ✔ | ⚠️ «бесплатно» | §I.3 |

## I.2 Осознанные отклонения (зафиксировать, не «чинить» молча)

| Механика | XCOM | У нас | Решение |
|---|---|---|---|
| **Фланг/открытый даёт +50% крита** | ✔ обе игры | ❌ крита нет вообще | **Ф12 (опционально)**; сначала правка GDD |
| **Модель Hunker Down** | XCOM 1 — удвоение укрытия; XCOM 2 — плоские +30 защиты и +50 dodge, в WotC гасится флангом | удвоение (модель XCOM 1) | Оставляем модель XCOM 1: у нас нет dodge, а удвоение читается проще. **Требование укрытия — добавить (Ф6)** |
| **Штраф Overwatch** | XCOM 2: ×0.7 к финальному шансу | −10 плоско (`ReactionAimPenalty`) | Оставить −10, зафиксировано в GDD §5.4 |
| **Штраф высоты снизу вверх** | XCOM: только бонус сверху | ±20 симметрично | Отмечено в `09_GAMEPLAY_STATUS` §D |
| **Suppression** (подавление: −аим цели, пришпиливание к укрытию) | ✔ обе игры | ❌ нет | Вне скоупа курсовой; архитектуре не мешает (это модификатор аима + тег) |
| **Concealment / активация пода со «scamper»** | ✔ XCOM 2 | ❌ нет, есть Patrol/Investigate/Combat | Вне скоупа; ближайший аналог — **Ф9** (AI занимает укрытие) |
| **Две низких = одна высокая для перекрытия LOS** | ✔ XCOM 2 | ❌ нет | Вне скоупа: у нас непрерывные высоты, трейс меряет реальную геометрию |
| **Dodge / graze**, **Armor / shred** | ✔ XCOM 2 | ❌ нет | Вне скоупа курсовой |
| **Тайловая сетка** | ✔ обе игры | непрерывные координаты | Осознанно; peek делаем edge-aware, §III |

## I.3 Механики, которые архитектура обязана НЕ ЗАБЛОКИРОВАТЬ

1. **Разрушение укрытий.** Укрытие определяется живым трейсом на каждый запрос,
   а не запекается, — уничтожение меша автоматически даёт `Exposed` без единой
   строки кода. **Единственный кэш** — `BestCoverAround` (обновляется в
   `EvaluateSurroundings`); при разрушении его надо дёрнуть вручную. Не ломать
   это кэшированием «укрытия против врага».
2. **Гранаты / AOE игнорируют укрытие.** Когда появятся — не должны звать
   `ComputeHitChance` (там вычитается укрытие).
3. **Ближний бой = автоматический фланг.** Решается тем же `GetCoverAgainst`.
4. **Suppression.** Если появится — это модификатор аима и тег состояния, он не
   требует менять модель укрытий.

---

# Часть II — Диагноз текущей реализации

## II.1 Что уже работает (не переписывать — достраивать)

| Механика | Где | Статус |
|---|---|---|
| Тип укрытия против конкретной угрозы | `UCoverDetectionComponent::GetCoverAgainst(Threat)` ([CoverDetectionComponent.cpp](../Source/XRU1/Tactics/CoverDetectionComponent.cpp)) | ✅ |
| Укрытие в произвольной точке (AI-план) | `EvaluateCoverAtLocation(Base, ThreatLoc)` ([CoverDetectionComponent.cpp](../Source/XRU1/Tactics/CoverDetectionComponent.cpp)) | ✅ план=факт |
| Переиспользуемое ядро трейса | `static TraceCoverAtLocation(World, Base, Dir, Dist, HalfH, FullH, Channel, Ignored)` ([CoverDetectionComponent.h](../Source/XRU1/Tactics/CoverDetectionComponent.h)) | ✅ **peek строить на нём** |
| Бонус 20/40 + hunker → шанс | `GetDefenseBonusAgainst` → `ComputeHitChance` ([TacticsCombatStatics.cpp](../Source/XRU1/Tactics/TacticsCombatStatics.cpp)) | ⚠️ ×2 **захардкожен** → Ф3 |
| Линия огня | `HasLineOfSight` / `HasLineOfSightFromLocation` — сферо-трейсы, step-out ±70, 2 точки цели ([TacticsCombatStatics.cpp](../Source/XRU1/Tactics/TacticsCombatStatics.cpp)) | ⚠️ §II.2, §II.4 |
| Список целей игрока | `GetAttackTargets` → `CanTargetActor` → `GetTargetStatus` ([TacticalPlayerController.cpp](../Source/XRU1/Tactics/TacticalPlayerController.cpp), [GA_Attack.cpp](../Source/XRU1/Tactics/GA_Attack.cpp)) | ⚠️ наследует ограничения LOS |
| Укрытие цели в HUD | `TacticalHUDWidget` → `GetCoverAgainst(Shooter)` ([TacticalHUDWidget.cpp](../Source/XRU1/UI/TacticalHUDWidget.cpp)) | ⚠️ нет «flanked» |
| Иконка укрытия над головой | `UCoverIconWidget` ([CoverIconWidget.h](../Source/XRU1/UI/CoverIconWidget.h)) | ⚠️ показывает `BestCoverAround`, без стрелка |
| Иконка укрытия в карточке | `UTacticalHUDWidget::ApplyPortraitCardLayout` → `GetCoverIconForUnit` | ✅ показывает тот же локальный `BestCoverAround`; не является per-shooter |
| AI: выбор точки с укрытием и LOS | `FindCoverPoint` ([UnitAIController.cpp](../Source/XRU1/Tactics/UnitAIController.cpp)) | ⚠️ план по центру |
| Глухая оборона | `UGA_HunkerDown` ([TacticalClassAbilities.cpp](../Source/XRU1/Tactics/TacticalClassAbilities.cpp)) | ⚠️ **не требует укрытия** → §II.5 |
| Стойки укрытий | BP-хук `OnCoverStateChanged` есть, к ABP не подключён | ❌ §B.5 |

## II.2 Дефект А — peek стрелка «от центра», а не от края

Выглядывание — **±70 см вбок от ЦЕНТРА** стрелка
([TacticsCombatStatics.cpp](../Source/XRU1/Tactics/TacticsCombatStatics.cpp)).
Боец вплотную к **широкой** полной стене: шаг 70 см упирается в ту же стену →
LOS нет → **из-за полного укрытия стрелять нельзя**. Лечит **Ф4**.

## II.3 Дефект Б — высоты укрытий меряются от центра капсулы ⚠️

Трейсы укрытия стартуют от `Base + (0,0,Height)`, где `Base` —
`Owner->GetActorLocation()` ([CoverDetectionComponent.cpp](../Source/XRU1/Tactics/CoverDetectionComponent.cpp)).
У `ACharacter` это **центр капсулы**, т.е. пол + `CapsuleHalfHeight` (≈88 см).
Подтверждается комментарием `EvaluateCoverAtLocation` («Base = ... точка пола +
половина капсулы») и вызовом из AI (`StandBase = Projected.Location + CapsuleHalfHeight`,
[UnitAIController.cpp](../Source/XRU1/Tactics/UnitAIController.cpp)).

Но объявление параметра говорит обратное: *«Высота трейса ... **от пола юнита**»*
([CoverDetectionComponent.h](../Source/XRU1/Tactics/CoverDetectionComponent.h)).
**Комментарий и код расходятся**, а числа подобраны как «от пола»:

| Параметр | Задумано (от пола) | Фактически трейсит | Что засчитывается |
|---|---|---|---|
| `HalfCoverHeight = 60` | ящик 60 см | **148 см** над полом | стена ≥ ~148 см |
| `FullCoverHeight = 150` | стена 150 см | **238 см** над полом | стена ≥ ~238 см |

Последствия: ящик 60–100 см **не даёт укрытия вообще**; стена 150 см даёт Half
вместо Full; укрытием считаются только стены ≥1.5 м, из-за которых центр не
простреливает. Рекомендация «расставить укрытия ≥60 см» из `09_GAMEPLAY_STATUS`
§C при текущем коде **не даст ничего**. Лечит **Ф2**.

Что правильно и трогать не надо: `EyeHeightOffset = 60` тоже центр-относительный
и даёт глаза на ≈148 см над полом — это адекватно.

## II.4 Дефект В — укрытие ЦЕЛИ прячет её от выбора ⭐

**Это ответ на вопрос «а если цели за разными укрытиями».** LOS — прямой
сферо-свип от глаз стрелка к двум точкам цели (глаза `+60`, корпус `−20` от
центра, [TacticsCombatStatics.cpp](../Source/XRU1/Tactics/TacticsCombatStatics.cpp)).
Если цель стоит за полным укрытием, свип упирается в **её собственную стену**:

- цель за стеной 150 см: её глаза на ≈148 см — **ниже верха стены** → луч в
  глаза блокирован; луч в корпус (≈68 см) блокирован тем более;
- итог: `HasLineOfSight` = false → `GetTargetStatus` = `NoLineOfSight` →
  **цель не попадает в `GetAttackTargets` и её нельзя выбрать вообще**.

А в XCOM враг в полном укрытии — самая обычная цель: *«Cover provides an aiming
penalty defense bonus rather than making units completely untargetable»*.

**Правило XCOM:** peek работает **с обеих сторон** — *«soldiers can draw line of
sight from the tiles on either side»*. Юнит в укрытии стоит привалившись к краю,
поэтому узкое укрытие его не прячет (только защищает), а длинная глухая стена —
прячет по-настоящему. Значит правильное лечение — не «исключение для укрытия
цели», а **симметрия**: та же механика выглядывания, применённая к цели
(§III.2). Лечит **Ф5**.

## II.5 Дефект Г — глухая оборона не требует укрытия

`UGA_HunkerDown` ([TacticalClassAbilities.cpp](../Source/XRU1/Tactics/TacticalClassAbilities.cpp))
не проверяет наличие укрытия. При этом `ComputeHitChance` удваивает бонус только
`if (DefenseBonus > 0)`. Значит **в чистом поле глухая оборона сжигает всю
активацию юнита и не даёт ничего** — игрок теряет ход впустую и не понимает,
почему. В XCOM 2 способность прямо *«can only be used in cover»*. Лечит **Ф7**.

## II.6 Ловушки, на которых легко ошибиться

1. **Нельзя выкидывать центр из огневых позиций** — сломается стрельба поверх
   низкого укрытия (§I.1 п.8).
2. **Высота — это не фланг** (§I.1 п.11). Не делать «стрелок выше → укрытие цели
   не работает».
3. **Не кэшировать «укрытие против врага»** — сломает разрушаемость (§I.3 п.1).
4. **`BestCoverAround` считается по мировым осям**, а `GetCoverAgainst` — по
   точному направлению; стена под 45° даст расхождение (лечится в **Ф8**).
5. **Peek — не телепорт.** Сдвиг к краю укрытия существует только для расчёта LOS
   и для анимации. Позиция юнита, его укрытие и диск занятости остаются на его
   настоящей клетке. В самом XCOM анимация условна: *«The animation that plays
   displays shots in the straightest line possible»*.

---

# Часть III — Архитектура решения

Три независимых механизма, каждый в своей фазе:

| Сторона | Что решает | Фаза |
|---|---|---|
| **Стрелок** | выглянуть из-за края своего укрытия | Ф4 |
| **Цель** | своё укрытие цель не прячет, а защищает | Ф5 |
| **Стойка** | half → встать и стрелять поверх; full → выйти за край | Ф4 (данные) + Ф10 (анимация) |

## III.1 Сторона стрелка: огневые позиции (строго ДОБАВОЧНО)

> ⚠️ **Главная ловушка.** Нельзя «раз есть укрытие — стрелять только из углов,
> центр выкинуть». За НИЗКИМ укрытием юнит стреляет ПОВЕРХ стены из центра —
> выкинув центр, мы сломаем работающую стрельбу из half cover.

Разложение на две функции — **обязательное**, иначе в Ф5 код придётся
переписывать. `GetFiringPositions` только СОБИРАЕТ позиции и ничего не решает про
видимость; `HasLineOfSightFromLocation` перебирает и свипает.

```
GetFiringPositions(World, Unit, EyeLocation, OtherLocation) -> список точек глаз:

    результат = { EyeLocation }                       // ЦЕНТР — ВСЕГДА, см. ловушку выше
    для side in {+1,-1}:                              // быстрый step-out, как сейчас
        если свип EyeLocation → EyeLocation ± LosPeekOffset чист:
            результат += эта точка

    // после Ф2 Base для TraceCoverAtLocation — точка ПОЛА (§II.3)
    FootBase = EyeLocation - (0,0, EyeHeightOffset + CapsuleHalfHeight(Unit))
    dirToOther = normalize2D(OtherLocation - EyeLocation)
    если TraceCoverAtLocation(World, FootBase, dirToOther, ...) != None:
        для side in {+1,-1}:                          // края СВОЕГО укрытия
            offset шагами PeekEdgeStep до PeekEdgeMaxDistance:
                P = FootBase + Side*side*offset
                если TraceCoverAtLocation(World, P, dirToOther, ...) == None:
                    peekFoot = P + Side*side*(радиус капсулы Unit + PeekOutwardOffset)
                    спроецировать peekFoot на навмеш; нет проекции → отбросить
                    ближе Clearance к чужому диску занятости → отбросить
                    peekEye = peekFoot + (0,0, CapsuleHalfHeight + EyeHeightOffset)
                    свип EyeLocation → peekEye не чист → отбросить
                    результат += peekEye
                    break                             // край по этой стороне найден
    return результат            // ⚠️ НИКОГДА не пустой и НИКОГДА не «false»
    // Порядок в списке ЗНАЧИМ: центр первым, потом быстрый step-out, потом края.
    // По нему GetFiringStance определяет стойку (§III.3): если стрелять можно
    // из центра, это OverCover/Open, а не StepOut.
    // Unit == nullptr → вернуть { EyeLocation } и выйти (нет капсулы — нет peek).


HasLineOfSightFromLocation(World, EyeLocation, Target, Shooter = nullptr):

  1. БЫСТРЫЙ ПУТЬ (нынешнее поведение, дословно, не трогать):
        позиции = { EyeLocation } ∪ { ±LosPeekOffset вбок, если проход туда чист }
        если хоть из одной чистая линия до одной из 2 точек цели → true

  2. ЗАПАСНОЙ ПУТЬ (только если шаг 1 не дал результата и Shooter != null):
        PositionsA = GetFiringPositions(World, Shooter, EyeLocation, TargetLoc)
        PositionsB = GetFiringPositions(World, Target, глазаTarget, EyeLocation)  // Ф5
        PositionsB += точка КОРПУСА цели (TargetLoc - (0,0,20))                   // Ф5
        для каждой пары (a, b): чистый свип → true
        return false
```

> ⚠️ **Здесь легко посадить логическую дыру.** `GetFiringPositions` **не должна**
> возвращать «ничего»/`false`, когда у юнита нет укрытия, — она возвращает
> `{ центр (+ быстрый step-out) }`. Если вместо этого сделать ранний выход
> «нет укрытия → LOS нет», сломается сценарий **«стрелок в открытом поле, цель за
> узким укрытием»**: позиции цели просто не будут посчитаны, и цель окажется
> невидимой — притом что это критерий приёмки Ф5 №2.

В Ф4 реализуется `GetFiringPositions` и вызов для **стрелка**; строку `PositionsB`
добавляет Ф5. Шаг 1 не изменён — **всё, что стреляло раньше, стреляет и сейчас**;
шаг 2 — надмножество шага 1, поэтому запуск его после неудачи безопасен, а
включается он лишь когда цель не видна (это снимает вопрос производительности в
цикле AI на 48 кандидатов).

## III.2 Сторона цели: симметричное выглядывание (ядро Ф5)

**Формулировка правила.** Видимость в XCOM симметрична: из укрытия высовывается
не только стрелок, но и цель — юнит в укрытии так и стоит, привалившись к краю.
Поэтому вопрос «прячет ли цель её укрытие» решается **не порогом высоты, а тем
же механизмом Ф4, применённым к цели**:

> **LOS(A → B) есть, если существует огневая позиция A и позиция выглядывания B,
> между которыми луч чист.**

Это ровно **шаг 2 из §III.1** — отдельного алгоритма здесь нет, повторим его
суть для наглядности:

```
    PositionsA = GetFiringPositions(World, A, глазаA, локацияB)  // центр + края укрытия A
    PositionsB = GetFiringPositions(World, B, глазаB, локацияA)  // ТА ЖЕ функция, аргументы наоборот
    для каждой пары (a из PositionsA, b из PositionsB): чистый свип → true
```

⚠️ Функция **одна и та же**, просто вызывается дважды с переставленными
аргументами. Второй функции («`GetVisiblePositions`» и т.п.) заводить **не надо** —
это тот же «центр + края своего укрытия», просто посчитанный для другого юнита.
Для цели к списку по-прежнему добавляются обе её точки (глаза и корпус).

⚠️ **Укрытие считается от НАСТОЯЩЕЙ позиции цели, а не от её позиции
выглядывания.** Цель, которую видно «из-за края пиллара», всё равно получает свои
40 защиты: `GetCoverAgainst` вызывается от `ActorLocation`, позиции выглядывания
нужны ТОЛЬКО для проверки луча. Иначе получится абсурд «высунулся → потерял
укрытие → его проще подстрелить». Это частный случай инварианта «peek — не
телепорт» (§II.6 п.5).

**Почему это правильнее порога.** Разница «укрытие или здание» перестаёт быть
числом и становится **свойством самой геометрии, измеренным трейсом**:

| Сцена | Что даёт симметричный peek | Совпадение с XCOM |
|---|---|---|
| Цель за ящиком 60 см | глаза цели (≈148 см) и так выше — видна сразу, peek не нужен | ✔ низкое укрытие простреливается поверх |
| Цель за **узким** пилларом/машиной 160 см | край рядом → цель «привалена» к краю → видна, защита 40 | ✔ *«cover ... rather than making units completely untargetable»* |
| Цель за **длинной** стеной 160 см | края нет в пределах `PeekEdgeMaxDistance` → не видна | ✔ *«most full cover ... breaking Line of Sight for all units on the same plane of elevation»* |
| Цель ВНУТРИ здания | стены со всех сторон, края нет → не видна | ✔ здание скрывает |
| Преграда на полпути (3+ м от цели) | это не укрытие цели, края у неё не ищутся, луч блокирован | ✔ |

Обрати внимание: строка про длинную стену — **не баг, а механика XCOM**. Высокое
укрытие ДОЛЖНО рвать линию видимости; именно поэтому в XCOM воюют у углов, а не
вдоль стен.

**Что из этого следует для карты (важно для Ф0).** Модель хорошо играет там, где
укрытия **узкие**: пиллары, ящики, машины, короткие сегменты стен. Длинные
глухие стены будут честно рвать LOS. Это ровно тот принцип, по которому строят
карты в XCOM: *«maps were designed around partial and full cover as a way of
focusing fights to those areas»*.

**Никакой ручной разметки.** Всё считается по мешам в момент запроса, поэтому
система работает на любой карте, включая уже собранную и ту, что придёт из Fab, —
компонент-маркер на каждое укрытие не нужен. Это и было целью.

**Единственный оставшийся тюнинг** — `PeekEdgeMaxDistance`: «насколько далеко
юнит может выглянуть вдоль своего укрытия». Это не порог-затычка, а **игровой
параметр**, прямой аналог «одной клетки» в XCOM (у нас ≈250 см). Он уже лежит в
Data Asset и одинаково управляет обеими сторонами — стрелком и целью.

> Полезно знать: свип фильтруется **по типам объектов** `WorldStatic` /
> `WorldDynamic` ([TacticsCombatStatics.cpp](../Source/XRU1/Tactics/TacticsCombatStatics.cpp)),
> а юниты живут в канале `Pawn` — попасть в саму цель луч не может, добавлять её
> в игнор-лист не нужно.

**Цена по производительности** ограничена: не более 3 позиций стрелка × 3 позиции
цели = 9 свипов, плюс поиск краёв (≤2 стороны × ≤10 трейсов на сторону). И всё
это — **только на медленном пути**, который включается, когда прямая видимость
не прошла.

**Побочная выгода: видимость стала взаимной.** `LOS(A→B)` и `LOS(B→A)` теперь
считаются одним и тем же выражением с переставленными аргументами, поэтому не
может возникнуть «я его вижу, а он меня нет» — раньше такая асимметрия была
возможна.

**Комбинация Ф4 + Ф5 — ответ на «цели за разными укрытиями»:** оба выглядывают
из-за своих краёв, цель выбирается, а её укрытие уходит в шанс попадания (20/40).
Ровно XCOM.

## III.3 Стойка выстрела (half → встать, full → выйти)

`GetFiringPositions` возвращает не только точку, но и **как** оттуда стреляют:

```cpp
UENUM(BlueprintType)
enum class EFiringStance : uint8
{
    Open        UMETA(DisplayName = "Open"),        // укрытия нет — стреляет с места стоя
    OverCover   UMETA(DisplayName = "Over Cover"),  // half: привстать и выстрелить ПОВЕРХ, с места
    StepOut     UMETA(DisplayName = "Step Out")     // full: выйти из-за края и выстрелить
};
```

Определяется тем, какая позиция дала LOS:
- LOS из центра **и** между стрелком и целью укрытие `Half` → `OverCover`;
- LOS из центра, укрытия нет → `Open`;
- LOS только из peek-позиции у края → `StepOut` (типично для `Full`).

Потребители: **Ф10** (анимация: привстать / шаг вбок + выстрел), **Ф11**
(превью — откуда будет выстрел). ⚠️ Геймплейно юнит **не перемещается**
(§II.6 п.5): стойка — это выбор монтажа и, максимум, визуальный сдвиг на время
анимации с возвратом.

## III.4 Flanked / жёлтый щит

**flanked == цель в укрытии «вообще», но против этого стрелка укрытие не
работает.** Три состояния HUD: **синий** (half/full против меня) → **жёлтый**
(flanked) → **нет щита** (открыт). Ровно XCOM-семантика (§I.1 п.4).

## III.5 Сигнатуры

`HasLineOfSightFromLocation` сейчас — `(World, EyeLocation, Target)`
([TacticsCombatStatics.h](../Source/XRU1/Tactics/TacticsCombatStatics.h)) и не
знает стрелка; без него неоткуда взять капсулу, настройки укрытий и посчитать
условие (1) из §III.2.

```cpp
// ИЗМЕНИТЬ (параметр по умолчанию — старые вызовы компилируются)
static bool HasLineOfSightFromLocation(const UWorld* World, const FVector& EyeLocation,
    const AActor* Target, const AActor* Shooter = nullptr);

// НОВОЕ — единый источник позиций выглядывания. Зовётся ДВАЖДЫ с переставленными
// аргументами: для стрелка (огневые позиции, §III.1) и для цели (позиции, из
// которых её видно, §III.2). Отдельной функции для цели заводить не нужно.
static void GetFiringPositions(const UWorld* World, const AActor* Unit,
    const FVector& EyeLocation, const FVector& OtherLocation,
    TArray<FVector, TInlineAllocator<4>>& OutEyePositions);

// НОВОЕ — стойка выстрела для анимации и превью
UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
static EFiringStance GetFiringStance(const AActor* Shooter, const AActor* Target,
    FVector& OutFiringEyeLocation);

// НОВОЕ — фланг (§III.4)
UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
static bool IsTargetFlankedBy(const AActor* Target, const AActor* Shooter);
```

Мелочи, на которых легко споткнуться:
- forward-declare `class UCoverDetectionComponent;` и `class UCoverTuningDataAsset;`
  в `TacticsCombatStatics.h`; заголовки — в .cpp;
- `EFiringStance` положить рядом с `ECoverType` (`CoverTypes.h`) — там уже живёт
  енам укрытия, и его включают и UI, и геймплей;
- радиус/полувысота капсулы — как в `GetUnitClearance` (фолбэк 34 / 88);
- занятость — `GetUnitObstacles` + `GetUnitClearance`;
- проекция — `UNavigationSystemV1::ProjectPointToNavigation`.

## III.6 Data Asset тюнинга (реализуется в Ф3)

Сейчас параметры размазаны: часть — `UPROPERTY` на компоненте, часть —
`static constexpr` в `TacticsCombatStatics.h`, множитель hunker — литерал `2.f`.
Тюнить это дизайнеру нельзя. Собираем в один ассет.

```cpp
// Source/XRU1/Tactics/CoverTuningDataAsset.h
UCLASS(BlueprintType)
class XRU1_API UCoverTuningDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    // --- Геометрия детекта укрытия ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Detect", meta=(ClampMin="10"))
    float CoverTraceDistance = 120.f;          // до стены, чтобы считаться «в укрытии»
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Detect", meta=(ClampMin="10"))
    float HalfCoverHeight = 60.f;              // низкое укрытие, ОТ ПОЛА (после Ф2)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Detect", meta=(ClampMin="10"))
    float FullCoverHeight = 150.f;             // высокое укрытие, ОТ ПОЛА (после Ф2)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Detect")
    TEnumAsByte<ECollisionChannel> CoverTraceChannel = ECC_WorldStatic;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Detect", meta=(ClampMin="4", ClampMax="16"))
    int32 SurroundingDirections = 8;           // 4 оси или 8 (с диагоналями) — Ф8

    // --- Защита (числа XCOM) ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Defense", meta=(ClampMin="0", ClampMax="100"))
    float HalfCoverDefenseBonus = 20.f;        // XCOM: 20
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Defense", meta=(ClampMin="0", ClampMax="100"))
    float FullCoverDefenseBonus = 40.f;        // XCOM: 40
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Defense", meta=(ClampMin="1", ClampMax="4"))
    float HunkerDownMultiplier = 2.f;          // модель XCOM 1: удвоение

    // --- LOS и выглядывание ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|LOS")
    float EyeHeightOffset = 60.f;              // глаза над ActorLocation (центр капсулы) ≈148 см над полом
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|LOS", meta=(ClampMin="1"))
    float LosSphereRadius = 15.f;              // толщина луча (гасит «выстрел в щель»)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|LOS", meta=(ClampMin="0"))
    float LosPeekOffset = 70.f;                // быстрый step-out вбок от центра
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Peek", meta=(ClampMin="5"))
    float PeekEdgeStep = 25.f;                 // шаг поиска края укрытия
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Peek", meta=(ClampMin="50"))
    float PeekEdgeMaxDistance = 250.f;         // насколько далеко юнит выглядывает вдоль укрытия
                                               // (аналог «одной клетки» XCOM; управляет И стрелком, И целью)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Peek", meta=(ClampMin="0"))
    float PeekOutwardOffset = 15.f;            // вынос за угол сверх радиуса капсулы

    // --- Высота ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Height")
    float HeightAdvantageZ = 150.f;            // порог «выше цели»
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Height")
    float HeightAdvantageAimBonus = 20.f;      // XCOM: +20 сверху
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Height")
    bool bSymmetricHeightPenalty = true;       // у нас ±20; в XCOM штрафа снизу НЕТ
};
```

> ✅ **В проекте уже есть точно такой паттерн — копировать его, не изобретать.**
> `UTacticalHUDStyleData : public UDataAsset` ([TacticalHUDStyleData.h](../Source/XRU1/UI/TacticalHUDStyleData.h))
> лежит в `Content/XRU1Game/Data/DA_TacticalHUDStyle`, хранится на
> `UTacticsGameInstance::UITheme` с геттером `GetUITheme()`
> ([TacticsGameInstance.h](../Source/XRU1/Tactics/TacticsGameInstance.h)).
> Новый ассет — туда же: **`Content/XRU1Game/Data/DA_CoverTuning`**.
> GameInstance — **блюпринт** `/Game/XRU1Game/Core/BP_TacticsGameInstance`
> (`Config/DefaultEngine.ini`), поэтому назначение ассета — правка BP, а не C++.

**Как достаётся (функции статические, мира у них может не быть):**
1. `UTacticsGameInstance`:
   `UPROPERTY(EditDefaultsOnly, Category="Tactics|Cover") TObjectPtr<UCoverTuningDataAsset> CoverTuning;`
   — рядом с существующим `UITheme`, тем же стилем.
2. В `TacticsCombatStatics` — статический резолвер с **фолбэком на CDO**:
   ```cpp
   static const UCoverTuningDataAsset* GetCoverTuning(const UWorld* World);
   // GameInstance->CoverTuning, иначе GetDefault<UCoverTuningDataAsset>()
   ```
   Фолбэк обязателен: дефолты класса равны текущим числам, поэтому **без
   назначенного ассета поведение не меняется вообще**.
3. `UCoverDetectionComponent`:
   `UPROPERTY(EditDefaultsOnly) TObjectPtr<UCoverTuningDataAsset> TuningOverride;`
   (пер-юнитное переопределение), чтение: `TuningOverride` → глобальный → CDO.

**Что перевести на ассет (полный список):**
- `UCoverDetectionComponent`: `CoverTraceDistance`, `HalfCoverHeight`,
  `FullCoverHeight`, `CoverTraceChannel`, `HalfCoverDefenseBonus`,
  `FullCoverDefenseBonus`;
- `ComputeHitChance` — литерал `DefenseBonus *= 2.f` → `HunkerDownMultiplier`;
- `static constexpr` из `TacticsCombatStatics.h`: `EyeHeightOffset`,
  `LosPeekOffset`, `LosSphereRadius`, `HeightAdvantageZ`, `HeightAdvantageAimBonus`
  — **у них есть внешние потребители**: `GA_Attack.cpp` в `ComputeEffectiveAim`
  (бонус/штраф высоты) и `UnitAIController.cpp` в `FindCoverPoint`
  (`EyeHeight = CapsuleHalfHeight + EyeHeightOffset`
  — точка навмеша лежит на полу, поэтому там прибавляется ещё и половина капсулы;
  в `HasLineOfSight` прибавляется только `EyeHeightOffset`, потому что
  `ActorLocation` уже центр — **обе формулы дают одно и то же, не «чинить»**).

> ⚠️ **Миграция — НЕ копировать вслепую.** Значения `UPROPERTY` компонента могли
> быть подкручены в BP-классах юнитов. Прочитать их перед удалением полей нужно,
> но переносить дословно можно **не всё**:
>
> | Поле | Как мигрировать |
> |---|---|
> | `CoverTraceDistance`, `HalfCoverDefenseBonus`, `FullCoverDefenseBonus`, `CoverTraceChannel` | дословно: смысл не менялся |
> | `HalfCoverHeight`, `FullCoverHeight` | **дословно НЕЛЬЗЯ**: Ф2 сменила точку отсчёта с центра капсулы на пол. Значение, подкрученное под старую семантику, в новой означает другую высоту. Ставить целевые **60 / 150 от пола**; если в BP был осознанный override — пересчитать как `староеЗначение + CapsuleHalfHeight` и убедиться, что результат попадает в безопасные полосы (Ф0) |
>
> `SquadVisionRange` НЕ трогать — это не про укрытия.

---

# Часть IV — План по фазам

## Ф0 — Контент карты (предусловие) `[ ]`
> **Статус (2026-07-23):** editor-задача за игроком (агент карту без редактора не
> трогает). Весь C++ фаз Ф2–Ф5 готов и собран, поэтому расстановка укрытий
> теперь сразу заработает (высоты — от пола). Именно Ф0 разблокирует всю
> PIE-приёмку Ф2–Ф5. Компактный чеклист расстановки — в конце файла, §VI.3.

- На `Lvl_TopDown` расставить **WorldStatic**-укрытия. Высоты брать не «на глаз»,
  а из безопасных полос ниже.

> ⚠️ **Безопасные полосы высот (арифметика, а не вкус).** Глаза бойца — на
> `CapsuleHalfHeight + EyeHeightOffset` ≈ **148 см** над полом, а луч LOS — сфера
> радиуса `LosSphereRadius` = 15 см, поэтому её нижний край идёт на **133 см**.
> Отсюда:
>
> | Высота меша | Классификация | LOS из центра | Годится? |
> |---|---|---|---|
> | ≤ **125 см** | Half (20 защиты) | перелетает поверх | ✅ низкое укрытие |
> | **133–150 см** | Half (20 защиты) | **блокирует** | ❌ мёртвая зона — не ставить |
> | ≥ **160 см** | Full (40 защиты) | блокирует, нужен peek | ✅ высокое укрытие |
>
> Мёртвая зона 133–150 даёт худшее из двух миров: защита всего 20, а стрелять
> поверх уже нельзя. Это следствие того, что у нас непрерывные высоты, а не
> бинарное укрытие XCOM. Ставить низкие укрытия **60–120 см**, высокие **от 160 см**.
> Ровно 150 не ставить: трейс `FullCoverHeight` идёт на высоте 150 и по верхней
> грани такой стены скользит — детект Full получится неустойчивым.
> Если полосы захочется сдвинуть — менять `LosSphereRadius`/`EyeHeightOffset` в
> Data Asset, а не подгонять меши.
- **Основу должны составлять УЗКИЕ укрытия** — пиллары, ящики, машины, короткие
  сегменты стен. Это не косметика: модель выглядывания (§III.2) даёт хорошую игру
  именно у краёв, а длинные глухие стены будут честно рвать линию видимости.
  Так же строят карты и в XCOM — *«maps were designed around partial and full
  cover as a way of focusing fights to those areas»*.
- Нужна пара «оба бойца за разными УЗКИМИ укрытиями друг напротив друга» —
  главная сцена Ф5.
- Нужна **длинная глухая стена** и **здание** — негативные тесты Ф5 (за ними
  цель обязана оставаться невидимой).
- Хотя бы один уступ — для бонуса высоты.
- **Приёмка:** боец у стены → тег `Cover.Half`/`Cover.Full` (`showdebug abilitysystem`).
  ⚠️ **До Ф2 низкие укрытия тега НЕ дадут** — это дефект Б (§II.3), а не ошибка
  расстановки. Полную приёмку Ф0 проводить после Ф2.

## Ф1 — Движение точно в клик `[x]` ГОТОВО, принято игроком
**Причина бага.** Финальная вершина маршрута — уже спроецированная на навмеш
точка клика ([MoveRangeVisualizer.cpp](../Source/XRU1/Tactics/MoveRangeVisualizer.cpp)),
но приказ шёл с `AcceptanceRadius = 50`: path following считает цель достигнутой,
как только центр вошёл в радиус, — боец замирал в полуметре от курсора.

**Правка:** [TacticalPlayerController.cpp](../Source/XRU1/Tactics/TacticalPlayerController.cpp)
радиус 50 → **10 см**; [UnitAIController.cpp `RequestNextRouteLeg`](../Source/XRU1/Tactics/UnitAIController.cpp)
для финального отрезка `bStopOnOverlap = false`.

Матчасть: движок считает достижение как `AcceptanceRadius + GoalRadius +
MinAgentRadiusPct * AgentRadius`, где `MinAgentRadiusPct = 0.05` (вклад капсулы
~2 см) — решала именно величина 50. Ноль не ставим: допуск гасит «подползание» на
торможении `CharacterMovement`, а сорванный финальный отрезок штатно разбирается
в `OnMoveCompleted`. `RouteCornerAcceptance` (25) **не трогать**.

## Ф2 — Высоты укрытий от ПОЛА, а не от центра капсулы `[~]` ⭐
> **Статус (2026-07-23):** C++ реализован и собран (`Result: Succeeded`).
> PIE-приёмка за игроком (нужен редактор + контент Ф0). Что сделано:
> `TraceCoverInDirection` считает `Base = ActorLocation − CapsuleHalfHeight`
> (точка пола, хелпер `OwnerCapsuleHalfHeight`); `FindCoverPoint` передаёт
> `Projected.Location` в `EvaluateCoverAtLocation` без `+ CapsuleHalfHeight`;
> комментарии `EvaluateCoverAtLocation`/AI обновлены на «Base = пол».

Диагноз — §II.3. **Первая C++-правка:** пока укрытием считается только стена
≥1.5 м, ни peek, ни AI проверить нечем.

- Сменить смысл `Base` на точку пола (это же упрощает код):
  - `TraceCoverInDirection`: `Base = Owner->GetActorLocation() - (0,0,CapsuleHalfHeight)`;
  - `EvaluateCoverAtLocation` / вызов из AI: передавать **`Projected.Location`
    напрямую** (это уже пол) — из `FindCoverPoint` убрать `+ CapsuleHalfHeight`
    ([UnitAIController.cpp](../Source/XRU1/Tactics/UnitAIController.cpp));
  - обновить комментарии `TraceCoverAtLocation` / `EvaluateCoverAtLocation`:
    Base — **точка пола**.
- Проверить, что числа означают написанное: `HalfCoverHeight = 60` → ящик 60 см =
  half; `FullCoverHeight = 150` (порог) → стена 160 см = full.
- Сверить с XCOM-семантикой (§I.1 п.8): глаза на ≈148 см над полом → поверх
  60-см ящика видно, 150-см стена перекрывает (нужен peek).
- **Приёмка:** боец у ящика 60 см → `Cover.Half`; у стены 160 см → `Cover.Full`;
  из-за ящика цель напротив доступна, из-за стены — нет (до Ф4/Ф5).

## Ф3 — Data Asset тюнинга `[~]`
> **Статус (2026-07-23):** C++ реализован и собран. Что сделано:
> создан `UCoverTuningDataAsset` (`Source/XRU1/Tactics/CoverTuningDataAsset.h`)
> со всеми полями §III.6, дефолты = прежним числам; резолвер
> `UTacticsCombatStatics::GetCoverTuning(World)` с фолбэком на CDO;
> `UTacticsGameInstance::CoverTuning` и пер-юнитный
> `UCoverDetectionComponent::TuningOverride` + `GetTuning()`. Мигрированы:
> 6 полей компонента, `static constexpr` (`EyeHeightOffset`, `LosPeekOffset`,
> `LosSphereRadius`, `HeightAdvantageZ`, `HeightAdvantageAimBonus`), литерал
> hunker `2.f`→`HunkerDownMultiplier`, подключён `bSymmetricHeightPenalty` в
> `ComputeEffectiveAim`. **За игроком (editor):** 1) создать
> `Content/XRU1Game/Data/DA_CoverTuning` (Right-click → Miscellaneous → Data
> Asset → `CoverTuningDataAsset`), 2) назначить его в
> `BP_TacticsGameInstance.CoverTuning`, 3) высоты в ассете уже целевые (60/150 от
> пола). ⚠️ **Проверить BP-override:** до миграции у BP-юнитов поля укрытия
> компонента были `EditAnywhere` — если в каком-то `BP_*` они были подкручены,
> override молча потерян (поля удалены). Свериться и при нужде перенести в
> `DA_CoverTuning`/`TuningOverride` (§III.6 таблица миграции). Также, если в
> BP-графах были ноды Get/Set этих полей — их надо убрать.

Полная спецификация — §III.6. После Ф2 (числа уже с верным смыслом) и до Ф4/Ф5
(они читают параметры оттуда).
- **Приёмка:** создать `DA_CoverTuning` в `Content/XRU1Game/Data/`, назначить в
  `BP_TacticsGameInstance`; сменить `FullCoverDefenseBonus` 40 → 80 → в PIE шанс
  по бойцу в полном укрытии заметно падает; вернуть 40. **Без назначенного
  ассета игра ведёт себя ровно как до правки.**
- `SurroundingDirections` до Ф8 ни на что не влияет (`EvaluateSurroundings` пока
  жёстко перебирает 4 оси) — поле заводится заранее, чтобы не трогать ассет дважды.

## Ф4 — Огневые позиции стрелка (peek) `[~]`
> **Статус (2026-07-23):** C++ реализован и собран. Что сделано:
> `EFiringStance` в `CoverTypes.h`; `GetFiringPositions` (всегда непустой
> список: центр→быстрый step-out→края укрытия; порядок значим);
> `HasLineOfSightFromLocation` получил `const AActor* Shooter = nullptr` и
> запасной путь (быстрый путь дословно сохранён); `HasLineOfSight` пробрасывает
> `Viewer` как `Shooter`; `GetFiringStance` (Open/OverCover/StepOut + точка
> выстрела); CVar **`xru1.LOS.Debug`** (лог числа позиций + `DrawDebugSphere`).
> AI (`FindCoverPoint`) намеренно всё ещё зовёт LOS без `Shooter` — это Ф9.
> PIE-приёмка (критерии ниже) за игроком.

- Реализовать `GetFiringPositions` по §III.1 на базе `TraceCoverAtLocation`.
  ⚠️ Она **всегда** возвращает непустой список (минимум центр) — «нет укрытия»
  это НЕ повод вернуть пусто или отказать в видимости, см. врезку в §III.1.
- Добавить параметр `Shooter` в `HasLineOfSightFromLocation`, включить запасной
  путь. **Быстрый путь оставить дословно.**
- `HasLineOfSight(Viewer, Target)` — пробрасывать `Viewer` как `Shooter`.
- Заодно реализовать `GetFiringStance` (§III.3) — данные для Ф10/Ф11.
- **Добавить отладочный вывод, иначе приёмку нечем проверять.** В проекте уже
  принят паттерн CVar (`xru1.AI.LogCombat` в `UnitAIController.cpp`,
  `TAutoConsoleVariable<int32>`). Завести по образцу **`xru1.LOS.Debug`**: при
  значении 1 логировать для запроса LOS число огневых позиций, какая из них дала
  видимость и итоговую `EFiringStance`; полезно также рисовать позиции
  `DrawDebugSphere`. Без этого критерии 4 (стойка) и Ф5 №6 (взаимность)
  проверяются только на глаз.
- **Приёмка (PIE):**
  1. вплотную к УЗКОМУ full, враг сбоку за углом → враг в списке целей (Tab),
     выстрел проходит (до правки — не появлялся);
  2. в СЕРЕДИНЕ ШИРОКОЙ full, враг строго за стеной **в открытом поле** → цель
     НЕ доступна (регрессии «стрельба сквозь стену» быть не должно);
  3. за НИЗКИМ укрытием, враг напротив → доступна **так же, как сразу после Ф2**
     (не «как до всех правок»: до Ф2 низкое укрытие вообще не детектилось) —
     проверка на регрессию half cover, §II.6 п.1;
  4. `GetFiringStance`: у низкого = `OverCover`, у высокого с выглядыванием =
     `StepOut`, в открытом поле = `Open`.

## Ф5 — Симметричное выглядывание: цель тоже высовывается `[~]` ⭐
> **Статус (2026-07-23):** C++ реализован и собран. В запасном пути
> `HasLineOfSightFromLocation` добавлен второй вызов **той же**
> `GetFiringPositions(World, Target, глазаЦели, EyeLocation, PositionsB)` +
> точка корпуса цели (`TargetPoints[1]`), перебор пар (позиция стрелка ×
> позиция цели). Новой функции/порогов не заведено. Укрытие цели по-прежнему
> считается от её НАСТОЯЩЕЙ позиции в `ComputeHitChance` (peek — только для
> луча). PIE-приёмка за игроком; ключевой негативный тест — цель за длинной
> глухой стеной/в здании обязана остаться НЕвидимой.

Диагноз — §II.4, алгоритм — §III.2. **Без этой фазы Ф4 не даёт эффекта в самой
частой сцене «оба за укрытиями».**

- Позвать **ту же** `GetFiringPositions` второй раз, с переставленными
  аргументами, — получить позиции выглядывания ЦЕЛИ; перебирать пары
  (позиция стрелка × позиция цели). Новой функции и новых порогов не заводить.
- Работает только после Ф4 (там появляются и сама функция, и параметр `Unit`),
  поэтому **порядок Ф4 → Ф5 обязателен**.
- Быстрый путь не трогать: пары перебираются только если прямая видимость
  не прошла.
- Шанс попадания не трогать: укрытие цели уже вычитается в `ComputeHitChance`.
- ⚠️ **Длинная стена, за которой стоит цель, обязана по-прежнему её скрывать** —
  это не регрессия, а механика XCOM («высокое укрытие рвёт LOS»). Проверять
  надо именно это, а не «видно всех и всегда».

**Ожидаемые побочные эффекты — это НЕ баги, не «чинить».** `HasLineOfSight` —
общий предикат, поэтому после Ф4/Ф5 он станет разрешительнее сразу для всех
потребителей:

| Потребитель | Что изменится | Верно? |
|---|---|---|
| `UGA_Overwatch` (через `UGA_Attack::CanTargetActor`) | реакции срабатывают чаще: бегущие мимо укрытий теперь видны | ✔ как в XCOM |
| Камера «отряд видит действующего врага» (`ATacticalPlayerController`) | камера чаще летит к врагу в укрытии | ✔ |
| `SquadHasLineOfSight` (Squadsight снайпера) | союзники «подсвечивают» больше целей | ✔ |
| `FindCoverPoint` у AI | враг находит больше позиций «укрытие + можно ответить» | ✔ это и есть цель B.2 |

Единственное, за чем реально стоит следить, — **время хода AI**: у него
`HasLineOfSightFromLocation` зовётся на 48 позиций-кандидатов. Если ход врага
стал заметно дольше, уменьшать надо `PeekEdgeMaxDistance` / увеличивать
`PeekEdgeStep` в Data Asset, а не выключать симметрию.
- **Приёмка (PIE):**
  1. **оба бойца за разными УЗКИМИ укрытиями друг напротив друга** → цель
     выбирается, в панели видно её укрытие, шанс снижен на 20/40 (главный сценарий);
  2. цель за узким полным укрытием, стрелок в открытом поле → доступна, шанс −40;
  3. цель за **длинной глухой стеной** (края дальше `PeekEdgeMaxDistance`) →
     **недоступна**;
  4. цель ВНУТРИ здания → **недоступна**;
  5. цель за стеной на полпути (3+ м от неё) → недоступна;
  6. взаимность: если A видит B, то B видит A (проверить сменой хода).

## Ф6 — Список целей и выстрел на одних правилах `[x]` (кода не потребовалось)
> **Статус (2026-07-24):** подтверждено по коду — `GetTargetStatus`,
> `CanTargetActor`, `GetAttackTargets`, `ResolveShot` все идут через единый
> `HasLineOfSight`, поэтому peek/симметрия Ф4/Ф5 доходят до выбора целей и
> выстрела автоматически. Отдельного кода не нужно; PIE-приёмка — за игроком.

Проверочная фаза **сразу после Ф5** — чтобы поймать ошибку рано, пока правки
свежие. Кода может не потребоваться: `GetTargetStatus`/`CanTargetActor`/
`GetAttackTargets`/`ResolveShot` уже бьют по одному `HasLineOfSight`
([GA_Attack.cpp](../Source/XRU1/Tactics/GA_Attack.cpp)), поэтому Ф4 и Ф5
доходят до них автоматически.
- **Приёмка:** нет случаев «цель в списке, но выстрел отклоняется по LOS» и наоборот.

## Ф7 — Глухая оборона требует укрытия `[~]`
> **Статус (2026-07-24):** C++ написан (нужна пересборка). `UGA_HunkerDown`
> переопределяет `CanActivateAbility` — требует тег `Cover.Half`/`Cover.Full`
> (без укрытия активация отклоняется, AP не тратится). HUD: в `CanIssueCommand`
> case `HunkerDown` добавлена проверка `BestCoverAround != None` — кнопка серая
> без укрытия. PIE-приёмка за игроком.

Диагноз — §II.5. Мелкая, но заметная игроку правка.
- `UGA_HunkerDown`: запретить активацию без укрытия. Идиоматично для GAS —
  `ActivationRequiredTags` с тегами `Cover.Half` / `Cover.Full` (их уже вешает
  `UCoverDetectionComponent::ApplyCoverEffect`), либо проверка в `CanActivateAbility`.
- HUD: кнопка глухой обороны серая без укрытия (механизм серости уже есть).
- **Приёмка:** в чистом поле кнопка недоступна и активация не тратит AP; у стены
  работает как раньше.

## Ф8 — Flanked / жёлтый щит `[ ]`
- `EvaluateSurroundings`: 4 → `SurroundingDirections` (8 с диагоналями), §II.6 п.4.
- Реализовать `IsTargetFlankedBy` (§III.4).
- **UI-поверхностей ДВЕ:**
  1. **Иконка над головой** — `UCoverIconWidget` ([CoverIconWidget.h](../Source/XRU1/UI/CoverIconWidget.h))
     уже есть, но показывает `BestCoverAround` — укрытие **по кругу**, без
     стрелка, поэтому «флангирован» в текущем виде показать не может. Добавить
     режим «считать против текущего стрелка» на время прицеливания;
  2. **Панель цели** — [TacticalHUDWidget.cpp](../Source/XRU1/UI/TacticalHUDWidget.cpp),
     уже per-shooter: добавить третье состояние.
  - Иконка в карточке отряда (`GetCoverIconForUnit`) остаётся **локальной**
    (`BestCoverAround`), её per-shooter делать не надо — это статус своего бойца.
- **Текстуру «флангирован» добавить в `UTacticalHUDStyleData`** рядом с half/full;
  локальные поля BP остаются фолбэком (приём уже применён в `UCoverIconWidget`).
- **Приёмка:** зайти сбоку от врага в укрытии → жёлтый щит **и над головой, и в
  панели цели**, шанс вырос.

## Ф9 — AI на тех же правилах `[ ]`
- В `FindCoverPoint` ([UnitAIController.cpp](../Source/XRU1/Tactics/UnitAIController.cpp))
  **передавать `Unit` как `Shooter`** — иначе AI планирует по центру, а стреляет
  из peek (план ≠ факт, ровно тот баг, что чиним).
- Скор укрытия — против **всех видимых угроз**: начать просто (укрытие против
  ближайших N, штраф если хоть от кого-то открыт).
- **Удержание намерения:** не отменять начатый манёвр при временной потере LOS
  (`bManeuverInProgress` / `PendingManeuverPoint`).
- **Приёмка:** враг добегает до укрытия и **стреляет** оттуда; за 3–4 хода не мечется.

## Ф10 — Стойки и анимация выстрела из укрытия `[ ]`
- `OnCoverStateChanged(NewBestCover)` → ABP: half → присесть, full → вжаться,
  None → обычная стойка. Направление стены отдать в BP отдельным полем.
- Выстрел по `EFiringStance` (§III.3): `OverCover` → привстать и выстрелить с
  места; `StepOut` → шаг вбок к краю, выстрел, возврат; `Open` → обычный выстрел.
- ⚠️ Сдвиг при `StepOut` — **визуальный**, с возвратом (§II.6 п.5).
- **Приёмка:** у низкого приседает и привстаёт для выстрела; у высокого вжимается
  и выходит за край, потом возвращается.

## Ф11 — Превью peek для игрока `[ ]`
- Маркер точки, из которой боец выстрелит (`GetFiringStance` даёт и точку, и тип).
- **Приёмка:** игрок видит выглядывание до подтверждения.

## Ф12 — Крит от фланга (опционально, XCOM-паритет) `[ ]`
XCOM обеих частей: фланг/открытая цель = **+50% к криту**; крит-системы у нас нет
(§I.2).
- **Сначала** внести крит в `01_GDD.md` §5.4 (шанс, множитель урона).
- Затем: `CritChance` на юните, бонус при `IsTargetFlankedBy` или отсутствии
  укрытия, множитель урона в `ResolveShot`, «КРИТ» в HUD (стык с B.7).
- **Приёмка:** фланг заметно чаще даёт крит; в панели цели видно.

---

# Часть V — Проверка

## V.1 Тест-матрица (PIE на `Lvl_TopDown`)

| Сцена | Ожидание | Фаза |
|---|---|---|
| Ящик 60 см | тег `Cover.Half` | Ф2 |
| Стена 160 см | тег `Cover.Full` | Ф2 |
| Вплотную к УЗКОМУ full, враг за углом сбоку | враг в списке, выстрел из-за угла проходит | Ф4 |
| В середине ШИРОКОЙ full, враг за стеной в открытом поле | цель недоступна | Ф4 |
| За НИЗКИМ укрытием, враг напротив | доступна (стрельба поверх), бонус half работает | Ф2/Ф4 |
| **Оба бойца за разными УЗКИМИ укрытиями напротив друг друга** | **цель выбирается, шанс снижен на её 20/40** | **Ф5** |
| Цель за длинной глухой стеной 160 см | недоступна — высокое укрытие рвёт LOS (механика XCOM, не баг) | Ф5 |
| Цель ВНУТРИ здания | недоступна (нет «стрельбы сквозь дом») | Ф5 |
| Цель за стеной на полпути (3+ м от цели) | недоступна | Ф5 |
| Взаимность: A видит B | B видит A (проверить сменой хода) | Ф5 |
| Глухая оборона в чистом поле | кнопка недоступна, AP не тратится | Ф7 |
| Стрелок зашёл во фланг | жёлтый щит, укрытие не вычитается, шанс выше | Ф8 |
| Враг видит отряд из открытого поля | добегает в укрытие и стреляет тем же ходом | Ф9 |
| Выстрел из half / из full | привстал с места / вышел за край и вернулся | Ф10 |
| ПКМ вплотную к стене | боец встаёт точно в точку, тег `Cover.*` появляется | Ф1 ✅ |
| Hunker у стены + обстрел | входящий шанс вдвое ниже базового укрытия | — |
| Стрелок на уступе | +20 к аиму, укрытие цели **сохраняется** (§I.1 п.11) | — |
| `DA_CoverTuning` не назначен | поведение идентично прежнему (фолбэк на CDO) | Ф3 |

## V.2 Инварианты — НЕ ломать

1. **План == факт.** Превью/зона/список целей считаются той же математикой, что
   приказ/выстрел.
2. **Только добавляем.** Быстрый путь LOS сохраняется **по поведению**; edge-peek
   и исключение укрытия цели — дополнительные ветки. (Ф3 меняет источник констант
   на Data Asset — допустимо, дефолты равны прежним числам; сами числа и порядок
   проверок в быстром пути не менять.) Если что-то перестало стрелять — правка
   неверна, откатить.
3. **Единый источник правды.** LOS — `HasLineOfSight*`; стоимость хода —
   `GetMoveCostForDistance`; клиренс — `GetUnitClearance`; трейс укрытия —
   `TraceCoverAtLocation`; тюнинг — Data Asset. Параллельных реализаций не заводить.
4. **Укрытие против врага не кэшировать** — сломает разрушаемость (§I.3 п.1).
5. **Peek — не телепорт** (§II.6 п.5): геймплейная позиция, укрытие и диск
   занятости остаются на настоящей клетке юнита.
6. **Видимость симметрична и считается ОДНОЙ функцией** (§III.2): позиции
   выглядывания и стрелка, и цели даёт `GetFiringPositions` с переставленными
   аргументами. Не заводить для цели отдельную логику и не вводить порогов
   «укрытие или здание» — различие обязано вытекать из геометрии (есть ли край
   в пределах `PeekEdgeMaxDistance`), иначе система перестанет работать на
   произвольной карте.
7. **Навмеш статичен**, занятость — дисками; peek-точки проверять клиренсом.
8. **GE-компоненты укрытий** — как сейчас (`ApplyGameplayEffectToSelf`), НЕ
   `FindOrAddComponent` в конструкторе (фатал на старте редактора).
9. **Публичные `UFUNCTION`** — новые параметры только со значением по умолчанию.
10. **Донор — read-only.**

---

# Часть VI — Приложения

## VI.1 Источники

- «A Deep Dive Into XCOM and XCOM 2»: https://www.gamedeveloper.com/design/a-deep-dive-into-xcom-and-xcom-2
- Flanking (XCOM: EU) — фланг снимает укрытие, **+50% крит**, жёлтый щит:
  https://xcom.fandom.com/wiki/Flanking_(XCOM:_Enemy_Unknown)
- Cover — half/full, hunker (40%/80% снижение точности):
  https://xcom.fandom.com/wiki/Cover
- Hunker Down (XCOM 2) — **+30 защиты, +50 dodge, только в укрытии**:
  https://xcom.fandom.com/wiki/Hunker_Down_(XCOM_2)
- Critical hit (XCOM) — «Enemy Exposed»/фланг даёт +50% крита:
  https://xcom.fandom.com/wiki/Critical_hit
- Suppression (XCOM): https://xcom.fandom.com/wiki/Suppression
- XCOM 2: фланг = жёлтый щит + крит:
  https://steamcommunity.com/app/268500/discussions/0/3563973712299284677/
- **Укрытие даёт штраф к аиму, а не невидимость; peek берёт LOS с соседних
  клеток; анимация условна («straightest line»)**:
  https://steamcommunity.com/app/268500/discussions/0/1850323802586917410/
- «Gotcha Again» — референс превью LOS/фланга/step-out:
  https://steamcommunity.com/sharedfiles/filedetails/?id=866874504
- Dodge (XCOM 2): https://xcom.fandom.com/wiki/Dodge_(XCOM_2)
- Overwatch (XCOM 2) — множитель точности реакции ×0.7:
  https://xcom.fandom.com/wiki/Overwatch_(XCOM_2)
- Aim Bonuses (XCOM 2): https://strategywiki.org/wiki/XCOM_2/Aim_Bonuses
- Tactics (XCOM2), UFOpaedia: https://www.ufopaedia.org/index.php/Tactics_(XCOM2)

## VI.2 Журнал аудита документа

| Проход | Что найдено | Итог |
|---|---|---|
| 1 | Центр исключался из огневых позиций (сломал бы half cover); функция без актора-стрелка; peek заменял старый путь; фланг ломался на диагоналях | Исправлено |
| 2 | Не было сверки с XCOM 1; отсутствовали крит от фланга, разрушаемость, AOE-игнор, «высота ≠ фланг»; hunker ×2 захардкожен; параметры не тюнятся | Добавлены Часть I, Data Asset, фаза крита |
| 3 | Неверный путь ассета; не учтён **существующий** паттерн Data Asset и `UCoverIconWidget` | §III.6 переписан на существующий паттерн; фаза флангов расширена |
| 4 | **Дефект Б:** высоты укрытий от центра капсулы, а не от пола | Добавлены §II.3 и отдельная фаза |
| 5–6 | Противоречия, внесённые новой фазой (псевдокод, путь ассета, заголовок, формулировки приёмки и инварианта) | Исправлено |
| 7 | Межфайловая: `09_GAMEPLAY_STATUS` §C советовал укрытия «≥60 см» | Добавлено предупреждение |
| 8–9 | Расхождений не найдено | ✅ ✅ |
| 10 | **Дефект В:** укрытие ЦЕЛИ прячет её от выбора; нет понятия стойки выстрела; нумерация фаз (2.0/2/2.5) как ловушка | Добавлены §II.4, §III.2, §III.3, Ф5, Ф10; сквозная нумерация |
| 11 | Межфайловая: ссылка на исчезнувшую «Фазу 1.5»; §B.3 описывал один дефект из трёх | `09_GAMEPLAY_STATUS` §B.3 переписан |
| 12–13 | Расхождений не найдено | ✅ ✅ |
| 14 | **Дефект Г:** `UGA_HunkerDown` не требует укрытия → в чистом поле сжигает активацию с нулевым эффектом; строка Hunker в чек-листе была неверна для XCOM 2 (там +30/+50 dodge, а не удвоение); отсутствовали Suppression и Concealment/scamper в отклонениях; исключение укрытия цели держалось на двух эвристиках без связи с бонусом защиты | Добавлены §II.5, Ф6; уточнены §I.1 п.9 и §I.2; в §III.2 введено **условие (1)** — связь с `GetCoverAgainst` |
| 15 | Реструктуризация в Части 0/I–VI. При этом найдено: фаза-проверка «список целей» стояла через две фазы после Ф4/Ф5, которые она проверяет (ошибку поймали бы поздно); после вставки Ф7 устарели ссылки на фазы в `09_GAMEPLAY_STATUS` | Порядок Ф6–Ф8 переставлен (проверка сразу после Ф5), ссылки в `09_GAMEPLAY_STATUS` обновлены + добавлен дефект Г |
| 16 | Структурная сверка: 13 заголовков Ф0–Ф12 подряд, все ссылки на фазы в обоих файлах существуют, Части 0/I–VI на месте | ✅ согласован |
| 17 | Содержательная сверка: каждая механика §I.1 привязана к существующей фазе; каждая фаза имеет критерий приёмки; каждая строка тест-матрицы ссылается на существующую фазу; §I.2 отклонения не конфликтуют с фазами | ✅ согласован |
| 18 | **По замечанию игрока:** карта не ячеистая и не проектировалась под ручную разметку, значит различие «укрытие/здание» обязано вычисляться по мешам автоматически. Порог `CoverPeekHeadroom` и путь «пометить укрытия компонентом» этому противоречили | §III.2 переписан на **симметричное выглядывание**: позиции цели даёт та же `GetFiringPositions` с переставленными аргументами. Порог удалён из Data Asset, ручная разметка отменена; различие теперь вытекает из геометрии (есть ли край в пределах `PeekEdgeMaxDistance`). Обновлены §II.4, Ф0, Ф5, §V.1, §V.2 п.6 |
| 19 | В псевдокоде §III.2 фигурировало имя `GetVisiblePositions`, будто это вторая функция — риск, что агент заведёт дубль | Псевдокод переписан на два вызова `GetFiringPositions`; добавлен явный запрет заводить вторую функцию |
| 20 | Структурная сверка: порог удалён везде, кроме журнала; Части 0/I–VI и 13 фаз Ф0–Ф12 на месте; ссылки из `09_GAMEPLAY_STATUS` (Ф2, Ф4, Ф5, Ф7, Ф10, Ф11) существуют | ✅ согласован |
| 21 | Контракт функций: §III.1 расписывал поиск краёв встроенно в `HasLineOfSightFromLocation`, а §III.2 вызывал его как функцию — реализуя Ф4 буквально, агент писал бы код, который в Ф5 пришлось бы переписывать | В §III.1 добавлено требование сразу оформить тело шага 2 как `GetFiringPositions`, возвращающую список позиций |
| 22 | Не было сказано, что укрытие цели считается от её НАСТОЯЩЕЙ позиции, а не от позиции выглядывания — агент мог бы получить абсурд «высунулся → потерял 40 защиты»; плюс грамматика в §III.2 | Добавлено явное правило в §III.2 со ссылкой на инвариант «peek — не телепорт»; текст поправлен |
| 23 | Сквозная сверка: §III.1 ↔ §III.2 ↔ §III.5 ↔ Ф4 ↔ Ф5, отсутствие дублей функций, согласованность с §V.2 п.5 и п.6 | ✅ согласован |
| 24 | Контрольный проход | ✅ согласован |
| 25 | **Глубокая сверка с реальным кодом.** Найдены разъехавшиеся номера строк (`TacticalPlayerController` 664→672, `GetAttackTargets` 802→806, `TacticalHUDWidget` 110→111 — файлы правились и мной, и по ходу работ над HUD) | Номера строк убраны из **всех 26** ссылок: план правит те же файлы, на которые ссылается, поэтому ссылки переведены на **имена символов**; правило внесено в §0 |
| 26 | **Логическая дыра в §III.1.** Запасной путь начинался с «если у СТРЕЛКА нет укрытия → `return false`». После перехода на симметричную модель это ломало сценарий «стрелок в открытом поле, цель за узким укрытием»: позиции цели не считались бы вовсе — притом что это критерий приёмки **Ф5 №2**. Плюс `GetFiringPositions` возвращает точки глаз, и при переборе пар терялась вторая точка цели (корпус), нужная для целей на уступе | §III.1 переписан в факторизованный вид: `GetFiringPositions` **всегда** возвращает непустой список (минимум центр) и ничего не решает про видимость; перебор пар вынесен в `HasLineOfSightFromLocation`; добавлена точка корпуса цели; врезка с предупреждением и уточнение в Ф4 |
| 27 | Сквозная сверка после правок: §III.1 ↔ §III.2 ↔ Ф4 ↔ Ф5 ↔ приёмка, отсутствие номеров строк, нумерация Ф0–Ф12 | ✅ согласован |
| 28 | Контрольный проход | ✅ согласован |
| 29 | Проверены **все потребители** `HasLineOfSight` (Overwatch через `CanTargetActor`, камера «отряд видит врага», Squadsight, `FindCoverPoint`). Ф4/Ф5 делают предикат разрешительнее для всех сразу — это верно по XCOM, но нигде не было сказано, и агент мог принять «овервотч срабатывает чаще» за регрессию | В Ф5 добавлена таблица ожидаемых побочных эффектов + единственный реальный риск (время хода AI) с правильным способом лечения через Data Asset |
| 30 | Сквозная сверка после добавления: ссылки, нумерация, отсутствие противоречий с §V.1/§V.2 | ✅ согласован |
| 31 | Контрольный проход | ✅ согласован |
| 32 | **Числовая согласованность.** Глаза на ≈148 см, нижний край сферы LOS — 133 см, порог Full — 150 см. Значит меши высотой **133–150 см** классифицируются как Half (20 защиты), но луч из центра их НЕ перелетает — худшее из двух миров. Полосы высот нигде не были заданы, Ф0 говорила расплывчатое «~60 и ~150» | В Ф0 добавлена таблица безопасных полос (≤125 / мёртвая зона 133–150 / ≥150) с выводом арифметики |
| 33 | **Зависимость Ф2 → Ф3.** Ф3 предписывала переносить значения `UPROPERTY` из BP в Data Asset дословно, но Ф2 меняет точку отсчёта высот с центра капсулы на пол — дословный перенос `HalfCoverHeight`/`FullCoverHeight` дал бы другую высоту | Миграция разбита на таблицу: что переносится дословно, а что пересчитывается (`старое + CapsuleHalfHeight`) со сверкой по полосам Ф0 |
| 34 | **Наблюдаемость приёмки.** Критерий Ф4 №4 (`EFiringStance`) и Ф5 №6 (взаимность) не наблюдаемы в PIE — енам и позиции нигде не выводятся | В Ф4 добавлено требование завести CVar `xru1.LOS.Debug` по образцу существующего `xru1.AI.LogCombat` + `DrawDebugSphere` для позиций |
| 35 | **Однозначность и вырожденные случаи.** Не был задан порядок позиций в списке, а от него зависит стойка (перебор с края первым классифицировал бы half-cover как `StepOut`); не оговорён `Unit == nullptr` | В §III.1 зафиксирован порядок «центр → быстрый step-out → края» и поведение при `nullptr` |
| 36 | Сквозная сверка после правок направлений 1–5 | ✅ согласован |
| 37 | Граничный случай: стена ровно 150 см — трейс `FullCoverHeight` идёт на высоте 150 и скользит по её верхней грани, детект Full неустойчив | Полоса высокого укрытия поднята до **≥160 см**, добавлено пояснение почему |
| 38 | После подъёма полосы весь документ (§III.2, Ф2, приёмка, тест-матрица) продолжал использовать 150 см как тестовую стену — расхождение с собственной рекомендацией | Тестовые сцены переведены на 160 см; `FullCoverHeight = 150` оставлен как ПОРОГ (это разные вещи, различие проговорено) |
| 39 | Сквозная сверка: полосы высот ↔ Ф0 ↔ Ф2 ↔ §III.2 ↔ тест-матрица; порог vs высота меша нигде не путаются | ✅ согласован |
| 40 | Контрольный проход | ✅ согласован |

## VI.3 Передача игроку: что осталось в РЕДАКТОРЕ (2026-07-23)

Весь C++ фаз **Ф2–Ф5 реализован и собран** (`.\Build-XRU1.ps1` →
`Result: Succeeded` после каждой фазы, редактор был закрыт). Ниже — то, что
агент без открытого редактора сделать не может; всё это разблокирует PIE-приёмку.

**1. Ф3 — создать и назначить Data Asset (5 минут):**
- В `Content/XRU1Game/Data/`: ПКМ → Miscellaneous → Data Asset → выбрать класс
  **`CoverTuningDataAsset`**, назвать **`DA_CoverTuning`**.
- Открыть `BP_TacticsGameInstance` (`/Game/XRU1Game/Core/`), в поле
  **`Cover Tuning`** (категория Tactics|Cover) назначить `DA_CoverTuning`.
- Дефолты ассета уже целевые (высоты 60/150 **от пола**, защита 20/40, hunker ×2,
  peek 25/250/15). Без назначения игра ведёт себя как раньше (фолбэк на CDO).
- ⚠️ **Проверить BP-override укрытий:** поля `CoverTraceDistance`,
  `HalfCoverHeight`, `FullCoverHeight`, `CoverTraceChannel`,
  `HalfCoverDefenseBonus`, `FullCoverDefenseBonus` **удалены** с
  `UCoverDetectionComponent`. Если в каком-то `BP_*`-юните они были подкручены в
  дефолтах компонента — значение потеряно; перенести в `DA_CoverTuning`
  (глобально) или в `TuningOverride` юнита. Если в BP-графах были ноды
  Get/Set этих полей — заменить на чтение из ассета. Скорее всего override не
  было (дефолты кода = дефолты ассета), но свериться нужно.

**2. Ф0 — расставить укрытия на `Lvl_TopDown` (главный разблокиратор):**
- Меши **WorldStatic**. Высоты из безопасных полос (§Ф0): **низкие 60–120 см**
  (Half, простреливаются поверх), **высокие ≥160 см** (Full, нужен peek). Зону
  **133–150 см НЕ использовать** (мёртвая), ровно 150 тоже не ставить.
- Основа — **УЗКИЕ** укрытия (пиллары, ящики, машины, короткие сегменты стен):
  у них есть края, модель выглядывания играет.
- Обязательные сцены под тесты Ф5: **пара «оба бойца за разными узкими укрытиями
  напротив друг друга»** (главный сценарий), **длинная глухая стена ≥160** и
  **здание** (негативные — за ними цель обязана остаться невидимой), **уступ**
  (бонус высоты).
- Проверить навмеш (`P`): вокруг узких укрытий должен быть проход, иначе
  peek-точки не спроецируются на навмеш и края не найдутся.

**3. PIE-приёмка по фазам** (после 1 и 2):
- Включить диагностику: в консоли (`~`) `xru1.LOS.Debug 1` — в лог пойдёт
  `[LOS] shooter -> target: shooterPos=N targetPos=M visible=0/1`, а позиции
  нарисуются сферами (зелёные/красные — стрелок, жёлтые — цель); стойку печатает
  `[LOS] Stance ...`. Для AI-решений — `xru1.AI.LogCombat 1`.
- Прогнать критерии приёмки **Ф2** (теги `Cover.Half`/`Cover.Full` у ящика 60 /
  стены 160, `showdebug abilitysystem`), **Ф3** (сменить `FullCoverDefenseBonus`
  40→80 в ассете → шанс по цели в Full заметно падает; вернуть 40), **Ф4**
  (§Ф4 №1–4: узкий full/угол, широкий full/поле, низкое, стойки), **Ф5**
  (§Ф5 №1–6: оба за узкими укрытиями, длинная стена/здание невидимы,
  взаимность).
- Ожидаемые побочные эффекты (Overwatch чаще, камера чаще летит к врагу,
  Squadsight шире) — **не баги** (§Ф5). Следить только за временем хода AI.

**4. Дальше по плану — стоп на Ф5** до подтверждения игрока (Ф6 и далее — новая
сессия). Ф9 (AI на новых правилах) намеренно НЕ тронута: `FindCoverPoint` всё
ещё зовёт LOS без `Shooter`, поэтому AI пока на быстром пути — так и задумано.

## VI.4 Полировка боевого потока: камера выстрела + автопереход (2026-07-24)

> **Это НЕ фаза Ф0–Ф12** — это отдельная полировка «action state flow» поверх
> готового ядра укрытий. Здесь всё, что нужно новому чату, чтобы продолжить.
> Файлы: [TacticalCameraPawn.cpp/.h](../Source/XRU1/Tactics/TacticalCameraPawn.cpp),
> [TacticalPlayerController.cpp/.h](../Source/XRU1/Tactics/TacticalPlayerController.cpp),
> [GA_Attack.cpp](../Source/XRU1/Tactics/GA_Attack.cpp).

**⚠️ Статус сборки: C++ НАПИСАН, НУЖНА ПОЛНАЯ ПЕРЕСБОРКА (редактор был открыт).**
Добавлены новые `UPROPERTY` → **Live Coding не потянет**, только `.\Build-XRU1.ps1`
при закрытом редакторе. До сборки правки в игре не проявятся.

### Что сделано — камера кадра выстрела (`EnterShotFraming`)
Кадр «из-за плеча» (XCOM action-cam) переписан под реальную механику XCOM 2
(камера позади стрелка, лицом к цели; в модах чинят ровно «наезд в стену»).
Три механики вместо прежних трёх фиксированных значений:
1. **Выбор плеча по геометрии** (`PickShoulderOffset`): трассирует оба знака
   `ShotFrameYawOffset`, берёт сторону, откуда цель видна и камера не за стеной →
   чинит «всегда 1 ракурс».
2. **Адаптив по дистанции**: зум `ShotFrameZoom`→`ShotFrameZoomFar`, точка обзора
   `ShotFrameTargetBias`→`ShotFrameTargetBiasFar` по `Dist/ShotFrameFarDistance` →
   на дальней цели в кадр входят и стрелок, и цель.
3. **Наклон под перепад высот** (`ShotFramePitchPerZ`): цель ниже — круче вниз.
4. **Безопасная длина пружины ОДНОРАЗОВО** (трейс pivot→камера при входе в кадр),
   а НЕ поштучный `bDoCollisionTest` — он давал резкий скачок и мигание на близких
   целях (пружина каждый кадр упиралась в укрытие у стрелка). ⚠️ **Не возвращать
   `bDoCollisionTest=true` в кадр** — это и была причина мигания.

**Тюнинг — на `BP_TacticalCameraPawn`** (категория `Tactics|Camera|Shot`):
`ShotFrameZoom` (ближний зум), `ShotFrameZoomFar`, `ShotFrameZoomNear` (пол),
`ShotFrameFarDistance`, `ShotFrameTargetBias`/`…Far`, `ShotFrameYawOffset` (модуль
доворота плеча), `ShotFramePitch`/`ShotFramePitchPerZ`, `ShotFrameLookHeight`.

### Что сделано — автопереход хода (гонка с кадром выстрела)
**Корень:** `GA_Attack` списывает AP в `CommitAbility` (`ApplyCost`) → делегат
`HandleSelectedUnitAPChanged(AP=0)` срабатывал и делал `SelectNextUnit` СРАЗУ, а
кадр выстрела стартует позже (`ResolveShot`→`NotifyShotFired`). Переход рвал
кинематограф ещё до его начала. **Фикс:** делегат теперь ВСЕГДА откладывает
переход (`bPendingAutoAdvance` + штамп `PendingAutoAdvanceFrame = GFrameCounter`),
а `PlayerTick` завершает его, дождавшись конца кадра выстрела; штамп не даёт
сработать в тот же кадр, где кадр выстрела ещё не начался. Ходьба (1 и 2 AP) шла
отдельным путём (по остановке в `PlayerTick`) и работала — не тронута.
**Подтверждено игроком после сборки: «ход переходится».**

### Проверка в PIE (после сборки)
- Выстрел игрока → плавный кадр «из-за плеча» **без стены в кадре и без мигания**,
  особенно на близкой цели; на разных целях — разные плечи.
- После доигрывания кадра — автопереход к след. юниту (не посреди выстрела).

### Что осталось доработать (для нового чата)
1. **Тонкая настройка камеры по скринам** — числа выше на `BP_TacticalCameraPawn`.
2. **Перемещение камеры по врагам (Q/E циклит цели)** — игрок: «не идеально, но
   лучше, можно оставить». Низкий приоритет.
3. **Стрельба по врагу В УКРЫТИИ** — проверить нельзя, пока AI не умеет прятаться
   так, чтобы можно было пикать. Отложено в **AI-блок (Ф9)**: там же подкрутить
   моменты. Это основной следующий крупный блок.
