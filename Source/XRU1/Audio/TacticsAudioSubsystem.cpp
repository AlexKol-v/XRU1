#include "TacticsAudioSubsystem.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "TacticsDebug.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "XRU1Log.h"

void UTacticsAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Слот кампании на этот момент ещё не загружен; громкости применит
	// ApplyAudioSettingsFromSave после Continue/New Game либо меню настроек.
	AppliedSettings = FTacticsAudioSettings();
}

UTacticsAudioSettingsDataAsset* UTacticsAudioSubsystem::GetAudioSettingsAsset() const
{
	const UTacticsGameInstance* GameInstance = Cast<UTacticsGameInstance>(GetGameInstance());
	return GameInstance ? GameInstance->AudioSettings : nullptr;
}

void UTacticsAudioSubsystem::ApplyClassVolume(USoundClass* SoundClass, float Volume)
{
	UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset();
	if (!Asset || !Asset->UserVolumeMix || !SoundClass)
	{
		return;
	}

	UGameplayStatics::SetSoundMixClassOverride(this, Asset->UserVolumeMix, SoundClass,
		FMath::Clamp(Volume, 0.f, 1.f), /*Pitch=*/1.f, /*FadeInTime=*/0.f,
		/*bApplyToChildren=*/true);
}

void UTacticsAudioSubsystem::ApplyAudioSettings(const FTacticsAudioSettings& Settings)
{
	AppliedSettings = Settings;

	UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset();
	if (!Asset || !Asset->UserVolumeMix)
	{
		UE_LOG(LogXRU1Audio, Warning,
			TEXT("Не назначен DA_TacticsAudio (GameInstance->AudioSettings) — ползунки громкости ни на что не влияют"));
		return;
	}

	// SoundMix должен быть активен, иначе override по SoundClass не применяется.
	if (!bMixPushed)
	{
		UGameplayStatics::PushSoundMixModifier(this, Asset->UserVolumeMix);
		bMixPushed = true;
	}

	// Мастер применяется к своему классу; остальные классы — его дети, поэтому
	// итоговая громкость перемножается движком, а не нами вручную.
	ApplyClassVolume(Asset->MasterClass, Settings.MasterVolume);
	ApplyClassVolume(Asset->MusicClass, Settings.MusicVolume);
	ApplyClassVolume(Asset->SfxClass, Settings.SfxVolume);
	ApplyClassVolume(Asset->UIClass, Settings.UIVolume);
	ApplyClassVolume(Asset->VoiceClass, Settings.VoiceVolume);

	UE_LOG(LogXRU1Audio, Log,
		TEXT("Громкости применены: master=%.2f music=%.2f sfx=%.2f ui=%.2f voice=%.2f"),
		Settings.MasterVolume, Settings.MusicVolume, Settings.SfxVolume,
		Settings.UIVolume, Settings.VoiceVolume);
}

void UTacticsAudioSubsystem::ApplyAudioSettingsFromSave()
{
	const UTacticsGameInstance* GameInstance = Cast<UTacticsGameInstance>(GetGameInstance());
	if (GameInstance && GameInstance->CurrentSave)
	{
		ApplyAudioSettings(GameInstance->CurrentSave->AudioSettings);
		return;
	}
	// Кампании ещё нет (главное меню до Continue) — играем на дефолтах.
	ApplyAudioSettings(FTacticsAudioSettings());
}

void UTacticsAudioSubsystem::PlayCueAtLocation(const FTacticsSoundCue& Cue,
	const FVector& Location, USoundAttenuation* Attenuation)
{
	USoundBase* Sound = Cue.PickVariant();
	if (!Sound || !GetWorld())
	{
		return;
	}

	const float Pitch = 1.f + FMath::FRandRange(-Cue.PitchVariance, Cue.PitchVariance);
	UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, Location, FRotator::ZeroRotator,
		Cue.VolumeMultiplier, Pitch, /*StartTime=*/0.f, Attenuation);
}

void UTacticsAudioSubsystem::PlayCueAttached(const FTacticsSoundCue& Cue, AActor* Actor,
	USoundAttenuation* Attenuation)
{
	USoundBase* Sound = Cue.PickVariant();
	if (!Sound || !Actor)
	{
		if (!Sound && TacticsDebug::IsAudioLogEnabled())
		{
			UE_LOG(LogXRU1Audio, Verbose, TEXT("Пустая звуковая реплика у %s"), *GetNameSafe(Actor));
		}
		return;
	}

	const float Pitch = 1.f + FMath::FRandRange(-Cue.PitchVariance, Cue.PitchVariance);
	UGameplayStatics::SpawnSoundAttached(Sound, Actor->GetRootComponent(), NAME_None,
		FVector::ZeroVector, EAttachLocation::SnapToTarget, /*bStopWhenAttachedToDestroyed=*/false,
		Cue.VolumeMultiplier, Pitch, /*StartTime=*/0.f, Attenuation);
}

void UTacticsAudioSubsystem::PlayCue2D(const FTacticsSoundCue& Cue)
{
	USoundBase* Sound = Cue.PickVariant();
	if (!Sound || !GetWorld())
	{
		return;
	}

	const float Pitch = 1.f + FMath::FRandRange(-Cue.PitchVariance, Cue.PitchVariance);
	UGameplayStatics::PlaySound2D(GetWorld(), Sound, Cue.VolumeMultiplier, Pitch);
}

void UTacticsAudioSubsystem::PlayUIHover()
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayCue2D(Asset->UIHover);
	}
}

void UTacticsAudioSubsystem::PlayUIClick()
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayCue2D(Asset->UIClick);
	}
}

void UTacticsAudioSubsystem::PlayUIDenied()
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayCue2D(Asset->UIDenied);
	}
}

void UTacticsAudioSubsystem::PlayUnitSelected()
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayCue2D(Asset->UnitSelected);
	}
}

void UTacticsAudioSubsystem::PlayTurnStarted(bool bPlayerTurn)
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayCue2D(bPlayerTurn ? Asset->TurnStartedPlayer : Asset->TurnStartedEnemy);
	}
}
