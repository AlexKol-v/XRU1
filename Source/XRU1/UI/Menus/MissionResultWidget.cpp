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
	// Перезагрузка текущего уровня начисто.
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
