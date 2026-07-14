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
- [ ] Прописать в Project Settings: GameInstance = `BP_TacticsGameInstance` (этап 3)
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

**DoD достигается в редакторе (этапы 3–7)** — весь код для него готов; осталось
собрать BP/уровни. Оставшийся C++ — только P2-полировка (03_CODE_OVERVIEW §3).

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
- [ ] Интро-видео: 6 кадров → 6 клипов → монтаж в DaVinci Resolve →
  `Content/Movies/Intro.mp4` (сценарий: 07 §2.2)
- [ ] Портреты (5), иконки (~16), фоны/брифинги (7) — 07 §3–5
- [ ] Озвучка «Купол» + реплики бойцов (07 §6, тексты из 02_LORE_SCRIPT §5–6)
- [ ] Музыка: меню/бой/хаб (07 §7)
- [ ] Зафиксировать использованные ассеты/сервисы в «Об авторе» и GDD §12

**DoD:** паки в `Content/ThirdParty/<PackName>/`, анимации в
`Content/XRU1Game/Units/Anims/`, арт в `UI/Art|Icons`, видео в `Movies/`;
проект открывается, `git lfs status` показывает новые бинарники в LFS.

## Этап 3 — Каркас UI и меню в редакторе — ~1 сессия

По [05_EDITOR_GUIDE.md](05_EDITOR_GUIDE.md) §2–3:

- [ ] BP_TacticsGameInstance (наследник `UTacticsGameInstance`): задать
  `HubLevel`, `MainMenuLevel`; прописать в Project Settings
- [ ] WBP_PrimaryGameLayout (4 стека с точными именами)
- [ ] WBP_MainMenu / WBP_DifficultySelect / WBP_Settings / WBP_About /
  WBP_PauseMenu (визуал по 02_LORE_SCRIPT §3)
- [ ] BP_MenuPlayerController + GM_MainMenu + уровень L_MainMenu
- [ ] Прогон: меню → сложность → (пока пустой) хаб; Продолжить активна после сейва

**DoD:** из главного меню доступны все экраны, «Новая игра» создаёт сейв и
уводит на L_Hub (пусть пока пустой), Esc-пауза работает в игровом уровне.

## Этап 4 — Юниты: BP-классы и анимации — ~2 сессии

- [ ] ABP_Soldier: Idle/Move (Rifle Walk/Jog), слоты монтажей
  Fire/HitReact/Death, поза Downed (лежит)
- [ ] **Стойки укрытий**: ABP читает `BestCoverAround`/`OnCoverStateChanged` →
  half cover = **crouch** (GASP crouch idle/walk), full cover = стоя у стены,
  открыт = боевая стойка; Overwatch = ADS-поза (GDD §12.1)
- [ ] BP_Unit_Assault/Sniper/Healer/Tank (наследники `AUnit_*`): меш Manny/Quinn,
  винтовка в сокет, ABP, статы по GDD §7 (HP/aim/дальность), TeamId=1,
  `AIControllerClass = AUnitAIController`
- [ ] BP_Unit_Marauder (враг, `AUnitBase`): TeamId=2, статы GDD §10
- [ ] Монтажи: AM_Fire, AM_HitReact, AM_Death (+ хук из BP-событий
  OnShotFired/OnDied/OnReactionShot)
- [ ] HUD юнита: пипсы AP + иконка укрытия/статуса в WBP над головой
  (расширить существующий WBP UnitHUD)

**DoD:** на тестовой карте 4 бойца и 2 врага стреляют с анимацией и звуком,
у half-cover юнит приседает, у full — стоит у стены, смерть/Downed
проигрываются, HP/AP видны над головами.

## Этап 5 — Способности классов (C++ P1 + BP-обвязка) — ~1–2 сессии

- [ ] `UGA_HunkerDown`, `UGA_Heal`, `UGA_RunAndGun`, `UGA_Taunt`, Squadsight,
  штраф overwatch (03_CODE_OVERVIEW §3 P1)
- [ ] BP-наследники GA с иконками/звуком, добавлены в `ClassAbilities` юнитов
- [ ] Кнопки способностей в боевом HUD (этап 6 виджет — заглушка уже тут)

**DoD:** каждая способность демонстрируема в PIE и соответствует числам GDD §7.

## Этап 6 — Боевой HUD — ~1 сессия

- [ ] WBP_TacticalHUD (на C++ базе): портреты 1–4 (HP/AP/статус, клик-выбор),
  панель действий, шанс попадания у курсора/цели, индикатор фазы и хода,
  кнопка «Завершить ход», счётчик врагов
- [ ] Декаль радиуса перемещения у выбранного юнита
- [ ] Всплывающие «−25 / ПРОМАХ / НАБЛЮДЕНИЕ!» (P2, если успеваем)

**DoD:** весь бой играется мышью по HUD, без клавиатуры (хоткеи — бонус).

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

## Этап 8 — Полишинг и баланс — ~1–2 сессии

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
