#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
// FKey/EKeys: интро различает клавиши пропуска в NativeOnKeyDown.
#include "InputCoreTypes.h"
#include "SubtitleTypes.h"
#include "TacticsAudioTypes.h"
#include "TacticsTypes.h"
#include "TacticalHUDStyleData.h"
#include "MenuWidgets.generated.h"

class UTacticsGameInstance;
class UTacticsAudioSubsystem;
class UTacticalHUDStyleData;
class UMediaPlayer;
class UButton;
class UCheckBox;
class UComboBoxString;
class UImage;
class UProgressBar;
class USlider;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDifficultyChosen, EDifficultyLevel, Difficulty);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMenuAction);

/**
 * Общий предок экранов меню на CommonUI activatable-стеке. Навигация каноном
 * CommonUI: следующий экран проталкивается на слой Menu корневого
 * UPrimaryGameLayout (через UGameUIManagerSubsystem), «назад» — деактивация
 * себя, стек сам показывает предыдущий экран. Дизайн виджетов — в BP-наследниках.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UMenuScreenBase : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Просьба закрыть экран и вернуться на предыдущий в стеке. */
	UPROPERTY(BlueprintAssignable, Category = "Menu")
	FOnMenuAction OnBackRequested;

	/** Закрывает экран (деактивация = снятие со стека) и шлёт OnBackRequested. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void RequestBack();

	/** Проталкивает экран ScreenClass на слой Menu корневого лейаута. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	UCommonActivatableWidget* PushScreen(TSubclassOf<UCommonActivatableWidget> ScreenClass);

	/** Корневой лейаут текущего игрока или nullptr (Designer preview). */
	UFUNCTION(BlueprintPure, Category = "Menu")
	class UPrimaryGameLayout* GetRootLayout() const;

	/** Единая UI-тема из GameInstance; доступна всем WBP-экранам меню. */
	UFUNCTION(BlueprintPure, Category = "Menu|Style")
	UTacticalHUDStyleData* GetUITheme() const;

protected:
	/** GameInstance проекта (nullptr, если проект настроен на другой класс). */
	UTacticsGameInstance* GetTacticsGameInstance() const;

	/** Подсистема звука UI (nullptr вне рантайма — Designer preview). */
	UTacticsAudioSubsystem* GetAudioSubsystem() const;

	/**
	 * Играет реплику, ПРИНАДЛЕЖАЩУЮ ЭКРАНУ (брифинг, итог операции).
	 *
	 * Такая реплика обязана умереть вместе с экраном: игрок, закрывший брифинг
	 * через секунду после открытия, не должен слушать вводную поверх хаба и
	 * видеть её субтитр на чужом экране. Владение звуком у того, кто его начал —
	 * стандартная схема UI-аудио (так же устроены экраны в Lyra/CommonUI).
	 *
	 * Реплики МИРА (вводная хаба, реплики боя) сюда не относятся: они живут
	 * своей жизнью и играются напрямую через `UTacticsAudioSubsystem`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Audio")
	void PlayScreenVoice(USoundBase* Voice);

	/**
	 * Обрывает реплику этого экрана. Идемпотентно.
	 *
	 * Субтитр снимать отдельно не нужно: он привязан к тому же звуковому
	 * компоненту и уходит вместе с ним (`OnAudioFinished` шлётся и при `Stop()`).
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Audio")
	void StopScreenVoice();

	/**
	 * Вешает на кнопку звуки интерфейса (наведение/клик). AddUniqueDynamic:
	 * повторный вызов безопасен. Действие кнопки биндится отдельно — мультикаст
	 * позволяет иметь и звук, и обработчик на одном OnClicked.
	 */
	void RegisterButtonSounds(UButton* Button);

	/** Общий обработчик «Назад» для кнопок Btn_Back всех экранов. */
	UFUNCTION()
	void HandleBackClicked();

	/**
	 * Ставить ли игру на паузу, пока экран открыт.
	 *
	 * По умолчанию ВЫКЛЮЧЕНО: под главным меню, выбором сложности и экраном
	 * результата живого геймплея нет, и лишняя пауза там только создаёт риск
	 * «залипшей» причины. Включают её экраны поверх идущей игры — пауза
	 * и настройки.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Menu")
	bool bPauseGameWhileActive = false;

	virtual void NativeConstruct() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

	/** Снимает удерживаемую этим экраном паузу (идемпотентно). */
	void ReleasePauseHold();

	/**
	 * Какой крупный арт из общей темы показывать фоном этого экрана.
	 * Экран сам знает свою роль, а картинку берёт из `UTacticalHUDStyleData` —
	 * так арт меняется в одном месте и не дублируется по WBP.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Menu")
	EXRU1UIScreenArt ScreenArtKind = EXRU1UIScreenArt::MainMenu;

	/**
	 * Прятать ли игровой слой (HUD), пока экран открыт.
	 *
	 * Слои CommonUI независимы: HUD хаба живёт на `Game`, брифинг — на `Menu`,
	 * и без этого карточка точки продолжала висеть под окном брифинга.
	 * Выключают только сами HUD-экраны — они и есть игровой слой.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Menu")
	bool bHidesGameLayer = true;

	/**
	 * Ставит фон экрана из темы — в ОБЩИЙ слой корневого лейаута, а не в свою
	 * вёрстку. Причина в устройстве CommonUI: стек рисует только верхний виджет,
	 * поэтому фон, лежащий внутри экрана, обязан исчезать и появляться заново на
	 * каждом переходе. Локальный `Img_Background`, если он остался в вёрстке,
	 * прячется, чтобы картинка не дублировалась.
	 *
	 * `ScreenArtKind == None` означает «фона не надо» (HUD, экран результата
	 * поверх боя) — общий фон при этом скрывается.
	 */
	void ApplyScreenArt();

private:
	UFUNCTION()
	void HandleButtonHovered();

	UFUNCTION()
	void HandleButtonClicked();

	/**
	 * Причина паузы этого экземпляра. Уникальная, а не общая на все меню:
	 * с общим именем закрытие настроек, открытых из паузы, сняло бы и паузу.
	 */
	FName PauseReasonId;

	/** Звуковой компонент реплики этого экрана; слабая ссылка — он авто-уничтожается. */
	TWeakObjectPtr<class UAudioComponent> ScreenVoice;
};

/**
 * Главное меню: Продолжить / Новая игра / Настройки / Об авторе / Выйти.
 * BP вешает кнопки на Request*-методы; переходы экранов реализованы здесь
 * (пуш на слой Menu), делегаты остаются для дополнительной BP-логики.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UMainMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Доступна ли кнопка «Продолжить» (есть ли сохранение). Опрашивает UTacticsGameInstance. */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool CanContinue() const;

	/** Экран настроек, открываемый кнопкой Settings. */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Screens")
	TSubclassOf<UMenuScreenBase> SettingsScreenClass;

	/** Экран «Об авторе». */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Screens")
	TSubclassOf<UMenuScreenBase> AboutScreenClass;

	/** Экран выбора сложности (открывается кнопкой «Новая игра»). */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Screens")
	TSubclassOf<UMenuScreenBase> DifficultyScreenClass;

	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnContinueClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnNewGameClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnSettingsClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnAboutClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnQuitClicked;

	/** Загружает кампанию и отправляет игрока в хаб. */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestContinue();

	/** Открывает экран выбора сложности. */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestNewGame();

	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestSettings();
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestAbout();

	/** Завершает игру. */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestQuit();

protected:
	/**
	 * Авто-биндинг: кнопки с каноничными именами из Designer сами получают
	 * обработчики Request* — граф WBP остаётся пустым (STATUS_MainMenu_UI §4).
	 * Отсутствующий виджет просто пропускается.
	 */
	virtual void NativeOnInitialized() override;

	/** Актуализирует доступность «Продолжить» при каждом показе экрана. */
	virtual void NativeOnActivated() override;

	// Имена совпадают с виджетами в WBP_MainMenu (уже сверстан).
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Continue;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_NewGame;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Settings;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_About;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Quit;

private:
	UFUNCTION() void HandleContinueClicked();
	UFUNCTION() void HandleNewGameClicked();
	UFUNCTION() void HandleSettingsClicked();
	UFUNCTION() void HandleAboutClicked();
	UFUNCTION() void HandleQuitClicked();
};

/**
 * Экран настроек звука и изображения.
 *
 * Виджет НЕ хранит состояние: он читает и пишет слот кампании, а применением
 * занимаются подсистема звука и UGameUserSettings. Иначе ползунок и реальная
 * громкость неизбежно разъезжаются после выхода в меню и обратно.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API USettingsMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Текущие громкости из слота кампании (для инициализации ползунков). */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	FTacticsAudioSettings GetAudioSettings() const;

	/** Текущие настройки изображения из слота кампании. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	FTacticsVideoSettings GetVideoSettings() const;

	/**
	 * Применяет громкости немедленно (игрок должен слышать результат, двигая
	 * ползунок) и сохраняет их в слот.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ApplyAudioSettings(const FTacticsAudioSettings& NewSettings, bool bSaveToSlot = true);

	/** Применяет качество/разрешение через UGameUserSettings и сохраняет в слот. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ApplyVideoSettings(const FTacticsVideoSettings& NewSettings, bool bSaveToSlot = true);

	/** Текущие настройки камеры (обзор, чувствительность, инверсия, edge scroll). */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	FTacticsCameraSettings GetCameraSettings() const;

	/**
	 * Применяет настройки камеры немедленно: обзор и чувствительность игрок
	 * подбирает глазами, поэтому результат обязан быть виден сразу, как и у
	 * громкости. `bSaveToSlot` отделяет перетаскивание ползунка от его отпускания.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ApplyCameraSettings(const FTacticsCameraSettings& NewSettings, bool bSaveToSlot = true);

	// --- Субтитры и язык ------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	FTacticsSubtitleSettings GetSubtitleSettings() const;

	/** Применяет настройки субтитров сразу: строка на экране меняется на глазах. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ApplySubtitleSettings(const FTacticsSubtitleSettings& NewSettings, bool bSaveToSlot = true);

	/** Языки проекта; индекс совпадает с позицией в `Cmb_Language`. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	TArray<FString> GetAvailableLanguages() const;

	/** Возвращает и применяет значения по умолчанию. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ResetToDefaults();

protected:
	virtual void NativeOnInitialized() override;

	/** Каждый показ экрана перечитывает контролы из слота (виджет ничего не хранит). */
	virtual void NativeOnActivated() override;

	// --- Звук: пять слайдеров 0..1 (имена из STATUS_MainMenu_UI §2.2) ---------
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_Master;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_Music;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_Sfx;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_UI;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_Voice;

	// --- Изображение ----------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UComboBoxString> Cmb_Quality;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_ResolutionScale;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> Chk_Fullscreen;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> Chk_VSync;

	// --- Камера ---------------------------------------------------------------
	// Обзор в градусах (40..110), чувствительности — множители (0.25..2.5);
	// слайдеры отдают 0..1, перевод в реальные значения — в Collect/Refresh.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_CameraFov;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_CameraSensitivity;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<USlider> Sld_CameraPitchSensitivity;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> Chk_InvertPitch;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> Chk_EdgeScroll;

	// --- Субтитры и язык ------------------------------------------------------
	// Язык применяется ОТЛОЖЕННО (по «Применить»): смена культуры перезагружает
	// весь текст игры, и делать это на каждый клик по списку нельзя.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> Chk_Subtitles;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> Chk_SubtitleSpeakers;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UComboBoxString> Cmb_SubtitleSize;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UComboBoxString> Cmb_SubtitleBackdrop;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UComboBoxString> Cmb_Language;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Apply;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Reset;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Back;

private:
	/** Собирает структуру из ВСЕХ пяти слайдеров (какой изменился — не важно). */
	FTacticsAudioSettings CollectAudioSettings() const;

	/** Собирает настройки изображения из контролов секции «Изображение». */
	FTacticsVideoSettings CollectVideoSettings() const;

	/** Собирает настройки камеры из контролов секции «Камера». */
	FTacticsCameraSettings CollectCameraSettings() const;

	/** Собирает настройки субтитров из контролов секции «Субтитры». */
	FTacticsSubtitleSettings CollectSubtitleSettings() const;

	/** Границы, в которых слайдер 0..1 отображается в реальные значения камеры. */
	static constexpr float CameraFovMin = 45.f;
	static constexpr float CameraFovMax = 100.f;
	static constexpr float CameraSensitivityMin = 0.25f;
	static constexpr float CameraSensitivityMax = 2.5f;

	/** Расставляет контролы по текущим настройкам приложения. */
	void RefreshControlsFromSettings();

	/**
	 * Идёт программная расстановка контролов. `SetValue` слайдера вызывает
	 * `OnValueChanged`, и без этого флага экран принимал бы собственную
	 * инициализацию за действие игрока.
	 */
	bool bUpdatingControls = false;

	/** Перетаскивание любого слайдера звука: применить без записи в слот. */
	UFUNCTION() void HandleAudioSliderValue(float NewValue);

	/** Слайдер отпущен: то же значение, но с записью в слот. */
	UFUNCTION() void HandleAudioCaptureEnd();

	/** Перетаскивание/переключение в секции камеры: применить без записи на диск. */
	UFUNCTION() void HandleCameraSliderValue(float NewValue);
	UFUNCTION() void HandleCameraCaptureEnd();
	UFUNCTION() void HandleCameraCheckChanged(bool bIsChecked);

	/** Переключение галочек и списков секции «Субтитры»: применяем немедленно. */
	UFUNCTION() void HandleSubtitleCheckChanged(bool bIsChecked);
	UFUNCTION() void HandleSubtitleComboChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	/** Выбор языка: только запоминаем, применение — по «Применить». */
	UFUNCTION() void HandleLanguageChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION() void HandleApplyClicked();
	UFUNCTION() void HandleResetClicked();
};

/** Экран «Об авторе». Текстовые поля задаёт дизайнер в BP. */
UCLASS(Abstract, Blueprintable)
class XRU1_API UAboutMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "About")
	FText AuthorName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "About", meta = (MultiLine = true))
	FText ProjectInfo;

protected:
	/** Тексты подставляются и в Designer-превью (PreConstruct). */
	virtual void NativePreConstruct() override;
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Author;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_ProjectInfo;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Back;
};

/** Экран проигрывания интро после выбора сложности. */
UCLASS(Abstract, Blueprintable)
class XRU1_API UIntroPlayerWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Завершить/пропустить интро и перейти в хаб. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Intro")
	void FinishIntro();

protected:
	virtual void NativeOnInitialized() override;

	/** Старт ролика при показе экрана. */
	virtual void NativeOnActivated() override;

	/** Остановка плеера: экран мог быть закрыт до конца ролика. */
	virtual void NativeDestruct() override;

	/** Полноэкранная прозрачная кнопка: ловит УДЕРЖАНИЕ мыши, а не клик. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Skip;

	/** Полотно ролика: сюда ставится материал с MediaTexture или fallback-арт. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UImage> Img_Intro;

	/** Полоска прогресса удержания; видна только пока игрок держит. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> Bar_SkipHold;

	/** Подсказка «удерживайте, чтобы пропустить». */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_SkipHint;

	/**
	 * Страховка от «вечного интро»: если медиа не открылось или событие конца
	 * не пришло, экран всё равно уйдёт в хаб. 0 — выключить страховку.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Intro", meta = (ClampMin = "0"))
	float MaxIntroDuration = 90.f;

	/**
	 * Сколько держать, чтобы пропустить ролик. Пропуск намеренно НЕ по клику:
	 * случайный щелчок мышью не должен стоить игроку вступления.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Intro", meta = (ClampMin = "0.1"))
	float SkipHoldDuration = 0.8f;

	/** Ловит Space/Enter: пропуск обязан работать и без мыши. */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	/** Клавиша/кнопка пропуска зажата — начать отсчёт удержания. */
	UFUNCTION() void HandleSkipPressed();

	/** Отпущено раньше времени — отсчёт сбрасывается. */
	UFUNCTION() void HandleSkipReleased();

	/** Удержание доведено до конца. */
	void CompleteSkipHold();

	/** Обновляет полоску прогресса удержания (таймер, а не Tick). */
	void TickSkipHold();

	/** Показывает/прячет полоску и возвращает подсказку в исходный вид. */
	void ResetSkipHoldUI();

	/** Является ли клавиша клавишей пропуска (Space/Enter). */
	static bool IsSkipKey(const FKey& Key);

	/** Медиа открылось: печатаем, что именно открылось и играет ли оно. */
	UFUNCTION() void HandleMediaOpened(FString OpenedUrl);

	/** Ролик доигран до конца — уходим в хаб. */
	UFUNCTION() void HandleMediaEndReached();

	/** Медиа не открылось (нет файла/кодека) — не задерживать игрока. */
	UFUNCTION() void HandleMediaOpenFailed(FString FailedUrl);

	/** Останавливает плеер и снимает подписки (идемпотентно). */
	void StopIntroPlayback();

	/** Создаёт и запускает звуковой компонент ролика (идемпотентно). */
	void CreateIntroSound();

	/**
	 * Запускает ведение титров ролика (идемпотентно).
	 *
	 * Титров в самом видео нет намеренно: вшитые не переводятся, не
	 * масштабируются и не отключаются. Текст лежит в `USubtitleTrackDataAsset`
	 * (поле темы `IntroSubtitleTrack`), а время даёт сам плеер.
	 */
	void StartIntroSubtitles();

	/** Плеер интро из темы; держим ссылку, чтобы отписаться и остановить. */
	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> IntroPlayer;

	/**
	 * Звук ролика. MediaPlayer сам ничего не озвучивает: без этого компонента
	 * интро идёт немым — ровно так оно и вело себя до 2026-08-02.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class UMediaSoundComponent> IntroSound;

	/** Драйвер титров ролика; снимается вместе с воспроизведением. */
	UPROPERTY(Transient)
	TObjectPtr<class UMediaSubtitleDriver> SubtitleDriver;

	/** Интро уже завершено — второй переход в хаб не нужен. */
	bool bIntroFinished = false;

	FTimerHandle IntroTimeoutTimer;

	/** Тикает только пока игрок держит: прогресс удержания без Tick виджета. */
	FTimerHandle SkipHoldTimer;

	/** Момент начала удержания по времени мира; < 0 — не держат. */
	double SkipHoldStartTime = -1.0;
};

/** Экран выбора сложности при старте новой игры. */
UCLASS(Abstract, Blueprintable)
class XRU1_API UDifficultySelectWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Экран интро; если не назначен, после выбора сразу открывается хаб. */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Screens")
	TSubclassOf<UIntroPlayerWidget> IntroScreenClass;

	UPROPERTY(BlueprintAssignable, Category = "Menu")
	FOnDifficultyChosen OnDifficultyChosen;

	/**
	 * BP вызывает по нажатию сложности: создаёт новую кампанию, затем пушит
	 * IntroScreenClass. Если интро не назначено — сразу отправляет игрока в хаб.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void ChooseDifficulty(EDifficultyLevel Difficulty);

protected:
	virtual void NativeOnInitialized() override;

	// Имена совпадают с виджетами в WBP_DifficultySelect (уже сверстан).
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Easy;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Medium;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Hard;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Back;

private:
	UFUNCTION() void HandleEasyClicked();
	UFUNCTION() void HandleMediumClicked();
	UFUNCTION() void HandleHardClicked();
};

/** Экран паузы во время миссии. */
UCLASS(Abstract, Blueprintable)
class XRU1_API UPauseMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnResumeClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnReturnToMenuClicked;

	/** Снимает паузу и закрывает экран. */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestResume();

	/** Снимает паузу и возвращает в главное меню (уровень MainMenuLevel из GameInstance). */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestReturnToMenu();

	/** Экран настроек, открываемый из паузы (тот же WBP_Settings, что и в меню). */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Screens")
	TSubclassOf<UMenuScreenBase> SettingsScreenClass;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Resume;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Settings;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_ReturnToMenu;

private:
	UFUNCTION() void HandleResumeClicked();
	UFUNCTION() void HandleSettingsClicked();
	UFUNCTION() void HandleReturnToMenuClicked();
};
