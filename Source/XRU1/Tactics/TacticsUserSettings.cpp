#include "TacticsUserSettings.h"

#include "TacticsAudioSubsystem.h"
#include "TacticalPlayerController.h"
#include "SubtitleProjectSettings.h"
#include "SubtitleSubsystem.h"
#include "XRU1Log.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ConfigCacheIni.h"

UTacticsUserSettings* UTacticsUserSettings::Get()
{
	return GEngine ? Cast<UTacticsUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

FTacticsAudioSettings UTacticsUserSettings::GetAudioSettings() const
{
	FTacticsAudioSettings Settings;
	Settings.MasterVolume = FMath::Clamp(MasterVolume, 0.f, 1.f);
	Settings.MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
	Settings.SfxVolume = FMath::Clamp(SfxVolume, 0.f, 1.f);
	Settings.UIVolume = FMath::Clamp(UIVolume, 0.f, 1.f);
	Settings.VoiceVolume = FMath::Clamp(VoiceVolume, 0.f, 1.f);
	return Settings;
}

void UTacticsUserSettings::SetAudioSettings(const FTacticsAudioSettings& NewSettings)
{
	MasterVolume = FMath::Clamp(NewSettings.MasterVolume, 0.f, 1.f);
	MusicVolume = FMath::Clamp(NewSettings.MusicVolume, 0.f, 1.f);
	SfxVolume = FMath::Clamp(NewSettings.SfxVolume, 0.f, 1.f);
	UIVolume = FMath::Clamp(NewSettings.UIVolume, 0.f, 1.f);
	VoiceVolume = FMath::Clamp(NewSettings.VoiceVolume, 0.f, 1.f);
}

FTacticsVideoSettings UTacticsUserSettings::GetVideoSettings() const
{
	FTacticsVideoSettings Settings;
	Settings.ScalabilityLevel = FMath::Clamp(QualityLevel, 0, 3);
	Settings.ResolutionScale = FMath::Clamp(ScreenScale, 0.25f, 1.f);
	Settings.bFullscreen = bFullscreenMode;
	Settings.bVSync = bVerticalSync;
	return Settings;
}

void UTacticsUserSettings::SetVideoSettings(const FTacticsVideoSettings& NewSettings)
{
	QualityLevel = FMath::Clamp(NewSettings.ScalabilityLevel, 0, 3);
	ScreenScale = FMath::Clamp(NewSettings.ResolutionScale, 0.25f, 1.f);
	bFullscreenMode = NewSettings.bFullscreen;
	bVerticalSync = NewSettings.bVSync;

	// Штатные поля движка держим в синхроне: их применяет базовый ApplySettings.
	SetOverallScalabilityLevel(QualityLevel);
	SetResolutionScaleNormalized(ScreenScale);
	SetFullscreenMode(bFullscreenMode ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
	SetVSyncEnabled(bVerticalSync);
}

FTacticsSubtitleSettings UTacticsUserSettings::GetSubtitleSettings() const
{
	FTacticsSubtitleSettings Settings;
	Settings.bEnabled = bSubtitlesEnabled;
	Settings.bShowSpeakerNames = bSubtitleSpeakerNames;
	Settings.TextSize = SubtitleTextSize;
	Settings.Backdrop = SubtitleBackdrop;
	return Settings;
}

void UTacticsUserSettings::SetSubtitleSettings(const FTacticsSubtitleSettings& NewSettings)
{
	bSubtitlesEnabled = NewSettings.bEnabled;
	bSubtitleSpeakerNames = NewSettings.bShowSpeakerNames;
	SubtitleTextSize = NewSettings.TextSize;
	SubtitleBackdrop = NewSettings.Backdrop;

	// Движковый выключатель держим в синхроне: субтитры, пришедшие любым другим
	// путём (SoundWave-cue, будущий плагин Epic), обязаны подчиняться той же
	// галочке, иначе «субтитры выключены» окажется полуправдой.
	if (GEngine)
	{
		GEngine->bSubtitlesEnabled = bSubtitlesEnabled;
	}

	// Строку, висящую на экране в момент выключения, снимаем сразу: ждать конца
	// реплики после снятия галочки игрок не должен.
	if (!bSubtitlesEnabled)
	{
		if (const UWorld* World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr)
		{
			if (UXRU1SubtitleSubsystem* Subtitles = UXRU1SubtitleSubsystem::Get(World))
			{
				Subtitles->HideAll();
			}
		}
	}
}

FString UTacticsUserSettings::GetLanguage()
{
	return FInternationalization::Get().GetCurrentLanguage()->GetName();
}

TArray<FString> UTacticsUserSettings::GetAvailableLanguages()
{
	return UXRU1SubtitleSettings::Get().AvailableCultures;
}

void UTacticsUserSettings::SetPendingLanguage(const FString& Culture)
{
	PendingCulture = (Culture == GetLanguage()) ? FString() : Culture;
}

FString UTacticsUserSettings::GetSelectedLanguage() const
{
	return PendingCulture.IsEmpty() ? GetLanguage() : PendingCulture;
}

bool UTacticsUserSettings::ApplyPendingLanguage()
{
	if (PendingCulture.IsEmpty())
	{
		return false;
	}

	const FString Culture = PendingCulture;
	PendingCulture.Reset();

	if (!FInternationalization::Get().SetCurrentLanguageAndLocale(Culture))
	{
		UE_LOG(LogXRU1UI, Warning, TEXT("[Settings] не удалось переключить язык на '%s'"), *Culture);
		return false;
	}

	// Персист там же, откуда движок читает язык при старте упакованной игры.
	GConfig->SetString(TEXT("Internationalization"), TEXT("Culture"), *Culture, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);

	UE_LOG(LogXRU1UI, Display, TEXT("[Settings] язык переключён на '%s'%s"), *Culture,
		GIsEditor ? TEXT(" (в редакторе выбор при старте не перечитывается)") : TEXT(""));
	return true;
}

FTacticsCameraSettings UTacticsUserSettings::GetCameraSettings() const
{
	FTacticsCameraSettings Settings;
	Settings.FieldOfView = FMath::Clamp(CameraFieldOfView, 40.f, 110.f);
	Settings.RotationSensitivity = FMath::Clamp(CameraRotationSensitivity, 0.1f, 4.f);
	Settings.PitchSensitivity = FMath::Clamp(CameraPitchSensitivity, 0.1f, 4.f);
	Settings.bInvertPitch = bCameraInvertPitch;
	Settings.bEdgeScroll = bCameraEdgeScroll;
	return Settings;
}

void UTacticsUserSettings::SetCameraSettings(const FTacticsCameraSettings& NewSettings,
	const UObject* WorldContext)
{
	CameraFieldOfView = FMath::Clamp(NewSettings.FieldOfView, 40.f, 110.f);
	CameraRotationSensitivity = FMath::Clamp(NewSettings.RotationSensitivity, 0.1f, 4.f);
	CameraPitchSensitivity = FMath::Clamp(NewSettings.PitchSensitivity, 0.1f, 4.f);
	bCameraInvertPitch = NewSettings.bInvertPitch;
	bCameraEdgeScroll = NewSettings.bEdgeScroll;

	// Применяем сразу: угол обзора и чувствительность игрок настраивает «на глаз»,
	// и отложенное применение (по «Применить») лишало бы его обратной связи.
	ApplyCameraSettings(WorldContext);
}

void UTacticsUserSettings::ApplyCameraSettings(const UObject* WorldContext)
{
	const UTacticsUserSettings* Settings = Get();
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!Settings || !World)
	{
		return;
	}

	ATacticalPlayerController* PC = Cast<ATacticalPlayerController>(World->GetFirstPlayerController());
	if (!PC)
	{
		return; // не бой (меню, хаб) — применять некуда, и это нормально
	}
	PC->ApplyCameraUserSettings(Settings->GetCameraSettings());
}

const UTacticsAudioSubsystem* UTacticsUserSettings::FindAudioSubsystem(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}
	// GameInstance может прийти напрямую (на его Init мира ещё нет) либо через
	// любой объект мира — поддерживаем оба пути, чтобы дефолты не «терялись».
	const UGameInstance* GameInstance = Cast<UGameInstance>(WorldContext);
	if (!GameInstance)
	{
		const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		GameInstance = World ? World->GetGameInstance() : nullptr;
	}
	return GameInstance ? GameInstance->GetSubsystem<UTacticsAudioSubsystem>() : nullptr;
}

void UTacticsUserSettings::ResetToProjectDefaults(const UObject* WorldContext)
{
	// Дефолты громкости задаёт дизайнер в DA_TacticsAudio; изображение —
	// значения структуры (они же дефолты полей этого класса).
	const UTacticsAudioSubsystem* Audio = FindAudioSubsystem(WorldContext);
	SetAudioSettings(Audio ? Audio->GetDefaultAudioSettings() : FTacticsAudioSettings());

	// Режим окна при сбросе НЕ меняем: дефолт структуры (`bFullscreen = true`)
	// самовольно включал полноэкранный режим у игрока, который его не выбирал.
	FTacticsVideoSettings DefaultVideo;
	DefaultVideo.bFullscreen = bFullscreenMode;
	SetVideoSettings(DefaultVideo);

	SetCameraSettings(FTacticsCameraSettings(), WorldContext);

	// Субтитры возвращаются к дефолтам структуры; язык остаётся выбранным
	// игроком (см. комментарий в SetToDefaults).
	SetSubtitleSettings(FTacticsSubtitleSettings());

	UE_LOG(LogXRU1UI, Display,
		TEXT("[Settings] сброс к дефолтам проекта: Master=%.2f Music=%.2f Sfx=%.2f UI=%.2f Voice=%.2f%s"),
		MasterVolume, MusicVolume, SfxVolume, UIVolume, VoiceVolume,
		Audio ? TEXT("") : TEXT(" (DA_TacticsAudio недоступен — взяты дефолты кода)"));
}

void UTacticsUserSettings::InitializeFromProjectIfNeeded(const UObject* WorldContext)
{
	if (bInitializedFromProject)
	{
		return;
	}
	bInitializedFromProject = true;

	// Громкости — из DA_TacticsAudio (дизайнерские дефолты).
	if (const UTacticsAudioSubsystem* Audio = FindAudioSubsystem(WorldContext))
	{
		SetAudioSettings(Audio->GetDefaultAudioSettings());
	}

	// Изображение — фактическое состояние окна, а не дефолт структуры: иначе
	// экран настроек с первого открытия предлагает применить полноэкранный
	// режим, в котором игра не запускалась.
	QualityLevel = FMath::Clamp(GetOverallScalabilityLevel() >= 0 ? GetOverallScalabilityLevel() : 2, 0, 3);
	ScreenScale = FMath::Clamp(GetResolutionScaleNormalized(), 0.25f, 1.f);
	// Галочка обязана показывать РЕАЛЬНОСТЬ, в том числе в редакторе: там игра
	// всегда идёт в окне, и «включённый полноэкранный режим» на первом же
	// открытии настроек — прямая ложь экрана.
	bFullscreenMode = GetFullscreenMode() != EWindowMode::Windowed;
	bVerticalSync = IsVSyncEnabled();

	UE_LOG(LogXRU1UI, Display,
		TEXT("[Settings] первый запуск: Master=%.2f Music=%.2f Sfx=%.2f UI=%.2f Voice=%.2f | ")
		TEXT("Quality=%d Scale=%.2f Fullscreen=%d VSync=%d"),
		MasterVolume, MusicVolume, SfxVolume, UIVolume, VoiceVolume,
		QualityLevel, ScreenScale, bFullscreenMode ? 1 : 0, bVerticalSync ? 1 : 0);

	SaveSettings();
}

void UTacticsUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	MasterVolume = 1.f;
	MusicVolume = 0.7f;
	SfxVolume = 1.f;
	UIVolume = 1.f;
	VoiceVolume = 1.f;

	QualityLevel = 2;
	ScreenScale = 1.f;
	bFullscreenMode = true;
	bVerticalSync = true;

	// Субтитры — дефолты структуры (тот же приём, что у камеры ниже). Язык при
	// сбросе НЕ трогаем: игрок, сбросивший настройки на незнакомом языке, не
	// должен ещё и терять понятный ему интерфейс.
	const FTacticsSubtitleSettings DefaultSubtitles;
	bSubtitlesEnabled = DefaultSubtitles.bEnabled;
	bSubtitleSpeakerNames = DefaultSubtitles.bShowSpeakerNames;
	SubtitleTextSize = DefaultSubtitles.TextSize;
	SubtitleBackdrop = DefaultSubtitles.Backdrop;

	// Камера — дефолты структуры: один источник значений по умолчанию.
	const FTacticsCameraSettings DefaultCamera;
	CameraFieldOfView = DefaultCamera.FieldOfView;
	CameraRotationSensitivity = DefaultCamera.RotationSensitivity;
	CameraPitchSensitivity = DefaultCamera.PitchSensitivity;
	bCameraInvertPitch = DefaultCamera.bInvertPitch;
	bCameraEdgeScroll = DefaultCamera.bEdgeScroll;
	// bInitializedFromProject НЕ сбрасываем: «Сбросить» в меню не должно
	// превращаться в повторный «первый запуск».
}

void UTacticsUserSettings::ApplySettings(bool /*bCheckForCommandLineOverrides*/)
{
	// В редакторе окно принадлежит НЕ игре. Любое применение разрешения/режима
	// растягивает PIE-окно на весь экран (известная проблема UE — форум Epic
	// «Calling UGameUserSettings::ApplySettings forces the dimensions of a PIE
	// window»), а смена scalability вешает на вьюпорт предупреждение
	// «нестандартные настройки масштабируемости». Поэтому в редакторе настройки
	// только СОХРАНЯЮТСЯ: проверять картинку нужно в Standalone или билде.
	const bool bApplyToEngine = !GIsEditor;

	if (bApplyToEngine)
	{
		// НЕ вызываем Super::ApplySettings: он применяет ещё и разрешение экрана
		// (`ResolutionSizeX/Y` из ini = разрешение монитора). Выбора разрешения
		// в меню нет, поэтому «Применить» без единого изменения растягивало окно.
		ApplyNonResolutionSettings();

		// Режим окна трогаем, только если он действительно меняется.
		const EWindowMode::Type DesiredMode = bFullscreenMode
			? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
		if (GetFullscreenMode() != DesiredMode)
		{
			SetFullscreenMode(DesiredMode);
			ApplyResolutionSettings(/*bCheckForCommandLineOverrides=*/false);
		}
	}

	SaveSettings();

	UE_LOG(LogXRU1UI, Display,
		TEXT("[Settings] применены: Quality=%d Scale=%.2f Fullscreen=%d VSync=%d | ")
		TEXT("Master=%.2f Music=%.2f Sfx=%.2f UI=%.2f Voice=%.2f | %s"),
		QualityLevel, ScreenScale, bFullscreenMode ? 1 : 0, bVerticalSync ? 1 : 0,
		MasterVolume, MusicVolume, SfxVolume, UIVolume, VoiceVolume,
		bApplyToEngine ? TEXT("применено к движку") : TEXT("редактор: только сохранено"));
}
