#include "TacticsGameInstance.h"
#include "QuestDefinition.h"
#include "QuestSubsystem.h"
#include "TacticalScenarioDataAsset.h"
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
	ActiveScenario = nullptr;
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
	ActiveScenario = nullptr;
	if (!MainMenuLevel.IsNull())
	{
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, MainMenuLevel);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] MainMenuLevel не задан в BP-наследнике — TravelToMainMenu() ничего не сделал"));
	}
}

bool UTacticsGameInstance::StartCombatScenario(UTacticalScenarioDataAsset* Scenario)
{
	if (!Scenario || Scenario->ScenarioId.IsNone() || SharedCombatLevel.IsNull() ||
		Scenario->ScenarioSublevel.IsNull() || Scenario->QuestDefinition.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] Нельзя запустить сценарий: "
			"Scenario/ScenarioId/SharedCombatLevel/Sublevel/Quest не настроены"));
		return false;
	}

	// Quest runtime принадлежит GameInstance и переживает OpenLevel. Каждый
	// scenario run обязан начинаться с чистого instance, иначе Completed/Failed
	// квест не создаст нового runner, а Active сохранит actor старого World.
	if (!Scenario->QuestDefinition.IsNull())
	{
		UQuestDefinition* QuestDefinition = Scenario->QuestDefinition.LoadSynchronous();
		UQuestSubsystem* Quests = GetSubsystem<UQuestSubsystem>();
		if (!QuestDefinition || !QuestDefinition->QuestId.IsValid() || !Quests ||
			(Quests->GetQuestInstance(QuestDefinition->QuestId) &&
				!Quests->ResetQuestRuntime(QuestDefinition->QuestId)))
		{
			UE_LOG(LogTemp, Error, TEXT("[GameInstance] Не удалось сбросить quest runtime сценария %s"),
				*Scenario->ScenarioId.ToString());
			return false;
		}
	}

	ActiveScenario = Scenario;
	ActiveScenarioRunId = ActiveScenarioRunId >= MAX_int32 ? 1 : ActiveScenarioRunId + 1;
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, SharedCombatLevel);
	return true;
}

bool UTacticsGameInstance::RestartActiveScenario()
{
	UTacticalScenarioDataAsset* Scenario = ActiveScenario;
	return Scenario && StartCombatScenario(Scenario);
}
