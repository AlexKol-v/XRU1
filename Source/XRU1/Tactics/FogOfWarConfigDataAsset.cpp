#include "FogOfWarConfigDataAsset.h"

#include "TacticsGameInstance.h"

#include "Engine/World.h"

const UFogOfWarConfigDataAsset* UFogOfWarConfigDataAsset::Get(const UWorld* World)
{
	// Тот же резолвер, что у тюнинга укрытий (`UTacticsCombatStatics::GetCoverTuning`):
	// ассет с GameInstance, иначе CDO. Дефолты класса рабочие, поэтому не
	// назначенный ассет — это штатная ситуация, а не поломка слоя.
	if (World)
	{
		if (const UTacticsGameInstance* GameInstance = World->GetGameInstance<UTacticsGameInstance>())
		{
			if (const UFogOfWarConfigDataAsset* Config = GameInstance->FogConfig)
			{
				return Config;
			}
		}
	}
	return GetDefault<UFogOfWarConfigDataAsset>();
}
