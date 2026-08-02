#include "MenuWidgets.h"
#include "TacticsAudioSubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
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
#include "Components/Slider.h"
#include "Components/TextBlock.h"

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
	if (Btn_Skip)
	{
		Btn_Skip->OnClicked.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleSkipClicked);
		RegisterButtonSounds(Btn_Skip);
	}
}

void UIntroPlayerWidget::HandleSkipClicked() { FinishIntro(); }

void UIntroPlayerWidget::FinishIntro()
{
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
	UGameplayStatics::SetGamePaused(this, false);
	DeactivateWidget();
}

void UPauseMenuWidget::RequestReturnToMenu()
{
	OnReturnToMenuClicked.Broadcast();
	UGameplayStatics::SetGamePaused(this, false);

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->TravelToMainMenu();
	}
}

// --- USettingsMenuWidget ----------------------------------------------------

void USettingsMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

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
	// USlider::SetValue не бродкастит OnValueChanged — обратной петли нет.
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
	// Во время перетаскивания результат слышен сразу, но слот не переписывается
	// на каждый кадр.
	ApplyAudioSettings(CollectAudioSettings(), /*bSaveToSlot=*/false);
}

void USettingsMenuWidget::HandleAudioCaptureEnd()
{
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
	const UTacticsGameInstance* GameInstance = GetTacticsGameInstance();
	if (GameInstance && GameInstance->CurrentSave)
	{
		return GameInstance->CurrentSave->AudioSettings;
	}
	// Меню открыто до создания кампании — показываем дефолты, а не нули.
	return FTacticsAudioSettings();
}

FTacticsVideoSettings USettingsMenuWidget::GetVideoSettings() const
{
	const UTacticsGameInstance* GameInstance = GetTacticsGameInstance();
	if (GameInstance && GameInstance->CurrentSave)
	{
		return GameInstance->CurrentSave->VideoSettings;
	}
	return FTacticsVideoSettings();
}

void USettingsMenuWidget::ApplyAudioSettings(const FTacticsAudioSettings& NewSettings, bool bSaveToSlot)
{
	UTacticsGameInstance* GameInstance = GetTacticsGameInstance();
	if (!GameInstance)
	{
		return;
	}

	// Сначала применяем: игрок должен слышать результат прямо во время
	// перетаскивания ползунка, а не после нажатия «Сохранить».
	if (UTacticsAudioSubsystem* Audio = GameInstance->GetSubsystem<UTacticsAudioSubsystem>())
	{
		Audio->ApplyAudioSettings(NewSettings);
	}

	if (bSaveToSlot && GameInstance->CurrentSave)
	{
		GameInstance->CurrentSave->AudioSettings = NewSettings;
		GameInstance->SaveCampaign();
	}
}

void USettingsMenuWidget::ApplyVideoSettings(const FTacticsVideoSettings& NewSettings, bool bSaveToSlot)
{
	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings)
	{
		UE_LOG(LogXRU1UI, Warning, TEXT("UGameUserSettings недоступны — настройки изображения не применены"));
		return;
	}

	if (NewSettings.ScalabilityLevel >= 0)
	{
		UserSettings->SetOverallScalabilityLevel(FMath::Clamp(NewSettings.ScalabilityLevel, 0, 3));
	}
	UserSettings->SetResolutionScaleNormalized(FMath::Clamp(NewSettings.ResolutionScale, 0.25f, 1.f));
	UserSettings->SetFullscreenMode(NewSettings.bFullscreen
		? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
	UserSettings->SetVSyncEnabled(NewSettings.bVSync);
	// bCheckForCommandLineOverrides=false: параметры запуска не должны молча
	// отменять осознанный выбор игрока в меню.
	UserSettings->ApplySettings(/*bCheckForCommandLineOverrides=*/false);

	UTacticsGameInstance* GameInstance = GetTacticsGameInstance();
	if (bSaveToSlot && GameInstance && GameInstance->CurrentSave)
	{
		GameInstance->CurrentSave->VideoSettings = NewSettings;
		GameInstance->SaveCampaign();
	}
}

void USettingsMenuWidget::ResetToDefaults()
{
	ApplyAudioSettings(FTacticsAudioSettings());
	ApplyVideoSettings(FTacticsVideoSettings());
}
