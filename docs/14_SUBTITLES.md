# 14. Субтитры и локализация: исследование и принятая архитектура

Статус: **C++-слой реализован и собран; остаток — контент в редакторе (§8).**
Документ прошёл два независимых прохода исследования; второй проход уточнил
факты о паузе, таймингах обучения и хранении настроек — расхождения с первым
проходом отмечены явно в §3.4 и §7.

Задача: один самодостаточный слой субтитров, который обслуживает обучение
(с теми же таймингами, что сейчас), любую озвучку в проекте и интро-ролик, а
позже переключается на английский вместе со всем текстом игры.

Рамки, заданные постановкой:

- **закрытые субтитры (CC) для неречевых звуков** («выстрел справа») — **не
  делаем**; поле канала в модели данных остаётся, машинерия — нет;
- **цвет по говорящему и пользовательская настройка цвета** — не первостепенно;
- **интро** — массив строк с привязкой ко времени;
- **локализация** — только переключение языка;
- **позиция** — чуть выше нижнего края экрана, со своей подложкой; в бою — чуть
  выше иконок способностей.

---

## 1. Исходное состояние XRU1

### 1.1. Субтитры существуют только в обучении

| Звено | Файл | Роль |
|---|---|---|
| Данные такта | [TutorialPresentation.h:17](../Source/XRU1/Tactics/TutorialPresentation.h#L17) | `FTacticalTutorialBeat`: `Speaker`, `Subtitle`, `Voice`, `Duration` + ответная реплика (`FollowUp*`) |
| Часы такта | [TacticalQuestTasks.cpp](../Source/XRU1/Tactics/TacticalQuestTasks.cpp) → `FTacticalTask_TutorialBeat::Tick` | `Inst.ElapsedTime += DeltaTime`, сравнение с `Duration`/`FollowUpDuration`, `ConsumeSkipRequest()`, переход к ответной реплике, `FinishBeat()` |
| Шина | [TutorialPresentation.cpp:41](../Source/XRU1/Tactics/TutorialPresentation.cpp#L41) | `UTutorialPresentationSubsystem::StartBeat/FinishBeat`: играет голос, наводит камеру, рассылает `OnBeatStarted/Finished` |
| Кэш «что показано» | [TacticalPlayerController.h:184](../Source/XRU1/Tactics/TacticalPlayerController.h#L184) | `GetTutorialBeatSubtitle()` |
| Рендер | [TutorialHintOverlay.cpp:84](../Source/XRU1/Tactics/TutorialHintOverlay.cpp#L84) | слот Slate-оверлея подсказок; оверлей ставится в viewport с `ZOrder = 8` из боевого контроллера |

**Ключевой факт для проектирования:** тайминг реплики обучения принадлежит
**задаче StateTree**, а не презентации. `StartBeat`/`FinishBeat` — это уже
готовые «показать»/«убрать», вызываемые ровно в нужные моменты, включая
ответную реплику (`StartBeat` вызывается второй раз с подменёнными полями) и
пропуск игроком. Значит новый слой **не должен заводить собственные часы для
обучения** — он обязан подписаться на эти две точки. Совпадение таймингов
получается по построению, а не «настройкой похожих чисел».

### 1.2. Где озвучка звучит без субтитров

| Место | Код |
|---|---|
| Интро-ролик | `UIntroPlayerWidget` ([MenuWidgets.h:357](../Source/XRU1/UI/Menus/MenuWidgets.h#L357)), титров в видео нет |
| Брифинг миссии | [MissionBriefingWidget.cpp:43](../Source/XRU1/UI/Menus/MissionBriefingWidget.cpp#L43) |
| Экран результата | [MissionResultWidget.cpp:71](../Source/XRU1/UI/Menus/MissionResultWidget.cpp#L71) |
| Прибытие в хаб | `UTacticsAudioSubsystem::PlayHubArrivalVoiceOnce()` |

Все три «нероликовые» точки идут через **один** метод —
`UTacticsAudioSubsystem::PlayVoice2D` ([TacticsAudioSubsystem.cpp:497](../Source/XRU1/Audio/TacticsAudioSubsystem.cpp#L497)).
Это единственная точка входа голоса в проекте, и у неё уже есть два свойства,
которые архитектурно определяют весь слой:

1. **Голос ровно один**: `VoiceComponent` — одиночный указатель, новая реплика
   останавливает предыдущую;
2. **Голос ставится на паузу вместе с миром**:
   `Voice->SetPaused(bPaused)` ([TacticsAudioSubsystem.cpp:351](../Source/XRU1/Audio/TacticsAudioSubsystem.cpp#L351)).

### 1.3. Чего нет

- Локализации: нет `Config/Localization/*`, нет `Content/Localization/*`, нет
  культур в ini. В коде 88 `LOCTEXT` (собираются) и 12 `FText::FromString`
  (в перевод не попадут — подлежат аудиту).
- Настроек субтитров и языка в `UTacticsUserSettings` и на экране настроек.
- Любого показа речи вне боевого мира.

### 1.4. Что уже готово и работает на нас

- `UPrimaryGameLayout` (4 стека CommonUI) создаётся **во всех мирах**: меню,
  хаб, бой ([MenuPlayerController.cpp:31](../Source/XRU1/UI/Menus/MenuPlayerController.cpp#L31),
  [HubPlayerController.cpp:74](../Source/XRU1/Hub/HubPlayerController.cpp#L74),
  [TacticalPlayerController.cpp:99](../Source/XRU1/Tactics/TacticalPlayerController.cpp#L99)),
  переживает travel и умеет прятать игровой слой (`SetGameLayerHidden`).
- `UTacticsUserSettings : UGameUserSettings` — единственный источник правды
  пользовательских настроек.
- `UTacticalHUDStyleData` — единая тема UI с нумерованными секциями.
- `UGamePauseSubsystem` — стек причин паузы; паузу держат **экраны меню**
  (`Menu.<ИмяВиджета>`) и потеря фокуса окном.
- `UDialogueSubsystem` из STQuestSystem (перенесён, простаивает) — готовая шина
  реплик `OnLine(FText, SpeakerId)` с приёмом `ReplayCurrent()`.

---

## 2. Что предлагает движок UE 5.7

### 2.1. Легаси `FSubtitleManager`

`FSubtitleCue` на `USoundWave`, очередь по приоритету, рендер на Canvas из
`GameViewportClient`. Ценная деталь, проверенная в коде
(`SubtitleManager.cpp:421`): если подписан делегат `OnSetSubtitleText`, движок
**полностью переключается на него** и Canvas не рисует ничего (`Prioritize using
display objects over the canvas`). Именно так работает Lyra. Побочный факт:
в этой ветке проверка `GEngine->bSubtitlesEnabled` **не выполняется** — гасить
субтитры обязан сам подписчик.

### 2.2. Плагин `SubtitlesAndClosedCaptions` (Experimental, выключен по умолчанию)

Первая попытка Epic отвязать субтитры от звука. Разбор:

| Класс | Что делает |
|---|---|
| `FSubtitleAssetData` | `FText Text`, `Duration`, `SubtitleDurationType` (`UseSoundDuration`/`UseDurationProperty`), `StartOffset`, `Priority`, `ESubtitleType` (`Subtitle`/`ClosedCaption`/`AudioDescription`) |
| `USubtitleAssetUserData : UAssetUserData` | Массив строк, вешается на любой ассет (в первую очередь на звук) |
| `USubtitlesSubsystem : UWorldSubsystem` | Очередь по приоритету, таймеры мира, отложенный старт, владение виджетом |
| `USubtitleWidget : UUserWidget` | Три `UTextBlock` + три `UBorder` (по каналу) |
| `USubtitlesAudioSubsystem : UAudioEngineSubsystem` | Автоочередь субтитров при старте любого `FActiveSound`; `au.UseNewSubtitles 1` добавляет `UDialogueWave` |
| `UMovieSceneSubtitleTrack` | Трек в Sequencer со скрабом и time dilation |
| `FSubtitlesAndClosedCaptionsDelegates` | Статический фасад (`QueueSubtitle`/`StopSubtitle`/…) — источники не знают про подсистему |

Полезные идеи, которые мы забираем в свою модель: субтитр как самостоятельная
единица данных; `ExternallyTimed` (владелец сам снимает строку); длительность
«от звука» либо «своя»; текст, живущий на ассете озвучки; фасад вместо прямой
зависимости от подсистемы.

### 2.3. `UDialogueWave`

Канонический VO-ассет движка: `SpokenText` + `SubtitleOverride` + контексты
(говорящий/слушатели) + **локализованный `USoundWave` на культуру**,
`GetLocalizedSubtitle()`, отдельная ветка пайплайна локализации
(`*_ExportDialogueScript.ini`). Правильная модель «озвучка+субтитр+перевод в
одном ассете», но избыточная для четырёх говорящих: требует `UDialogueVoice` на
каждого и отдельного импорта скриптов. **Не берём**, но модель данных держим
совместимой на случай будущего перехода.

### 2.4. Медиа: чем таймить титры ролика

- `UMediaPlayer` умеет перечислять/выбирать caption-треки
  (`EMediaPlayerTrack::Caption`), плагин `ElectraSubtitles` парсит TTML/WebVTT/TX3G;
- **но текста наружу нет**: в публичном API `MediaAssets` нет `GetOverlays()`,
  оверлей-сэмплы живут ниже (`IMediaOverlaySample`, `FMediaPlayerFacade`);
- зато есть `UMediaPlayer::GetTime()` и события `OnMediaOpened/OnEndReached`,
  которыми `UIntroPlayerWidget` уже пользуется
  ([MenuWidgets.cpp:481](../Source/XRU1/UI/Menus/MenuWidgets.cpp#L481)).

Вывод: титры интро ведём своим массивом строк с временами, синхронизируясь по
`GetTime()`. Дополнительный плюс — такие титры переводятся, а вшитые в mp4 нет.

---

## 3. Lyra: эталон Epic

Lyra экспериментальный плагин **не использует**. У неё отдельный плагин
`Plugins/GameSubtitles`:

| Класс | Роль |
|---|---|
| `USubtitleDisplayOptions : UDataAsset` | Вся визуальная схема: шрифт, карта размеров (`ExtraSmall…ExtraLarge`), цвета (`White`/`Yellow`), обводка (`None`/`Outline`/`DropShadow`), непрозрачность подложки (`Clear…Solid`), кисть фона |
| `FSubtitleFormat` | Выбор игрока: размер + цвет + обводка + фон |
| `USubtitleDisplaySubsystem : UGameInstanceSubsystem` | Хранит формат, рассылает `DisplayFormatChangedEvent` |
| `USubtitleDisplay : UWidget` | UMG-обёртка: `Format`, `Options`, `WrapTextAt`, `bPreviewMode`/`PreviewText` (превью в Designer) |
| `SSubtitleDisplay : SCompoundWidget` | `SBorder` + `SRichTextBlock`; в `Construct` подписывается на `FSubtitleManager::OnSetSubtitleText()`, **если не включён `ManualSubtitles`** |

Три вывода, применимые к нам:

1. **Рендерер отделён от источника** и имеет ручной режим (`ManualSubtitles` +
   `SetCurrentSubtitleText`) — штатная точка входа для роликов и любых
   нестандартных источников.
2. **Стиль — DataAsset + выбор игрока**, а не константы в коде.
3. **`SRichTextBlock`, а не `STextBlock`** — разметка внутри строки (имя
   говорящего другим начертанием) без второго виджета.

Настройки и язык — в `ULyraSettingsShared : ULocalPlayerSaveGame` (на игрока,
облако), отдельно от машинных `ULyraSettingsLocal : UGameUserSettings`.
Механику языка забираем целиком: **отложенная культура** (`PendingCulture`,
применяется по «Применить»), а персист — записью в ini:

```cpp
GConfig->SetString(TEXT("Internationalization"), TEXT("Culture"), *CultureToApply, GGameUserSettingsIni);
GConfig->Flush(false, GGameUserSettingsIni);
```

### 3.4. Уточнение к первому проходу: пауза

Первый проход утверждал, что «инструктаж останавливает мир, поэтому таймеры
мира ломают субтитры». Проверка кода это **не подтвердила**: паузу держат только
экраны меню (`UMenuScreenBase`, флаг `bPauseGameWhileActive`) и потеря фокуса
окном; такты обучения идут в непаузированном мире и тикают вместе со StateTree.

Правильный вывод — обратный: голос на паузе замирает
(`VoiceComponent->SetPaused`), значит **субтитр обязан замирать вместе с ним**.
Тайминг субтитров должен быть pause-**aware**, а не pause-**immune**: игровое
время и звуковые события — да, реальное время (`FApp::GetCurrentTime`) — нет.

---

## 4. Донор: готовый пайплайн локализации

Донор — не пример субтитров (их там нет), но лучший доступный пример
локализации, наполовину уже перенесённый к нам.

`Source/TopDownCST/Localization`:

- `UTDLocalizationSettings : UDeveloperSettings` — `AvailableCultures = {ru, en}`,
  `DefaultCulture`, `bApplyDefaultOnStartup`; появляется в Project Settings.
- `ULocalizationSubsystem : UGameInstanceSubsystem` — `SetLanguage` через
  `FInternationalization::SetCurrentLanguageAndLocale`, подписка на
  `FTextLocalizationManager::OnTextRevisionChangedEvent` → рассылка
  `OnLanguageChanged` **и повторная выдача текущей реплики**
  (`UDialogueSubsystem::ReplayCurrent()`), `FPolyglotTextData` (переводы в
  рантайме), `/Game/L10N/<culture>/…` и `SetAssetGroupCulture` — **культура
  ассетов отдельно от языка текста** (текст на английском, озвучка русская).

Пайплайн, которого у нас нет: `Config/Localization/Game_{Gather,Compile,Export,
Import,GenerateReports}.ini` + `Content/Localization/Game/{en,ru-RU}/…`
(`.manifest`, `.archive`, `.locres`).

Из донора берём три вещи: **скелет пайплайна**, `SetAssetGroupCulture` и приём
«переотправить активную реплику после смены языка». Демонстрационную часть
(polyglot / plural / gender) не переносим.

---

## 5. Отраслевые требования, влияющие на модель данных

Из рекомендаций по доступности (Game Accessibility Guidelines, Xbox
Accessibility Guidelines 104):

1. **Говорящий — отдельное поле**, а не часть текста: имя показывается при смене
   говорящего, отключается настройкой, переводится независимо.
2. **Размер и подложка настраиваются**; ориентир — не меньше ~18px@1080p для PC,
   контрастная подложка обязательна.
3. **Отдельный выключатель субтитров.**
4. Разделение «речь / подписи к звукам / аудиоописание» — у нас в рамках задачи
   реализуется только речь, поле канала остаётся под будущее.

---

## 6. Принятое решение

> **Собственный слой субтитров уровня GameInstance, с моделью данных,
> изоморфной `FSubtitleAssetData`, рендером внутри `UPrimaryGameLayout` и
> ПОЛНЫМ отказом от собственных часов там, где часы уже есть у источника.**

### 6.1. Почему не плагин Epic (проверенные факты, а не вкус)

| Факт | Последствие |
|---|---|
| `USubtitlesSubsystem` создаёт виджет и делает `AddToViewport()` без ZOrder (`SubtitlesSubsystem.cpp:324`), наш `UPrimaryGameLayout` тоже добавлен с ZOrder 0 | Субтитр окажется **под** экранами CommonUI. Интро, брифинг и результат — это экраны. Требование «субтитры к интро» плагином не выполняется без правки его кода |
| Новая строка отбрасывается, если активна строка того же типа с `Priority >= New` (`SubtitlesSubsystem.cpp:163`) | При дефолтном приоритете вторая реплика подряд (наш `FollowUp`) молча не покажется. Обход — принудительный `StopSubtitle` перед каждой строкой, то есть отказ от очереди плагина |
| `WorldSubsystem` | Пересоздаётся на каждом travel меню→хаб→бой; активная строка и подписки теряются |
| Нет поля говорящего, нет позиционных пресетов, нет пользовательских настроек (`SubtitleFontInfo` не заполняется) | Три из пяти наших требований пришлось бы всё равно писать самим |
| Модуль объявлен `"PlatformAllowList": ["Win64"]`, плагин `IsExperimentalVersion: true` | Зависимость от нестабильного API; если ассеты озвучки понесут `USubtitleAssetUserData`, поломка API станет поломкой **контента**, а не кода |
| Что он даёт нам полезного: автоочередь субтитров с ассета звука, трек в Sequencer, миграция легаси | У нас **одна** точка входа голоса (`PlayVoice2D`) — автоочередь экономит один вызов; Sequencer для VO не используется; легаси-субтитров нет |

Итог: плагин закрывает 20% нашей задачи и приносит два дефекта, каждый из
которых виден в первом же тесте. Свой слой — ~400 строк C++, без экспериментальных
зависимостей. Совместимость имён и полей с `FSubtitleAssetData` сохраняем, чтобы
переход на плагин (если он дойдёт до Production в 5.8/5.9) был механическим.

### 6.2. Инвариант №1: слой — дисплей, а не планировщик

Три способа задать жизнь строки, **выбираются источником**:

| Режим | Кто снимает строку | Кто им пользуется |
|---|---|---|
| `ShowLine(Line)` → `Handle`, `HideLine(Handle)` | сам источник | такт обучения (`StartBeat`/`FinishBeat` уже стоят в нужных местах), драйвер интро |
| `ShowLineForSound(Line, UAudioComponent*)` | `OnAudioFinished` компонента; `Stop()` тоже снимает | всё, что идёт через `PlayVoice2D`: брифинг, результат, хаб |
| `ShowLineForDuration(Line, Seconds)` | таймер **игрового** мира | строки без озвучки и без владельца (редкий случай) |

Почему это правильно:

- **тайминги обучения совпадают по построению** — никакого второго счётчика,
  который может разойтись с `Duration` такта; авторские такты A–D и порядок
  «реплика → ответ → пропуск» работают без единой правки StateTree;
- **пауза бесплатна**: в режиме «по звуку» слой вообще ничего не считает, а
  компонент замирает вместе с миром; в режиме «по длительности» таймер мира
  замирает так же;
- **обрыв бесплатен**: новая реплика останавливает предыдущий компонент — то же
  событие снимает и её субтитр.

### 6.3. Инвариант №2: одна строка на экране

Слой повторяет уже существующее правило звука: голос один
(`VoiceComponent` — одиночный), значит и субтитр один; новая строка вытесняет
предыдущую. Никакой очереди приоритетов — она в нашем сценарии не нужна и,
как показывает плагин Epic, порождает класс багов «строка молча не показалась».
Поле `Channel` в данных остаётся (`Dialogue` сейчас, `Caption` зарезервирован) —
если понадобятся подписи к звукам, добавится вторая независимая ячейка, а не
переписывание слоя.

### 6.4. Состав слоя

Каталог `Source/XRU1/Subtitles/` (самодостаточен, зависит только от Core/UMG):

| Файл | Класс | Ответственность |
|---|---|---|
| `SubtitleTypes.h` | `FXRU1SubtitleLine`, `FXRU1SubtitleHandle`, `FXRU1SubtitleStyle`, `FTacticsSubtitleSettings`, `EXRU1SubtitleChannel/TextSize/Backdrop` | Модель данных слоя: `Speaker` (FText), `Text` (FText), `Channel`, `bSkippable`, `SourceId` (FName) |
| `SubtitleSubsystem.h/.cpp` | `UXRU1SubtitleSubsystem : UGameInstanceSubsystem` | Три режима показа (§6.2), `HideLine`/`HideAll`, `GetActiveLine()`, `GetResolvedStyle()`, событие `OnLineChanged`, `RefreshDisplay()` для смены языка и позднего рождения UI |
| `SubtitleProjectSettings.h/.cpp` | `UXRU1SubtitleSettings : UDeveloperSettings` | Проектные знания: `bUseBuiltInDisplay`, `DisplayZOrder`, `AvailableCultures`, `DefaultCulture` |
| `SubtitleTrackDataAsset.h/.cpp` | `USubtitleTrackDataAsset : UDataAsset` | Массив `{StartTime, Duration, Speaker, Text}` — титры интро и любых будущих роликов; `Duration = 0` означает «до следующей реплики» |
| `MediaSubtitleDriver.h/.cpp` | `UMediaSubtitleDriver : UObject` | Ведёт трек по `UMediaPlayer::GetTime()` через core ticker, вызывает `ShowLine/HideLine`; `Stop()` снимает свою строку |
| `SoundSubtitleData.h` | `USoundSubtitleData : UAssetUserData` | `Speaker` + `Text` + `HoldAfterSound`, живущие **на самом ассете озвучки**; поля повторяют `FSubtitleAssetData` |
| `SubtitleOverlay.h/.cpp` | `SXRU1SubtitleOverlay : SCompoundWidget` | Рендер: имя говорящего, текст, подсказка пропуска в подложке; стиль целиком получает из `GetResolvedStyle()` |

Стиль — новая секция `09. Субтитры` в `UTacticalHUDStyleData` (шрифт, размеры
трёх ступеней, цвет текста, цвет и непрозрачность подложки, ширина переноса,
два отступа снизу). Отдельный DataAsset не заводим: субтитр — часть интерфейса,
а тема интерфейса одна. (Существующий `DA_Tutorial_Style` — не контрпример: он
отвечает за слой обучения, включая мировые декали, и живёт своей жизнью.)

### 6.5. Где живёт рендерер и как встаёт по вертикали

Оверлей кладётся прямо в viewport (`AddViewportWidgetContent`) с
`ZOrder = DisplayZOrder` из настроек проекта (по умолчанию **10**): выше
корневого лейаута CommonUI (он добавляется с ZOrder 0) и выше оверлея подсказок
обучения (`ZOrder = 8`). Слот внутри `UPrimaryGameLayout` сознательно НЕ
используется: он потребовал бы ручной правки `WBP_PrimaryGameLayout` в
редакторе, а слой обязан работать сразу после сборки, на любой машине и без
единого действия дизайнера. Взамен подсистема сама следит за жизненным циклом —
`PostLoadMapWithWorld` чистит ссылки (viewport-виджеты снимаются вместе с
миром) и ставит оверлей заново при следующей реплике.

Виджет создаётся **лениво**, на первой же строке: в мирах, где субтитров не
было, лишнего виджета не появляется. Дисплей заменяем: `bUseBuiltInDisplay=false`
отключает встроенный оверлей, и рисовать берётся любой другой подписчик
`OnLineChanged` (например WBP), не трогая ни подсистему, ни источники.

Позиция — два пресета из темы, **выбираются автоматически**:

| Пресет | Когда | Ориентир |
|---|---|---|
| `Cinematic` | игровой слой скрыт или его нет (главное меню, интро, брифинг, результат) | ~8% высоты экрана от нижнего края |
| `Gameplay` | игровой слой (боевой HUD) виден | выше панели способностей — отступ снизу порядка высоты панели + зазор |

Признак берётся у лейаута: `UPrimaryGameLayout` уже ведёт учёт «кто скрыл
игровой слой» (`SetGameLayerHidden`), к нему добавляется публичный
`IsGameLayerVisible()`. Никаких «контекстов» в вызовах показа — источник не
должен знать, где рисуется строка.

Подложка — своя, полупрозрачная, по цвету панелей темы; выключателя цвета
у игрока пока нет (по постановке), но цвет и непрозрачность лежат в теме.

### 6.6. Источники (адаптеры)

| # | Источник | Изменение |
|---|---|---|
| 1 | Такт обучения | В `UTutorialPresentationSubsystem::StartBeat` — `ShowLine({Speaker, Subtitle, bSkippable})`, в `FinishBeat` — `HideLine`. StateTree-задачи **не трогаем**. Из `STutorialHintOverlay` и `ATacticalPlayerController` слот субтитра удаляется |
| 2 | Любая озвучка | `PlayVoice2D` читает `USoundSubtitleData` со звука и вызывает `ShowLineForSound(Line, Component)`. Одно изменение закрывает брифинг, результат и прибытие в хаб; всякая новая реплика получает субтитр автоматически, если у ассета заполнены данные |
| 3 | Интро | `UIntroPlayerWidget` создаёт `UMediaSubtitleDriver` на `USubtitleTrackDataAsset` из темы; `HandleMediaEndReached`, скип-удержание и `StopIntroPlayback` снимают титры |
| 4 | Диалоги (когда/если) | Подписка на `UDialogueSubsystem::OnLine` → `ShowLineForSound`/`ShowLine`. Ноль изменений в STQuestSystem |
| 5 | Легаси-мост | **Не делаем.** Подписка на `FSubtitleManager::OnSetSubtitleText` отключила бы Canvas-рендер движка и потребовала бы самим гасить субтитры по `bSubtitlesEnabled`; легаси-cue в проекте нет. Оставлено как задокументированная возможность |

### 6.7. Настройки: где хранить и почему

**Решение: `UTacticsUserSettings` (наследник `UGameUserSettings`, файл
`GameUserSettings.ini`).** Обоснование:

1. Это документированный способ UE для пользовательских опций; экраны настроек
   ожидают `LoadSettings/ApplySettings/SaveSettings`.
2. В проекте уже принято и задокументировано решение «настройки приложения — не
   в слоте кампании, а в `UTacticsUserSettings`» ([TacticsUserSettings.h:8](../Source/XRU1/Tactics/TacticsUserSettings.h#L8));
   заводить второе хранилище — воспроизводить ровно тот баг, который тогда чинили.
3. **Сам движок хранит язык там же**: при старте упакованной игры
   `FTextLocalizationManager` читает `[Internationalization] Culture/Language`
   из `GGameUserSettingsIni` (`TextLocalizationManager.cpp:287`), и Lyra пишет
   туда же. То есть язык и остальные настройки оказываются в одном файле без
   изобретений.
4. Схему Lyra (`ULyraSettingsShared : ULocalPlayerSaveGame`) **не берём**: она
   существует ради нескольких профилей на консоли и облачной синхронизации;
   у нас один локальный игрок и нет аккаунтов — это была бы сложность без выгоды.

Состав `FTacticsSubtitleSettings`: `bSubtitlesEnabled`, `bShowSpeakerNames`,
`TextSize` (3 ступени: обычный/крупный/очень крупный), `BackgroundOpacity`
(3 ступени). Цвет — из темы, в настройки пока не выносим.
Плюс `Language` (код культуры) отдельным полем.

На экране настроек — секция «Субтитры и язык» теми же `BindWidgetOptional`,
что и остальные секции `USettingsMenuWidget`.

### 6.8. Локализация: минимальный, но правильный объём

1. **Пайплайн**: скопировать из донора цели `Config/Localization/Game_*.ini`
   (Gather/Compile/Export/Import), native-культура `ru`, целевая `en`, создать
   `Content/Localization/Game`. Диалоговую ветку (`*DialogueScript*`) не берём —
   `UDialogueWave` не используем.
2. **Правило текста**: любой текст для игрока — `FText` (LOCTEXT или поле
   ассета). Аудировать 12 мест с `FText::FromString`.
3. **Субтитры переводятся автоматически**: они лежат в `FText`-полях DataAsset'ов
   (такты обучения, трек интро, `USoundSubtitleData`) и попадают в gather ассетов.
4. **Озвучка не переводится**: русский голос остаётся при английском тексте.
   Механизм на случай необходимости — `SetAssetGroupCulture` (приём донора).
5. **Переключение языка** — в `UTacticsUserSettings`:
   `SetPendingLanguage()` (запоминает) → `ApplyLanguage()` по кнопке «Применить»
   (`FInternationalization::SetCurrentLanguageAndLocale` + запись в
   `[Internationalization] Culture` файла `GameUserSettings.ini`). Отдельная
   подсистема локализации **не заводится**.
6. **Живое обновление**: подписка на
   `FTextLocalizationManager::OnTextRevisionChangedEvent` →
   `UXRU1SubtitleSubsystem::RefreshActiveLine()`. Приём донора: строка,
   собранная в C++, сама не перерезолвится.
7. **Список языков** — `UXRU1LocalizationSettings : UDeveloperSettings`
   (`AvailableCultures = {ru, en}`), как у донора: список языков — константа
   проекта, а не пользовательская настройка.

**Практическая оговорка:** чтение культуры из `GameUserSettings.ini` при старте
выполняется только вне редактора (`!GIsEditor`, `TextLocalizationManager.cpp:284`).
В PIE переключение работает вживую, но «запомнился ли язык между запусками»
проверяется только в упакованной сборке.

---

## 7. Что изменилось относительно первого прохода

| Утверждение прохода 1 | Проверка прохода 2 |
|---|---|
| «Инструктаж останавливает мир → таймеры мира ломают субтитры → слой обязан тикать при паузе» | Неверно: паузу держат экраны меню и потеря фокуса, такты идут в живом мире. Правильное требование — обратное: субтитр обязан замирать вместе с голосом, то есть тайминг привязан к игровому времени и звуковым событиям |
| «Нужна очередь с приоритетами» | Не нужна: голос в проекте один по конструкции, слой копирует это правило (одна строка, новая вытесняет) |
| «Полезен мост к легаси `FSubtitleManager`» | Отклонено: подписка отключает Canvas-рендер движка и переносит на нас проверку `bSubtitlesEnabled`, а легаси-cue в проекте нет |
| «Свой слой vs плагин Epic — по совокупности» | Решено однозначно: плагин не может рисовать поверх экранов CommonUI и молча теряет вторую реплику подряд; это блокеры, а не предпочтения |

---

## 8. Что реализовано (C++ собран) и что осталось в редакторе

### 8.1. Сделано в коде

| Блок | Где |
|---|---|
| Ядро слоя: модель, подсистема, три режима показа, дескрипторы | `Source/XRU1/Subtitles/SubtitleTypes.h`, `SubtitleSubsystem.h/.cpp` |
| Встроенный дисплей (Slate), позиция по двум пресетам, подложка | `Source/XRU1/Subtitles/SubtitleOverlay.h/.cpp` |
| Проектные настройки слоя (ZOrder, замена дисплея, список языков) | `Source/XRU1/Subtitles/SubtitleProjectSettings.h/.cpp` |
| Титры носителей со своим временем | `SubtitleTrackDataAsset.h/.cpp`, `MediaSubtitleDriver.h/.cpp` |
| Субтитр на ассете озвучки | `SoundSubtitleData.h` |
| Стиль субтитров в теме UI (секция «09. Субтитры») + `IntroSubtitleTrack` | `UI/TacticalHUDStyleData.h` |
| Признак «идёт геймплей» для выбора высоты строки | `UPrimaryGameLayout::IsGameLayerVisible()` |
| Обучение переведено на слой; из оверлея подсказок субтитр убран | `Tactics/TutorialPresentation.cpp`, `Tactics/TutorialHintOverlay.*` |
| Автосубтитр любой озвучки | `UTacticsAudioSubsystem::PlayVoice2D(..., bAutoSubtitle)` |
| Титры интро | `UIntroPlayerWidget::StartIntroSubtitles()` + снятие в `StopIntroPlayback()` |
| Настройки игрока и отложенная смена языка | `TacticsUserSettings.h/.cpp`, секция в `USettingsMenuWidget` |
| Цель локализации «Game» (ru → en) без машинных путей | `Config/DefaultEditor.ini`, `Config/Localization/Game_{Gather,Compile}.ini` |

Проверка сборки: модуль собирается, UHT сгенерировал все классы слоя.

### 8.2. Осталось сделать в редакторе (кода не требует)

1. **Титры интро.** Создать `DA_Subtitles_Intro` (класс «Таймлайн субтитров») в
   `/Game/XRU1Game/Data/Missions`, забить реплики с временами по монтажу ролика
   (тексты — `02_LORE_SCRIPT.md`) и назначить его в теме UI:
   `DA_TacticalHUDStyle` → «04. Экраны|Интро» → `Intro Subtitle Track`.
2. **Субтитры к остальной озвучке.** Открыть ассеты реплик (брифинги миссий,
   победа/поражение, вводная хаба) → Advanced → `Asset User Data` → `+` →
   «Субтитр озвучки» → заполнить `Speaker` и `Text`. После этого субтитр
   появляется сам, без правок кода.
3. **Экран настроек.** Добавить в `WBP_Settings` контролы с каноничными именами
   (`Chk_Subtitles`, `Chk_SubtitleSpeakers`, `Cmb_SubtitleSize`,
   `Cmb_SubtitleBackdrop`, `Cmb_Language`) — C++ привяжет их сам
   (`BindWidgetOptional`), пока их нет, секция просто отсутствует.
4. **Подсказка пропуска.** Убрать «[Пробел — пропустить]» из авторских текстов
   тактов обучения, затем включить `bSubtitleShowSkipHint` в теме — подсказку
   начнёт рисовать слой (её текст локализуется отдельно от реплики).
5. **Локализация.** Localization Dashboard → цель `Game` → Gather → перевести
   `en/Game.archive` → Compile. До первого Gather переключатель языка работает,
   но переводить ему нечего.

### 8.3. Осталось в коде мелочью

- `ATacticalPlayerController::GetTutorialBeatSubtitle()` больше никем не
  вызывается. Не удалён намеренно: в момент работы файл правил параллельный
  агент (туман войны). Убрать при ближайшей правке контроллера.

---

## 9. Риски и на что смотреть при реализации

1. **Смена уровня.** Viewport-виджеты снимаются вместе с миром, поэтому на
   `PostLoadMapWithWorld` слой сбрасывает ссылки на оверлей и гасит активную
   строку: её владелец остался в прошлом мире и снять её уже не сможет.
   Оверлей ставится заново на первой же реплике нового мира.
2. **`bAutoDestroy` у голосового компонента.** `PlayVoice2D` спаунит звук с
   `bAutoDestroy = true`, поэтому кроме подписки на `OnAudioFinishedNative`
   стоит страховочный таймер на длительность звука + 0.5 с: компонент может
   умереть, не успев оповестить, и строка иначе зависла бы навсегда.
3. **Такт без озвучки.** GDD допускает субтитр без голоса — режим «по звуку» для
   него неприменим, но такт обучения и так работает в режиме «владелец снимает».
4. **Позиция в бою.** Отступ «выше иконок способностей» задаётся числом в теме;
   при изменении раскладки HUD его придётся править — это ожидаемая цена за
   отсутствие связывания виджетов между собой.
5. **Длинные реплики.** Ширина переноса и максимум строк — в теме; при переводе
   на английский длина строк изменится, проверять после первого прогона gather.
