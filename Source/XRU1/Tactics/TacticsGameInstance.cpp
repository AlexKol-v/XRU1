#include "TacticsGameInstance.h"
#include "XRU1Log.h"
#include "QuestDefinition.h"
#include "QuestSubsystem.h"
#include "TacticalScenarioDataAsset.h"
#include "TacticsAudioSubsystem.h"
#include "TacticsSaveGame.h"
#include "GameFramework/GameUserSettings.h"
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
		ApplySavedUserSettings();
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
	ApplySavedUserSettings();
	return CurrentSave;
}

void UTacticsGameInstance::ApplySavedUserSettings()
{
	if (UTacticsAudioSubsystem* Audio = GetSubsystem<UTacticsAudioSubsystem>())
	{
		Audio->ApplyAudioSettingsFromSave();
	}

	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings || !CurrentSave)
	{
		return;
	}

	const FTacticsVideoSettings& Video = CurrentSave->VideoSettings;
	if (Video.ScalabilityLevel >= 0)
	{
		UserSettings->SetOverallScalabilityLevel(FMath::Clamp(Video.ScalabilityLevel, 0, 3));
	}
	UserSettings->SetResolutionScaleNormalized(FMath::Clamp(Video.ResolutionScale, 0.25f, 1.f));
	UserSettings->SetFullscreenMode(Video.bFullscreen
		? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
	UserSettings->SetVSyncEnabled(Video.bVSync);
	UserSettings->ApplySettings(/*bCheckForCommandLineOverrides=*/false);
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
		UE_LOG(LogXRU1Scenario, Warning, TEXT("[GameInstance] HubLevel не задан в BP-наследнике — TravelToHub() ничего не сделал"));
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
		UE_LOG(LogXRU1Scenario, Warning, TEXT("[GameInstance] MainMenuLevel не задан в BP-наследнике — TravelToMainMenu() ничего не сделал"));
	}
}

bool UTacticsGameInstance::PrepareScenarioRun(UTacticalScenarioDataAsset* Scenario)
{
	if (!Scenario || Scenario->ScenarioId.IsNone() ||
		Scenario->ScenarioSublevel.IsNull() || Scenario->QuestDefinition.IsNull())
	{
		UE_LOG(LogXRU1Scenario, Warning, TEXT("[GameInstance] Нельзя запустить сценарий: "
			"Scenario/ScenarioId/Sublevel/Quest не настроены"));
		return false;
	}

	// Quest runtime принадлежит GameInstance и переживает OpenLevel. Каждый
	// scenario run обязан начинаться с чистого instance, иначе Completed/Failed
	// квест не создаст нового runner, а Active сохранит actor старого World.
	UQuestDefinition* QuestDefinition = Scenario->QuestDefinition.LoadSynchronous();
	UQuestSubsystem* Quests = GetSubsystem<UQuestSubsystem>();
	if (!QuestDefinition || !QuestDefinition->QuestId.IsValid() || !Quests ||
		(Quests->GetQuestInstance(QuestDefinition->QuestId) &&
			!Quests->ResetQuestRuntime(QuestDefinition->QuestId)))
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[GameInstance] Не удалось сбросить quest runtime сценария %s"),
			*Scenario->ScenarioId.ToString());
		return false;
	}

	ActiveScenario = Scenario;
	ActiveScenarioRunId = ActiveScenarioRunId >= MAX_int32 ? 1 : ActiveScenarioRunId + 1;
	return true;
}

bool UTacticsGameInstance::StartCombatScenario(UTacticalScenarioDataAsset* Scenario)
{
	if (SharedCombatLevel.IsNull())
	{
		UE_LOG(LogXRU1Scenario, Warning, TEXT("[GameInstance] SharedCombatLevel не задан в BP-наследнике"));
		return false;
	}
	if (!PrepareScenarioRun(Scenario))
	{
		return false;
	}

	UGameplayStatics::OpenLevelBySoftObjectPtr(this, SharedCombatLevel);
	return true;
}

bool UTacticsGameInstance::AdoptScenarioInPlace(UTacticalScenarioDataAsset* Scenario)
{
	// Общая карта уже открыта (прямой PIE или editor-preview) — travel не нужен,
	// но run обязан пройти тот же сброс quest runtime и получить новый RunId.
	if (ActiveScenario)
	{
		return ActiveScenario == Scenario;
	}
	return PrepareScenarioRun(Scenario);
}

bool UTacticsGameInstance::RestartActiveScenario()
{
	UTacticalScenarioDataAsset* Scenario = ActiveScenario;
	return Scenario && StartCombatScenario(Scenario);
}
