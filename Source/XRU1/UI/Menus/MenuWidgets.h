#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "TacticsAudioTypes.h"
#include "TacticsTypes.h"
#include "MenuWidgets.generated.h"

class UTacticsGameInstance;
class UTacticsAudioSubsystem;
class UTacticalHUDStyleData;
class UMediaPlayer;
class UButton;
class UCheckBox;
class UComboBoxString;
class UImage;
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

	/** Единая UI-тема из GameInstance; доступна всем WBP-экранам меню. */
	UFUNCTION(BlueprintPure, Category = "Menu|Style")
	UTacticalHUDStyleData* GetUITheme() const;

protected:
	/** GameInstance проекта (nullptr, если проект настроен на другой класс). */
	UTacticsGameInstance* GetTacticsGameInstance() const;

	/** Подсистема звука UI (nullptr вне рантайма — Designer preview). */
	UTacticsAudioSubsystem* GetAudioSubsystem() const;

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

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Apply;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Reset;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Back;

private:
	/** Собирает структуру из ВСЕХ пяти слайдеров (какой изменился — не важно). */
	FTacticsAudioSettings CollectAudioSettings() const;

	/** Собирает настройки изображения из контролов секции «Изображение». */
	FTacticsVideoSettings CollectVideoSettings() const;

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

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Skip;

	/** Полотно ролика: сюда ставится материал с MediaTexture или fallback-арт. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UImage> Img_Intro;

	/**
	 * Страховка от «вечного интро»: если медиа не открылось или событие конца
	 * не пришло, экран всё равно уйдёт в хаб. 0 — выключить страховку.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Intro", meta = (ClampMin = "0"))
	float MaxIntroDuration = 90.f;

private:
	UFUNCTION() void HandleSkipClicked();

	/** Ролик доигран до конца — уходим в хаб. */
	UFUNCTION() void HandleMediaEndReached();

	/** Медиа не открылось (нет файла/кодека) — не задерживать игрока. */
	UFUNCTION() void HandleMediaOpenFailed(FString FailedUrl);

	/** Останавливает плеер и снимает подписки (идемпотентно). */
	void StopIntroPlayback();

	/** Плеер интро из темы; держим ссылку, чтобы отписаться и остановить. */
	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> IntroPlayer;

	/** Интро уже завершено — второй переход в хаб не нужен. */
	bool bIntroFinished = false;

	FTimerHandle IntroTimeoutTimer;
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
