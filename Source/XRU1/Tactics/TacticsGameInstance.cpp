#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "Kismet/GameplayStatics.h"

bool UTacticsGameInstance::HasSaveGame() const
{
	return UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0);
}

UTacticsSaveGame* UTacticsGameInstance::StartNewCampaign(EDifficultyLevel Difficulty)
{
	CurrentSave = Cast<UTacticsSaveGame>(UGameplayStatics::CreateSaveGameObject(UTacticsSaveGame::StaticClass()));
	if (CurrentSave)
	{
		CurrentSave->Difficulty = Difficulty;
		CurrentSave->CompletedMissions.Reset();
		CurrentSave->SquadRoles = { EUnitRole::Assault, EUnitRole::Sniper, EUnitRole::Healer, EUnitRole::Tank };
		SaveCampaign();
	}
	return CurrentSave;
}

bool UTacticsGameInstance::SaveCampaign()
{
	if (!CurrentSave)
	{
		return false;
	}
	return UGameplayStatics::SaveGameToSlot(CurrentSave, SaveSlotName, 0);
}

UTacticsSaveGame* UTacticsGameInstance::LoadCampaign()
{
	CurrentSave = Cast<UTacticsSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	return CurrentSave;
}

void UTacticsGameInstance::TravelToHub()
{
	if (!HubLevel.IsNull())
	{
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, HubLevel);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] HubLevel не задан в BP-наследнике — TravelToHub() ничего не сделал"));
	}
}

void UTacticsGameInstance::TravelToMainMenu()
{
	if (!MainMenuLevel.IsNull())
	{
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, MainMenuLevel);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] MainMenuLevel не задан в BP-наследнике — TravelToMainMenu() ничего не сделал"));
	}
}
