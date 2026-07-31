# Бриф: голографическая карта хаба

Самодостаточное задание для агента. Читать вместе с
[../03_CODE_OVERVIEW.md](../03_CODE_OVERVIEW.md) и
[../09_UI_HUD.md](../09_UI_HUD.md). Проект — UE 5.7, модуль `XRU1`,
сборка `.\Build-XRU1.ps1 -StopEditor` при закрытом редакторе.

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
| `DA_Scenario_Tutorial` / `DA_Scenario_Mission01` | `/Game/XRU1Game/Data/` | Назначаются в поле `Scenario` каждой POI |
| `UTacticsSaveGame::CompletedMissions` | [TacticsSaveGame.h](../../Source/XRU1/Tactics/TacticsSaveGame.h) | Источник блокировки Mission01 |
| `UTacticsAudioSubsystem` | [TacticsAudioSubsystem.h](../../Source/XRU1/Audio/TacticsAudioSubsystem.h) | `PlayUIHover()`, `PlayUIClick()`, `PlayUIDenied()` |
| `UPrimaryGameLayout` + `UGameUIManagerSubsystem` | `Source/XRU1/UI/` | Слои HUD/Menu для интерфейса хаба |

**Голографический материал брать из донора** (только чтение, донор не править):
`D:/UE5/UnrealProjects/cst-3d-gubkin-2026-04/Content/HologramShader/`
— там `Materials/HologramShader/M_Hologram`, набор `MI_Hologram_*` и пример
`BP_Hologram_Map/HologramMap`. Скопировать материал и нужные текстуры в
`/Game/XRU1Game/Hub/Materials/` через Content Browser (Migrate или ручное
копирование `.uasset` с последующим Fix Up Redirectors).

Меш рельефа на момент написания брифа отсутствует. Работать на плейсхолдере
(`SM_Cube` со скейлом или любой ландшафтный меш из `US_Military`), меш
подставляется в одно поле `TerrainMesh` без правок кода.

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
- `TerrainMesh` → плейсхолдер-меш, материал `MI_Hologram_Terrain`.
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

| Маркер | `MissionId` | `Scenario` | `RequiredCompletedMission` | `Title` |
|---|---|---|---|---|
| POI обучения | `Tutorial` | `DA_Scenario_Tutorial` | `None` | Полигон «Купол» |
| POI миссии | `Mission01` | `DA_Scenario_Mission01` | `Tutorial` | Станция «Узел-7» |

- `LevelToLoad` у обоих оставить **пустым**: маршрут идёт через `Scenario`.
- Прописать `HubLevel = L_Hub` в `BP_TacticsGameInstance`.

## 5. Критерии готовности

- [ ] Проект собирается; в `L_Hub` нет ошибок в Output Log.
- [ ] Карта вращается ЛКМ-drag, наклон ограничен, автовращение возобновляется
      через паузу после ввода.
- [ ] Оба маркера вращаются вместе с рельефом и не «съезжают» с него.
- [ ] Наведение меняет вид маркера и играет звук; клик по доступной POI
      открывает боевую карту с нужным сценарием.
- [ ] Маркер Mission01 серый и по клику даёт отказ, пока `CompletedMissions`
      не содержит `Tutorial`; после победы в обучении становится доступным.
- [ ] HUD показывает название и описание выбранной точки; кнопка «Начать»
      недоступна для заблокированной POI.
- [ ] Возврат из миссии в хаб не создаёт второй HUD и не ломает вращение.

## 6. Чего НЕ делать

- Не писать запуск миссии заново — он уже в `AMissionPointOfInterest`.
- Не класть логику в Level Blueprint.
- Не править проект-донор.
- Не трогать тактические классы (`ATacticalPlayerController`, `GM_Tactics`):
  хаб — отдельный режим со своим GameMode.
