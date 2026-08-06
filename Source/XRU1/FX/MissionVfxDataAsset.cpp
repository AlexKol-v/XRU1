#include "MissionVfxDataAsset.h"

#include "TacticsGameInstance.h"
#include "Engine/World.h"

const UMissionVfxDataAsset* UMissionVfxDataAsset::Get(const UWorld* World)
{
	// Тот же резолвер, что у CoverTuning: назначение с GameInstance, иначе CDO.
	// CDO пуст по построению — отсутствие ассета выключает эффекты, не систему.
	if (World)
	{
		if (const UTacticsGameInstance* GI = World->GetGameInstance<UTacticsGameInstance>())
		{
			if (const UMissionVfxDataAsset* Asset = GI->MissionVfx)
			{
				return Asset;
			}
		}
	}
	return GetDefault<UMissionVfxDataAsset>();
}
