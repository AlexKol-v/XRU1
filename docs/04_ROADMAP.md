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

Целевая карта: `/Game/US_Military/Levels/Showreel_Scene`.

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
- [ ] Оставить общий арт/NavMesh/light/camera bounds в persistent
      `Showreel_Scene`; создать `SL_Showreel_Tutorial` и
      `SL_Showreel_Mission01`.
- [ ] Создать `DA_Scenario_Tutorial` и `DA_Scenario_Mission01`; оба открывают
      `SharedCombatLevel`, но выбирают разные sublevel/quest/fog profile.
- [ ] Создать `BP_TacticalScenarioDirector`, загрузить ровно один sublevel и
      после `On Level Loaded` вызвать `StartConfiguredQuest`.
- [ ] Создать `ST_Quest_Tutorial` и `DA_Quest_Tutorial`;
      собрать A1–A9, B1–B5, C1–C2, D1–D3 без Level Blueprint.
- [ ] Подключить события только в authoritative completion hooks и сделать
      отдельный action gate текущего шага.
- [ ] Расставить squad, staged holograms, tactical quest zones, bomb/evac и
      mission spawners по соответствующим sublevels.
- [ ] Исправить `NavData RegistrationFailed_AgentNotValid` и пересобрать Paths.
- [ ] Найти и убрать `SpawnActor` с пустым Class; исправить/заменить `MM_Sky` SM6.
- [ ] Сделать два сквозных PIE-прогона как два чистых запуска одного persistent
      map package `Showreel_Scene`, включая retry/abort и отсутствие состояния
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
- [ ] После C++ сборки создать `DA_AI_Marauder_Default`, назначить его enemy controller.
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
- [ ] Packaging maps: MainMenu, Hub, Showreel_Scene и оба scenario sublevel.
- [ ] Development build → проверка → Shipping build → чистая машина/папка.
- [ ] Финальная сверка с учебным заданием и тег `v1.0-demo`.

## 9. Отложенный backlog

Не брать до завершения tutorial vertical slice, если задача не блокирует игру:

- переработка ракурсов и поведения камеры;
- IK/Control Rig второй руки на оружии;
- кинематографическая полировка Overwatch/slow motion;
- PCG-раскладка укрытий;
- дополнительные косметические анимации и VFX.
