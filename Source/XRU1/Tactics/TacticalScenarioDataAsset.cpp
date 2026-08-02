#include "TacticalScenarioDataAsset.h"

#include "TacticsSaveGame.h"
#include "Internationalization/Text.h"

bool UTacticalScenarioDataAsset::ArePrerequisitesMet(const UTacticsSaveGame* Save) const
{
	if (RequiredMissions.IsEmpty())
	{
		return true;
	}
	if (!Save)
	{
		// Кампании нет (прямой запуск хаба в PIE): решает флаг миссии, а не
		// молчаливое «разрешить всё» — иначе прогрессию нельзя проверить вживую.
		return bAvailableWithoutCampaign;
	}

	for (const TSoftObjectPtr<UTacticalScenarioDataAsset>& Required : RequiredMissions)
	{
		const UTacticalScenarioDataAsset* RequiredScenario = Required.LoadSynchronous();
		if (!RequiredScenario || RequiredScenario->ScenarioId.IsNone())
		{
			continue; // незаполненная строка требований не должна блокировать миссию
		}
		if (!Save->IsMissionCompleted(RequiredScenario->ScenarioId))
		{
			return false;
		}
	}
	return true;
}

FText UTacticalScenarioDataAsset::GetLockedReason(const UTacticsSaveGame* Save) const
{
	if (ArePrerequisitesMet(Save))
	{
		return FText::GetEmpty();
	}
	if (!LockedHintOverride.IsEmpty())
	{
		return LockedHintOverride;
	}

	// Перечисляем именно НЕВЫПОЛНЕННЫЕ требования: «пройдите обучение» после
	// пройденного обучения выглядит как баг.
	TArray<FText> MissingNames;
	for (const TSoftObjectPtr<UTacticalScenarioDataAsset>& Required : RequiredMissions)
	{
		const UTacticalScenarioDataAsset* RequiredScenario = Required.LoadSynchronous();
		if (!RequiredScenario || RequiredScenario->ScenarioId.IsNone())
		{
			continue;
		}
		if (!Save || !Save->IsMissionCompleted(RequiredScenario->ScenarioId))
		{
			MissingNames.Add(RequiredScenario->GetDisplayNameSafe());
		}
	}

	if (MissingNames.IsEmpty())
	{
		// Требования есть, но все ссылки битые/пустые — не врём про конкретику.
		return NSLOCTEXT("XRU1.Scenario", "LockedGeneric", "Операция пока недоступна");
	}

	// Кавычки не добавляем: DisplayName их уже содержит («Полигон «Купол»»
	// читалось как ошибка вёрстки).
	return FText::Format(
		NSLOCTEXT("XRU1.Scenario", "LockedRequires", "Недоступно: сначала пройдите {0}"),
		FText::Join(NSLOCTEXT("XRU1.Scenario", "LockedJoin", ", "), MissingNames));
}

FText UTacticalScenarioDataAsset::GetDisplayNameSafe() const
{
	return DisplayName.IsEmpty() ? FText::FromName(ScenarioId) : DisplayName;
}
