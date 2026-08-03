#include "MissionResultWidget.h"
#include "TacticsGameInstance.h"
#include "TacticsAudioSubsystem.h"
#include "TacticalScenarioDataAsset.h"
#include "TacticalHUDStyleData.h"
#include "Kismet/GameplayStatics.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UMissionResultWidget::SetupResult(bool bInVictory, bool bInDefeatByTimeout)
{
	bVictory = bInVictory;
	bDefeatByTimeout = bInDefeatByTimeout;
	UpdateResultVisuals();
	PlayOutcomeVoice();
	OnResultReady(bVictory, bDefeatByTimeout);
}

bool UMissionResultWidget::IsDemoComplete() const
{
	if (!bVictory)
	{
		return false;
	}
	const UTacticsGameInstance* GameInstance = GetTacticsGameInstance();
	const UTacticalScenarioDataAsset* Scenario = GameInstance ? GameInstance->GetActiveScenario() : nullptr;
	return Scenario && Scenario->Kind != ETacticalScenarioKind::Tutorial;
}

void UMissionResultWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Экран результата всплывает поверх боевой карты и рисует свой арт сам
	// (Img_ResultArt): полноэкранный фон здесь только закрыл бы поле боя.
	ScreenArtKind = EXRU1UIScreenArt::None;

	if (Btn_Retry)  { Btn_Retry->OnClicked.AddUniqueDynamic(this, &UMissionResultWidget::HandleRetryClicked);   RegisterButtonSounds(Btn_Retry); }
	if (Btn_ToHub)  { Btn_ToHub->OnClicked.AddUniqueDynamic(this, &UMissionResultWidget::HandleToHubClicked);   RegisterButtonSounds(Btn_ToHub); }
	if (Btn_ToMenu) { Btn_ToMenu->OnClicked.AddUniqueDynamic(this, &UMissionResultWidget::HandleToMenuClicked); RegisterButtonSounds(Btn_ToMenu); }
}

void UMissionResultWidget::PlayOutcomeVoice()
{
	// Реплика исхода принадлежит СЦЕНАРИЮ: экран результата один на все миссии,
	// а «Зачёт, отряд к вылету готов» — текст конкретного зачёта.
	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	const UTacticalScenarioDataAsset* Scenario = GameInstance
		? GameInstance->GetActiveScenario() : nullptr;
	if (!Scenario)
	{
		return;
	}

	TSoftObjectPtr<USoundBase> Line = bVictory
		? Scenario->VictoryVoice
		: (bDefeatByTimeout && !Scenario->DefeatByTimeoutVoice.IsNull()
			? Scenario->DefeatByTimeoutVoice
			: Scenario->DefeatVoice);
	if (Line.IsNull())
	{
		return; // сценарий молчит — это штатная настройка, не ошибка
	}

	// Реплика принадлежит ЭКРАНУ: уйдя с итогов, игрок не должен слушать эпилог
	// поверх хаба (и видеть его субтитр там же).
	PlayScreenVoice(Line.LoadSynchronous()); // экран результата — не горячий путь
}

bool UMissionResultWidget::IsTutorialScenario() const
{
	const UTacticsGameInstance* GameInstance = GetTacticsGameInstance();
	const UTacticalScenarioDataAsset* Scenario = GameInstance
		? GameInstance->GetActiveScenario() : nullptr;
	return Scenario && Scenario->Kind == ETacticalScenarioKind::Tutorial;
}

void UMissionResultWidget::UpdateResultVisuals()
{
	const bool bDemoComplete = IsDemoComplete();
	// Учебный полигон — не «победа в бою», а сданный зачёт: и текст, и арт
	// у него свои, иначе экран поздравляет с победой в миссии, которой не было.
	const bool bTutorialPassed = bVictory && IsTutorialScenario();

	if (Txt_ResultTitle)
	{
		FText Title;
		if (bTutorialPassed)
		{
			Title = NSLOCTEXT("XRU1.Result", "TutorialPassed", "ЗАЧЁТ СДАН");
		}
		else if (bDemoComplete)
		{
			Title = NSLOCTEXT("XRU1.Result", "DemoComplete", "ДЕМО ПРОЙДЕНО");
		}
		else if (bVictory)
		{
			Title = NSLOCTEXT("XRU1.Result", "Victory", "ПОБЕДА");
		}
		else if (bDefeatByTimeout)
		{
			Title = NSLOCTEXT("XRU1.Result", "DefeatTimeout", "ВРЕМЯ ВЫШЛО");
		}
		else
		{
			Title = NSLOCTEXT("XRU1.Result", "Defeat", "ПОРАЖЕНИЕ");
		}
		Txt_ResultTitle->SetText(Title);
	}

	if (Txt_ResultSubtitle)
	{
		FText Subtitle;
		if (bTutorialPassed)
		{
			Subtitle = NSLOCTEXT("XRU1.Result", "TutorialPassedSub",
				"Полигон пройден. Отряд допущен к боевой операции.");
		}
		else if (bDemoComplete)
		{
			Subtitle = NSLOCTEXT("XRU1.Result", "DemoCompleteSub",
				"Спасибо за игру! Демонстрационный сценарий завершён.");
		}
		else if (bVictory)
		{
			Subtitle = NSLOCTEXT("XRU1.Result", "VictorySub", "Задача выполнена. Отряд возвращается на базу.");
		}
		else if (bDefeatByTimeout)
		{
			Subtitle = NSLOCTEXT("XRU1.Result", "DefeatTimeoutSub", "Заряд сдетонировал. Попробуйте действовать быстрее.");
		}
		else
		{
			Subtitle = NSLOCTEXT("XRU1.Result", "DefeatSub", "Отряд потерян. Попробуйте другой план.");
		}
		Txt_ResultSubtitle->SetText(Subtitle);
	}

	// Крупный арт результата — из общей темы (soft reference; экран терминальный,
	// синхронная загрузка одного арта здесь допустима).
	if (Img_ResultArt)
	{
		if (const UTacticalHUDStyleData* Theme = GetUITheme())
		{
			// У зачёта — арт учебного брифинга: он про полигон, а не про
			// победу в боевой операции.
			const EXRU1UIScreenArt ArtKind = bTutorialPassed
				? EXRU1UIScreenArt::TutorialBriefing
				: (bDemoComplete
					? EXRU1UIScreenArt::DemoComplete
					: (bVictory ? EXRU1UIScreenArt::VictoryResult : EXRU1UIScreenArt::DefeatResult));
			const FXRU1UIScreenArtwork Artwork = Theme->GetScreenArtwork(ArtKind);
			if (UTexture2D* Texture = Artwork.Texture.LoadSynchronous())
			{
				Img_ResultArt->SetBrushFromTexture(Texture);
				Img_ResultArt->SetColorAndOpacity(Artwork.Tint);
				Img_ResultArt->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				Img_ResultArt->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	// «Повторить» не показывается после пройденного демо; «На базу» остаётся.
	if (Btn_Retry)
	{
		Btn_Retry->SetVisibility(bDemoComplete ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void UMissionResultWidget::HandleRetryClicked()  { RetryMission(); }
void UMissionResultWidget::HandleToHubClicked()  { GoToHub(); }
void UMissionResultWidget::HandleToMenuClicked() { GoToMainMenu(); }

void UMissionResultWidget::RetryMission()
{
	// Scenario retry проходит через GameInstance: quest runtime очищается,
	// RunId увеличивается и открывается та же shared combat map.
	if (UTacticsGameInstance* GameInstance = GetTacticsGameInstance())
	{
		if (GameInstance->RestartActiveScenario())
		{
			return;
		}
	}

	// Legacy fallback для старых боевых карт без Scenario Data Asset.
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}

void UMissionResultWidget::GoToHub()
{
	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->TravelToHub();
	}
}

void UMissionResultWidget::GoToMainMenu()
{
	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->TravelToMainMenu();
	}
}
