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

// --- UMainMenuWidget --------------------------------------------------------

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

void UIntroPlayerWidget::FinishIntro()
{
	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->TravelToHub();
	}
}

// --- UDifficultySelectWidget ------------------------------------------------

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

// --- UPauseMenuWidget -------------------------------------------------------

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
