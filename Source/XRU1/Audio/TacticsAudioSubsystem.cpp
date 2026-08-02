#include "TacticsAudioSubsystem.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "TacticsUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "TacticsDebug.h"
#include "TacticsGameInstance.h"
#include "XRU1Log.h"

namespace
{
	/** Диагностика звука по требованию: `xru1.Audio.Dump` в консоли игры. */
	static FAutoConsoleCommandWithWorld GDumpAudioStateCommand(
		TEXT("xru1.Audio.Dump"),
		TEXT("Печатает состояние звука: базовый микс, активные override'ы, SoundClass'ы, громкости."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			if (UTacticsAudioSubsystem* Audio = GameInstance
				? GameInstance->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
			{
				Audio->DumpAudioState();
			}
		}));
}

void UTacticsAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AppliedSettings = FTacticsAudioSettings();

	// Громкости обязаны переприменяться на КАЖДЫЙ мир. На старте GameInstance
	// аудио-устройство мира ещё не создано («Creating Audio Device» идёт позже
	// в логе), поэтому базовый SoundMix, назначенный там, не доживает до игры:
	// прямой запуск боевой карты звучал на полной громкости, хотя настройки
	// «применялись». Из хаба это маскировалось повторным применением.
	// Ровно то событие, которого нам не хватало: мир получил своё аудио-устройство.
	AudioDeviceRegisteredHandle = FAudioDeviceWorldDelegates::OnWorldRegisteredToAudioDevice.AddUObject(
		this, &UTacticsAudioSubsystem::HandleWorldRegisteredToAudioDevice);

	// Сторож музыки. «Музыка молча пропала» — жалоба, которую по логу не
	// восстановить: остановить компонент может кто угодно (смена трека, стингер,
	// приглушение на паузе, потеря аудио-устройства). Тикер СИСТЕМНЫЙ (не
	// мировой): он продолжает работать на паузе и потому отличает «мир стоит»
	// от «трек умер».
	MusicWatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTacticsAudioSubsystem::TickMusicWatchdog), 2.f);
}

void UTacticsAudioSubsystem::Deinitialize()
{
	// Ассеты SoundClass глобальны и переживают PIE: без восстановления редактор
	// остался бы с прикрученной громкостью, а ассет — изменённым.
	RestoreOriginalClassVolumes();

	if (AudioDeviceRegisteredHandle.IsValid())
	{
		FAudioDeviceWorldDelegates::OnWorldRegisteredToAudioDevice.Remove(AudioDeviceRegisteredHandle);
		AudioDeviceRegisteredHandle.Reset();
	}
	if (MusicWatchdogHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(MusicWatchdogHandle);
		MusicWatchdogHandle.Reset();
	}
	Super::Deinitialize();
}

void UTacticsAudioSubsystem::HandleWorldRegisteredToAudioDevice(const UWorld* World, Audio::FDeviceId /*DeviceId*/)
{
	// Чужие миры (другой PIE-инстанс, редакторские) не наши.
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	// Запоминаем именно этот мир: все звуковые вызовы должны идти в него.
	AudioWorld = const_cast<UWorld*>(World);

	UE_LOG(LogXRU1Audio, Display,
		TEXT("[Audio] мир '%s' получил аудио-устройство — переприменяю пользовательские громкости"),
		*World->GetName());
	ApplyAudioSettingsFromSave();
}

UTacticsAudioSettingsDataAsset* UTacticsAudioSubsystem::GetAudioSettingsAsset() const
{
	const UTacticsGameInstance* GameInstance = Cast<UTacticsGameInstance>(GetGameInstance());
	return GameInstance ? GameInstance->AudioSettings : nullptr;
}

void UTacticsAudioSubsystem::DumpAudioState()
{
	UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset();
	UWorld* World = GetAudioWorld();

	UE_LOG(LogXRU1Audio, Display, TEXT("======== [Audio] СОСТОЯНИЕ ЗВУКА ========"));
	UE_LOG(LogXRU1Audio, Display, TEXT("Ассет микшера: %s"), *GetNameSafe(Asset));
	UE_LOG(LogXRU1Audio, Display, TEXT("Мир: %s | AudioWorld закэширован: %s"),
		*GetNameSafe(World), AudioWorld.IsValid() ? TEXT("да") : TEXT("нет"));
	UE_LOG(LogXRU1Audio, Display,
		TEXT("Применённые громкости: master=%.2f music=%.2f sfx=%.2f ui=%.2f voice=%.2f"),
		AppliedSettings.MasterVolume, AppliedSettings.MusicVolume, AppliedSettings.SfxVolume,
		AppliedSettings.UIVolume, AppliedSettings.VoiceVolume);

	if (const UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		const FTacticsAudioSettings Saved = UserSettings->GetAudioSettings();
		UE_LOG(LogXRU1Audio, Display,
			TEXT("Сохранённые настройки:   master=%.2f music=%.2f sfx=%.2f ui=%.2f voice=%.2f"),
			Saved.MasterVolume, Saved.MusicVolume, Saved.SfxVolume, Saved.UIVolume, Saved.VoiceVolume);
	}
	else
	{
		UE_LOG(LogXRU1Audio, Warning, TEXT("UTacticsUserSettings НЕ доступен (проверь GameUserSettingsClassName)"));
	}

	if (Asset)
	{
		auto DumpClass = [](const TCHAR* Label, USoundClass* SoundClass)
		{
			if (!SoundClass)
			{
				UE_LOG(LogXRU1Audio, Warning, TEXT("  %s: НЕ НАЗНАЧЕН"), Label);
				return;
			}
			UE_LOG(LogXRU1Audio, Display, TEXT("  %s: %s | базовый Volume=%.2f | детей=%d | родитель=%s"),
				Label, *SoundClass->GetName(), SoundClass->Properties.Volume,
				SoundClass->ChildClasses.Num(), *GetNameSafe(SoundClass->ParentClass));
		};
		UE_LOG(LogXRU1Audio, Display, TEXT("SoundClass'ы:"));
		DumpClass(TEXT("Master"), Asset->MasterClass);
		DumpClass(TEXT("Music "), Asset->MusicClass);
		DumpClass(TEXT("Sfx   "), Asset->SfxClass);
		DumpClass(TEXT("UI    "), Asset->UIClass);
		DumpClass(TEXT("Voice "), Asset->VoiceClass);
	}

	if (World)
	{
		if (FAudioDeviceHandle Device = World->GetAudioDevice())
		{
			UE_LOG(LogXRU1Audio, Display, TEXT("Аудио-устройство: id=%u | базовый микс: %s"),
				(uint32)Device.GetDeviceID(), *GetNameSafe(Device->GetDefaultBaseSoundMixModifier()));

			for (const TPair<USoundMix*, FSoundMixState>& Pair : Device->GetSoundMixModifiers())
			{
				const FSoundMixState& State = Pair.Value;
				UE_LOG(LogXRU1Audio, Display,
					TEXT("  микс %s: базовый=%s | активных ссылок=%u | состояние=%d | интерполяция=%.2f"),
					*GetNameSafe(Pair.Key), State.IsBaseSoundMix ? TEXT("ДА") : TEXT("нет"),
					State.ActiveRefCount, (int32)State.CurrentState, State.InterpValue);
			}

			// Правила самого ассета микса (мы их не меняем — справочно).
			if (Asset->UserVolumeMix)
			{
				for (const FSoundClassAdjuster& Adjuster : Asset->UserVolumeMix->SoundClassEffects)
				{
					UE_LOG(LogXRU1Audio, Display,
						TEXT("    правило микса: %s → volume=%.2f (к детям: %s)"),
						*GetNameSafe(Adjuster.SoundClassObject), Adjuster.VolumeAdjuster,
						Adjuster.bApplyToChildren ? TEXT("да") : TEXT("нет"));
				}
			}
		}
		else
		{
			UE_LOG(LogXRU1Audio, Warning, TEXT("У мира нет аудио-устройства"));
		}
	}

	if (const UAudioComponent* Music = MusicComponent.Get())
	{
		const USoundBase* Track = Music->Sound;
		UE_LOG(LogXRU1Audio, Display,
			TEXT("Музыка: '%s' | играет=%s | SoundClass=%s | VolumeMultiplier=%.2f"),
			*GetNameSafe(Track), Music->IsPlaying() ? TEXT("да") : TEXT("НЕТ"),
			Track ? *GetNameSafe(Track->GetSoundClass()) : TEXT("<нет>"),
			Music->VolumeMultiplier);
	}
	else
	{
		UE_LOG(LogXRU1Audio, Display, TEXT("Музыка: компонента нет"));
	}
	UE_LOG(LogXRU1Audio, Display, TEXT("========================================="));
}

UWorld* UTacticsAudioSubsystem::GetAudioWorld() const
{
	// Мир, у которого есть аудио-устройство. Подсистема живёт в GameInstance, и
	// её GetWorld() в момент регистрации нового мира ещё указывает в пустоту —
	// звуковые вызовы уходили «в никуда», и при прямом запуске боевой карты
	// громкости не действовали (через хаб маскировалось повторным применением).
	if (UWorld* Known = AudioWorld.Get())
	{
		return Known;
	}
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

void UTacticsAudioSubsystem::ApplyClassVolume(USoundClass* SoundClass, float Volume)
{
	if (!SoundClass)
	{
		return;
	}
	const float Clamped = FMath::Clamp(Volume, 0.f, 1.f);

	// Исходную громкость класса запоминаем один раз: в редакторе её надо вернуть
	// на выходе из PIE, иначе ассет останется «прикрученным» и уедет в git.
	if (!OriginalClassVolumes.Contains(SoundClass))
	{
		OriginalClassVolumes.Add(SoundClass, SoundClass->Properties.Volume);
	}

	// ЕДИНСТВЕННЫЙ способ применения громкости игрока — свойство SoundClass.
	// SoundMix-override здесь НЕ дублируется намеренно: два пути к одному
	// значению — это два источника правды, а mix-путь в этом проекте до звука
	// вообще не доходил (микс числился базовым и активным, override
	// «применялся», громкость не менялась). Иерархия классов перемножается
	// движком, поэтому Master действует на все категории.
	SoundClass->Properties.Volume = Clamped;
}

void UTacticsAudioSubsystem::RestoreOriginalClassVolumes()
{
	// Только для редактора: в игре процесс всё равно завершается.
	for (const TPair<TObjectPtr<USoundClass>, float>& Pair : OriginalClassVolumes)
	{
		if (USoundClass* SoundClass = Pair.Key)
		{
			SoundClass->Properties.Volume = Pair.Value;
		}
	}
	OriginalClassVolumes.Reset();
}

void UTacticsAudioSubsystem::ApplyAudioSettings(const FTacticsAudioSettings& Settings)
{
	AppliedSettings = Settings;

	UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset();
	if (!Asset)
	{
		UE_LOG(LogXRU1Audio, Warning,
			TEXT("Не назначен DA_TacticsAudio (GameInstance->AudioSettings) — ползунки громкости ни на что не влияют"));
		return;
	}

	// Громкости применяются НЕЗАВИСИМО от наличия мира и микса: они живут в
	// SoundClass, а он — глобальный ассет. Раньше здесь стоял ранний выход по
	// отсутствию микса/мира, и настройки молча терялись.
	ApplyClassVolume(Asset->MasterClass, Settings.MasterVolume);
	ApplyClassVolume(Asset->MusicClass, Settings.MusicVolume);
	ApplyClassVolume(Asset->SfxClass, Settings.SfxVolume);
	ApplyClassVolume(Asset->UIClass, Settings.UIVolume);
	ApplyClassVolume(Asset->VoiceClass, Settings.VoiceVolume);

	UE_LOG(LogXRU1Audio, Log,
		TEXT("Громкости применены: master=%.2f music=%.2f sfx=%.2f ui=%.2f voice=%.2f"),
		Settings.MasterVolume, Settings.MusicVolume, Settings.SfxVolume,
		Settings.UIVolume, Settings.VoiceVolume);

	// Микс держим активным ради будущих эффектов (приглушить бой под реплику),
	// но пользовательская громкость от него уже не зависит.
	UWorld* AudioContext = GetAudioWorld();
	if (!AudioContext || !Asset->UserVolumeMix)
	{
		return;
	}
	UGameplayStatics::SetBaseSoundMix(AudioContext, Asset->UserVolumeMix);

	// Полная картина состояния микшера: без неё «громкости применены, но звук
	// не изменился» невозможно отличить от «применены не туда».
	if (FAudioDeviceHandle Device = AudioContext->GetAudioDevice())
	{
		const USoundMix* BaseMix = Device->GetDefaultBaseSoundMixModifier();
		const TMap<USoundMix*, FSoundMixState>& ActiveMixes = Device->GetSoundMixModifiers();
		FString MixList;
		for (const TPair<USoundMix*, FSoundMixState>& Pair : ActiveMixes)
		{
			MixList += FString::Printf(TEXT("%s(%s) "), *GetNameSafe(Pair.Key),
				Pair.Value.IsBaseSoundMix ? TEXT("базовый") : TEXT("активный"));
		}
		UE_LOG(LogXRU1Audio, Display,
			TEXT("[Audio] микшер: world='%s' device=%u | базовый микс: %s | нужен: %s | активные: %s"),
			*AudioContext->GetName(), (uint32)Device.GetDeviceID(),
			*GetNameSafe(BaseMix), *GetNameSafe(Asset->UserVolumeMix),
			MixList.IsEmpty() ? TEXT("<нет>") : *MixList);

		if (BaseMix != Asset->UserVolumeMix)
		{
			UE_LOG(LogXRU1Audio, Warning,
				TEXT("[Audio] базовый микс НЕ наш (%s). Проверь Project Settings → Audio → ")
				TEXT("Default Base Sound Mix = SM_UserVolumes — иначе ползунки громкости не звучат"),
				*GetNameSafe(BaseMix));
		}
	}
	else
	{
		UE_LOG(LogXRU1Audio, Warning,
			TEXT("[Audio] у мира '%s' нет аудио-устройства — микс не активирован"),
			*AudioContext->GetName());
	}
}

void UTacticsAudioSubsystem::ApplyAudioSettingsFromSave()
{
	// Громкости живут в настройках приложения, а не в слоте кампании: они не
	// часть прогресса и обязаны переживать «Новая игра» (docs/09_UI_HUD §5.5).
	if (const UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		ApplyAudioSettings(UserSettings->GetAudioSettings());
		return;
	}
	ApplyAudioSettings(GetDefaultAudioSettings());
}

FTacticsAudioSettings UTacticsAudioSubsystem::GetDefaultAudioSettings() const
{
	// Дизайнерские дефолты живут в DA_TacticsAudio; C++-значения структуры —
	// только страховка на случай неназначенного ассета микшера.
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		return Asset->DefaultVolumes;
	}
	return FTacticsAudioSettings();
}

void UTacticsAudioSubsystem::SetGameplayAudioPaused(bool bPaused)
{
	if (bGameplayAudioPaused == bPaused)
	{
		return;
	}
	bGameplayAudioPaused = bPaused;

	// Реплика именно ставится на паузу, а не обрывается: после снятия она
	// продолжится с того же слова, а не начнётся заново.
	UAudioComponent* Voice = VoiceComponent.Get();
	if (Voice && IsValid(Voice))
	{
		Voice->SetPaused(bPaused);
	}

	// Громкости игрока НЕ трогаем. Прежняя схема переписывала их на время паузы
	// и, если снятие не приходило (или приходило в другом GameInstance), звук
	// оставался выключенным навсегда. Transient-громкость устройства обратима
	// по определению: она не хранится и не сохраняется.
	// Мир берём тот же, что и для громкостей: GetWorld() подсистемы указывает
	// не туда ровно в тех же случаях (см. GetAudioWorld).
	if (UWorld* World = GetAudioWorld())
	{
		if (FAudioDeviceHandle Device = World->GetAudioDevice())
		{
			Device->SetTransientPrimaryVolume(bPaused ? 0.f : 1.f);
		}
	}

	UE_LOG(LogXRU1Audio, Display,
		TEXT("[Audio] звук %s | голос: %s | громкости игрока не менялись (Master=%.2f Sfx=%.2f)"),
		bPaused ? TEXT("ПРИГЛУШЁН (transient=0)") : TEXT("ВОССТАНОВЛЕН (transient=1)"),
		Voice ? (bPaused ? TEXT("на паузе") : TEXT("продолжен")) : TEXT("нет активной реплики"),
		AppliedSettings.MasterVolume, AppliedSettings.SfxVolume);
}

void UTacticsAudioSubsystem::PlayCueAtLocation(const FTacticsSoundCue& Cue,
	const FVector& Location, USoundAttenuation* Attenuation)
{
	USoundBase* Sound = Cue.PickVariant();
	if (!Sound || !GetAudioWorld())
	{
		return;
	}

	const float Pitch = 1.f + FMath::FRandRange(-Cue.PitchVariance, Cue.PitchVariance);
	UGameplayStatics::PlaySoundAtLocation(GetAudioWorld(), Sound, Location, FRotator::ZeroRotator,
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
	if (!Sound || !GetAudioWorld())
	{
		return;
	}

	const float Pitch = 1.f + FMath::FRandRange(-Cue.PitchVariance, Cue.PitchVariance);
	UGameplayStatics::PlaySound2D(GetAudioWorld(), Sound, Cue.VolumeMultiplier, Pitch);
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

void UTacticsAudioSubsystem::PlayBombTick()
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		// Именно 2D: тик — это счётчик миссии, а не объект на карте. Игрок не
		// должен «отъезжать камерой» от угрозы.
		PlayCue2D(Asset->BombTick);
	}
}

void UTacticsAudioSubsystem::PlayBombDefuse(const FVector& Location, bool bComplete)
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayCueAtLocation(bComplete ? Asset->BombDisarmed : Asset->BombDefuseStep, Location);
	}
}

void UTacticsAudioSubsystem::PlayEvacZoneActivated(const FVector& Location)
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayCueAtLocation(Asset->EvacZoneActivated, Location);
	}
}

void UTacticsAudioSubsystem::PlayEvacUnit(const FVector& Location)
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayCueAtLocation(Asset->EvacUnit, Location);
	}
}

UAudioComponent* UTacticsAudioSubsystem::PlayVoice2D(USoundBase* Voice, float VolumeMultiplier)
{
	if (!Voice || !GetAudioWorld())
	{
		return nullptr;
	}

	if (UAudioComponent* Previous = VoiceComponent.Get())
	{
		Previous->Stop();
	}

	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(GetAudioWorld(), Voice,
		VolumeMultiplier, /*PitchMultiplier=*/1.f, /*StartTime=*/0.f, /*ConcurrencySettings=*/nullptr,
		/*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/true);
	VoiceComponent = Component;
	return Component;
}

void UTacticsAudioSubsystem::PlayMusic(USoundBase* Track, float FadeInTime)
{
	if (!Track || !GetAudioWorld())
	{
		return;
	}

	UAudioComponent* Existing = MusicComponent.Get();
	if (CurrentMusicTrack.Get() == Track && Existing && Existing->IsPlaying())
	{
		// Повторный вход в то же состояние (retry миссии) не перезапускает трек
		// с нуля: рестарт музыки на ровном месте слышен сильнее, чем кажется.
		return;
	}

	const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset();
	const float Fade = FadeInTime >= 0.f ? FadeInTime : (Asset ? Asset->MusicFadeTime : 2.f);

	if (Existing)
	{
		Existing->FadeOut(Fade, 0.f);
	}

	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(GetAudioWorld(), Track,
		/*VolumeMultiplier=*/1.f, /*PitchMultiplier=*/1.f, /*StartTime=*/0.f,
		/*ConcurrencySettings=*/nullptr, /*bPersistAcrossLevelTransition=*/true,
		/*bAutoDestroy=*/false);
	if (Component)
	{
		Component->FadeIn(Fade, 1.f);
	}
	MusicComponent = Component;
	CurrentMusicTrack = Track;

	// Никаких подписок на OnAudioFinished: у ЗАЦИКЛЕННОГО звука это событие
	// означает «его остановили» (подтверждено форумом Epic — при обычном цикле
	// оно не приходит вовсе). Прежняя «страховка» ловила завершение FadeOut
	// предыдущего трека и перезапускала музыку поверх играющей, из-за чего
	// музыка и замолкала через ~20 секунд. Трек должен быть зациклен в ассете —
	// это его свойство, а не работа кода.
	UE_LOG(LogXRU1Audio, Log, TEXT("[Audio] музыка: '%s' (длительность %.0f с%s)"),
		*Track->GetName(), Track->GetDuration(),
		Track->IsLooping() ? TEXT(", зациклен") : TEXT(", НЕ зациклен — поставь Looping в ассете"));
}

void UTacticsAudioSubsystem::PlayMenuMusic()
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayMusic(Asset->MenuMusic);
	}
}

void UTacticsAudioSubsystem::PlayHubMusic()
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayMusic(Asset->HubMusic);
	}
}

void UTacticsAudioSubsystem::PlayCombatMusic()
{
	if (const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset())
	{
		PlayMusic(Asset->CombatMusic);
	}
}

bool UTacticsAudioSubsystem::TickMusicWatchdog(float DeltaTime)
{
	const UAudioComponent* Music = MusicComponent.Get();
	const bool bPlaying = Music && IsValid(Music) && Music->IsPlaying();
	if (bPlaying == bMusicWasPlaying)
	{
		return true; // состояние не менялось — молчим, чтобы не спамить
	}
	bMusicWasPlaying = bPlaying;

	const UWorld* World = GetAudioWorld();
	const bool bWorldPaused = World && World->IsPaused();
	if (bPlaying)
	{
		UE_LOG(LogXRU1Audio, Display, TEXT("[Audio|Сторож] музыка ПОШЛА: '%s'"),
			*GetNameSafe(Music->Sound));
		bMusicStoppedIntentionally = false;
		return true;
	}

	// Тишину заказали явно (StopMusic под стингер исхода) — это не находка.
	if (bMusicStoppedIntentionally)
	{
		UE_LOG(LogXRU1Audio, Display, TEXT("[Audio|Сторож] музыка молчит штатно (её остановили)"));
		return true;
	}

	// Ключевая строка расследования «музыка молча пропала»: сразу видно, кто
	// подозреваемый — некому играть, мир на паузе, приглушение или компонент.
	UE_LOG(LogXRU1Audio, Warning,
		TEXT("[Audio|Сторож] музыка МОЛЧИТ | компонент=%s | трек=%s | мир на паузе=%d | ")
		TEXT("звук приглушён=%d | мир=%s"),
		Music ? (IsValid(Music) ? TEXT("есть") : TEXT("невалиден")) : TEXT("нет"),
		*GetNameSafe(CurrentMusicTrack.Get()), bWorldPaused ? 1 : 0,
		bGameplayAudioPaused ? 1 : 0, *GetNameSafe(World));
	return true;
}

void UTacticsAudioSubsystem::StopMusic(float FadeOutTime)
{
	UE_LOG(LogXRU1Audio, Display, TEXT("[Audio] музыка ОСТАНОВЛЕНА явным вызовом StopMusic"));
	// Сторожу это НЕ находка: тишину заказали (стингер исхода, смена сцены).
	bMusicStoppedIntentionally = true;
	const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset();
	const float Fade = FadeOutTime >= 0.f ? FadeOutTime : (Asset ? Asset->MusicFadeTime : 2.f);
	if (UAudioComponent* Component = MusicComponent.Get())
	{
		Component->FadeOut(Fade, 0.f);
	}
	MusicComponent = nullptr;
	CurrentMusicTrack = nullptr;
}

void UTacticsAudioSubsystem::PlayOutcomeStinger(bool bVictory)
{
	const UTacticsAudioSettingsDataAsset* Asset = GetAudioSettingsAsset();
	if (!Asset || !GetAudioWorld())
	{
		return;
	}

	// Боевой трек уходит быстро: стингер должен звучать в тишине, иначе
	// смешивается с ритмом боя и не читается как «точка».
	StopMusic(1.f);
	if (USoundBase* Stinger = bVictory ? Asset->VictoryStinger : Asset->DefeatStinger)
	{
		UGameplayStatics::PlaySound2D(GetAudioWorld(), Stinger);
	}
}
