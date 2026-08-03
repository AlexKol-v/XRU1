#include "TutorialStyleData.h"

#include "Engine/World.h"
#include "TacticsGameInstance.h"

const UTutorialStyleData* UTutorialStyleData::Get(const UObject* WorldContext)
{
	// Тот же резолвер, что у UCoverTuningDataAsset: назначенный ассет → CDO.
	// Дефолты класса равны прежним числам, поэтому незаполненная ссылка не
	// выключает подсказки, а лишь лишает дизайнера настройки.
	if (WorldContext)
	{
		if (const UWorld* World = WorldContext->GetWorld())
		{
			if (const UTacticsGameInstance* GI = World->GetGameInstance<UTacticsGameInstance>())
			{
				if (const UTutorialStyleData* Style = GI->TutorialStyle)
				{
					return Style;
				}
			}
		}
	}
	return GetDefault<UTutorialStyleData>();
}
