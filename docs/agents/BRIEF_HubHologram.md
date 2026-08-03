# Бриф: голографическая карта хаба

Самодостаточное задание для агента. Читать вместе с
[../03_CODE_OVERVIEW.md](../03_CODE_OVERVIEW.md) и
[../09_UI_HUD.md](../09_UI_HUD.md). Проект — UE 5.7, модуль `XRU1`,
сборка `.\Build-XRU1.ps1 -StopEditor` при закрытом редакторе.

> **Статус на 2026-08-02: реализовано, с двумя отступлениями.**
> Всё в `Source/XRU1/Hub/`: `AHologramMapActor`, `AHubCameraPawn`,
> `AHubPlayerController`, `AHubPOIMarker`, `AHubGameMode`; HUD —
> `UHubHUDWidget` + `WBP_HubHUD`. Уровень `L_Hub` собран, `HubLevel` прописан.
>
> 1. **Наклон отдан камере, а не карте.** Карта вращается только по yaw
>    (`AddYawInput`), pitch с клампом живёт в `AHubCameraPawn`. Причина:
>    вертикальный drag должен менять ракурс, а не «ронять» карту набок.
> 2. **Вращение на ПКМ, а не на ЛКМ.** ЛКМ — только выбор маркера, поэтому
>    порог click/drag не нужен и клик никогда не проворачивает карту.
> 3. Логика маркера — в C++ (`AHubPOIMarker`, три состояния через DMI), а не
>    в BP-графе: MCP-мост не умеет писать BP-графы.
> 4. Меш голограммы подставлен (было: плейсхолдер-куб 2400×2400×20 с
>    `M_HubMapPlaceholder`). Замена сделана только полем `TerrainMesh` актора
>    `HologramMap`, код не тронут.
>
> **2026-08-02: голограмма на месте.** `TerrainMesh` = `SM_HoloTerrain` +
> `MI_Hologram_Terrain`, `RelativeScale3D` = 1.4 (итог 1774×2401×121 uu — по
> габариту прежнего плейсхолдера), `RelativeLocation` = (2.1, 4.3, −25.1):
> центр габаритов меша сведён к `RotationRoot`, иначе карта вращалась бы вокруг
> точки вне себя и «гуляла» по экрану.
>
> ⚠️ **Рельеф — не прямоугольник, а форма материка** (донорская карта Колумбии).
> Прежние координаты маркеров частью висели в пустоте: `POI_Mission01`
> (560, 420) не попадал на меш вовсе. Позиции подобраны трассировкой по сетке
> высот: `Tutorial` → (−350, −250, 27.9) на горном массиве, `Mission01` →
> (450, 250, −0.5) на восточной равнине. Привязка к `RotationRoot` сохранена.
>
> Трассировать рельеф можно только временно включив ему коллизию (в норме у
> `TerrainMesh` она `NoCollision`, чтобы клики доставались маркерам) и **исключив
> из трассы сами маркеры** — иначе луч упирается в их `HoverBounds`.

## 1. Что должно получиться

Уровень `L_Hub`: полная темнота, в центре зала висит голографическая проекция
местности (горный рельеф и степь). Игрок вращает проекцию мышью, наводит курсор
на оранжевые полупрозрачные сферы-маркеры и запускает миссию.

Точек интереса ровно две: **Полигон «Купол»** (обучение) и **Станция «Узел-7»**
(Mission01). Вторая заблокирована, пока не пройдено обучение.

## 2. Что уже есть и переиспользуется (НЕ писать заново)

| Готовое | Где | Роль в хабе |
|---|---|---|
| `AMissionPointOfInterest` | [MissionPointOfInterest.h](../../Source/XRU1/Tactics/MissionPointOfInterest.h) | Сама точка интереса: `Scenario`, `RequiredCompletedMission`, `Title`, `Description`, `IsLocked()`, `SelectPointOfInterest()`, попап при наведении |
| `UTacticsGameInstance::StartCombatScenario` | [TacticsGameInstance.h](../../Source/XRU1/Tactics/TacticsGameInstance.h) | Запуск сценария; POI уже её вызывает |
| `DA_Scenario_Tutorial` / `DA_Scenario_Mission01` | `/Game/XRU1Game/Data/Missions/` | Назначаются в поле `Scenario` каждой POI |
| `UTacticsSaveGame::CompletedMissions` | [TacticsSaveGame.h](../../Source/XRU1/Tactics/TacticsSaveGame.h) | Источник блокировки Mission01 |
| `UTacticsAudioSubsystem` | [TacticsAudioSubsystem.h](../../Source/XRU1/Audio/TacticsAudioSubsystem.h) | `PlayUIHover()`, `PlayUIClick()`, `PlayUIDenied()` |
| `UPrimaryGameLayout` + `UGameUIManagerSubsystem` | `Source/XRU1/UI/` | Слои HUD/Menu для интерфейса хаба |

### 2.1 Ассеты голограммы (перенесены 2026-08-02)

Источник — донор (только чтение), `Content/HologramShader/`. Перенесено
переносом пакетов по исходным путям + `rename_asset` в целевые папки, поэтому
все внутренние ссылки переписаны; проверено `asset_dependencies` — ни одной
ссылки за пределы `/Game/XRU1Game`.

| Ассет в проекте | Откуда у донора | Что это |
|---|---|---|
| `Hub/Meshes/SM_HoloTerrain` | `Map/free_colombia_3d_map_4k_texture/StaticMeshes/…` | меш рельефа (горы + равнина), габарит ≈ 1270×1715×86 uu, Nanite выключен |
| `Hub/Materials/M_Hologram` | `Materials/HologramShader/M_Hologram` | базовый шейдер голограммы (родитель для своих инстансов) |
| `Hub/Materials/M_HologramMap` | `Materials/HologramShader/M_Hologram1` | вариант шейдера с текстурной подложкой карты |
| `Hub/Materials/MI_Hologram_Terrain` | `MI/MI_Hologram_26` | **эталонный вид донора** (его ставил `BP_Hologram_Map`) |
| `Hub/Materials/MI_Hologram_Grid` | `MI/MI_Hologram_25` | чистая голосетка без подложки (дефолт на меше) |
| `Hub/Materials/Textures/` (11 шт.) | `Textures/**`, `Map/**`, `sA_PickupSet_1/**` | шум/паттерны/вода/подложка `T_HoloTerrain_Color` |

Не переносились: остальные 24 `MI_Hologram_*` (варианты для демо-объектов) и
неиспользуемые текстуры — они тянули ещё ~100 МБ балласта; при нужде берутся из
донора тем же способом. Пример-уровень `Maps/Hologram` и `BP_Hologram_Map` не
нужны: их роль выполняет `AHologramMapActor`.

⚠️ Материал голограммы — `Translucent`, поэтому Nanite у меша отключён. Если
подставлять другой меш — снимать Nanite и у него, иначе Output Log ругается
`Invalid material … used on Nanite static mesh`.

## 3. Новый C++ (написать)

Один актор. Больше ничего в C++ не нужно: логика запуска миссии уже есть в POI.

### `Source/XRU1/Hub/HologramMapActor.h/.cpp`

```
AHologramMapActor : AActor
  ├─ USceneComponent*      Root
  ├─ USceneComponent*      RotationRoot   // вращается; всё остальное — его дети
  ├─ UStaticMeshComponent* TerrainMesh    // меш рельефа + материал голограммы
  └─ UPointLightComponent* GlowLight      // мягкая подсветка проекции снизу
```

Публичный API:

| Член | Смысл |
|---|---|
| `TerrainMesh` (`VisibleAnywhere`) | сюда дизайнер кладёт меш рельефа и `MI_Hologram_*` |
| `float YawSpeed = 90.f` | град/сек при перетаскивании мышью |
| `float PitchSpeed = 45.f`, `MinPitch = -70.f`, `MaxPitch = -10.f` | наклон ограничен: карта не должна переворачиваться и уходить «под пол» |
| `float IdleSpinSpeed = 3.f` | медленное автовращение, пока игрок не трогает карту (жизнь в кадре) |
| `AddRotationInput(FVector2D Delta)` (`BlueprintCallable`) | вращение от контроллера; сбрасывает таймер автовращения |
| `TArray<AMissionPointOfInterest*> GetPointsOfInterest() const` | собирается в `BeginPlay` через `GetAttachedActors`, фильтр по классу |
| `ResetView()` | вернуть исходный ракурс |

Требования к реализации:

1. **POI прикрепляются к `RotationRoot`, а не к `Root`.** Они обязаны вращаться
   вместе с рельефом, иначе маркер уедет с горы на степь.
2. `IdleSpinSpeed` работает только когда с последнего ввода прошло
   `IdleSpinDelay` (по умолчанию 4 с). Автовращение, продолжающееся во время
   перетаскивания, читается как рассинхрон ввода.
3. Pitch клампится ВСЕГДА, включая автовращение. Yaw — свободный, нормализуется
   через `FRotator::NormalizeAxis`.
4. Ничего не тикать, когда актор не видим (`SetActorTickEnabled`).

### Правки существующего

- `XRU1.Build.cs`: добавить `"XRU1/Hub"` в `PublicIncludePaths`.
- Больше правок C++ не требуется.

## 4. Blueprint и ассеты (собрать в редакторе)

### 4.1 `BP_HologramMap` (от `AHologramMapActor`)
- `TerrainMesh` → `SM_HoloTerrain`, материал `MI_Hologram_Terrain` (§2.1).
  Меш меньше плейсхолдера — скейл подобрать по залу, POI-маркеры поднять на
  новую высоту рельефа.
- `GlowLight`: оранжевый (≈ `FLinearColor(1, 0.45, 0.1)`), интенсивность
  подобрать так, чтобы читался силуэт рельефа, но не засвечивался зал.

### 4.2 `BP_HubPOI_Marker` (от `AMissionPointOfInterest`)
- Добавить `UStaticMeshComponent` со сферой (`SM_Sphere`), скейл ≈ 0.15.
- Материал `MI_Hologram_POI`: **оранжевый, полупрозрачный, Unlit, Two Sided**,
  Blend Mode = Translucent. Пульсация — `Sine(Time)` в Emissive.
- Три визуальных состояния через `Dynamic Material Instance`:
  доступна (оранжевый, пульсирует) / под курсором (ярче + масштаб ×1.15) /
  заблокирована (серый, без пульсации). Состояние читать из `IsLocked()` и
  события `OnHoverChanged`.
- В `OnHoverChanged` → `PlayUIHover`; в `OnSelectionDenied` → `PlayUIDenied`.

### 4.3 `BP_HubPlayerController`
Отдельный контроллер хаба (НЕ `ATacticalPlayerController` — тактический ввод
здесь не нужен и будет мешать).
- `bShowMouseCursor = true`, `SetInputModeGameAndUI`.
- ЛКМ-drag → `AddRotationInput(Delta)` на `BP_HologramMap`.
- ЛКМ-клик без движения → трейс под курсором; если попали в POI —
  `SelectPointOfInterest()` + `PlayUIClick()`.
- Колесо → зум камеры (простой `SpringArm` на статичной `APawn` хаба).
- Порог различения click/drag — 5 px, иначе каждый клик будет слегка вращать карту.

### 4.4 `WBP_HubHUD`
- Слева: название выбранной точки (`Title`) и описание (`Description`).
- Кнопка **«Начать операцию»** — активна только когда POI выбрана и не
  заблокирована; по нажатию `SelectPointOfInterest()`.
- Строка статуса: «Обучение пройдено» / «Требуется пройти обучение».
- Кнопки «Настройки» (пуш `WBP_SettingsMenu`) и «В главное меню».
- Пушить через `UGameUIManagerSubsystem->GetRootLayout()->PushWidgetToLayer(EUILayer::Game, ...)`.

### 4.5 Уровень `L_Hub` (`/Game/XRU1Game/Maps/L_Hub`)
- `WorldSettings.DefaultGameMode` = новый `GM_Hub` (Pawn хаба + `BP_HubPlayerController` + `WBP_HubHUD`).
- Полная темнота: убрать/погасить DirectionalLight и SkyLight, `ExponentialHeightFog`
  с тёмным цветом. Единственные источники света — `GlowLight` голограммы и слабый
  fill-свет, чтобы зал не был абсолютно чёрным.
- В центре — `BP_HologramMap`. К его `RotationRoot` прикрепить две
  `BP_HubPOI_Marker`:

| Маркер | `MissionId` | `Scenario` | Блокировка | Название |
|---|---|---|---|---|
| POI обучения | `Tutorial` | `DA_Scenario_Tutorial` | нет | Полигон «Купол» (из сценария) |
| POI миссии | `Mission01` | `DA_Scenario_Mission01` | `DA_Scenario_Mission01.RequiredMissions` → Tutorial | Станция «Узел-7» (из сценария) |

⚠️ 2026-08-03: `Title`, `Description` и legacy-поле `RequiredCompletedMission`
на маркерах **очищены** — название, описание и требование берутся из сценария
(`DisplayName`/`BriefingText`/`RequiredMissions`), чтобы текст не жил в двух
местах.

- `LevelToLoad` у обоих оставить **пустым**: маршрут идёт через `Scenario`.
- Прописать `HubLevel = L_Hub` в `BP_TacticsGameInstance`.

## 5. Критерии готовности

- [x] Проект собирается; в `L_Hub` нет ошибок в Output Log (прогон 2026-08-02:
      в `LogXRU1*` ноль Warning/Error).
- [x] Карта вращается ПКМ-drag (§0.2), наклон ограничен, автовращение
      возобновляется через паузу после ввода.
- [x] Оба маркера вращаются вместе с рельефом и стоят НА нём.
- [ ] Наведение меняет вид маркера и играет звук (проверяется только мышью).
- [x] Клик по доступной POI ведёт в брифинг, оттуда — на боевую карту с нужным
      сценарием.
- [x] Маркер Mission01 серый и заблокирован, пока `CompletedMissions` не
      содержит `Tutorial` (`[Hub] Точка 'POI_Mission01': Недоступно…`).
      Открыто: разблокировка после реальной победы в обучении.
- [x] HUD показывает название и описание выбранной точки; кнопка «Начать»
      недоступна для заблокированной POI.
- [x] Возврат из миссии в хаб не создаёт второй HUD и не ломает вращение
      (лейаут пересоздаётся по смене мира — 09_UI_HUD §5.4).

## 6. Чего НЕ делать

- Не писать запуск миссии заново — он уже в `AMissionPointOfInterest`.
- Не класть логику в Level Blueprint.
- Не править проект-донор.
- Не трогать тактические классы (`ATacticalPlayerController`, `GM_Tactics`):
  хаб — отдельный режим со своим GameMode.
