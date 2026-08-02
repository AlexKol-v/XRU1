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

namespace UnitAudio_Internal
{
	/** Защита от кольца ParentProfile → …: глубина наследования заведомо мала. */
	constexpr int32 MaxParentDepth = 8;
}

const FTacticsSoundCue* UUnitAudioDataAsset::FindEvent(EUnitSoundEvent Event) const
{
	const UUnitAudioDataAsset* Profile = this;
	for (int32 Depth = 0; Profile && Depth < UnitAudio_Internal::MaxParentDepth; ++Depth)
	{
		if (const FTacticsSoundCue* Found = Profile->Events.Find(Event))
		{
			// Пустая запись в дочернем профиле — это «звук намеренно выключен»
			// только если в ней есть варианты; иначе спускаемся к родителю.
			if (Found->IsValidCue())
			{
				return Found;
			}
		}
		Profile = Profile->ParentProfile;
	}
	return nullptr;
}

const FTacticsSoundCue& UUnitAudioDataAsset::FindFootstep(EPhysicalSurface Surface) const
{
	const UUnitAudioDataAsset* Profile = this;
	for (int32 Depth = 0; Profile && Depth < UnitAudio_Internal::MaxParentDepth; ++Depth)
	{
		if (const FTacticsSoundCue* Found = Profile->FootstepsBySurface.Find(Surface))
		{
			if (Found->IsValidCue())
			{
				return *Found;
			}
		}
		if (Profile->DefaultFootstep.IsValidCue())
		{
			return Profile->DefaultFootstep;
		}
		Profile = Profile->ParentProfile;
	}
	return DefaultFootstep;
}

bool UUnitAudioDataAsset::UsesSurfaceFootsteps() const
{
	const UUnitAudioDataAsset* Profile = this;
	for (int32 Depth = 0; Profile && Depth < UnitAudio_Internal::MaxParentDepth; ++Depth)
	{
		if (Profile->FootstepsBySurface.Num() > 0)
		{
			return true;
		}
		Profile = Profile->ParentProfile;
	}
	return false;
}

USoundAttenuation* UUnitAudioDataAsset::ResolveAttenuation() const
{
	const UUnitAudioDataAsset* Profile = this;
	for (int32 Depth = 0; Profile && Depth < UnitAudio_Internal::MaxParentDepth; ++Depth)
	{
		if (Profile->Attenuation)
		{
			return Profile->Attenuation;
		}
		Profile = Profile->ParentProfile;
	}
	return nullptr;
}
