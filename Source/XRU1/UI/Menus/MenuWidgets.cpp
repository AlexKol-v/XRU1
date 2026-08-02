#include "MenuWidgets.h"
#include "TacticsAudioSubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "TacticsUserSettings.h"
#include "GamePauseSubsystem.h"
#include "GameFramework/GameUserSettings.h"
#include "XRU1Log.h"
#include "TacticalHUDStyleData.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "Engine/World.h"
#include "TimerManager.h"

// --- UMenuScreenBase --------------------------------------------------------

void UMenuScreenBase::RequestBack()
{
	OnBackRequested.Broadcast();
	// Канон CommonUI: деактивация снимает виджет со стека, предыдущий экран
	// активируется автоматически.
	DeactivateWidget();
}

UCommonActivatableWidget* UMenuScreenBase::PushScreen(TSubclassOf<UCommonActivatableWidget> ScreenClass)
{
	if (!ScreenClass)
	{
		return nullptr;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	UGameUIManagerSubsystem* UIManager = GameInstance ? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
	UPrimaryGameLayout* RootLayout = UIManager ? UIManager->GetRootLayout() : nullptr;
	if (!RootLayout)
	{
		return nullptr;
	}

	return RootLayout->PushWidgetToLayer(EUILayer::Menu, ScreenClass);
}

UTacticsGameInstance* UMenuScreenBase::GetTacticsGameInstance() const
{
	return GetGameInstance<UTacticsGameInstance>();
}

UTacticalHUDStyleData* UMenuScreenBase::GetUITheme() const
{
	const UTacticsGameInstance* GameInstance = GetTacticsGameInstance();
	return GameInstance ? GameInstance->GetUITheme() : nullptr;
}

UTacticsAudioSubsystem* UMenuScreenBase::GetAudioSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UTacticsAudioSubsystem>() : nullptr;
}

void UMenuScreenBase::RegisterButtonSounds(UButton* Button)
{
	if (!Button)
	{
		return;
	}
	Button->OnHovered.AddUniqueDynamic(this, &UMenuScreenBase::HandleButtonHovered);
	Button->OnClicked.AddUniqueDynamic(this, &UMenuScreenBase::HandleButtonClicked);
}

void UMenuScreenBase::NativeConstruct()
{
	Super::NativeConstruct();

	// Пауза держится, пока экран СУЩЕСТВУЕТ в стеке, а не пока он активен.
	// При открытии настроек поверх паузы CommonUI деактивирует нижний экран,
	// и привязка к активации давала «окно без паузы» на кадр между
	// деактивацией паузы и активацией настроек — игра успевала тикнуть.
	if (!bPauseGameWhileActive)
	{
		return;
	}
	if (UGamePauseSubsystem* Pause = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UGamePauseSubsystem>() : nullptr)
	{
		if (PauseReasonId.IsNone())
		{
			PauseReasonId = FName(*FString::Printf(TEXT("Menu.%s"), *GetName()));
		}
		UE_LOG(LogXRU1UI, Display, TEXT("[Menu] экран '%s' открыт — держит паузу"), *GetName());
		Pause->PushPauseReason(PauseReasonId);
	}
}

void UMenuScreenBase::NativeOnActivated()
{
	Super::NativeOnActivated();
}

void UMenuScreenBase::NativeOnDeactivated()
{
	// Паузу НЕ отпускаем: экран мог просто уйти под другой экран стека.
	Super::NativeOnDeactivated();
}

void UMenuScreenBase::NativeDestruct()
{
	// Единственная точка снятия: экран уходит со стека (закрыт, travel, смена
	// уровня). Пауза не должна пережить свой экран.
	ReleasePauseHold();
	Super::NativeDestruct();
}

void UMenuScreenBase::ReleasePauseHold()
{
	if (PauseReasonId.IsNone())
	{
		return;
	}
	if (UGamePauseSubsystem* Pause = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UGamePauseSubsystem>() : nullptr)
	{
		UE_LOG(LogXRU1UI, Display, TEXT("[Menu] экран '%s' закрыт — отпускает паузу"), *GetName());
		Pause->PopPauseReason(PauseReasonId);
	}
	// Имя очищается: следующий показ этого же экрана возьмёт причину заново,
	// а «висящей» причины от уничтоженного виджета не останется.
	PauseReasonId = NAME_None;
}

void UMenuScreenBase::HandleBackClicked()
{
	RequestBack();
}

void UMenuScreenBase::HandleButtonHovered()
{
	if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->PlayUIHover();
	}
}

void UMenuScreenBase::HandleButtonClicked()
{
	if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->PlayUIClick();
	}
}

// --- UMainMenuWidget --------------------------------------------------------

void UMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Авто-биндинг: вёрстке достаточно каноничных имён, граф WBP пуст.
	if (Btn_Continue) { Btn_Continue->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleContinueClicked); RegisterButtonSounds(Btn_Continue); }
	if (Btn_NewGame)  { Btn_NewGame->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleNewGameClicked);   RegisterButtonSounds(Btn_NewGame); }
	if (Btn_Settings) { Btn_Settings->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleSettingsClicked); RegisterButtonSounds(Btn_Settings); }
	if (Btn_About)    { Btn_About->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleAboutClicked);       RegisterButtonSounds(Btn_About); }
	if (Btn_Quit)     { Btn_Quit->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleQuitClicked);         RegisterButtonSounds(Btn_Quit); }
}

void UMainMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// «Продолжить» активна только при существующем сохранении; перечитывается
	// при каждом возврате на экран (кампания могла появиться за это время).
	if (Btn_Continue)
	{
		Btn_Continue->SetIsEnabled(CanContinue());
	}

	// Музыка меню — отсюда, а не из GameMode: GM_MainMenu собран в Blueprint от
	// движкового класса, а этот экран точно существует в каждом заходе в меню.
	// Повторная активация (возврат из настроек) трек не перезапускает.
	if (UTacticsAudioSubsystem* Audio = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
	{
		Audio->PlayMenuMusic();
	}
}

void UMainMenuWidget::HandleContinueClicked() { RequestContinue(); }
void UMainMenuWidget::HandleNewGameClicked()  { RequestNewGame(); }
void UMainMenuWidget::HandleSettingsClicked() { RequestSettings(); }
void UMainMenuWidget::HandleAboutClicked()    { RequestAbout(); }
void UMainMenuWidget::HandleQuitClicked()     { RequestQuit(); }

bool UMainMenuWidget::CanContinue() const
{
	if (const UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		return GI->HasSaveGame();
	}
	return false;
}

void UMainMenuWidget::RequestContinue()
{
	OnContinueClicked.Broadcast();

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		if (GI->LoadCampaign())
		{
			GI->TravelToHub();
		}
	}
}

void UMainMenuWidget::RequestNewGame()
{
	OnNewGameClicked.Broadcast();
	PushScreen(DifficultyScreenClass);
}

void UMainMenuWidget::RequestSettings()
{
	OnSettingsClicked.Broadcast();
	PushScreen(SettingsScreenClass);
}

void UMainMenuWidget::RequestAbout()
{
	OnAboutClicked.Broadcast();
	PushScreen(AboutScreenClass);
}

void UMainMenuWidget::RequestQuit()
{
	OnQuitClicked.Broadcast();
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

// --- UIntroPlayerWidget -----------------------------------------------------

void UIntroPlayerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Пауза остановила бы и само видео: интро — единственный экран, который
	// обязан жить своим временем.
	bPauseGameWhileActive = false;

	if (Btn_Skip)
	{
		Btn_Skip->OnClicked.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleSkipClicked);
		RegisterButtonSounds(Btn_Skip);
	}
}

void UIntroPlayerWidget::HandleSkipClicked() { FinishIntro(); }

void UIntroPlayerWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	const UTacticalHUDStyleData* Theme = GetUITheme();
	if (!Theme)
	{
		return;
	}

	// Материал с MediaTexture — единственный способ показать видео в UMG:
	// Image рисует кадры плеера только через него.
	UMaterialInterface* VideoMaterial = Theme->IntroVideoMaterial.LoadSynchronous();
	UMediaSource* Source = Theme->IntroMediaSource.LoadSynchronous();
	IntroPlayer = Theme->IntroMediaPlayer.LoadSynchronous();

	if (Img_Intro && VideoMaterial && IntroPlayer && Source)
	{
		Img_Intro->SetBrushFromMaterial(VideoMaterial);
		Img_Intro->SetVisibility(ESlateVisibility::HitTestInvisible);

		IntroPlayer->OnEndReached.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleMediaEndReached);
		IntroPlayer->OnMediaOpenFailed.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleMediaOpenFailed);
		IntroPlayer->SetLooping(false);
		if (!IntroPlayer->OpenSource(Source))
		{
			UE_LOG(LogXRU1UI, Warning, TEXT("[Intro] Не удалось открыть медиа-источник интро"));
			HandleMediaOpenFailed(FString());
			return;
		}
		IntroPlayer->Play();
	}
	else
	{
		// Ролика нет — показываем статичный арт, экран остаётся проходимым.
		if (Img_Intro)
		{
			const FXRU1UIScreenArtwork Artwork = Theme->GetScreenArtwork(EXRU1UIScreenArt::IntroFallback);
			if (UTexture2D* Texture = Artwork.Texture.LoadSynchronous())
			{
				Img_Intro->SetBrushFromTexture(Texture);
				Img_Intro->SetColorAndOpacity(Artwork.Tint);
			}
		}
		UE_LOG(LogXRU1UI, Display,
			TEXT("[Intro] Видео не настроено (Media Player/Material/Source в теме) — показан статичный экран"));
	}

	// Страховка: без неё сбой воспроизведения оставил бы игрока на чёрном экране.
	if (MaxIntroDuration > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(IntroTimeoutTimer, this,
				&UIntroPlayerWidget::HandleMediaEndReached, MaxIntroDuration, /*bLoop=*/false);
		}
	}
}

void UIntroPlayerWidget::HandleMediaEndReached()
{
	FinishIntro();
}

void UIntroPlayerWidget::HandleMediaOpenFailed(FString /*FailedUrl*/)
{
	// Не держим игрока на пустом экране из-за отсутствующего файла/кодека.
	FinishIntro();
}

void UIntroPlayerWidget::StopIntroPlayback()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IntroTimeoutTimer);
	}
	if (IntroPlayer)
	{
		IntroPlayer->OnEndReached.RemoveDynamic(this, &UIntroPlayerWidget::HandleMediaEndReached);
		IntroPlayer->OnMediaOpenFailed.RemoveDynamic(this, &UIntroPlayerWidget::HandleMediaOpenFailed);
		IntroPlayer->Close();
		IntroPlayer = nullptr;
	}
}

void UIntroPlayerWidget::NativeDestruct()
{
	StopIntroPlayback();
	Super::NativeDestruct();
}

void UIntroPlayerWidget::FinishIntro()
{
	// Кнопка «Пропустить», конец ролика и таймаут могут прийти почти одновременно —
	// travel должен произойти ровно один раз.
	if (bIntroFinished)
	{
		return;
	}
	bIntroFinished = true;
	StopIntroPlayback();

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->TravelToHub();
	}
}

// --- UDifficultySelectWidget ------------------------------------------------

void UDifficultySelectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (Btn_Easy)   { Btn_Easy->OnClicked.AddUniqueDynamic(this, &UDifficultySelectWidget::HandleEasyClicked);     RegisterButtonSounds(Btn_Easy); }
	if (Btn_Medium) { Btn_Medium->OnClicked.AddUniqueDynamic(this, &UDifficultySelectWidget::HandleMediumClicked); RegisterButtonSounds(Btn_Medium); }
	if (Btn_Hard)   { Btn_Hard->OnClicked.AddUniqueDynamic(this, &UDifficultySelectWidget::HandleHardClicked);     RegisterButtonSounds(Btn_Hard); }
	if (Btn_Back)   { Btn_Back->OnClicked.AddUniqueDynamic(this, &UDifficultySelectWidget::HandleBackClicked);     RegisterButtonSounds(Btn_Back); }
}

void UDifficultySelectWidget::HandleEasyClicked()   { ChooseDifficulty(EDifficultyLevel::Easy); }
void UDifficultySelectWidget::HandleMediumClicked() { ChooseDifficulty(EDifficultyLevel::Medium); }
void UDifficultySelectWidget::HandleHardClicked()   { ChooseDifficulty(EDifficultyLevel::Hard); }

void UDifficultySelectWidget::ChooseDifficulty(EDifficultyLevel Difficulty)
{
	OnDifficultyChosen.Broadcast(Difficulty);

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->StartNewCampaign(Difficulty);
		if (IntroScreenClass)
		{
			PushScreen(IntroScreenClass);
		}
		else
		{
			GI->TravelToHub();
		}
	}
}

// --- UAboutMenuWidget -------------------------------------------------------

void UAboutMenuWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// Тексты из Class Defaults видны и в Designer-превью.
	if (Txt_Author)
	{
		Txt_Author->SetText(AuthorName);
	}
	if (Txt_ProjectInfo)
	{
		Txt_ProjectInfo->SetText(ProjectInfo);
	}
}

void UAboutMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (Btn_Back)
	{
		Btn_Back->OnClicked.AddUniqueDynamic(this, &UAboutMenuWidget::HandleBackClicked);
		RegisterButtonSounds(Btn_Back);
	}
}

// --- UPauseMenuWidget -------------------------------------------------------

void UPauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	bPauseGameWhileActive = true;

	if (Btn_Resume)       { Btn_Resume->OnClicked.AddUniqueDynamic(this, &UPauseMenuWidget::HandleResumeClicked);             RegisterButtonSounds(Btn_Resume); }
	if (Btn_Settings)     { Btn_Settings->OnClicked.AddUniqueDynamic(this, &UPauseMenuWidget::HandleSettingsClicked);         RegisterButtonSounds(Btn_Settings); }
	if (Btn_ReturnToMenu) { Btn_ReturnToMenu->OnClicked.AddUniqueDynamic(this, &UPauseMenuWidget::HandleReturnToMenuClicked); RegisterButtonSounds(Btn_ReturnToMenu); }
}

void UPauseMenuWidget::HandleResumeClicked() { RequestResume(); }

void UPauseMenuWidget::HandleSettingsClicked()
{
	PushScreen(SettingsScreenClass);
}

void UPauseMenuWidget::HandleReturnToMenuClicked() { RequestReturnToMenu(); }

void UPauseMenuWidget::RequestResume()
{
	OnResumeClicked.Broadcast();
	// Паузу снимет деактивация экрана (NativeOnDeactivated → PopPauseReason):
	// прямой SetGamePaused здесь снял бы и чужие причины, например «нет фокуса».
	DeactivateWidget();
}

void UPauseMenuWidget::RequestReturnToMenu()
{
	OnReturnToMenuClicked.Broadcast();
	// Уход с уровня: снимаем ВСЕ причины, иначе пауза, взятая другим экраном
	// или потерей фокуса, переживёт смену мира и заморозит меню.
	if (UGamePauseSubsystem* Pause = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UGamePauseSubsystem>() : nullptr)
	{
		Pause->ClearAllPauseReasons();
	}

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->TravelToMainMenu();
	}
}

// --- USettingsMenuWidget ----------------------------------------------------

void USettingsMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Настройки открываются и поверх боя, и поверх хаба: пока экран открыт,
	// мир под ним стоит.
	bPauseGameWhileActive = true;

	// Пять слайдеров звука: один обработчик на всех — структура всё равно
	// собирается целиком из текущих значений (STATUS §2.3).
	USlider* const AudioSliders[] = { Sld_Master, Sld_Music, Sld_Sfx, Sld_UI, Sld_Voice };
	for (USlider* Slider : AudioSliders)
	{
		if (Slider)
		{
			Slider->OnValueChanged.AddUniqueDynamic(this, &USettingsMenuWidget::HandleAudioSliderValue);
			Slider->OnMouseCaptureEnd.AddUniqueDynamic(this, &USettingsMenuWidget::HandleAudioCaptureEnd);
		}
	}

	// Выпадающий список качества: опции создаются здесь, а не в Designer,
	// чтобы порядок 0..3 всегда совпадал с ScalabilityLevel.
	if (Cmb_Quality && Cmb_Quality->GetOptionCount() == 0)
	{
		Cmb_Quality->AddOption(TEXT("Низкое"));
		Cmb_Quality->AddOption(TEXT("Среднее"));
		Cmb_Quality->AddOption(TEXT("Высокое"));
		Cmb_Quality->AddOption(TEXT("Эпическое"));
	}

	if (Btn_Apply) { Btn_Apply->OnClicked.AddUniqueDynamic(this, &USettingsMenuWidget::HandleApplyClicked); RegisterButtonSounds(Btn_Apply); }
	if (Btn_Reset) { Btn_Reset->OnClicked.AddUniqueDynamic(this, &USettingsMenuWidget::HandleResetClicked); RegisterButtonSounds(Btn_Reset); }
	if (Btn_Back)  { Btn_Back->OnClicked.AddUniqueDynamic(this, &USettingsMenuWidget::HandleBackClicked);   RegisterButtonSounds(Btn_Back); }
}

void USettingsMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	RefreshControlsFromSettings();
}

void USettingsMenuWidget::RefreshControlsFromSettings()
{
	const FTacticsAudioSettings Audio = GetAudioSettings();
	{
		const FTacticsVideoSettings Video = GetVideoSettings();
		// «Настройки открылись не с теми значениями» проверяется только логом:
		// видно, что именно прочитано и из какого источника.
		UE_LOG(LogXRU1UI, Display,
			TEXT("[Settings] прочитано: Master=%.2f Music=%.2f Sfx=%.2f UI=%.2f Voice=%.2f | ")
			TEXT("Quality=%d Scale=%.2f Fullscreen=%d VSync=%d (изображение — из GameUserSettings)"),
			Audio.MasterVolume, Audio.MusicVolume, Audio.SfxVolume, Audio.UIVolume, Audio.VoiceVolume,
			Video.ScalabilityLevel, Video.ResolutionScale, Video.bFullscreen ? 1 : 0, Video.bVSync ? 1 : 0);
	}
	// USlider::SetValue ВЫЗЫВАЕТ OnValueChanged (в логе это видно как лишнее
	// «Громкости применены» сразу после чтения). Пока значения совпадают, вреда
	// нет, но обработчик не должен принимать программную установку за действие
	// игрока — иначе первое же усложнение обработчика станет багом.
	TGuardValue<bool> RefreshGuard(bUpdatingControls, true);

	if (Sld_Master) { Sld_Master->SetValue(Audio.MasterVolume); }
	if (Sld_Music)  { Sld_Music->SetValue(Audio.MusicVolume); }
	if (Sld_Sfx)    { Sld_Sfx->SetValue(Audio.SfxVolume); }
	if (Sld_UI)     { Sld_UI->SetValue(Audio.UIVolume); }
	if (Sld_Voice)  { Sld_Voice->SetValue(Audio.VoiceVolume); }

	const FTacticsVideoSettings Video = GetVideoSettings();
	if (Cmb_Quality)
	{
		Cmb_Quality->SetSelectedIndex(FMath::Clamp(Video.ScalabilityLevel, 0, 3));
	}
	if (Sld_ResolutionScale) { Sld_ResolutionScale->SetValue(Video.ResolutionScale); }
	if (Chk_Fullscreen)      { Chk_Fullscreen->SetIsChecked(Video.bFullscreen); }
	if (Chk_VSync)           { Chk_VSync->SetIsChecked(Video.bVSync); }
}

FTacticsAudioSettings USettingsMenuWidget::CollectAudioSettings() const
{
	// База — текущий слот: отсутствующий в вёрстке слайдер не занулит громкость.
	FTacticsAudioSettings Settings = GetAudioSettings();
	if (Sld_Master) { Settings.MasterVolume = Sld_Master->GetValue(); }
	if (Sld_Music)  { Settings.MusicVolume = Sld_Music->GetValue(); }
	if (Sld_Sfx)    { Settings.SfxVolume = Sld_Sfx->GetValue(); }
	if (Sld_UI)     { Settings.UIVolume = Sld_UI->GetValue(); }
	if (Sld_Voice)  { Settings.VoiceVolume = Sld_Voice->GetValue(); }
	return Settings;
}

FTacticsVideoSettings USettingsMenuWidget::CollectVideoSettings() const
{
	FTacticsVideoSettings Settings = GetVideoSettings();
	if (Cmb_Quality && Cmb_Quality->GetSelectedIndex() != INDEX_NONE)
	{
		Settings.ScalabilityLevel = Cmb_Quality->GetSelectedIndex();
	}
	if (Sld_ResolutionScale) { Settings.ResolutionScale = Sld_ResolutionScale->GetValue(); }
	if (Chk_Fullscreen)      { Settings.bFullscreen = Chk_Fullscreen->IsChecked(); }
	if (Chk_VSync)           { Settings.bVSync = Chk_VSync->IsChecked(); }
	return Settings;
}

void USettingsMenuWidget::HandleAudioSliderValue(float /*NewValue*/)
{
	if (bUpdatingControls)
	{
		return; // это мы сами расставили контролы, а не игрок двинул ползунок
	}
	// Во время перетаскивания результат слышен сразу, но на диск не пишем.
	ApplyAudioSettings(CollectAudioSettings(), /*bSaveToSlot=*/false);
}

void USettingsMenuWidget::HandleAudioCaptureEnd()
{
	if (bUpdatingControls)
	{
		return;
	}
	ApplyAudioSettings(CollectAudioSettings(), /*bSaveToSlot=*/true);
}

void USettingsMenuWidget::HandleApplyClicked()
{
	ApplyVideoSettings(CollectVideoSettings(), /*bSaveToSlot=*/true);
}

void USettingsMenuWidget::HandleResetClicked()
{
	ResetToDefaults();
	RefreshControlsFromSettings();
}

FTacticsAudioSettings USettingsMenuWidget::GetAudioSettings() const
{
	// Единственный источник правды — UTacticsUserSettings (docs/09_UI_HUD §5.5).
	if (const UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		return UserSettings->GetAudioSettings();
	}
	return FTacticsAudioSettings();
}

FTacticsVideoSettings USettingsMenuWidget::GetVideoSettings() const
{
	if (const UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		return UserSettings->GetVideoSettings();
	}
	return FTacticsVideoSettings();
}

void USettingsMenuWidget::ApplyAudioSettings(const FTacticsAudioSettings& NewSettings, bool bSaveToSlot)
{
	// Сначала применяем: игрок должен слышать результат прямо во время
	// перетаскивания ползунка, а не после нажатия «Сохранить».
	if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->ApplyAudioSettings(NewSettings);
	}

	// Значение живёт в настройках приложения; на диск пишем по отпусканию
	// ползунка (bSaveToSlot), а не на каждый кадр перетаскивания.
	if (UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		UserSettings->SetAudioSettings(NewSettings);
		if (bSaveToSlot)
		{
			UserSettings->SaveSettings();
		}
	}
}

void USettingsMenuWidget::ApplyVideoSettings(const FTacticsVideoSettings& NewSettings, bool /*bSaveToSlot*/)
{
	UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get();
	if (!UserSettings)
	{
		UE_LOG(LogXRU1UI, Warning,
			TEXT("[Settings] UTacticsUserSettings недоступны — проверь GameUserSettingsClassName в DefaultEngine.ini"));
		return;
	}

	UserSettings->SetVideoSettings(NewSettings);
	// ApplySettings сам пишет GameUserSettings.ini, поэтому отдельного
	// сохранения (и параметра bSaveToSlot) здесь больше нет.
	// bCheckForCommandLineOverrides=false: параметры запуска не должны молча
	// отменять осознанный выбор игрока в меню.
	UserSettings->ApplySettings(/*bCheckForCommandLineOverrides=*/false);
}

void USettingsMenuWidget::ResetToDefaults()
{
	if (UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		// Дефолты звука задаёт дизайнер в DA_TacticsAudio, а не константы кода.
		UserSettings->ResetToProjectDefaults(this);
		UserSettings->ApplySettings(/*bCheckForCommandLineOverrides=*/false);
		if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
		{
			Audio->ApplyAudioSettings(UserSettings->GetAudioSettings());
		}
	}
}
