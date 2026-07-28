# Состояние проекта и передача между машинами

> Обновлено: **2026-07-28**. E1.7–E1.8 физически собраны: `ABP_Solider`,
> Inertialization/Default Slot, пять монтажей, ability BP и unit hooks. Глобальная
> MCP-проверка прочитала 689 ассетов и все 36 BP/94 графа, но первый PIE-прогон
> выявил системные гонки StepOut/fire/death/cover-turn и старый enemy route.
> Текущий источник истины и порядок исправления —
> [`16_UNREAL_MCP_TECH_AUDIT.md`](16_UNREAL_MCP_TECH_AUDIT.md); E1.10 не принят.
>
> **Последний факт:** C++ A16 собран успешно дважды на UE 5.7. Сохранённые
> fire/death BP ещё старые, поэтому выстрел зависает до watchdog и AP
> возвращается. Начинать не с нового C++, а с
> [`17_MANUAL_EDITOR_CHECKLIST.md`](17_MANUAL_EDITOR_CHECKLIST.md).

## Где мы сейчас

**Базовое ядро боя было принято вживую** (PIE-прогоны 2026-07-21): перемещение,
зона хода, флоу атаки (пробел-двухтакт, камера «из-за плеча», Tab по целям),
процентовка (дистанция/высота/укрытие/LOS), автопереход хода, AI с укрытиями
(базовый). Исходные функциональные проверки этапа 6 закрыты. Интеграция
анимаций 2026-07-27/28 добавила новый action flow и выявила гонки, поэтому эта
ранняя приёмка не распространяется на StepOut, fire timing, death и enemy route.

**Что дальше — отработка геймплея на стартовой локации `Lvl_TopDown`** перед
уровнями и меню. Карта готовности и разрыв с XCOM 2 — в
**[`09_GAMEPLAY_STATUS.md`](09_GAMEPLAY_STATUS.md)** (там же блоки доработки:
AI, укрытия/peek, анимации, slow-mo реакций, числа урона, звук, добить HUD).

Вся логика боя — в C++ (`Source/XRU1/Tactics`, `.../UI`). Источник правды
модального режима — `EPlayerTargetingMode`; доступности любой команды —
`CanIssueCommand(ETacticalPlayerCommand)`; последняя защита уже выполняющихся
GA — общий тег `Ability.TacticalAction` (см. `03_CODE_OVERVIEW.md §2.6`).

**Параллельный UI-трек:** добавлены `TU_Intro.mp4`/`FMS_TU_Intro`, 5 портретов,
иконки и 7 фонов/брифингов. `UTacticalHUDStyleData` расширен до единой темы,
глобальная ссылка — `BP_TacticsGameInstance.UITheme`; яркость PNG-кнопок теперь
управляется отдельными state palettes. DA уже заполнен текущим артом; точечное
подключение WBP — строго по
[`10_UI_THEME_GUIDE.md`](10_UI_THEME_GUIDE.md).

## Ближайший шаг — ручная BP-интеграция готового C++ A16

Не добавлять прямой `ResolveShot` или Delay. C++ уже держит action до terminal,
блокирует turn/selection, разделяет eye/root, синхронизирует notify commit,
владеет Overwatch reaction и death pose. Теперь вручную: два Branching Point,
два новых pre-presentation BP-графа и callbacks пяти Death Montage — строго по
документу 17. Затем PIE-матрица; только после неё полировка.

Ниже сохранён статус параллельного UI-трека; он не блокирует A16-P1.

В `WBP_UnitPortrait.Refresh` портреты, классы, статусы и рамка подключены;
пользователь вручную подтвердил четыре класса и матрицу статусов. В C++ от
2026-07-23 добавлены: `CoverSizeBox → CoverIcon` карточки, нативный
`UUnitStatusIconWidget` над головой, автослот и независимые size/padding в
`UUnitHUDLayoutData`; оба используют один `DA_TacticalHUDStyle`.

Полный build/restart для `UUnitStatusIconWidget`, камеры, арбитра команд и
счётчика завершился успешно. Пользователь подтвердил одновременные cover +
status в карточке и над головой, контрастную подложку status PNG,
`T_Icon_EnemyCount` с отдельной подложкой, сохранение Q/E при смене бойца и
блокировку Overwatch внутри Attack-targeting.

В `WBP_TacticalHUD` уже используются стабильные имена `EnemyCountIcon` и
`EnemyCounterBackground` (**Is Variable**). Внешний `EnemyCounterBorder`
отвечает за layout, внутренний `EnemyCounterBackground` — за texture/color/
padding из темы. Дополнительный Blueprint graph для этих свойств не нужен.

После этого перевести пять готовых меню на тему по
  `10_UI_THEME_GUIDE.md` и создать отсутствующие Intro/CommonUI styles/menu
  level. `/Game/XRU1Game/Data/DA_TacticalHUDStyle` уже заполнен текущими
  текстурами и назначен в `BP_TacticsGameInstance.UITheme`.

## С ЧЕГО НАЧАТЬ СЛЕДУЮЩУЮ СЕССИЮ (2026-07-28)

1. Выполнить `17_MANUAL_EDITOR_CHECKLIST.md` §§1–4.
2. Compile/Save/reopen каждого BP; затем короткая UE 5.7 build.
3. Пройти PIE-матрицу документа 17 §6 и аудита 16 §7.
4. Только после этого возвращаться к A6/scamper/Ф11/Ф12 и UI polish.

---

Для геймплейного трека приоритет — **PIE-приёмка сессии 2026-07-25** (ничего из
неё в игре не проверялось, только сборка):

- **Укрытия/фланг** — цикл 17: юниты больше не считаются укрытием, луч укрытия
  обрезается дистанцией до стрелка. Проверять с `xru1.Cover.Debug 1`.
- **Камера прицеливания** переписана (цикл 17). ⚠️ Поля `Tactics|Camera|Shot` в
  `BP_TacticalCameraPawn` заменены целиком — старые переопределения отпали.
- **Овервотч на движение** (W1) и **оборонительные оценщики AI** (W2).
- ~~**Боты больше не ходят гуськом**~~ — superseded аудитом/PIE 2026-07-28:
  enemy всё ещё использует `MoveToLocation`, в узком месте упирается в союзника.
  Исправление — A16-P3.
- **Полный цикл наблюдения** (цикл 25): реакция ПРЕРЫВАЕТ бегущего, мир
  замедляется 0.66, наблюдатели стреляют по очереди.
- **Провокация танка**: радиус 1200 → 2500 см (был вдвое короче дистанции боя).
- **Камера**: verbatim-скоринг `X2Camera_OverTheShoulder`, настоящий OTS у
  плеча (85 см), правило 180° против разворотов.
- **S2 закрыт**: `FUnitVisualState` — единая точка состояния для ABP.

Полный чеклист приёмки — тест-матрица §V.1 в `11_COVER_AND_ENEMY_PLAN.md`.
Диагностика в PIE: `xru1.AI.LogCombat 1`, `xru1.LOS.Debug 1`, `xru1.Cover.Debug 1`.

После A16-P1–A16-P3 — `13_AI_STATE_MACHINE_PLAN.md`: **A7** (`SafeToMove`), **A8**
(лимит атакующих, scamper), **A6** (профили весов). S2/Ф10 физически собраны,
но не приняты из-за найденных гонок.

Отдельно по контенту: **уступы разной высоты на `Lvl_TopDown`** — проверить
бонус/штраф высоты (±20). Целевой дизайн укрытий: `WorldStatic` 60 см = Half,
150 см = Full (высоты считаются от ПОЛА — Ф2 закрыта).

---

## Переезд на другую машину

Пути НЕ хардкодить (см. `CLAUDE.md`): машина 1 — `D:/UE5/UnrealProjects/XRU1`,
машина 2 — `D:/Unrial_Projects/XRU1`; движок ищется через реестр.

```powershell
git pull
git lfs pull                      # бинарники ассетов
.\Build-XRU1.ps1                  # редактор должен быть ЗАКРЫТ
```
Затем открыть `XRU1.uproject`. Для MCP-моста к редактору (UnrealClaude):
`npm install` в `Plugins/UnrealClaude/Resources/mcp-bridge/` — если ещё не
делали на этой машине. При отвале stdio-моста от сессии Claude Code HTTP-сервер
плагина доступен напрямую: `POST http://localhost:3000/mcp/tool/{toolname}`.

Диагностические cvar'ы: `xru1.AI.LogCombat 1` (решения AI в бою),
`xru1.LOS.Debug 1` (огневые позиции и стороны укрытия),
`xru1.Cover.Debug 1` (что именно засчиталось стеной против стрелка),
`xru1.MoveRange.LogBuildTime 1` (стоимость построения зоны хода).

**codebase-memory индекс привязан к пути и на каждой машине свой** —
переиндексировать: `index_repository` с `repo_path` = корень проекта.
