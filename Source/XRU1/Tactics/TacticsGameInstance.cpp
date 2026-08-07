#include "TacticsGameInstance.h"
#include "XRU1Log.h"
#include "QuestDefinition.h"
#include "QuestSubsystem.h"
#include "TacticalScenarioDataAsset.h"
#include "TacticsAudioSubsystem.h"
#include "TacticsSaveGame.h"
#include "TacticsUserSettings.h"
#include "GamePauseSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h" // поиск обучающих сценариев по классу
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Modules/ModuleManager.h"

void UTacticsGameInstance::Init()
{
	Super::Init();

	// Первый запуск подтягивает дизайнерские дефолты из DA_TacticsAudio и
	// фактическое состояние окна; последующие — просто применяют сохранённое.
	if (UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		UserSettings->InitializeFromProjectIfNeeded(this);
	}
	ApplySavedUserSettings();
}

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
		// Громкость и качество картинки НЕ трогаем: это настройки приложения
		// (UTacticsUserSettings), а не прогресса. Новая игра не должна сбрасывать
		// то, что игрок настроил под себя.
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

void UTacticsGameInstance::MarkTutorialScenariosCompleted()
{
	if (!CurrentSave)
	{
		UE_LOG(LogXRU1UI, Warning,
			TEXT("[Campaign] пропуск обучения запрошен без активной кампании — игнорирую"));
		return;
	}

	const FAssetRegistryModule& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Found;
	Registry.Get().GetAssetsByClass(
		UTacticalScenarioDataAsset::StaticClass()->GetClassPathName(), Found);

	int32 Marked = 0;
	for (const FAssetData& Data : Found)
	{
		const UTacticalScenarioDataAsset* Scenario = Cast<UTacticalScenarioDataAsset>(Data.GetAsset());
		if (Scenario && Scenario->Kind == ETacticalScenarioKind::Tutorial
			&& !Scenario->ScenarioId.IsNone())
		{
			CurrentSave->CompletedMissions.AddUnique(Scenario->ScenarioId);
			++Marked;
		}
	}

	SaveCampaign();
	UE_LOG(LogXRU1UI, Display,
		TEXT("[Campaign] обучение пропущено: зачтено сценариев — %d"), Marked);
}

UTacticsSaveGame* UTacticsGameInstance::LoadCampaign()
{
	CurrentSave = Cast<UTacticsSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	ApplySavedUserSettings();
	return CurrentSave;
}

void UTacticsGameInstance::ApplySavedUserSettings()
{
	// Единственный источник — UTacticsUserSettings. Слот кампании больше не
	// участвует: пока настройки жили в двух местах, меню показывало одно, а
	// движок применял другое (docs/03_ARCHITECTURE.md §12).
	if (UTacticsAudioSubsystem* Audio = GetSubsystem<UTacticsAudioSubsystem>())
	{
		Audio->ApplyAudioSettingsFromSave();
	}
	if (UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		UserSettings->ApplySettings(/*bCheckForCommandLineOverrides=*/false);
	}
}

void UTacticsGameInstance::ClearPauseBeforeTravel()
{
	if (UGamePauseSubsystem* Pause = GetSubsystem<UGamePauseSubsystem>())
	{
		Pause->ClearAllPauseReasons();
	}
}

void UTacticsGameInstance::TravelToHub()
{
	ActiveScenario = nullptr;
	if (!HubLevel.IsNull())
	{
		ClearPauseBeforeTravel();
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
		ClearPauseBeforeTravel();
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

	ClearPauseBeforeTravel();
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
