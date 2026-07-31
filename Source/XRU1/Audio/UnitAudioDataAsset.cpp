#include "UnitAudioDataAsset.h"

#include "Sound/SoundBase.h"

USoundBase* FTacticsSoundCue::PickVariant() const
{
	if (Variants.Num() == 0)
	{
		return nullptr;
	}
	// Пустые слоты в массиве — обычная ситуация при заполнении ассета дизайнером;
	// они не должны превращаться в «тишину через раз».
	TArray<USoundBase*> Valid;
	Valid.Reserve(Variants.Num());
	for (const TObjectPtr<USoundBase>& Variant : Variants)
	{
		if (Variant)
		{
			Valid.Add(Variant);
		}
	}
	if (Valid.Num() == 0)
	{
		return nullptr;
	}
	return Valid[FMath::RandRange(0, Valid.Num() - 1)];
}

const FTacticsSoundCue& UUnitAudioDataAsset::FindFootstep(EPhysicalSurface Surface) const
{
	if (const FTacticsSoundCue* Found = FootstepsBySurface.Find(Surface))
	{
		if (Found->IsValidCue())
		{
			return *Found;
		}
	}
	return DefaultFootstep;
}
