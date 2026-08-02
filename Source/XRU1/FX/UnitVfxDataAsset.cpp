#include "UnitVfxDataAsset.h"

UNiagaraSystem* UUnitVfxDataAsset::FindImpact(EPhysicalSurface Surface) const
{
	if (const TObjectPtr<UNiagaraSystem>* Found = ImpactBySurface.Find(Surface))
	{
		if (*Found)
		{
			return *Found;
		}
	}
	return DefaultImpact;
}
