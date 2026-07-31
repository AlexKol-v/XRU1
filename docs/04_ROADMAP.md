# Актуальный roadmap XRU1

Обновлено 2026-07-29. Здесь только открытая работа. Закрытые этапы не хранятся
как многосотстрочные чек-листы: их результат зафиксирован кратко в разделе 1 и
в тематических документах.

## 1. Готовая база

Считаются завершёнными:

- C++ тактическое ядро, GAS-способности, AP и ход сторон;
- player/enemy movement на общей маршрутизации;
- Half/Full cover, LOS, flanking, firing positions и occupancy;
- анимации locomotion/cover/peek/StepOut/fire/HitReact/death;
- синхронизация выстрела через montage `FireCommit`;
- разные модели оружия для четырёх классов;
- функциональное ядро HUD и базовая камера;
- AI-профиль DataAsset и воспроизводимый seed, не зависящий от FPS;
- F0 тумана: единый gameplay visibility, безопасные preview/enemy-count API;
- C++-каркас общей карты: scenario DataAsset, GameInstance routing,
  ScenarioDirector, quest-event bridge и tactical quest zone.

Старые PIE-матрицы закрыты. Если проявится регрессия, она становится отдельным
багом с воспроизводимым сценарием, а не возвращает весь этап в работу.

## 2. Текущий этап — два сценария одной карты

Целевая карта: `/Game/XRU1Game/Maps/Main_Map_Showreel`.

Фактическая база карты:

- `WorldSettings.DefaultGameMode = GM_Tactics`;
- есть `NavMeshBoundsVolume` и `RecastNavMesh`;
- добавлен `PlayerStart_Tutorial` рядом с текущим отрядом;
- стоят `BP_Unit_Marauder` и `BP_Unit_Sniper`; это пока тестовый состав, не
  финальная постановка секций A–D.

Архитектура и полный Editor-план —
[11_SHARED_MAP_TUTORIAL.md](11_SHARED_MAP_TUTORIAL.md).

- [x] Нативный bootstrap: Scenario Data Asset, shared travel, Director,
      stream-ready `StartScenarioCombat`, POI routing, quest reset/retry и
      единая terminal-финализация до save/result.
- [x] Quest registry, native `Quest.Event.Tactical.*` и confirmed hooks для
      turn/player combat/class abilities/objectives/scenario result.
- [x] Exact-match для одиночных/group objectives, terminal
      `Quest Wait Outcome` и config-теги `Quest.Objective.*`.
- [x] Payload квест-событий (`Source`/`Target`/`ScenarioRunId`) доезжает до
      StateTree; `Tactical Objective` фильтрует по `AnchorId` и умеет
      «N разных источников».
- [x] Реестр акторов сценария по `AnchorId` и единый `SetScenarioActorActive`.
- [x] Action Gate (`UTutorialActionGateSubsystem`) встроен в команды, выбор,
      перемещение, атаку, способности, End Turn и автозавершение хода.
- [x] Emitters `Unit.Selected`, `Camera.Adjusted`, `Movement.Settled.Open/InCover`
      публикуются из подтверждённых точек.
- [x] Сценарный выстрел A4/A7/B4 через общий attack pipeline
      (`FScriptedShotOverride` + `SetScriptedAttackOrder`).
- [x] StateTree-задачи обучения: gate, tactical objective, staged actor, scripted
      shot, tutorial beat.
- [x] Победа зачисткой отключается при наличии evac-зоны (иначе туториал
      заканчивался бы после D2, не дойдя до D3).
- [x] Создан persistent `Main_Map_Showreel` и оба scenario sublevel; общий
      арт/NavMesh/light остаются в persistent.
- [x] Созданы `DA_Scenario_Tutorial`/`DA_Scenario_Mission01`,
      `DA_Quest_*`/`ST_Quest_*` и `BP_TacticalScenarioDirector` со
      streaming-загрузкой и `StartConfiguredQuest`.
- [x] `DA_Quest_*` получили `QuestId` (`Quest.Tutorial`/`Quest.Mission01`) и
      `QuestLogic` → `ST_Quest_*`.
- [x] Streaming обоих scenario sublevel переведён на Blueprint/Dynamic и выключен
      по умолчанию; загрузку и старт делает нативный `ATacticalScenarioDirector`
      (`bAutoStreamScenarioSublevel`), BP-граф больше не обязателен.
- [x] Добавлен `PreviewScenario` у Director + `AdoptScenarioInPlace` у
      GameInstance — прямой PIE общей карты без Hub/POI.
- [x] `SL_Showreel_Tutorial` заселён: squad, голограммы A–D, зоны, якоря, evac;
      у каждого `UScenarioActorIdComponent` с `AnchorId` и `bStartDeactivated`.
- [x] `SL_Showreel_Mission01` заселён: отряд, 6 врагов, бомба, эвакуация, spawn-якоря.
- [x] `NavMeshBoundsVolume` перенесён в persistent (лежал в sublevel и исчезал
      вместе с ним), Paths пересобраны.
- [x] **Секция A обучения пройдена end-to-end (2026-07-31)**: A1–A9 в PIE от
      выбора Медика до подъёма Клина; `ST_Quest_Tutorial` собран для секции A
      (states/gates/objectives + Description для HUD), расстановка секции A
      выверена пользователем в редакторе.
- [x] Попутно закрыты найденные прогоном баги: `Tasks Completion=All` (дефолт
      `Any` пролетал всё дерево за кадр), профиль `ScenarioTrigger`
      (зона давала укрытие/резала LOS/дырявила навмеш), потерянные one-shot
      события до входа шага (перевзвод камеры/сброс выбора), Slate-трекер целей
      и маркеры точек, «AP>1 через точку», симметрия LOS Ф5
      ([13_LOS_TARGETING.md](13_LOS_TARGETING.md)), привязка scripted-форса к
      цели, quest registry: скан AssetManager расширен на `/Data`.
- [x] Ревизия способностей (2026-07-31): Heal — радиус 200 см «вплотную», круг
      радиуса + подсветка целей + причины отказа; Squadsight — модель XCOM 2
      (обнаружение союзником, геометрия обязательна, статус «Цель вне обзора»);
      `InitialHealth` на экземпляре, `SetHealthDirect`, задача `Scripted Move`.
- [x] Режиссура v2.1 принята (2026-07-31): Overwatch/Hunker встроены тактами
      C0–C2, фланг+Run&Gun добивает (C3), бомба на 2 действия (C4). Черновые
      v2.1-акторы созданы скриптом, Holo_D настроен (HP 50, патруль), якорь
      камеры починен (label ≠ AnchorId — теперь совпадают).
- [ ] Выверить позиции v2.1-акторов глазами по [12 §2](12_TUTORIAL_LAYOUT_SPEC.md)
      и собрать состояния B0–D1 в `ST_Quest_Tutorial` по [11 §5.0](11_SHARED_MAP_TUTORIAL.md).
- [ ] Сгенерировать озвучку реплик v2 (список — [02 §6.1](02_LORE_SCRIPT.md)).
- [ ] Заполнить `ST_Quest_Mission01` по §6.2.
- [ ] Исправить `NavData RegistrationFailed_AgentNotValid` и пересобрать Paths.
- [x] Найти и убрать `SpawnActor` с пустым Class (это был `HUDClass=None` в
      `GM_Tactics` — движок спавнил AHUD без класса; назначен базовый
      `/Script/Engine.HUD`, 2026-07-31). Осталось: исправить/заменить `MM_Sky` SM6.
- [ ] Сделать два сквозных PIE-прогона как два чистых запуска одного persistent
      map package `Main_Map_Showreel`, включая retry/abort и отсутствие состояния
      предыдущего runtime World.

DoD: оба POI открывают один persistent World; одновременно существует только
контент выбранного сценария; tutorial проходит A1–D3, Mission01 — бомбу и
эвакуацию, без Level BP и межсценарных утечек состояния.

## 3. AI — текущий системный прогон

Архитектурная база уже есть; задача теперь — настроить решения на реальных
укрытиях tutorial/mission layout. Полная программа — [08_AI.md](08_AI.md).

- [x] Зафиксировать архитектуру `Alert FSM + Utility + AP-aware executor`;
      donor real-time StateTree/BT не переносить.
- [x] Убрать зависимость розыгрышей от `GFrameCounter`.
- [x] Добавить `UAIBehaviorProfileDataAsset` для общего тюнинга.
- [x] Поведенческие оси сложности (`FAIStyleTuning`): множители веса оценщиков,
      готовность фланкировать, сведение огня, добивание раненых.
- [x] Назначение профиля по сложности из GameMode; перцепция перенастраивается
      при смене профиля.
- [x] Диагностика: `xru1.AI.DebugDraw`, категория `LogXRU1AI`, `xru1.Debug.List`.
- [ ] Создать `DA_AI_Easy`/`DA_AI_Medium`/`DA_AI_Hard` по таблице из
      [agents/BRIEF_AI_Refactor.md](agents/BRIEF_AI_Refactor.md) §3 и назначить
      их в `BP_TacticsGameInstance.AIProfilesByDifficulty`.
- [ ] Прогнать фиксированную матрицу из [08_AI.md](08_AI.md) §6 на
      Tutorial/Mission01 layout и сохранить baseline-логи.
- [ ] Ввести `FAIContactMemory`: источник/тип/достоверность/возраст контакта.
- [ ] Разделить монолитный `FindCoverPoint` на generation → filters → features
      → scoring → stable tie-break, не меняя baseline.
- [ ] Перейти к полным proposals `(Action, Target, Destination, APCost)`.
- [ ] Добавить group reservations, shared contacts, focus/attack budgets.
- [ ] Настроить fairness/сложность без omniscient targeting и скрытых бонусов.

DoD: три последовательных боя на каждой сложности проходят без зависаний;
враги используют укрытия, не кучкуются, не читают скрытые позиции и принимают
объяснимые решения.

## 4. Туман войны

Архитектура и Editor-checklist — [10_FOG_OF_WAR.md](10_FOG_OF_WAR.md).

- [x] Централизовать текущую видимость в `UFogOfWarSubsystem`.
- [x] Убрать скрытых врагов из move preview; дать HUD безопасный visible count.
- [ ] Перевести targeting/hover/outline/overhead/camera/VFX на actor gating.
- [ ] Реализовать событийный CPU-grid `Unknown/Explored/Visible` с этажами.
- [ ] Создать `FogVision` trace channel и authored bounds/profiles/reveal volumes.
- [ ] Создать один RT + post-process renderer на tactical camera.
- [ ] Проверить независимый reset Tutorial/Mission01 по `ScenarioId`.

DoD: неизвестный враг не раскрывается ни геометрией тумана, ни HUD, preview,
камерой или эффектами; одна карта стартует с чистым состоянием каждого scenario.

## 5. Точная настройка способностей классов

- [ ] Assault: проверить стоимость/одноразовость Run & Gun и понятность окна
      дополнительного AP.
- [ ] Sniper: настроить Squadsight, штраф без собственной LOS и дальностную
      кривую снайперского оружия.
- [ ] Medic: радиус, лечение себя/союзника, подъём Downed, 2 заряда и tutorial
      override без лимита.
- [ ] Tank: радиус Taunt 2500, обязательный target override и −50% входящего
      урона до следующего хода танка.
- [ ] Для каждой ability добавить/проверить HUD state, montage/VFX/SFX и ясный
      feedback причины недоступности.
- [ ] Согласовать итоговые числа с [01_GDD.md](01_GDD.md).
- [ ] Устранить текущие missing dependencies у optional attachments Sniper/SMG/
      LMG (grip/laser/silencer/front sight и `MI_Attachments_02`) либо удалить
      эти ссылки, если детали намеренно не используются.

DoD: каждая способность используется в туториале, имеет один источник расхода
AP/зарядов и не конкурирует с другим `Ability.TacticalAction`.

## 5.5. Звук и настройки

Каркас закрыт кодом 2026-07-30; открыт только контент.

- [x] Иерархия SoundClass/SoundMix и применение громкостей через подсистему.
- [x] `UUnitAudioDataAsset`: выстрел, реакция, смерть, способности, шаги по
      поверхностям; единый `AUnitBase::PlayUnitSound` в подтверждённых точках.
- [x] `UAnimNotify_UnitFootstep` с выбором звука по физматериалу.
- [x] Громкости и настройки изображения в слоте кампании, рабочий backend
      экрана настроек, применение при загрузке слота.
- [ ] Создать в редакторе `DA_TacticsAudio`, SoundMix `SM_UserVolumes` и пять
      SoundClass (Master → Music/Sfx/UI/Voice).
- [ ] Найти и импортировать звуки: выстрелы четырёх стволов, шаги по бетону/
      траве/металлу, боль/смерть, интерфейс, смена фазы.
- [ ] Заполнить `DA_UnitAudio_*` для четырёх классов и мародёра.
- [ ] Расставить `XRU1 Footstep` notify в анимациях ходьбы/бега.
- [ ] Музыка и голос «Купола» по GDD/сценарию.

## 6. HUD и экраны

Подробности — [09_UI_HUD.md](09_UI_HUD.md).

- [ ] Добавить боевую обратную связь: урон, промах, Overwatch, недоступность.
- [ ] Доделать карточку/информацию активного врага в его ход.
- [ ] Привести все блоки HUD к `DA_TacticalHUDStyle` и проверить 16:9/16:10.
- [ ] Завершить Main Menu, difficulty, intro, briefing, result и DemoComplete.
- [ ] Подключить Main Menu → Hub → `StartCombatScenario(Tutorial/Mission01)`.

DoD: весь обязательный бой управляется мышью через HUD; состояния не требуют
чтения Output Log, а экраны образуют непрерывный пользовательский цикл.

## 7. Остальные уровни и контент

- [ ] `L_MainMenu` и интро.
- [ ] `L_Hub` с двумя POI.
- [ ] Звук, музыка и голос «Купола» по GDD/сценарию.
- [ ] Финальные тексты, титры и сведения об использованных ассетах.

## 8. Полировка и сдача

- [ ] Эталонное прохождение не менее 10 минут.
- [ ] Победа, поражение по таймеру и поражение отряда корректно завершаются.
- [ ] Настройки громкости/Scalability реально применяются.
- [ ] Очистить временные логи и debug draw; оставить cvar-диагностику.
- [ ] Packaging maps: MainMenu, Hub, Main_Map_Showreel и оба scenario sublevel.
- [ ] Development build → проверка → Shipping build → чистая машина/папка.
- [ ] Финальная сверка с учебным заданием и тег `v1.0-demo`.

## 9. Отложенный backlog

Не брать до завершения tutorial vertical slice, если задача не блокирует игру:

- переработка ракурсов и поведения камеры;
- IK/Control Rig второй руки на оружии;
- кинематографическая полировка Overwatch/slow motion;
- PCG-раскладка укрытий;
- дополнительные косметические анимации и VFX.
