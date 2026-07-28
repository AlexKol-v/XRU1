# 17 — Ручное завершение A16 после C++-рефактора

> **Актуально на 2026-07-28.** Базовый C++-рефактор собирался на UE 5.7; после
> текущих правок fire/camera требуется новая сборка или Live Coding. `BP_GA_Attack`
> уже частично мигрирован, но возврат из step-out всё ещё требует ручной правки
> графа, см. раздел 0.1.

## 0.1. Обновление после проверки `BP_GA_Attack` 2026-07-28

Пользователь пересобрал `BP_GA_Attack`, и MCP-аудит сохранённого графа показал:

- `OnFireActionStarted` уже есть, `GetFireActionPresentation` вызывается, montage запускается через `PlayMontageAndWait`.
- В логе PIE native notify доходил до C++, но отклонялся (`LogFireCommitNotify: [FireCommit] Отклонён notify`). C++ исправлен: поиск active ability instance и проверка montage instance теперь устойчивее, а лог дополнительно пишет `reject reason=...`.
- Камера обычного выстрела теперь стартует в начале fire transaction, а `FrameShotForDuration(..., -1)` снова значит “держать до terminal callback”, не 1.6 секунды.
- `IsUnitInTransit` теперь учитывает фактическую velocity `CharacterMovement`, поэтому step-out через Blueprint `AI MoveTo` должен включать Moving-позу, а не скольжение в idle/cover.
- В `BP_GA_Attack` всё ещё найден ручной возврат через `Set Actor Location` (`bTeleport=true`, `bSweep=false`) перед `HugCover`. Это надо заменить вручную на обратный `AI Move To` к `StepOutHomeLocation`: `OnSuccess` -> `HugCover` -> `CompleteFireAction`, `OnFail` -> `HugCover` -> `AbortFireAction`. Пока этот узел остаётся, возврат из step-out будет выглядеть резко/телепортно.

После C++-правок обязательно закрыть Editor или собрать через Live Coding (`Ctrl+Alt+F11`), затем повторить PIE. Если снова будет отказ notify, смотреть новую строку `LogFireCommitNotify: [FireCommit] reject reason=...`.

## 0. Что уже сделано и что сейчас сломано

- `UGA_Attack` и `UGA_Overwatch` больше не наносят урон при activation.
- Механика выстрела разрешена только нативным `XRU1 Fire Commit` notify из
  активного montage; guard проверяет `ActionId`, asset и `MontageInstanceId`.
- Target, chance, damage, firing eye, stance, home root и presentation root
  замораживаются до списания AP. До commit техническая отмена возвращает AP.
- Action/reaction держат GAS/UI/AI barrier до terminal callback; Overwatch
  владеет pause mover/camera/slow-mo до завершения реакции.
- StepOut-кандидат теперь строится от активной стены, проходит nav/occupancy/
  capsule/LOS проверки и не использует eye point как root goal.
- Enemy combat movement использует тот же occupancy-aware planner/executor, что
  и отряд.
- Смерть имеет одного владельца: при назначенном `DeathMontage` AnimBP `Dead`
  state не запускает вторую death sequence; montage callback ведёт в ragdoll.
- В сохранённом `ABP_Solider`: HighCover Look заменён на standing ADS, удалён
  duplicate `Fall_Land → Locomotion`, удалены stale `CoverEnterAnim`-узлы.
- **Не сделано:** старые `BP_GA_Attack`/`BP_GA_Overwatch` всё ещё начинают
  montage из `OnShotFired`/`OnReactionShot`, то есть после точки, которая теперь
  наступает только на commit. Получается цикл ожидания и watchdog возвращает AP.

Незавершённые MCP-заготовки переменных/события не были сохранены в `.uasset`:
ручную миграцию начинайте с текущих чистых 53/50-node графов.

## 1. Обязательные Montage Notify

Открыть по очереди `AM_Fire_Open` и `AM_Fire_OverCover` в Montage Editor.

1. На реальном кадре muzzle flash/отдачи добавить notify класса
   **`XRU1 Fire Commit`** (`UAnimNotify_FireCommit`).
2. В свойствах notify поставить **Montage Tick Type = Branching Point**.
3. В каждом montage оставить ровно один такой notify во всех используемых
   sections. Не ставить второй notify «для надёжности».
4. Проверить Slot/Section и сохранить asset.
5. `AM_Fire_OverCover` сейчас использует `MM_Rifle_DryFire`. Если в нём нет
   читаемого выстрела/отдачи, заменить segment подходящим fire-клипом; нельзя
   двигать gameplay notify на визуально пустой кадр.

Dedicated MCP notify track не читает, поэтому этот пункт подтверждается только
Montage Editor + PIE/Animation Insights.

## 2. `BP_GA_Attack`: полностью заменить старый action-flow

Создать переменные без `Instance Editable`/`Expose on Spawn`:

- `ActiveFireActionId` — `Guid`;
- `bAbortAfterReturn` — `bool`;
- можно переиспользовать существующие `UnitBase`, `Target`, `FireMontage`,
  `StepOutHomeLocation`, `ProjectedLocation` (последняя теперь содержит
  **presentation root**, а не eye/nav projection).

Новый вход — **Event `OnFireActionStarted(Target, ActionId)`**.

1. Сохранить `Target` и `ActiveFireActionId`; получить Avatar, cast к
   `UnitBase`, сохранить `UnitBase`.
2. Вызвать `GetFireActionPresentation(ActionId)` и один раз сохранить:
   `FireMontage`, `OutStance`, `OutHomeRootLocation → StepOutHomeLocation`,
   `OutPresentationRootLocation → ProjectedLocation`.
3. Если montage невалиден — `AbortFireAction(ActionId)`.
4. Если stance не `StepOut`: повернуть `UnitBase` к `Target.ActorLocation` и
   запустить `PlayMontageAndWait`.
5. Если stance `StepOut`: `AI Move To` для `UnitBase` в
   **`ProjectedLocation` (root)**, `Acceptance Radius` 5–10, `Stop on Overlap`
   false. На `OnSuccess` сначала проверить
   `IsFireActionCurrent(ActiveFireActionId)`, затем повернуться к target и
   запустить тот же montage. На `OnFail` — `AbortFireAction`.
6. У используемого `PlayMontageAndWait` поставить
   **`bStopWhenAbilityEnds = true`**. `OnBlendOut` не является terminal.
7. `OnCompleted`: проверить ActionId. Для `StepOut` вернуть capsule/root в
   `StepOutHomeLocation`, вызвать `HugCover`, затем
   `CompleteFireAction(ActionId)`. Для direct shot вызвать Complete сразу.
8. `OnInterrupted` и `OnCancelled`: до/после возврата вызвать
   `AbortFireAction(ActionId)`. После commit этот метод не откатывает урон/AP,
   а только завершает presentation; до commit возвращает AP snapshot.
9. Каждый callback `AI Move To`/montage сначала пропускать через
   `IsFireActionCurrent(ActionId)`. Callback старого action не должен запустить
   montage уже новой цели.
10. `OnShotFired` оставить только для muzzle VFX/звука. Удалить из него
    `GetFireMontageFor`, `Project Point to Navigation`, movement, montage,
    return и `HugCover`.
11. `OnFireActionTerminated` только очищает transient BP-поля.

`FireCommit` вручную из Event Graph **не вызывать**: его вызывает только
нативный notify конкретного montage instance.

## 3. `BP_GA_Overwatch`: зеркальный reaction-flow

Создать `ActiveReactionActionId : Guid` и `bAbortAfterReturn : bool` (не
instance editable). Новый вход —
`OnReactionActionStarted(Target, ActionId)`.

Повторить раздел 2 с API:

- `GetReactionActionPresentation`;
- `IsReactionActionCurrent`;
- `CompleteReactionAction`;
- `AbortReactionAction`.

`OnReactionShot` оставить только VFX/звук. Не вызывать из BP
`NotifyShotFired`, не управлять global time dilation и не возобновлять mover:
этими ресурсами единолично владеет C++ reaction subaction.

## 4. Пять `BP_Unit_*`: завершение Death Montage

В `BP_Unit_Assault`, `BP_Unit_Sniper`, `BP_Unit_Medic`, `BP_Unit_Tank`,
`BP_Unit_Marauder` открыть `OnDied`.

1. Оставить ровно один запуск назначенного `DeathMontage`.
2. `Completed` подключить к `NotifyDeathMontageFinished(false)`.
3. `Interrupted` и `Cancelled` подключить к
   `NotifyDeathMontageFinished(true)`.
4. Не запускать вторую death sequence/state вручную и не ставить отдельный
   Delay для ragdoll. `RagdollDelay` в C++ — только watchdog потерянного callback.
5. `OnHitReact` должен играть только нелетальную реакцию; не вызывать его из
   death-ветки.

## 5. Компиляция и сохранение

Для каждого изменённого BP:

1. Compile: зелёная галочка, 0 errors и 0 warnings action-lifecycle.
2. Save сразу после успешной компиляции.
3. Закрыть и снова открыть asset; проверить, что связи не потеряны.
4. В Output Log не должно быть `FireAction Watchdog abort`,
   `Reject stale/duplicate commit` или `Отсутствует FireCommit` при штатном
   выстреле.

После сохранения выполнить из корня проекта при закрытом Editor:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build-XRU1.ps1
```

## 6. Минимальная PIE-приёмка

- Open/Half/Full cover × цель слева/справа: ровно один muzzle frame и один урон.
- До notify прервать montage: урона/шума нет, AP обычной атаки возвращены.
- После notify прервать montage: урон не дублируется и не откатывается; action
  завершается и камера освобождается.
- StepOut у внутреннего и внешнего угла: юнит достигает root-точки, стреляет и
  возвращается к home anchor; не врезается в угол.
- Во время action нельзя выбрать другого бойца или завершить ход.
- Overwatch останавливает mover, играет один reaction shot и только затем
  возобновляет тот же маршрут; прежний global time dilation восстанавливается.
- Враг обходит союзника либо replans; не стоит бесконечно в его capsule.
- HighCover aim остаётся standing; HalfCover остаётся crouched/over-cover.
- Смена цели не разворачивает корпус от выбранного target и не запускает второй
  montage.
- Летальное попадание: один Death Montage, затем ragdoll; нет наложения Dead
  state sequence.

Если AP снова возвращается, сначала проверить наличие/позицию Branching Point и
факт запуска `OnFireActionStarted`; Delay или прямой `ResolveShot` добавлять
нельзя — они снова обходят транзакцию.
