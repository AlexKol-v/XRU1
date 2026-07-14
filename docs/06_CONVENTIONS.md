# Конвенции и правила проекта

## 1. Нейминг ассетов (UE-стандарт)

| Префикс | Тип | Пример |
|---|---|---|
| `BP_` | Blueprint-класс (акторы, компоненты, GameMode `GM_`) | `BP_Unit_Assault`, `GM_Tactics` |
| `WBP_` | Widget Blueprint | `WBP_MainMenu`, `WBP_TacticalHUD` |
| `ABP_` | Animation Blueprint | `ABP_Soldier` |
| `AM_` | Anim Montage | `AM_Fire` |
| `BS_` | Blend Space | `BS_Idle_Walk_Run` |
| `GA_` / `GE_` | BP-наследники способностей/эффектов | `GA_Heal`, `GE_TauntShield` |
| `DA_` | Data Asset | `DA_Quest_Tutorial` |
| `IMC_` / `IA_` | Input Mapping Context / Input Action | `IMC_Tactical`, `IA_EndTurn` |
| `L_` | уровень (карта) | `L_Mission01` |
| `M_` / `MI_` / `MF_` | материал / инстанс / функция | `MI_Hologram` |
| `T_` / `SM_` / `SKM_` / `SK_` | текстура / стат-меш / скелет-меш / скелет | — |
| `S_` / `SC_` | Sound Wave / Sound Cue | `SC_Shot` |
| `NS_` | Niagara System | `NS_MuzzleFlash` |

Язык имён ассетов — английский. Тексты игрока — русские, в виджетах/DataAsset.

## 2. Структура Content/ 

Весь новый контент — ТОЛЬКО в `Content/XRU1Game/<Core|UI|Units|Maps|Input|Data|Audio|FX>`
(см. 05_EDITOR_GUIDE §1). Сторонние паки — в `Content/ThirdParty/<PackName>/`,
внутри пака ничего не переименовывать (проще обновлять). Перенесённые из донора
папки (`Characters/`, `MWLandscapeAutoMaterial/`, `TopDown/`) не трогаем.

## 3. Стиль C++ (дублирует CLAUDE.md, здесь — источник)

- Классы: префиксы UE (`A`/`U`/`F`/`E`), проектный API-макрос `XRU1_API`.
- Дизайнерские параметры — `UPROPERTY(EditDefaultsOnly|EditAnywhere, Category="Tactics|...")`;
  API для BP — `UFUNCTION(BlueprintCallable|BlueprintPure)`; события для
  визуала — `BlueprintImplementableEvent` (код не знает про анимации/VFX).
- Комментарии — **на русском**, объясняют «почему», а не «что».
- Боевые способности — наследники `UTacticalAbility`; урон — только через
  `UTacticsCombatStatics::ResolveShot`; теги — из `TacticsGameplayTags`
  (native), новые теги добавлять туда же.
- GE-компоненты в конструкторе CDO — только `CreateDefaultSubobject` +
  `GEComponents.Add` (НЕ `FindOrAddComponent` — фатал на старте редактора).
- Никакой репликации/сетевого кода — проект одиночный.

## 4. Стиль Blueprints

- BP — только «обвязка»: визуал, звук, разводка событий, настройка параметров.
  Игровая логика (правила, расчёты) — в C++.
- Граф чистить: функции вместо простыней, комментарии-блоки на русском,
  Sequence вместо цепочек exec через весь граф.
- Каст к C++ классам — по интерфейсным геттерам, не к конкретным BP (BP→BP
  касты запрещены, кроме виджет-детей).

## 5. Git и LFS

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

## 6. Правила работы агентов (Claude Code)

1. **Источник истины** — `docs/`. Перед задачей прочитать соответствующий
   раздел GDD/ROADMAP; после — обновить чекбоксы и, при изменении C++,
   03_CODE_OVERVIEW. Отклонился от GDD — сначала поправь GDD.
2. **Донор** `D:/UE5/UnrealProjects/cst-3d-gubkin-2026-04` — только чтение.
3. Сборка — `.\Build-XRU1.ps1` при закрытом редакторе; обычную сборку агент
   запускает сам (фон), долгие (>5 мин: lfs push, скачивание паков) — отдаёт
   пользователю.
4. После правок C++ — переиндексировать codebase-memory (`index_repository`).
5. Бинарные ассеты (.uasset) агент не редактирует — только через инструкции
   пользователю (05_EDITOR_GUIDE) либо создание из C++.
6. Русский язык в ответах/комментариях, идентификаторы — английские.

## 7. Определение «сделано» (общее)

Задача закрыта, когда: собирается (`Result: Succeeded`) → проверено в PIE →
чекбокс в ROADMAP проставлен → доки синхронизированы → изменения закоммичены.
