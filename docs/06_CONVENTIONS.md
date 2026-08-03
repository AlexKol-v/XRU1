# Конвенции и правила проекта

## 1. Нейминг ассетов (UE-стандарт)

| Префикс | Тип | Пример |
|---|---|---|
| `BP_` | Blueprint-класс (акторы, компоненты, GameMode `GM_`) | `BP_Unit_Assault`, `GM_Tactics` |
| `WBP_` | Widget Blueprint | `WBP_MainMenu`, `WBP_TacticalHUD` |
| `CBS_` / `CTS_` | CommonButtonStyle / CommonTextStyle Blueprint | `CBS_Menu_Primary`, `CTS_Title` |
| `ABP_` | Animation Blueprint | `ABP_Solider` (фактическое legacy-имя) |
| `AM_` | Anim Montage | `AM_Fire` |
| `BS_` | Blend Space | `BS_Idle_Walk_Run` |
| `BP_GA_` / `GE_` | BP-наследники ability / Gameplay Effect | `BP_GA_Attack`, `GE_TauntShield`; C++-классы — `UGA_*` |
| `DA_` | Data Asset | `DA_Quest_Tutorial` |
| `IMC_` / `IA_` | Input Mapping Context / Input Action | `IMC_Tactical`, `IA_EndTurn` |
| `L_` | уровень (карта) | `L_Mission01` |
| `M_` / `MI_` / `MF_` | материал / инстанс / функция | `MI_Hologram` |
| `T_` / `SM_` / `SKM_` / `SK_` | текстура / стат-меш / скелет-меш / скелет | — |
| `S_` / `SC_` | Sound Wave / Sound Cue | `SC_Shot` |
| `NS_` | Niagara System | `NS_MuzzleFlash` |

Язык имён ассетов — английский. Тексты игрока — русские, в виджетах/DataAsset.

## 2. Структура Content/

Весь новый контент — ТОЛЬКО в
`Content/XRU1Game/<Core|UI|Units|Maps|Input|Data|Tactics|AI|Quests|Audio|FX>`
(см. 05_EDITOR_GUIDE §1). Сторонние паки — в `Content/ThirdParty/<PackName>/`,
внутри пака ничего не переименовывать (проще обновлять). Перенесённые из донора
папки (`Characters/`, `MWLandscapeAutoMaterial/`, `TopDown/`) не трогаем.

## 3. Data Assets — единое дерево и правила заведения

**Все дизайнерские Data Assets проекта лежат в `/Game/XRU1Game/Data`** и нигде
больше (сведено 2026-08-03; до этого они расползлись по `Audio/Mix`,
`Audio/Profiles` и `FX`).

| Подпапка | Что там | Кто держит ссылку |
|---|---|---|
| `Data/Core/` | `DA_TacticalHUDStyle`, `DA_Tutorial_Style`, `DA_CoverTuning`, `DA_TacticsAudio` | `BP_TacticsGameInstance` |
| `Data/Units/` | `DA_UnitAudio_*`, `DA_UnitVfx_*`, `DA_UnitHUD_*` | `BP_Unit_*` |
| `Data/AI/` | `DA_AI_*` | `BP_AIController_*` |
| `Data/Missions/` | `DA_Scenario_*`, `DA_Quest_*` | карта (POI, Director) или другой Data Asset |

**Правило размещения — по владельцу ссылки.** Вопрос «кто держит указатель на
этот ассет?» имеет ровно один ответ и однозначно даёт подпапку. Глубже не
вкладывать: вложенность по системе (`Units/Audio/…`) ломает однозначность.

**Именование:** `DA_<Домен>_<Сущность>[_<Вариант>]`, домен — система-потребитель
(`DA_UnitAudio_Sniper`, `DA_AI_Marauder_Default`, `DA_Scenario_Tutorial`).

### 3.1 Как агенту завести новый Data Asset

1. **Класс** — наследник `UDataAsset`. `UPrimaryDataAsset` брать, только если
   ассет обязан находиться через AssetManager (как `UQuestDefinition`); тогда
   его дерево обязано быть в `PrimaryAssetTypesToScan`
   (`Config/DefaultGame.ini`), иначе поиск молча вернёт пусто.
2. **Поля** — `EditDefaultsOnly`/`EditAnywhere` + `BlueprintReadOnly`, категории
   и комментарии на русском, комментарий объясняет «почему такое значение», а не
   «что это за поле».
3. **Дефолты класса = текущее поведение кода.** Тогда незаполненный ассет ничего
   не ломает, а перенос числа из кода в ассет не меняет игру ни на йоту.
4. **Резолвер вместо обязательной ссылки.** Для глобальных ассетов —
   статический `Get(WorldContext)`: назначенный в `UTacticsGameInstance` →
   иначе CDO (образцы: `UTacticsCombatStatics::GetCoverTuning`,
   `UTutorialStyleData::Get`). Отсутствие ассета не должно выключать систему.
5. **Одна ссылка на ассет.** Дублировать назначение в двух местах нельзя, кроме
   осознанного Designer-preview (как `WBP_TacticalHUD.Style`) — тогда в C++
   обязателен комментарий, что рантайм-источник другой.
6. **Никаких жёстких путей к ассетам в C++.** `LoadObject("/Game/…")` ломается
   молча при первом же переносе контента. Искать по классу через AssetRegistry
   (образец — `FindProjectTheme` в `XRU1WidgetAuthoringLibrary.cpp`) либо брать
   ссылку у владельца.
7. **Данные лежат у своей системы.** Мировые декали не место в UI-теме, боевые
   числа — в визуальной; если у поля другой читатель, чем у остального ассета,
   это заявка на отдельный Data Asset.
8. **Не заводить поле без потребителя.** Настройка, которую никто не читает,
   хуже её отсутствия: дизайнер крутит значение и не понимает, почему тихо.
   Валидация `IsDataValid` пишется только для полей, которые реально
   применяются, иначе она годами ругается на пустоту, ни на что не влияющую.

### 3.2 Перенос и переименование Data Asset

Только средствами редактора: Move/Rename (создаёт редиректор) → **Fix Up
Redirectors** → **открыть и сохранить карты, которые на ассет ссылаются**
(иначе редиректор останется жив: `rename_asset` пересохраняет только загруженные
пакеты) → убедиться, что редиректоров не осталось → `git add -A` (в LFS
переименование = удаление + добавление). После переноса проверить: `ini`
AssetManager, жёсткие пути в C++, доки с путями.

## 4. Стиль C++ (дублирует CLAUDE.md, здесь — источник)

- Классы: префиксы UE (`A`/`U`/`F`/`E`), проектный API-макрос `XRU1_API`.
- Дизайнерские параметры — `UPROPERTY(EditDefaultsOnly|EditAnywhere, Category="Tactics|...")`;
  API для BP — `UFUNCTION(BlueprintCallable|BlueprintPure)`; события для
  визуала — `BlueprintImplementableEvent` (код не знает про анимации/VFX).
- Комментарии — **на русском**, объясняют «почему», а не «что».
- Боевые способности — наследники `UTacticalAbility`; урон — только через
  `UTacticsCombatStatics::ResolveShot`; теги — из `TacticsGameplayTags`
  (native), новые теги добавлять туда же.
- Quest ID и `Quest.Event.*`, используемые C++, — native gameplay tags.
  Content-only `Quest.Objective.*` хранятся в `DefaultGameplayTags.ini`, а их
  полный буквальный список дублируется в scenario-документе для Editor-сборки.
- GE-компоненты в конструкторе CDO — только `CreateDefaultSubobject` +
  `GEComponents.Add` (НЕ `FindOrAddComponent` — фатал на старте редактора).
- У `EditInlineNew`/`Instanced`-элемента категории его редактируемых полей
  **не должны совпадать или быть родителем категории содержащего свойства**.
  Например, массив `Tactics|AI|Actions`, а поля элемента — отдельная плоская
  категория `Evaluator`; иначе `PropertyEditor` может уйти в рекурсивный
  Details layout и упасть с `EXCEPTION_STACK_OVERFLOW`.
- Никакой репликации/сетевого кода — проект одиночный.

## 5. Стиль Blueprints

- BP — только «обвязка»: визуал, звук, разводка событий, настройка параметров.
  Игровая логика (правила, расчёты) — в C++.
- Граф чистить: функции вместо простыней, комментарии-блоки на русском,
  Sequence вместо цепочек exec через весь граф.
- Каст к C++ классам — по интерфейсным геттерам, не к конкретным BP (BP→BP
  касты запрещены, кроме виджет-детей).

## 6. Git и LFS

- LFS уже настроен (`.gitattributes`): `.uasset/.umap` и медиа — в LFS.
  После добавления паков проверять `git lfs status` — бинарники должны быть
  «LFS: …», не «Git: …».
- Если ассеты вдруг стали текстовыми файлами-указателями (редактор говорит
  «файл ресурса») — `git lfs checkout` (объекты уже в `.git/lfs/objects`).
- Коммиты — по завершении логического куска (этап/подэтап), сообщение:
  краткая строка на русском + что изменилось. Пуш крупных бинарников — вручную
  пользователем (может быть долгим), не из агентской сессии.
- Не коммитить: `Saved/`, `Intermediate/`, `DerivedDataCache/`, `Binaries/`
  (в .gitignore уже есть).
- Теги: `v0.x` по этапам, `v1.0-demo` — сдача.

## 7. Правила работы агентов (Claude Code)

1. **Источник истины** — `docs/`. Перед задачей прочитать соответствующий
   раздел GDD/ROADMAP; после — обновить чекбоксы и, при изменении C++,
   03_CODE_OVERVIEW. Отклонился от GDD — сначала поправь GDD.
2. **Донор** `D:/UE5/UnrealProjects/cst-3d-gubkin-2026-04` — только чтение.
3. Сборка — `.\Build-XRU1.ps1` при закрытом редакторе; обычную сборку агент
   запускает сам (фон), долгие (>5 мин: lfs push, скачивание паков) — отдаёт
   пользователю.
4. После правок C++ — переиндексировать codebase-memory (`index_repository`).
5. Бинарные ассеты `.uasset` менять только через Unreal Editor/UnrealClaude MCP,
   не файловыми утилитами. После каждой MCP-правки: compile/save, read-back тем
   же query и PIE-проверка пропорционально риску; Montage Notify/Slot tracks,
   которые MCP не читает, проверяет пользователь в Editor.
6. Русский язык в ответах/комментариях, идентификаторы — английские.

## 8. Определение «сделано» (общее)

Задача закрыта, когда: собирается (`Result: Succeeded`) → проверено в PIE →
чекбокс в ROADMAP проставлен → доки синхронизированы → изменения закоммичены.
