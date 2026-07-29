#include "MissionResultWidget.h"
#include "TacticsGameInstance.h"
#include "Kismet/GameplayStatics.h"

void UMissionResultWidget::SetupResult(bool bInVictory, bool bInDefeatByTimeout)
{
	bVictory = bInVictory;
	bDefeatByTimeout = bInDefeatByTimeout;
	OnResultReady(bVictory, bDefeatByTimeout);
}

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
