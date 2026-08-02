# UI и боевой HUD

Актуально на 2026-08-01. Документ хранит архитектуру и открытый остаток, а не
старые инструкции по сборке отдельных WBP.

## 1. Источник темы

Единый источник визуальных параметров —
`/Game/XRU1Game/Data/DA_TacticalHUDStyle` (`UTacticalHUDStyleData`). Глобальная
ссылка находится в `BP_TacticsGameInstance.UITheme`.

В Data Asset хранятся:

- action/status/class/cover icons;
- портреты;
- фоны, briefing/result art и intro media;
- палитры состояний кнопок и текста;
- размеры, padding и подложки HUD-блоков;
- CommonUI style classes.

WBP не должен дублировать эти значения, кроме чистого layout конкретного
экрана. Новый визуальный параметр сначала добавляется в тему/структуру C++.

## 2. Что уже работает

Функциональное ядро `WBP_TacticalHUD`:

- карточки отряда с HP/AP, выбором, cover и gameplay status;
- панель общих и классовых действий;
- target panel с HP, шансом попадания, LOS и shield/flanked;
- индикатор фазы/хода и кнопка завершения хода;
- enemy counter; C++ уже предоставляет безопасный для тумана войны
  `GetVisibleEnemyCount`, но WBP ещё нужно перевести с полного alive count;
- move range/path preview;
- overhead HP/AP, cover и status widgets; HP-бар секционный (C++
  `UAttributeBarWidget.bSegmented`, у `UHealthBarWidget` включён по умолчанию,
  секция = 10 HP);
- click-selection и обновление после AP, смерти/эвакуации;
- единый command arbiter блокирует недопустимые действия/targeting conflicts;
- floating боевой фидбек (2026-08-01): `UCombatFeedbackSubsystem` + Slate-оверлей
  рисуют урон, «+лечение», `ПРОМАХ` и `НАБЛЮДЕНИЕ` над юнитами; вызовы — из
  подтверждённых точек механики (`ResolveShotMechanics`, `UGA_Heal`,
  `UGA_Overwatch`), настройки — тема «08. Боевой фидбек»;
- карточка действующего врага (2026-08-01): в фазу Enemy `UTacticalHUDWidget`
  по `OnEnemyUnitActivated` показывает в SquadPanel карточку активного врага
  (HitTestInvisible, скрытые туманом враги отфильтрованы через
  `UFogOfWarSubsystem`).

Это считается готовой базой. Не пересобирать её по старым чек-листам.

## 3. Архитектура

| Слой | Ответственность |
|---|---|
| `UTacticalHUDWidget` | подписки на controller/turn manager, данные и доступность команд |
| Unit/attribute widgets | отображение конкретного HP/AP/status/cover |
| `UTacticalHUDStyleData` | визуальная тема и размеры |
| `WBP_*` | Designer layout, анимация появления, локальная presentation-логика |
| `ATacticalPlayerController` | targeting mode, hover, selected unit и команды |

HUD читает состояние и отправляет команды через controller API. Он не списывает
AP, не вычисляет шанс и не завершает ability самостоятельно.

## 4. Открытый боевой HUD

- [ ] Перевести player-facing enemy counter на `GetVisibleEnemyCount`; полный
      `GetAliveEnemyCount` оставить только внутренним условием конца боя.
- [ ] Пропускать enemy hover, target panel, overhead widgets, outline/custom
      depth, attack target list и camera focus через `UFogOfWarSubsystem`.
- [x] Floating feedback: урон/лечение, `ПРОМАХ`, `НАБЛЮДЕНИЕ` (2026-08-01,
      `UCombatFeedbackSubsystem`). Открыто: floating «недоступность» по отказу
      команды.
- [x] Понятная карточка действующего врага в его фазу без возможности клика
      (2026-08-01). Открыто: PIE-проверка на туториале.
- [ ] Причина disabled action рядом с кнопкой/tooltip, а не только в Output Log.
- [ ] Секционный HP в карточке отряда: `WBP_UnitPortrait` рисует HP своим
      ProgressBar в BP — заменить на child `WBP_UnitHealthBar` (секции придут
      сами из C++).
- [ ] Отдельный feedback зарядов/кулдауна классовой ability.
- [ ] Цель миссии, таймер и evacuation state на tutorial/mission maps.
- [ ] Проверка layout в 1920×1080, 2560×1440 и 16:10; safe margins.
- [ ] Единые transition/hover/pressed states из CommonUI styles.
- [ ] Проверка контраста overhead icons на светлом/тёмном окружении.

## 5. Меню и экраны

Обязательный пользовательский цикл GDD:

```text
Main Menu → New Game/Difficulty → Intro → Hub
→ Tutorial Briefing → Tutorial → Result → Hub
→ Mission Briefing → Mission01 → Result/DemoComplete → Menu
```

Состояние 2026-08-01: вёрстка всех экранов меню собрана программно
(`UXRU1WidgetAuthoringLibrary`, см. [agents/AGENT_UNREAL_TOOLING.md](agents/AGENT_UNREAL_TOOLING.md) §5.2.3),
обработчики кнопок/слайдеров привязываются в C++ `NativeOnInitialized` по
каноничным именам `Btn_*`/`Sld_*`/`Chk_*`/`Cmb_*`/`Txt_*` — графы WBP пустые,
дизайнер может свободно менять раскладку, сохраняя имена.

- [x] Main Menu, difficulty (вёрстка пользователя), settings, about — вёрстка и
      логика; ссылки Class Defaults проставлены.
- [x] `WBP_IntroPlayer` со skip-переходом (полноэкранная прозрачная кнопка).
      Открыто: MediaPlayer-видео вместо статичного фона.
- [x] `WBP_MissionResult`: victory/defeat/timeout и DemoComplete-вариант
      (победа в Kind=Mission), арт из темы; назначен в
      `GM_Tactics.MissionResultWidgetClass`.
- [x] `WBP_POIPopup` (Txt_Title/Txt_Description/Txt_Locked + C++
      `UPOIPopupWidget::SetupFromPOI`). Открыто: сам `L_Hub` и назначение
      `PopupWidgetClass` на расставленных POI.
- [x] Пауза: `WBP_PauseMenuWidget` (Resume/Settings/ReturnToMenu),
      `SettingsScreenClass` → `WBP_Settings`.
- [ ] Mission briefing перед стартом сценария (сейчас его роль играет POI popup).
- [ ] PIE-проверка полного цикла экранов по чек-листу
      [agents/BRIEF_MainMenu.md](agents/BRIEF_MainMenu.md) §4; только после неё —
      `GameDefaultMap = L_MainMenu`.
- [ ] Continue/save routing через `UTacticsGameInstance`/`UTacticsSaveGame`
      (код есть; проверить вживую «Продолжить» после созданной кампании).

## 6. Правила внесения изменений

1. Механические данные добавляются в C++ API/widget base.
2. Общий визуальный параметр добавляется в `UTacticalHUDStyleData`.
3. WBP только размещает widgets и вызывает готовое API.
4. Стабильные widget names совпадают с `BindWidget`/`BindWidgetOptional`.
5. После правки: Compile/Save, read-back графа и один PIE-сценарий состояния.
6. Player-facing данные о враге сначала проходят visibility gate из
   [10_FOG_OF_WAR.md](10_FOG_OF_WAR.md). Визуальный post-process не считается
   защитой от утечки информации.

Нельзя:

- считать chance/LOS/cover в WBP;
- списывать AP из `OnClicked`;
- вызывать `GetAliveEnemyCount` для отображаемого игроку счётчика;
- показывать target/overhead/outline скрытого enemy actor;
- хранить отдельные копии status priority в карточке и overhead widget;
- напрямую переходить между уровнями из каждой кнопки в обход GameInstance/UI
  routing.

## 7. Критерии готовности

HUD/экраны приняты, когда:

- игрок понимает текущую фазу, активного юнита, AP, цель и исход действия без
  Output Log;
- все обязательные команды доступны мышью;
- одинаковое состояние имеет одинаковую иконку/цвет во всех местах;
- layout не перекрывается на целевых разрешениях;
- все экраны образуют непрерывный цикл из раздела 5;
- art/text/styles меняются через тему, без массовой правки WBP.
