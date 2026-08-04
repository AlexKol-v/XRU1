# Рабочий процесс: сборка, редактор, конвенции

Как работать с проектом: где что лежит, как собирать, что настраивать в редакторе,
по каким правилам писать код и когда задача считается сделанной.

Устройство систем — [03_ARCHITECTURE.md](03_ARCHITECTURE.md). Открытая работа —
[04_BACKLOG.md](04_BACKLOG.md). Тулинг агента — [agents/AGENT_UNREAL_TOOLING.md](agents/AGENT_UNREAL_TOOLING.md).

---

## 1. Сборка

Редактор должен быть **закрыт**:

```powershell
.\Build-XRU1.ps1
```

Принудительно закрыть редактор и собрать:

```powershell
.\Build-XRU1.ps1 -StopEditor
```

Обёртка сама находит UE 5.7 через реестр
(`HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.7` → `InstalledDirectory`) и проект — от
текущего корня. **Пути не хардкодить**: разработка идёт на двух машинах с разными
путями к проекту, движку и донору (таблица — в `CLAUDE.md`).

Фолбэк, если обёртка не сработала:

```
& "<ENGINE>/Engine/Build/BatchFiles/Build.bat" XRU1Editor Win64 Development -project="<PROJECT>/XRU1.uproject" -waitmutex
```

При `Unable to build while Live Coding is active` закрыть редактор или собрать через
Live Coding (`Ctrl+Alt+F11` внутри редактора).

**Быстрый старт на второй машине:**

```powershell
git pull
git lfs pull
.\Build-XRU1.ps1
```

## 2. Пути ассетов

| Назначение | Путь |
|---|---|
| Core BP / GameMode / Input | `/Game/XRU1Game/Core`, `/Game/XRU1Game/Input` |
| Юниты и оружие | `/Game/XRU1Game/Units` |
| Анимации | `/Game/XRU1Game/Units/Anim` |
| Abilities | `/Game/XRU1Game/Tactics/Abilities` |
| UI (виджеты, арт, иконки) | `/Game/XRU1Game/UI` |
| **Все Data Assets** | `/Game/XRU1Game/Data/<Core\|Units\|AI\|Missions\|Intro>` |
| StateTree-графы квестов | `/Game/XRU1Game/Quests` |
| Проектные карты | `/Game/XRU1Game/Maps` |
| Общая карта Tutorial/Mission01 | `/Game/XRU1Game/Maps/Main_Map_Showreel` |
| Scenario sublevels | `/Game/XRU1Game/Maps/SubLavel/SL_Showreel_*` |

Весь новый контент — только в `Content/XRU1Game/…`. Сторонние паки — в
`Content/ThirdParty/<PackName>/`, внутри пака ничего не переименовывать (проще
обновлять). Перенесённые из донора папки (`Characters/`,
`MWLandscapeAutoMaterial/`, `TopDown/`) не трогаем.

Перенос ассетов между папками — **только Move/Rename в редакторе** с последующим
Fix Up Redirectors и сохранением ссылающихся карт (порядок — §3.2).

## 3. Data Assets

Все дизайнерские Data Assets лежат в `/Game/XRU1Game/Data` и нигде больше.
**Правило размещения — по владельцу ссылки**: вопрос «кто держит указатель на этот
ассет?» имеет ровно один ответ и однозначно даёт подпапку. Глубже не вкладывать:
вложенность по системе (`Units/Audio/…`) ломает однозначность.

| Подпапка | Кто держит ссылку |
|---|---|
| `Data/Core/` | `BP_TacticsGameInstance` |
| `Data/Units/` | `BP_Unit_*` |
| `Data/AI/` | `BP_AIController_*`, `BP_TacticsGameInstance` (профили сложности) |
| `Data/Missions/` | карта (POI, Director) или другой Data Asset |
| `Data/Intro/` | тема UI |

**Именование:** `DA_<Домен>_<Сущность>[_<Вариант>]`, домен — система-потребитель
(`DA_UnitAudio_Sniper`, `DA_AI_Marauder_Default`, `DA_Scenario_Tutorial`).

### 3.1 Как завести новый Data Asset

1. **Класс** — наследник `UDataAsset`. `UPrimaryDataAsset` брать, только если ассет
   обязан находиться через AssetManager (как `UQuestDefinition`); тогда его дерево
   обязано быть в `PrimaryAssetTypesToScan` (`Config/DefaultGame.ini`), иначе поиск
   молча вернёт пусто.
2. **Поля** — `EditDefaultsOnly`/`EditAnywhere` + `BlueprintReadOnly`, категории и
   комментарии на русском; комментарий объясняет «почему такое значение», а не «что
   это за поле».
3. **Дефолты класса = текущее поведение кода.** Тогда незаполненный ассет ничего не
   ломает, а перенос числа из кода в ассет не меняет игру ни на йоту.
4. **Резолвер вместо обязательной ссылки.** Для глобальных ассетов — статический
   `Get(WorldContext)`: назначенный в `UTacticsGameInstance` → иначе CDO (образцы:
   `UTacticsCombatStatics::GetCoverTuning`, `UTutorialStyleData::Get`). Отсутствие
   ассета не должно выключать систему.
5. **Одна ссылка на ассет.** Дублировать назначение в двух местах нельзя, кроме
   осознанного Designer-preview (как `WBP_TacticalHUD.Style`) — тогда в C++
   обязателен комментарий, что рантайм-источник другой.
6. **Никаких жёстких путей `/Game/…` в C++.** `LoadObject` ломается молча при первом
   же переносе контента. Искать по классу через AssetRegistry (образец —
   `FindProjectTheme` в `XRU1WidgetAuthoringLibrary.cpp`) либо брать ссылку у
   владельца.
7. **Данные лежат у своей системы.** Мировые декали не место в UI-теме, боевые числа —
   в визуальной; если у поля другой читатель, чем у остального ассета, это заявка на
   отдельный Data Asset.
8. **Не заводить поле без потребителя.** Настройка, которую никто не читает, хуже её
   отсутствия: дизайнер крутит значение и не понимает, почему тихо. `IsDataValid`
   пишется только для полей, которые реально применяются.

### 3.2 Перенос и переименование

Только средствами редактора: Move/Rename (создаёт редиректор) → **Fix Up
Redirectors** → **открыть и сохранить карты, которые на ассет ссылаются** (иначе
редиректор останется жив: `rename_asset` пересохраняет только загруженные пакеты) →
убедиться, что редиректоров не осталось → `git add -A` (в LFS переименование =
удаление + добавление). После переноса проверить: `ini` AssetManager, жёсткие пути в
C++, доки с путями.

## 4. Настройка тактической карты

Для новой карты проверить:

1. `World Settings → GameMode Override = GM_Tactics`.
2. Один `PlayerStart` внутри игровой области и недалеко от стартового отряда. Нет
   `PlayerStart` — camera pawn стартует в `(0,0,0)` и заметно летит к отряду.
3. `NavMeshBoundsVolume` покрывает все секции и перепады высоты; в режиме `P` зелёная
   область не прерывается на целевых проходах.
4. ⚠️ **`NavMeshBoundsVolume` и `NavModifierVolume` обязаны лежать в persistent**
   вместе с `RecastNavMesh`. Сейчас это нарушено — см.
   [04_BACKLOG.md](04_BACKLOG.md) §1.1. Последствия: область навигации зависит от
   того, какой sublevel сейчас видим, а при двух видимых sublevel Null-объёмы миссии
   режут навигацию обучения («дырки» на ровной земле).
5. В persistent остаются общий арт, навмеш, свет, camera bounds и один
   `BP_TacticalScenarioDirector`; gameplay-акторы — в scenario sublevel.
6. `SL_Showreel_Tutorial` и `SL_Showreel_Mission01` не загружены одновременно.
7. Четыре player BP и нужные enemy BP стоят капсулами на навмеше.
8. У врагов заполнены `PodId` (группа) и `PatrolPoints` (маршрут) — оба поля
   `EditInstanceOnly`, то есть ставятся на экземпляре в World Outliner, а не в
   BP-классе. Пустой `PodId` = боец сам себе группа (групповая активация не
   работает), пустой `PatrolPoints` = пост: боец встаёт в наблюдение.
9. Укрытия имеют collision для shot geometry; ориентиры дизайна — около 60 см для
   Half и 150 см для Full, высота считается от пола.
10. Mission actors (`ABombObjective`, `AEvacZone`) — только в mission sublevel;
    tutorial zones и staged actors — только в tutorial sublevel.
11. Группы врагов, зависящие от сложности, и подкрепления ставятся акторами
    `ATacticalEncounter` / `ATacticalReinforcementBeacon` (поля и рецепт —
    [03_ARCHITECTURE.md](03_ARCHITECTURE.md) §10). Расстановка проверяется командой
    `xru1.Mission.Validate`.

`BP_TacticalCameraPawn` вручную не размещать: его создаёт `GM_Tactics` как Default
Pawn в точке `PlayerStart`.

**Про «камера сама едет».** Кроме отсутствующего `PlayerStart` есть второй штатный
источник: edge scroll — пока курсор в 16 пикселях от края окна,
`UpdateEdgeScroll` постоянно подаёт pan. Выключается в настройках (раздел «КАМЕРА»),
это пользовательская настройка, а не свойство BP. WASD намеренно двигает камеру
относительно её yaw, а не по мировым X/Y — после Q/E направления поворачиваются
вместе с экраном. Тюнинг чисел камеры — в `BP_TacticalCameraPawn`, категории
`Tactics|Camera` и `Tactics|Camera|Shot`.

## 5. Юниты, AnimBP и оружие

У каждого `BP_Unit_*` должны быть:

- `Mesh → Anim Class = ABP_Solider`;
- weapon child actor нужного класса;
- `BP_GA_Attack` и `BP_GA_Overwatch`;
- `AM_Fire_Open`, `AM_Fire_OverCover`, `AM_HitReact`, `AM_Death`, `AM_Overwatch_Enter`;
- корректный HUD layout (`DA_UnitHUD_Squad` или `DA_UnitHUD_Enemy`).

| Класс | Weapon BP |
|---|---|
| Assault | `Weapons/AssaultRifle/BP_AssaultRifle_Default` |
| Sniper | `Weapons/Sniper/BP_Sniper_Default` |
| Medic | `Weapons/SMG/BP_SMG_Default` |
| Tank | `Weapons/LMG/BP_LMG_Default` |
| Marauder | `Weapons/AssaultRifle/BP_AssaultRifle_Default` |

В каждом fire montage ровно один `FireCommit` Branching Point на кадре выстрела.
Montage запускает BP presentation hook; урон напрямую из BP не вызывать. Death — один
montage, без параллельной state-sequence.

**Контракт будущего IK второй руки** (задача P2): на каждом weapon BP создать
socket/SceneComponent `LeftHandIK`; в AnimBP взять его transform в component/bone
space; применить `Two Bone IK` или Control Rig после базовой позы и до финального
output. **Не хранить координаты руки в `ABP_Solider`** — источник истины конкретное
оружие, иначе четыре модели потребуют ручных исключений. Это косметический слой: он
не меняет `FUnitVisualState`, cover state и fire action.

## 6. Нейминг ассетов

| Префикс | Тип | Пример |
|---|---|---|
| `BP_` | Blueprint-класс (акторы, компоненты; GameMode — `GM_`) | `BP_Unit_Assault`, `GM_Tactics` |
| `WBP_` | Widget Blueprint | `WBP_TacticalHUD` |
| `CBS_` / `CTS_` | CommonButtonStyle / CommonTextStyle | `CBS_Menu_Primary` |
| `ABP_` | Animation Blueprint | `ABP_Solider` (legacy-имя) |
| `AM_` | Anim Montage | `AM_Fire_Open` |
| `BS_` | Blend Space | `BS_Idle_Walk_Run` |
| `BP_GA_` / `GE_` | BP-наследник ability / Gameplay Effect | `BP_GA_Attack`; C++ — `UGA_*` |
| `DA_` | Data Asset | `DA_Quest_Tutorial` |
| `IMC_` / `IA_` | Input Mapping Context / Input Action | `IMC_Tactical`, `IA_EndTurn` |
| `L_` / `SL_` | уровень / streaming sublevel | `L_Hub`, `SL_Showreel_Mission01` |
| `M_` / `MI_` / `MF_` | материал / инстанс / функция | `MI_Hologram` |
| `T_` / `SM_` / `SKM_` / `SK_` | текстура / стат-меш / скелет-меш / скелет | — |
| `S_` / `SC_` | Sound Wave / Sound Class | `S_Shot_Rifle`, `SC_Voice` |
| `NS_` | Niagara System | `NS_MuzzleFlash` |

Язык имён ассетов — английский. Тексты игрока — русские, в виджетах и Data Asset'ах.

## 7. Стиль C++

- Классы: префиксы UE (`A`/`U`/`F`/`E`), проектный API-макрос `XRU1_API`.
- Дизайнерские параметры — `UPROPERTY(EditDefaultsOnly|EditAnywhere, Category="Tactics|…")`;
  API для BP — `UFUNCTION(BlueprintCallable|BlueprintPure)`; события для визуала —
  `BlueprintImplementableEvent` (код не знает про анимации и VFX).
- **Комментарии на русском**, объясняют «почему», а не «что».
- Боевые способности — наследники `UTacticalAbility`; урон — только через
  `UTacticsCombatStatics::ResolveShot`; теги — из `TacticsGameplayTags` (native),
  новые добавлять туда же.
- Quest ID и `Quest.Event.*`, используемые в C++, — native gameplay tags.
  Content-only `Quest.Objective.*` хранятся в `DefaultGameplayTags.ini`.
- ⚠️ **GE-компоненты в конструкторе CDO — только `CreateDefaultSubobject` +
  `GEComponents.Add`** (НЕ `FindOrAddComponent` — фатал на старте редактора).
- ⚠️ У `EditInlineNew`/`Instanced`-элемента категории его редактируемых полей **не
  должны совпадать или быть родителем** категории содержащего свойства. Например,
  массив в `Tactics|AI|Actions`, а поля элемента — отдельная плоская категория
  `Evaluator`; иначе `PropertyEditor` уходит в рекурсивный Details layout и падает с
  `EXCEPTION_STACK_OVERFLOW`.
- Никакой репликации и сетевого кода — проект одиночный.

## 8. Стиль Blueprints

- BP — только обвязка: визуал, звук, разводка событий, настройка параметров. Игровая
  логика (правила, расчёты) — в C++.
- Граф чистить: функции вместо простыней, комментарии-блоки на русском, `Sequence`
  вместо цепочек exec через весь граф.
- Каст к C++ классам — по интерфейсным геттерам; BP→BP касты запрещены, кроме
  виджет-детей.

## 9. Git и LFS

- LFS настроен (`.gitattributes`): `.uasset`/`.umap` и медиа — в LFS. После
  добавления паков проверять `git lfs status` — бинарники должны быть «LFS: …», не
  «Git: …».
- Если ассеты вдруг стали текстовыми файлами-указателями (редактор говорит «файл
  ресурса») — `git lfs checkout` (объекты уже в `.git/lfs/objects`).
- Коммиты — по завершении логического куска; сообщение: краткая строка на русском +
  что изменилось. **Пуш крупных бинарников запускает пользователь вручную** — он
  может быть очень долгим, агентская сессия его не ждёт.
- Не коммитить: `Saved/`, `Intermediate/`, `DerivedDataCache/`, `Binaries/`.
- Теги: `v0.x` по этапам, `v1.0-demo` — сдача.

## 10. Работа через UnrealClaude MCP

Редактор должен быть открыт; статус — `localhost:3000/mcp/status`. Полный справочник,
включая скрытые инструменты и проверенные границы, —
[agents/AGENT_UNREAL_TOOLING.md](agents/AGENT_UNREAL_TOOLING.md).

Безопасный порядок:

1. найти ассет через asset search;
2. прочитать BP / Anim / состояние актора;
3. внести одну логическую правку;
4. Compile / Save;
5. **повторно прочитать** изменённый граф или актор;
6. проверить в PIE.

⚠️ Для Montage Notify/Slot/Section tracks мост возвращает неполную картину — их
проверять в Persona вручную. Для сохранения уровня использовать level
`save_as`/Save Current Level, а не универсальный `asset.save_asset`: последний может
создать ошибочный `.uasset` рядом с `.umap`.

## 11. Правила работы агентов

1. **Источник истины — код, ассеты и git.** Доки описывают, как задумано; при
   расхождении прав код. Отклонился от GDD — сначала поправь GDD.
2. **Донор `cst-3d-gubkin-2026-04` — только чтение.** Это чужой git-репозиторий;
   при конфликте имён префиксовать своё, донор не патчить.
3. Сборка — `.\Build-XRU1.ps1` при закрытом редакторе. Обычную сборку агент
   запускает сам (в фоне); долгие операции (>5 мин: `git lfs push`, `npm install`,
   скачивание паков) отдаёт пользователю готовой командой.
4. **Геймплейные прогоны — на пользователе.** Агент собирает, поднимает редактор и
   выдаёт список «что включить и что прислать», а не гоняет PIE сам.
5. После правок C++ — переиндексировать codebase-memory (`index_repository`).
6. Бинарные ассеты `.uasset` менять только через Unreal Editor или UnrealClaude MCP,
   не файловыми утилитами.
7. Цикл «редактор ↔ сборка»: перед закрытием редактора **сохранить всё**
   (`unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)`), собрать с
   `-StopEditor`, после успешной сборки **сразу поднять редактор обратно**.
8. Русский язык в ответах и комментариях, идентификаторы — английские.

## 12. Определение «сделано»

Задача закрыта, когда:

1. собирается (`Result: Succeeded`);
2. проверено в PIE (или отдано пользователю на прогон с явным списком проверок);
3. пункт в [04_BACKLOG.md](04_BACKLOG.md) закрыт либо переведён в «Приёмку» — если
   код готов, но прогона не было, это **не** «сделано»;
4. доки синхронизированы (архитектура — при изменении устройства, GDD — при
   изменении правил);
5. изменения закоммичены.

## 13. Частые ошибки

| Симптом | Причина |
|---|---|
| Pawn появляется у origin и летит к отряду | нет `PlayerStart` |
| Создаются не тактические Controller/Pawn | неверный GameMode Override |
| AI и игрок не получают маршрут | навмеш не покрывает участок |
| Planner корректирует или отклоняет цель | юнит стоит внутри чужой капсулы |
| Анимации накладываются | два источника death/fire: montage и state graph |
| Рассинхрон анимации и механики | BP наносит урон до `FireCommit` |
| Новый C++ default не применяется | старый override в BP перекрывает его — Reset to Default и перенастроить |
| Missing dependencies оружейных аттачей | вернуть отсутствующий optional-ассет или очистить ссылку |
| `MM_Sky` не компилируется для SM6 | исправить материал или заменить sky |
