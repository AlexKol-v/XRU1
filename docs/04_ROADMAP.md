# Роадмап реализации (этапы 0–9)

Порядок: **сначала добить C++ (этап 1), потом контент в редакторе** — как
договорились. Каждый этап самодостаточен: цель, задачи, критерий готовности
(DoD). Отмечай чекбоксы. Агенту достаточно сказать: «делаем этап N».

Оценки грубые, в рабочих сессиях (1 сессия ≈ вечер работы с агентом).

---

## Этап 0 — Гигиена проекта ✅

- [x] Git + LFS настроены, донор read-only, сборка `.\Build-XRU1.ps1`
- [x] Боевое ядро C++ (AP, укрытия, overwatch, ход AI, меню-каркас) — см. 03_CODE_OVERVIEW
- [x] Пакет документации docs/
- [x] В `DefaultEngine.ini` прописаны `BP_TacticsGameInstance` и
  `CommonGameViewportClient`; настройки уже подхвачены после build/restart
  Editor (2026-07-22)
- [ ] **Чистка шаблона — ОТЛОЖЕНА до этапа 3** (когда будет L_MainMenu+GM_MainMenu):
  `XRU1Character/GameMode/PlayerController` завязаны на конфиги
  (`DefaultEngine.ini` ActiveClassRedirects, `DefaultGame.ini`) и служат
  fallback-GameMode текущей дефолт-карты `Lvl_TopDown`. Мой тактический код на
  них НЕ ссылается. Удалять только ПОСЛЕ переключения Default Map на L_MainMenu
  (иначе проект не запустится), вместе с чисткой их секций в Config/.

## Этап 1 — Достройка C++ ядра (P0+P1) ✅ (2026-07-14)

Реализовано и собрано (`Result: Succeeded`, CDO не крашат редактор):

- [x] `ATacticalCameraPawn` (панорама/поворот 45°/зум с интерполяцией)
- [x] `ATacticalPlayerController` (выбор ЛКМ/1–4/Tab, движение ПКМ, атака по
  клику, контекст F, хоткеи, режим цели медика, пауза, блокировка чужой фазы)
- [x] **Зона хода по навмешу** (`AMoveRangeVisualizer`): заливка полигонов
  Recast по стоимости пути (`FindPolysAroundCircle`+cost-фильтр), 1 AP + 2 AP;
  валидация приказа по `GetNavPathLength`
- [x] `UGA_Attack` (+Squadsight −10) + `bConsumesAllRemainingAP`/`MaxUsesPerMission`
  в `UTacticalAbility`
- [x] Способности классов: `UGA_HunkerDown`/`UGA_Heal`/`UGA_RunAndGun`/`UGA_Taunt`
  (P1 подтянут в этот этап)
- [x] Смерть/Downed/эвакуация в `AUnitBase` + `SetDowned`/`ReviveFromDowned`/`Evacuate`
- [x] `ATacticsGameMode` (сбор сторон по TeamId, сложность, бомба→эвакуация→
  победа, результат→сейв)
- [x] `ABombObjective` + `AEvacZone` + таймер ходов в TurnManager
- [x] AI-тревога XCOM: Patrol/Investigate/Combat (`AUnitAIController`)
- [x] C++ базы `UTacticalHUDWidget`, `UMissionResultWidget`; POI-гейт по обучению
- [x] 03_CODE_OVERVIEW актуализирован

**Основной программный каркас этапа готов**, но это не означает, что осталась
только редакторская сборка или P2. Помимо BP/уровней обязательны C++-исправления
укрытий, firing positions/peek и AI из `09_GAMEPLAY_STATUS.md` §B.2–B.3 /
`11_COVER_AND_ENEMY_PLAN.md`; остальной общий беклог — `03_CODE_OVERVIEW.md` §3.

## Этап 2 — Ассеты: Fab + GASP + генерация — ~1–2 сессии (ручная)

### 2A. Fab (бесплатное; проверено поиском 2026-07)

Конкретные кандидаты (проверить кнопку Free/цену на странице):
- [ ] **Military Base Megapack (+ULAT)** — fab.com/listings/fd638d23-5a42-4734-833e-817f5bb5fd7c
  (модульная военная база: стены/интерьеры/техника — закрывает обе карты)
- [ ] Запасной вариант: **Military Base Assets** — fab.com/listings/ca5bfd56-…
  или «Modular Container Houses — Survival Base Pack» (контейнерная база)
- [ ] **Винтовка**: «M416/M4 Assault Rifle — Modular» — fab.com/listings/39d0bfca-…
  (без реальных лого, game-ready) — нужен только static/skeletal mesh
- [ ] **Megascans** — fab.com/megascans-free: 800+ бесплатных сканов (ящики,
  бетонные блоки, мешки, камни для хаба) + Megaplants (трава на ландшафт)
- [ ] Пропсы-укрытия: «Realistic Weapons Arsenal & Tactical Combat Gear Props»
  (ящики патронов, стойки) — проверить цену

**Как искать на Fab:** фильтр слева Price=Free + Format=Unreal Engine; запросы
«military base modular», «sandbag», «industrial props», «radio tower»,
«sci-fi hologram material»; страница официальных раздач —
unrealengine.com/fabfreecontent (**лимитированные бесплатные дропы раз в
2 недели** — заглядывать каждую сессию и клеймить всё милитари/индастриал
впрок: добавленное в библиотеку остаётся навсегда).

### 2B. Анимации (GASP)
- [ ] Персонаж/анимации из `D:/UE5/UnrealProjects/GameAnimationSample`:
  путь 1 — взять UEFN-персонажа целиком, путь 2 — ретаргет секвенций через
  `RTG_UEFN_to_UE5_Mannequin` (инструкция 05_EDITOR_GUIDE §4.0; выбор — GDD §12.1)

### 2C. Генерация (по 07_CONTENT_PROMPTS, подписка на месяц)
- [ ] Выбрать сервис: **Kling 3.0 Standard (~$10)** или Veo 3.1 (Google AI Pro)
- [x] Интро-видео добавлено → `Content/Movies/TU_Intro.mp4` +
  `FMS_TU_Intro` (2026-07-22; сценарий: 07 §2.2)
- [x] Портреты (5), иконки и фоны/брифинги (7) добавлены в UI (2026-07-22)
- [x] `DA_TacticalHUDStyle` расширен до единой UI-темы: все изображения,
  палитра, размеры/padding блоков, soft refs крупного арта; гайд заполнения —
  `10_UI_THEME_GUIDE.md` (2026-07-22)
- [x] Сам `DA_TacticalHUDStyle` перенесён в `/Game/XRU1Game/Data`, заполнен
  всеми текущими 35 UI-текстурами и `FMS_TU_Intro`; ещё не созданы только
  `M_IntroVideo` и CommonUI styles (аудит UnrealClaude MCP, 2026-07-22)
- [ ] Озвучка «Купол» + реплики бойцов (07 §6, тексты из 02_LORE_SCRIPT §5–6)
- [ ] Музыка: меню/бой/хаб (07 §7)
- [ ] Зафиксировать использованные ассеты/сервисы в «Об авторе» и GDD §12

**DoD:** паки в `Content/ThirdParty/<PackName>/`, анимации в
`Content/XRU1Game/Units/Anims/`, арт в `UI/Art|Icons`, видео в `Movies/`;
проект открывается, `git lfs status` показывает новые бинарники в LFS.

## Этап 3 — Каркас UI и меню в редакторе — ~1 сессия

По [05_EDITOR_GUIDE.md](05_EDITOR_GUIDE.md) §2–3:

> Арт этапа 2C, C++-поля темы и ссылки текущего `DA_TacticalHUDStyle` уже
> готовы. Подключение каждого WBP и создание недостающих style/media assets
> выполнять по `10_UI_THEME_GUIDE.md`.

- [x] `BP_TacticsGameInstance` создан от `UTacticsGameInstance`, `UITheme` уже
  указывает на `DA_TacticalHUDStyle` (проверено MCP, 2026-07-22)
- [x] `BP_TacticsGameInstance` прописан как `GameInstanceClass`, а
  `CommonGameViewportClient` — как `GameViewportClientClassName`
  (`Config/DefaultEngine.ini`, 2026-07-22)
- [ ] После создания карт задать в `BP_TacticsGameInstance` только `HubLevel`
  и `MainMenuLevel`
- [x] `WBP_PrimaryGameLayout` создан от `UPrimaryGameLayout`
- [ ] В `WBP_PrimaryGameLayout` вручную сверить 4 стека с точными BindWidget-
  именами: `GameStack`, `GameMenuStack`, `MenuStack`, `ModalStack`
- [x] Созданы правильные наследники: `WBP_MainMenu`, `WBP_DifficultySelect`,
  `WBP_Settings`, `WBP_AboutMenuWidget`, `WBP_PauseMenuWidget`
- [ ] Перевести эти пять меню на арт/layout/CommonUI styles из темы; сейчас
  MainMenu/Difficulty ещё держат прямые background references
- [x] `WBP_UnitPortrait` пересобран в горизонтальный `Unit Flag`; portrait,
  class, status, selection frame, card size, padding и item spacing подключены
  к теме; исправлены четыре пропущенных провода `Texture`, удалён старый
  status-блок, Blueprint `UpToDate` без ошибок; созданные карточки теперь
  действительно добавляются в массив `WBP_TacticalHUD.Portraits`, а
  Hunker/Taunt/Overwatch/Moving посылают своевременный state refresh
  (MCP + C++, 2026-07-22)
- [x] `PortraitStatusIconSize → StatusSizeBox` применяет C++ после создания и
  возможной пересборки вложенных карточек; ручные Blueprint-узлы не нужны
- [x] Build/restart выполнен; скопированные Assault-defaults у
  Sniper/Medic/Tank исправлены и сохранены как `Sniper / Оса`,
  `Healer / Шприц`, `Tank / Молот`; PIE подтверждает карточку Assault и общий
  runtime-путь темы (2026-07-22)
- [x] Вручную проверены в PIE портреты/иконки четырёх классов и матрица
  статусов карточки (подтверждено пользователем 2026-07-23)
- [ ] WBP_IntroPlayer от `UIntroPlayerWidget`: `FMS_TU_Intro`, skip/end →
  `FinishIntro`; назначить в `WBP_DifficultySelect.IntroScreenClass`
- [ ] BP_MenuPlayerController + GM_MainMenu + уровень L_MainMenu
- [ ] Прогон: меню → сложность → (пока пустой) хаб; Продолжить активна после сейва

**DoD:** из главного меню доступны все экраны, «Новая игра» создаёт сейв и
уводит на L_Hub (пусть пока пустой), Esc-пауза работает в игровом уровне.

## Этап 4 — Юниты: BP-классы и анимации — ~2 сессии

> **Решение по анимациям (2026-07-16, GDD §12.1 уточнён практикой):**
> ABP_Soldier строим как **дубликат стокового ABP_Manny, расширенный state
> machine**, а НЕ на GASP-риге с motion matching: MM заточен под траектории
> ввода игрока — с `AIController::MoveTo` даёт футслайдинг и неверные
> направления (подтверждено форумом Epic), и для пошаговой тактики с
> дискретными стойками избыточен. Недостающие анимации (crouch idle/walk,
> ADS, death) **точечно ретаргетим из GASP** по конвейеру 05_EDITOR_GUIDE §4.0.
> База (idle/бег) уже работает на стоковом ABP — см. чекбокс ниже.

- [x] **Базовая локомоция юнитов работает** (2026-07-16): стоковый ABP на
  SK_Mannequin играет idle/бег при AI-движении. Потребовался C++-фикс:
  `bUseAccelerationForPaths` в конструкторе `AUnitBase` — иначе path following
  задаёт скорость напрямую без Acceleration, и условие Should Move стокового
  ABP (`Speed>0 AND Acceleration≠0`) никогда не срабатывает
- [ ] ABP_Soldier (дубликат стокового ABP → `Content/XRU1Game/Units/`):
  слоты монтажей Fire/HitReact/Death, поза Downed (лежит) — 05_EDITOR_GUIDE §4.1
- [ ] Ретаргет из GASP (§4.0): crouch idle + crouch walk loop (+ idle-вариации
  по желанию) → `Content/XRU1Game/Units/Anims/GASP/`
- [ ] **Стойки укрытий**: ABP читает `BestCoverAround`/`OnCoverStateChanged` →
  half cover = **crouch** (GASP crouch idle/walk), full cover = стоя у стены,
  открыт = боевая стойка; Overwatch = ADS-поза (GDD §12.1)
- [x] Созданы `BP_Unit_Assault/Sniper/Medic/Tank` с правильными родителями;
  у новых BP сохранены Manny, `ABP_Unarmed`, selection ring и squad HUD;
  нативные `BaseMaxHealth`/Aim/Range, Squadsight и классовые способности
  исправлены (HP 100/80/90/150 переносится в GAS до старта HUD); на
  `Lvl_TopDown` стоит ровно по одному бойцу каждого класса (MCP, 2026-07-22)
- [ ] Визуальная полировка четырёх BP: Quinn для разнообразия, винтовка в
  `weapon_r`, заменить временный `ABP_Unarmed` на `ABP_Soldier`
- [x] `BP_Unit_Marauder` (враг, `AUnitBase`) создан и размещён в двух
  экземплярах: TeamId=2, BaseAim=65, ShotDamage=20, squad abilities пусты;
  стартовые HP/Aim затем переопределяет `ATacticsGameMode` по сложности
- [ ] Монтажи: AM_Fire, AM_HitReact, AM_Death (+ хук из BP-событий
  OnShotFired/OnDied/OnReactionShot)
- [x] HUD юнита: пипсы AP + иконка укрытия в WBP над головой — 4
  WBP-наследника + `DA_UnitHUD_Squad/Enemy` + назначение в BP юнитов,
  проверено чтением через MCP 2026-07-20
- [x] Код независимого status badge над головой и cover badge в карточке:
  `UUnitStatusIconWidget`, автослот/настройки `UUnitHUDLayoutData`, общие
  резолверы темы и подписки на state/cover (2026-07-23)
- [x] Полный build/restart выполнен; пользователь вручную подтвердил, что
  status + cover одновременно видны в карточке и над головой (2026-07-23)
- [ ] Собрать добавленную контрастную подложку overhead status и принять её
  на светлом, сером и синем фоне уровня; Blueprint/WBP для неё не нужен

**DoD:** на тестовой карте 4 бойца и 2 врага стреляют с анимацией и звуком,
у half-cover юнит приседает, у full — стоит у стены, смерть/Downed
проигрываются, HP/AP видны над головами.

## Этап 5 — Способности классов (C++ P1 + BP-обвязка) — ~1–2 сессии

- [x] C++-механики `UGA_HunkerDown`, `UGA_Heal`, `UGA_RunAndGun`, `UGA_Taunt`,
  Squadsight и штраф Overwatch реализованы по GDD
- [ ] BP-презентация GA: монтажи/VFX/SFX в BP-наследниках способностей
- [x] Функциональные кнопки способностей в боевом HUD: доступность учитывает
  реальный AP-cost и заряды; у Sniper пассивная Squadsight не показывает
  активную кнопку, Run & Gun доступен при 0 AP (C++, аудит 2026-07-22)
- [x] `CanIssueCommand` стал единым арбитром Request*/HUD, а все tactical GA
  получили общий GAS-block tag: Overwatch/Hunker/ClassAbility больше не
  запускаются внутри Attack/Ability-targeting (код 2026-07-23)
- [x] GAS-block проверяется для Move/Interact/Skip; базовый
  `UTacticalAbility::EndAbility` обновляет HUD после снятия блокировки, поэтому
  мгновенные и длительные GA не оставляют кнопки или зону хода ошибочно disabled
- [x] Targeted-GA сама объявляет Gameplay Event и предикат цели; контроллер
  фиксирует действующего юнита и отправляет событие до UI-broadcast выхода из
  режима. Хардкод `Event.Heal` и окно смены состояния устранены (2026-07-23)

**DoD:** каждая способность демонстрируема в PIE и соответствует числам GDD §7.

## Этап 6 — Боевой HUD: функциональное ядро ✅, полировка B.9 открыта

Сборка в редакторе закрыта (историю см. `docs/archive/`); часть логики
позже ушла из WBP-графа в C++. Итог:

- [x] WBP_TacticalHUD на C++ базе: портреты 1–4 (HP/AP/статус, клик-выбор),
  панель действий, шанс попадания у курсора/цели, индикатор фазы и хода,
  кнопка «Завершить ход», счётчик врагов, `DA_TacticalHUDStyle`
- [x] Зона хода и превью пути: `AMoveRangeVisualizer` (волновое поле по сетке
  + marching squares) вместо декали радиуса — детали алгоритма и почему
  занятость решается на уровне запросов, а не вырезов навмеша —
  `03_CODE_OVERVIEW.md §2.5`
- [x] Подсветка юнитов (ховер-обводка + кольцо выбора) — `05_EDITOR_GUIDE.md §4.5`
- [x] Камера XCOM: плавный полёт/следование за бегущим бойцом и действующим
  врагом (LOS-гейт), edge scroll мышью у края
- [x] Пользовательский yaw/zoom отделён от временного action-camera ракурса:
  смена бойца/фазы не наследует поворот прицела; aim-frame не блокирует
  автопереход бесконечно (код 2026-07-23)
- [x] `EnemyCountIcon` и его размер теперь применяются из общей темы в C++;
  WBP использует стабильное имя и прямой `BindWidgetOptional`
- [x] В тему и C++ добавлена отдельная `EnemyCounterBackground`:
  texture/color/internal padding не дублируются в WBP; validation проверяет
  padding и контраст alpha (код 2026-07-23)
- [x] Карточка выбранного бойца и overhead HUD используют независимые
  cover/status badge из одного `DA_TacticalHUDStyle` (код 2026-07-23)
- [x] После полного build/restart визуально приняты overhead status + card
  cover в Half/Full cover и Overwatch/Hunker (пользователь, 2026-07-23)
- [x] Тёмная подложка прозрачного overhead status PNG принята после полного
  build/restart (пользователь, 2026-07-23)
- [x] После полного build/restart принята регрессия 2026-07-23:
  enemy-count glyph + подложку, сохранение Q/E при смене бойца, блокировку
  Overwatch внутри aim-mode и обычный Overwatch после отмены
- [ ] Всплывающие «−25 / ПРОМАХ / НАБЛЮДЕНИЕ!» (P2, см. `09_GAMEPLAY_STATUS.md §B.7`)

**DoD достигнут:** весь бой играется мышью по HUD, без клавиатуры (хоткеи —
бонус). Дальнейшая полировка HUD/боя — `09_GAMEPLAY_STATUS.md`, известные
мелочи (диагностические логи и т.п.) — там же §D.

## Этап 7 — Уровни — ~3–4 сессии

- [ ] **L_Tutorial** «Полигон “Купол”» (~60×40 м, кит из этапа 2): секции A–D
  по сценарию 02_LORE_SCRIPT §4 (пустырь → укрытия → дальняя позиция →
  финал+эвакуация), 3 врага-голограммы под контролем скрипта, NavMesh
- [ ] Скрипт туториала: шаги-objectives (STQuestSystem) + level BP: форс-выстрелы
  (`ResolveShot` 100/0), заскриптованный Downed-Клин (`SetDowned`), поэтапная
  активация врагов, подсветка целевых точек, зона эвакуации в финале;
  результат → сейв «Tutorial» → хаб
- [ ] **L_Hub**: ландшафт-миниатюра (MWLandscape MountainRange), голо-материал,
  орбитальная камера, 2 × `AMissionPointOfInterest` (тексты 02_LORE_SCRIPT §3),
  попап-виджет POI
- [ ] **L_Mission01** «Станция “Узел-7”» (~60×60 м): зоны A/B/C по сценарию §5,
  `ABombObjective` у аппаратной, `AEvacZone` у ворот, таймер ходов по сложности,
  спавн врагов по сложности (GM), реплики-нотификации; победа → WBP_DemoComplete
  → меню; поражение (таймер/отряд) → ретрай
- [ ] (стретч) PCG-раскладка укрытий для L_Mission01

**DoD:** полный цикл из GDD §3 проходится в PIE от меню до финального экрана;
оба исхода поражения (таймер, гибель отряда) корректно обрабатываются.

## Этап 8а — AI врагов по XCOM-правилам (ОТДЕЛЬНАЯ ЗАДАЧА, решено 2026-07-20)

**Проблема:** враг и игрок ходят по РАЗНЫМ правилам. Игрок получил единый план
поля (`PlanMoveTo`) и движение по ломаной; враг остался на funnel-пути с
усечением по просвету (`GetPointAlongPathBudget` → `MoveToLocation`). Значит
враг не умеет обходить занятые клетки — он лишь останавливается перед ними.
Просвет с раздуванием на радиус агента у них уже общий, маршрут — нет.

**Что сделать:**
- строить поле достижимости и для врага (перенести `BuildDistanceField` в
  модель, отделив от визуализатора — см. беклог P2 §3a в `03_CODE_OVERVIEW`);
- вести врага через `AUnitAIController::MoveAlongRoute`, как игрока;
- поверх — собственно XCOM-поведение: выбор точки С УКРЫТИЕМ от цели
  (сэмплинг зоны + `GetCoverAgainst`), scamper-рывок при первом обнаружении,
  Overwatch по остатку AP (беклог §3 п.1).

**Почему отдельно:** поле на каждого врага в каждый шаг хода — заметная
стоимость (замерить `xru1.MoveRange.LogBuildTime 1`), и это уже не багфикс
перемещения, а работа над поведением AI. Текущее поведение врагов рабочее.

> **✅ Прогресс 2026-07-21 — утилити-скоринг с весами** (`03_CODE_OVERVIEW.md
> §2.6`): отступление при низком HP, манёвр в укрытие с линией огня, рывок к
> дальнему укрытию (оба AP, вторая нога следующим шагом), веса — UPROPERTY
> `Tactics|AI|Weights`, лог решений `xru1.AI.LogCombat`. Осталось: движение
> врага по ломаной поля (см. «Что сделать» выше) + scamper при первом обнаружении.

## Этап 8 — Полишинг и баланс — ~1–2 сессии

> **✅ Механика атаки доведена до XCOM 2 (2026-07-20/21).** Сначала
> исправлены 2 бага прицеливания (ЛКМ стрелял мимо кнопки «Огонь»; «слишком
> далеко» путалось с «нет линии огня») и перекалибрована дальность оружия
> (LOS вместо жёсткого технического предела), затем добавлена обратная связь
> вооружённого режима: баннер прицеливания, Tab/Q/E по целям, кадр камеры
> «из-за плеча», двухтактное подтверждение (ЛКМ выбирает цель — «Огонь»/Enter
> стреляет). Точные константы и имена функций — `03_CODE_OVERVIEW.md §2.6`.
> Принято ручным PIE-прогоном (`08_HANDOFF.md`).

- [ ] Прогон эталонного прохождения: суммарно **не менее 10 минут** (ожидаемо
  12–18), при недоборе — усилить миссию (враги/таймер), а не растягивать паузы
- [ ] Поражение → ретрай работает; «Продолжить» после каждого этапа корректно
- [ ] Звук по списку GDD §14, музыка меню
- [ ] Настройки: громкость + Scalability реально применяются
- [ ] «Об авторе»/титры заполнены (ассеты, курс)
- [ ] Вычистить логи/дебаг-отрисовку, иконка и название проекта

**DoD:** три полных прохождения подряд (лёгкий/средний/сложный) без блокеров.

## Этап 9 — Билд и сдача — ~1 сессия

- [ ] Project Settings → Packaging: список карт (L_MainMenu, L_Hub, L_Tutorial,
  L_Mission01), default map = L_MainMenu
- [ ] Package Windows (Development → проверка, потом Shipping)
- [ ] Прогон билда на чистой машине/папке: цикл от меню до финала
- [ ] Тег в git (`v1.0-demo`), пуш; архив билда для формы сдачи
- [ ] Финальная сверка с чек-листом задания (GDD §16)

**DoD:** архив билда + ссылка на репозиторий отправлены на проверку.

---

## Порядок и зависимости

```
0 → 1 → (2 параллельно с 1) → 3 → 4 → 5 → 6 → 7 → 8 → 9
```
Этап 2 (ассеты) не блокирует 1 и 3 — можно качать параллельно.
Стретч-цели (PCG-генерация, интеракт-цель миссии, цифры урона) берём только
после DoD этапа 7.
