# Бриф: главное меню и экран настроек

> **Статус: выполнено (2026-08-02).** Экраны собраны, цикл проверен PIE —
> см. [STATUS_MainMenu_UI.md](STATUS_MainMenu_UI.md) и
> [../09_UI_HUD.md](../09_UI_HUD.md) §5. Бриф оставлен как постановка задачи.
> Фактические имена ассетов: `WBP_Settings`, `WBP_AboutMenuWidget`,
> `WBP_PauseMenuWidget` (в таблице ниже — исходные рабочие названия).

Самодостаточное задание для агента. Проект — UE 5.7, модуль `XRU1`, UI на
**CommonUI**. Сборка `.\Build-XRU1.ps1 -StopEditor` при закрытом редакторе.

## 1. Что должно получиться

Уровень `L_MainMenu` с рабочим циклом экранов:

```
MainMenu ──Продолжить──────────────────────────► L_Hub
    ├────Новая игра──► DifficultySelect ──► Intro ──► L_Hub
    ├────Настройки──► SettingsMenu (звук + изображение)
    ├────Об авторе──► AboutMenu
    └────Выход
```

Плюс экран паузы в бою с теми же настройками.

## 2. Что уже есть в C++ (НЕ писать заново)

Весь каркас лежит в [MenuWidgets.h](../../Source/XRU1/UI/Menus/MenuWidgets.h).
Задача агента — **только Blueprint-наследники и вёрстка**.

| Класс | Готовый API |
|---|---|
| `UMenuScreenBase` | `RequestBack()`, `PushScreen(Class)`, `GetUITheme()` |
| `UMainMenuWidget` | `CanContinue()`, `RequestContinue/NewGame/Settings/About/Quit()`, поля `SettingsScreenClass`/`AboutScreenClass`/`DifficultyScreenClass` |
| `USettingsMenuWidget` | `GetAudioSettings()`, `GetVideoSettings()`, `ApplyAudioSettings(New, bSave)`, `ApplyVideoSettings(New, bSave)`, `ResetToDefaults()` |
| `UDifficultySelectWidget` | `ChooseDifficulty(EDifficultyLevel)`, поле `IntroScreenClass` |
| `UIntroPlayerWidget` | `FinishIntro()` |
| `UPauseMenuWidget` | `RequestResume()`, `RequestReturnToMenu()` |
| `UAboutMenuWidget` | поля `AuthorName`, `ProjectInfo` |

Структуры настроек — [TacticsAudioTypes.h](../../Source/XRU1/Audio/TacticsAudioTypes.h):

- `FTacticsAudioSettings`: `MasterVolume`, `MusicVolume`, `SfxVolume`, `UIVolume`, `VoiceVolume` (все 0..1);
- `FTacticsVideoSettings`: `ScalabilityLevel` (0..3), `ResolutionScale` (0.25..1), `bFullscreen`, `bVSync`.

Звуки интерфейса: `UTacticsAudioSubsystem::PlayUIHover/PlayUIClick/PlayUIDenied`.
Единая тема оформления: `GetUITheme()` → `UTacticalHUDStyleData` (палитра, шрифты,
иконки). **Цвета и размеры брать оттуда, не хардкодить в каждом WBP.**

## 3. Что собрать в редакторе

### 3.1 Экраны (`/Game/XRU1Game/UI/Menus/`)

| Ассет | Родитель |
|---|---|
| `WBP_MainMenu` | `UMainMenuWidget` |
| `WBP_SettingsMenu` | `USettingsMenuWidget` |
| `WBP_AboutMenu` | `UAboutMenuWidget` |
| `WBP_DifficultySelect` | `UDifficultySelectWidget` |
| `WBP_IntroPlayer` | `UIntroPlayerWidget` |
| `WBP_PauseMenu` | `UPauseMenuWidget` |

В `WBP_MainMenu` заполнить поля `SettingsScreenClass`, `AboutScreenClass`,
`DifficultyScreenClass`. Кнопка «Продолжить» — `IsEnabled` биндится на
`CanContinue()`.

### 3.2 `WBP_SettingsMenu` — главное содержательное место

Две вкладки/секции.

**Звук.** Пять слайдеров (0..1) с подписями: Общая, Музыка, Эффекты, Интерфейс,
Голос.

- `OnInitialized` / `OnActivated`: прочитать `GetAudioSettings()` и расставить
  слайдеры.
- `OnValueChanged` каждого слайдера: собрать структуру целиком и вызвать
  `ApplyAudioSettings(New, bSaveToSlot = false)`. **Слышимый результат обязан
  появляться во время перетаскивания**, иначе настроить громкость на слух нельзя.
- `OnMouseCaptureEnd` (отпустили слайдер): тот же вызов с `bSaveToSlot = true`.
  Так слот кампании не переписывается на каждый кадр перетаскивания.

**Изображение.** Выпадающий список качества (Низкое/Среднее/Высокое/Эпическое →
0..3), слайдер `ResolutionScale`, чекбоксы полноэкранного режима и вертикальной
синхронизации. Применение — по кнопке «Применить» через `ApplyVideoSettings`.

Кнопки «Сбросить» (`ResetToDefaults()` + перечитать значения в контролы) и
«Назад» (`RequestBack()`).

⚠️ Виджет **не хранит** состояние в своих переменных. Единственный источник
правды — `UTacticsUserSettings` (`GameUserSettings.ini`); слот кампании настройки
НЕ хранит (решение 2026-08-02, [../09_UI_HUD.md](../09_UI_HUD.md) §5.5). Любая
локальная копия рано или поздно разъедется с реальной громкостью.

### 3.3 Уровень `L_MainMenu` (`/Game/XRU1Game/Maps/L_MainMenu`)

- `GM_MainMenu` с `AMenuPlayerController`
  ([MenuPlayerController.h](../../Source/XRU1/UI/Menus/MenuPlayerController.h)),
  который пушит `WBP_MainMenu` на слой `Menu`.
- Задник: статичная сцена или `WBP` с изображением из `UITheme`.
- Прописать `MainMenuLevel = L_MainMenu` в `BP_TacticsGameInstance`.
- В `Project Settings → Maps & Modes` поставить `L_MainMenu` как
  `GameDefaultMap` **в самом конце работы**, когда цикл экранов проверен.

### 3.4 Пауза в бою

`ATacticalPlayerController::PauseMenuClass` = `WBP_PauseMenu`. Внутри —
кнопки «Продолжить», «Настройки» (пуш того же `WBP_SettingsMenu`),
«В главное меню».

## 4. Обязательные проверки

- [ ] «Продолжить» неактивна без сохранения и активна после создания кампании.
- [ ] Слайдер громкости слышимо меняет звук во время перетаскивания.
- [ ] Настройки переживают выход в главное меню и возврат: значения на экране
      совпадают с реально звучащими.
- [ ] Смена качества изображения реально применяется (видно по кадру/FPS).
- [ ] Выбор сложности создаёт кампанию и уводит в интро/хаб.
- [ ] «Назад» с любого экрана возвращает на предыдущий, стек не растёт.
- [ ] Esc в бою открывает паузу; «Продолжить» снимает её без залипания курсора.
- [ ] Проверить 1920×1080 и 1920×1200: ничего не обрезается.

## 5. Чего НЕ делать

- Не добавлять в C++ новые функции настроек — весь нужный API уже есть.
- Не хранить громкость в переменных виджета: звук идёт через
  `UTacticsAudioSubsystem`, состояние — в `UTacticsUserSettings`.
- Не хардкодить цвета/шрифты мимо `UTacticalHUDStyleData`.
- Не менять `GameDefaultMap` до того, как весь цикл экранов проверен вживую.
