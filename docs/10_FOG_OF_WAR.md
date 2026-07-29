# Туман войны

Актуально на 2026-07-29. Цель — скрывать не только геометрию за пределами
разведки, но и любую игровую информацию, которая может выдать противника.
Источник истины всегда CPU-логика; post-process лишь визуализирует её результат.

## 1. Принятое решение

Для XRU1 используется гибридная архитектура:

```text
живые источники зрения отряда
        ↓
UFogOfWarSubsystem — авторитетная CPU-видимость
        ├─ точная проверка actor visibility → выбор цели, hover, HUD, камера
        ├─ CurrentVisible / Explored grid → состояние местности
        ├─ UFogRevealableComponent → mesh/widget/decal/custom depth
        └─ один Render Target → один post-process камеры
```

Почему не `SceneCapture2D` на каждом бойце:

- gameplay-решения не должны зависеть от картинки или частоты рендера;
- несколько capture-компонентов повторно рендерят сцену и плохо масштабируются;
- CPU-состояние можно тестировать, сохранять и одинаково использовать в C++/BP;
- один Render Target достаточно обновлять только после события видимости.

Состояния клетки:

| Состояние | Мир | Противники/интерактивные объекты |
|---|---|---|
| `Unknown` | почти полностью закрыт | скрыты |
| `Explored` | затемнённая запомненная геометрия | скрыты; last-known marker — только отдельная будущая механика |
| `Visible` | отображается нормально | показываются после точной actor-проверки |

`Visible` — объединение зрения всех живых бойцов игрока. Знания AI о бойцах
игрока являются отдельной системой памяти AI и не читают player fog grid.

## 2. СДЕЛАНО

Добавлен `UFogOfWarSubsystem` (`Source/XRU1/Tactics/FogOfWarSubsystem.*`) с
первым авторитетным gameplay-предикатом:

- `IsActorCurrentlyVisible` — хотя бы один живой боец отряда находится в
  `SquadVisionRange` и имеет боевой LOS до актора;
- `GetCurrentlyVisibleEnemies` — живые враги, видимые сейчас;
- `GetCurrentlyVisibleEnemyCount` — безопасный счётчик для HUD;
- `ATacticalPlayerController::IsVisibleToSquad` делегирует проверку subsystem;
- move preview больше не учитывает скрытых врагов и не выдаёт засаду через
  `EnemiesSeeing/Exposed/Flanked`;
- логика сопровождения вражеского хода камерой использует тот же предикат;
- `UTacticalHUDWidget::GetVisibleEnemyCount` доступен Blueprint.

Это только фундамент текущей видимости. Пока **не сделаны** persistent
`Explored`, сетка, кэш/события, автоматическое скрытие actor presentation и
визуальный post-process. Текущие запросы повторяют LOS-проверки синхронно, поэтому
их нельзя бездумно вызывать для всех акторов каждый кадр.

## 3. Известные каналы утечки

До завершения этапа F1 каждый пункт ниже считается открытым независимо от того,
что геометрия уже затемнена материалом:

- WBP может всё ещё отображать полный `GetAliveEnemyCount` вместо
  `GetVisibleEnemyCount`;
- hover trace, outline/custom depth и target panel требуют visibility gate;
- список целей атаки/abilities должен фильтроваться через subsystem;
- overhead widgets, health bars, status icons, decals и VFX скрытого врага
  должны выключаться вместе с его mesh;
- камера и auto-focus не должны центрироваться на скрытом враге;
- path/move preview не должен показывать скрытую occupancy или угрозу;
- звук, floating combat text и tutorial objective не должны раскрывать точную
  позицию без отдельного дизайнерского правила;
- скрытый actor не должен ловить click/hover, даже если его collision остаётся
  включённым для симуляции.

Полное количество врагов остаётся допустимым внутренним условием победы, но не
является player-facing данными.

## 4. ПЛАН реализации

### F0 — единый предикат gameplay visibility

- [x] Создать `UFogOfWarSubsystem`.
- [x] Перевести squad visibility, enemy camera и move preview на него.
- [x] Дать HUD безопасный `GetVisibleEnemyCount`.
- [ ] Добавить автоматические/functional tests на радиус, LOS и объединение
      зрения двух бойцов.

### F1 — actor gating и событийное обновление

- [ ] Добавить `UFogVisionComponent` бойцам игрока: радиус, высота глаз, floor
      layer и события перемещения/смерти/эвакуации.
- [ ] Добавить `UFogRevealableComponent` врагам и скрываемым objectives. Он
      централизованно управляет mesh, attached weapon, `WidgetComponent`, decal,
      VFX, custom depth, hover/click, не уничтожая actor и не ломая AI.
- [ ] Ввести `OnActorVisibilityChanged(Actor, bVisible)` и кэш видимых actor.
- [ ] Пересчитывать видимость после завершения шага движения, spawn/despawn,
      смерти, открытия двери и смены сценария; во время бега — с ограниченной
      частотой, а не из каждого Widget Tick.
- [ ] Фильтровать attack targets, hover, target panel, overhead HUD, outlines,
      camera focus и player-facing events одним API subsystem.
- [ ] Зафиксировать collision-политику: presentation/input скрываются, но
      симуляционная collision/occupancy остаётся; при физическом контакте
      выполняется reveal/replan без выдачи позиции заранее.

DoD F1: скрытый враг существует и действует, но не оставляет ни одного
визуального, UI, input или camera-сигнала до обнаружения.

### F2 — `Unknown / Explored / Visible` на CPU

- [ ] Добавить `UFogOfWarConfigDataAsset` и `AFogOfWarBoundsVolume`: world
      origin, extents, grid cell size, floor bands и выбранный trace channel.
- [ ] Хранить по каждому этажу два `TBitArray`: `CurrentVisible` и `Explored`.
      При пересчёте `Explored |= CurrentVisible`.
- [ ] Начать с клетки 50–100 см; подобрать размер по bounds, не создавать сетку
      на весь showcase-уровень.
- [ ] Для каждого источника строить radial/DDA visibility по CPU и
      объединять bitsets. Точная видимость actor остаётся дополнительным LOS,
      чтобы визуальная погрешность края RT не разрешала атаку сквозь стену.
- [ ] Поддержать несколько Z-band в CPU. В Render Target отображать активный
      этаж, а не создавать capture/RT на каждого бойца.
- [ ] Добавить dirty regions или хотя бы event batching: несколько перемещений
      за кадр дают один пересчёт.

DoD F2: состояние клеток детерминировано, не зависит от FPS и корректно
сбрасывается/восстанавливается для выбранного сценария.

### F3 — один Render Target и post-process

- [ ] Преобразовать CPU-слой в один `RT_Fog_Showreel` (`RGBA8`): один канал для
      current visibility, второй для explored.
- [ ] Обновлять RT после изменения bitset, а не каждый кадр. Не вызывать
      `DrawMaterialToRenderTarget` отдельно для каждого источника зрения.
- [ ] Создать `M_PP_FogOfWar`: world position из scene depth → UV bounds → RT;
      `Unknown` затемняется сильнее, `Explored` остаётся читаемым, `Visible`
      не меняет сцену.
- [ ] Передавать texture parameter через dynamic material instance, а origin,
      extents, active floor и интенсивность — через параметры/MPC.
- [ ] Подключить один blendable к `PostProcess` в `BP_TacticalCameraPawn` и
      проверить порядок с уже существующим `M_OutlinePP`.

DoD F3: картинка совпадает с gameplay visibility на ключевых границах; HUD не
затемняется; нет `SceneCapture2D` на юнитах.

### F4 — сценарии, save/load и дизайнерские исключения

- [ ] Конфиг начального reveal задаётся scenario data, а не Level Blueprint.
- [ ] Objective может иметь правила `AlwaysKnown`, `RevealWhenExplored` или
      `RevealWhenVisible`; враг всегда использует `RevealWhenVisible`.
- [ ] Если появится last-known marker, хранить отдельный timestamp/location и
      никогда не оставлять живой outline скрытого actor.
- [x] В scope демо mid-combat save не поддерживается: `Explored` сбрасывается
      на каждый новый `ScenarioRunId` и не переносится между Tutorial/Mission01.
- [ ] Добавить debug overlay/cvar: bounds, клетки, vision sources, LOS block и
      причина последнего reveal/hide.

### F5 — оптимизация и регрессия

- [ ] Профилировать в Unreal Insights на максимальном составе карты.
- [ ] Целевой предварительный бюджет: средний fog update до 2 мс на целевом ПК,
      без постоянной работы в кадрах, где никто не перемещается.
- [ ] Проверить массовую смерть/spawn, быструю смену этажей, дверь, retry,
      save/load и переход tutorial → mission.
- [ ] Оставить точный CPU-предикат обязательным даже при выключенном
      post-process на Low scalability.

## 5. Одна карта для tutorial и mission

`Showreel_Scene` используется двумя сценариями, поэтому имя загруженной карты
не может быть идентификатором fog-сессии. Scenario Director при старте обязан
передать subsystem как минимум `ScenarioId` и новый `RunId`.

`UTacticalScenarioDataAsset`, `UTacticsGameInstance::StartCombatScenario` и
`ATacticalScenarioDirector` уже добавлены; в Data Asset есть `ScenarioId`,
`FogProfileId` и `bResetFogOnStart`, а GameInstance выдаёт монотонный
`ScenarioRunId` каждому запуску. API `ResetForScenario` и проверка поколения
во всех fog/quest async callbacks относятся к F4/P0 и пока не реализованы.

Контракт запуска:

1. `ResetForScenario(ScenarioId, RunId)` очищает `CurrentVisible`, actor cache и
   pending update.
2. `Explored` также очищается при Tutorial → Mission01 и при полном retry. Оно
   загружается только из save того же `ScenarioId/Run` по явно принятому правилу.
3. Активируются vision sources и initial reveal именно выбранного scenario
   profile.
4. Только после первого полного пересчёта открывается управление/делается fade-in,
   чтобы скрытые акторы не мигнули один кадр.

Это предотвращает ситуацию, когда исследованная учебная зона автоматически
раскрывает боевую миссию на той же физической карте.

## 6. РУЧНАЯ НАСТРОЙКА в Unreal Editor

### Можно сделать после текущей C++ сборки

1. Закрыть UE, выполнить `.\Build-XRU1.ps1`, открыть проект.
2. Открыть `WBP_TacticalHUD`, найти binding/update enemy counter и заменить
   вызов `GetAliveEnemyCount` на `GetVisibleEnemyCount`.
3. `Compile` → `Save`, затем в PIE проверить: враг за непрозрачной стеной не
   увеличивает player-facing counter; после выхода в LOS увеличивает.

Это не включает визуальный fog: остальные пункты выполнять после появления
классов соответствующего этапа.

### После F1–F2

1. В `Project Settings → Collision` создать Trace Channel `FogVision` с
   Default Response `Ignore`.
2. На непрозрачной архитектуре, больших укрытиях, закрытых дверях и земле
   поставить `Block` для `FogVision`; на pawns, VFX, мелком декоре и прозрачных
   объектах — `Ignore`. Стекло решить явно по дизайну и применить одинаково.
3. Создать `/Game/XRU1Game/Data/DA_Fog_Showreel` класса
   `UFogOfWarConfigDataAsset`. Начальные значения: cell size 100 см, bounds
   только вокруг тактической зоны, floor bands по фактическим высотам.
4. Поставить один `BP_FogOfWarBoundsVolume` на `Showreel_Scene`, выровнять его
   локальные X/Y с материалом и назначить `DA_Fog_Showreel`.
5. Добавить `FogVisionComponent` четырём player units. Не добавлять его врагам.
6. Добавить `FogRevealableComponent` всем врагам и скрываемым objectives;
   проверить, что в список presentation входят skeletal/static meshes оружия,
   overhead widget, decals и Niagara components.
7. В Tutorial и Mission01 scenario profiles задать разные initial reveal и
   убедиться, что оба ссылаются на один bounds/config, но имеют разные
   `ScenarioId`.

### После F3

1. Создать `/Game/XRU1Game/FX/Fog/RT_Fog_Showreel`, формат `R8G8B8A8`, sRGB off,
   Address X/Y = Clamp. Начать с 256×256; увеличить до 512 только если профиль
   показывает заметные ступени при принятом world bounds.
2. Создать `/Game/XRU1Game/FX/Fog/M_PP_FogOfWar` с
   `Material Domain = Post Process`. Использовать
   `PostProcessInput0`, scene depth/world position и UV из origin/extents.
3. Создать `/Game/XRU1Game/FX/Fog/MI_PP_FogOfWar`; RT назначается runtime dynamic instance, не
   `Material Parameter Collection` (MPC не хранит texture parameters).
4. В `BP_TacticalCameraPawn → PostProcess → Blendables` добавить fog material.
   Проверить совместимость с outline: скрытый враг не должен проявляться через
   custom depth ни при каком порядке blendables.
5. Настроить цвета: `Unknown` почти чёрный, `Explored` читаемый, но приглушённый,
   `Visible` без tint. Не закрывать world-space подсказки видимых юнитов.
6. После реализации F4 debug overlay/cvar запустить PIE, включить фактические
   команды FogGrid/FogBounds и откалибровать volume/UV на четырёх углах и у
   стен; до появления cvar этот шаг не имитировать Blueprint debug-логикой.

## 7. Acceptance matrix

| Сценарий | Ожидаемый результат |
|---|---|
| Враг в радиусе, но за глухой стеной | actor/weapon/overhead/outline скрыты; hover, target panel и атака недоступны; counter не меняется |
| Враг выходит из-за угла | один reveal event; actor и UI появляются; камера может показать его ход |
| Враг снова теряет LOS | actor полностью скрыт; местность остаётся `Explored`; живого outline нет |
| Враг действует вне зрения | его AI-ход выполняется, но камера не панорамирует и floating text не выдаёт координаты |
| Два бойца видят разные зоны | `Visible` равно объединению обоих источников |
| Один источник умер/Downed/эвакуирован | только его вклад удалён после одного пересчёта |
| Preview движения рядом со скрытым врагом | нет счётчиков угрозы, фланга, occupancy marker или подсказки позиции |
| Враг становится видим в процессе движения | reveal происходит во время движения с ограниченной задержкой, не только в конце хода |
| Закрытая/открытая дверь | блок/reveal меняется после события двери без перезапуска карты |
| Разные этажи по одинаковым X/Y | активный Z-band не раскрывает другой этаж |
| Low scalability / fog material off | механические visibility gates продолжают работать |
| Tutorial → Mission01 на `Showreel_Scene` | новая fog-сессия; tutorial `Explored` и cache не переносятся |
| Retry текущего сценария | нет видимых один кадр врагов и состояния от прошлого run |
| Save/load, если включён | восстанавливается только состояние того же scenario по принятой политике |

## 8. Риски и защитные правила

| Риск | Защита |
|---|---|
| Материал и gameplay расходятся на краю стены | точный actor LOS авторитетен; RT никогда не разрешает действие |
| Дублирование LOS в HUD/controller/ability | все player-facing проверки через `UFogOfWarSubsystem` |
| `SetActorHiddenInGame` оставляет/ломает вложенные эффекты | единый `UFogRevealableComponent` кэширует и восстанавливает presentation |
| Скрытая collision выдаёт врага курсором/маршрутом | input gates + preview sanitization; симуляционную collision не отключать вслепую |
| Большая showcase-карта создаёт дорогую сетку | bounds только игровой зоны, 50–100 см cells, event batching |
| Несколько этажей раскрывают друг друга | CPU floor bands и active layer; отдельные RT на каждого бойца запрещены |
| RT обновляется каждый кадр | dirty/event update; один upload/draw на пакет изменений |
| Состояние течёт между режимами одной карты | обязательные `ScenarioId`, `RunId` и `ResetForScenario` до fade-in |

## 9. Первичные источники и технические опоры

- [XCOM 2 — официальный manual Feral Interactive](https://www.feralinteractive.com/en/manuals/xcom2/latest/steam/): concealment, обнаружение и базовая терминология интерфейса.
- [Firaxis: XCOM Enemy Unknown postmortem](https://www.gamedeveloper.com/design/classic-postmortem-i-xcom-enemy-unknown-i-which-turns-5-today): дизайнерские принципы читаемой пошаговой информации и упрощений.
- [Epic — Post Process Materials](https://dev.epicgames.com/documentation/unreal-engine/post-process-materials-in-unreal-engine?lang=en-US): material domain, blendable locations и scene textures.
- [Epic — Rendering Components](https://dev.epicgames.com/documentation/en-us/unreal-engine/rendering-components-in-unreal-engine): `SceneCapture`/rendering component trade-offs.
- [Epic — `UCanvasRenderTarget2D::UpdateResource`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UCanvasRenderTarget2D/UpdateResource): явное обновление render target.
- [Epic — `DrawMaterialToRenderTarget`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UKismetRenderingLibrary/DrawMaterialToRenderTarget): стоимость смены render target и причина пакетировать обновление.
- [Epic — Custom Primitive Data](https://dev.epicgames.com/documentation/en-us/unreal-engine/storing-custom-data-in-unreal-engine-materials-per-primitive): вариант передачи per-primitive presentation state без копий материалов.
- [Epic — Collision Settings](https://dev.epicgames.com/documentation/unreal-engine/collision-settings-in-the-unreal-engine-project-settings): настройка отдельного `FogVision` trace channel.
- [Epic — Traces](https://dev.epicgames.com/documentation/en-us/unreal-engine/traces-tutorials-in-unreal-engine?lang=en-US): CPU LOS/trace layer.
- [Epic — SaveGame](https://dev.epicgames.com/documentation/en-us/unreal-engine/saving-and-loading-your-game-in-unreal-engine): сохранение scenario-scoped exploration.
- [Epic Community — Fog of War tutorial](https://forums.unrealengine.com/t/tutorial-fog-of-war/18222): практический исторический reference; использовать только как опыт сообщества, не как архитектурный источник истины.
