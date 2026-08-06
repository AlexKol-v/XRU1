#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "SubtitleTypes.h"
#include "TacticsAudioTypes.h"
#include "TacticsUserSettings.generated.h"

/**
 * ЕДИНСТВЕННЫЙ источник правды пользовательских настроек: звук и изображение.
 *
 * Почему так, а не в слоте кампании (как было до 2026-08-02): громкость и
 * качество картинки — настройки приложения, а не прогресса. Пока они жили в
 * слоте, экран настроек читал одно, движок применял другое, и меню предлагало
 * применить полноэкранный режим, который игрок не выбирал. Это стандартная
 * схема UE (так устроен `ULyraSettingsLocal` в Lyra): наследник
 * `UGameUserSettings`, зарегистрированный в `DefaultEngine.ini` через
 * `GameUserSettingsClassName`, сам грузится и сохраняется в
 * `GameUserSettings.ini`.
 *
 * Отдельные `config`-поля вместо штатных геттеров движка нужны потому, что
 * `GetOverallScalabilityLevel()` возвращает **-1** при смешанных настройках
 * качества, а `GetResolutionScaleNormalized()` в PIE отдаёт значение окна
 * редактора. Показывать игроку такое нельзя: экран один раз показал «Низкое»
 * и по «Применить» действительно уронил качество карты.
 */
UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class XRU1_API UTacticsUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	/** Экземпляр настроек проекта; nullptr, если класс не прописан в ini. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Settings")
	static UTacticsUserSettings* Get();

	// --- Звук -----------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Tactics|Settings")
	FTacticsAudioSettings GetAudioSettings() const;

	/** Пишет громкости в настройки. Сохранение на диск — отдельным SaveSettings(). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void SetAudioSettings(const FTacticsAudioSettings& NewSettings);

	// --- Изображение ----------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Tactics|Settings")
	FTacticsVideoSettings GetVideoSettings() const;

	/** Пишет настройки изображения и синхронизирует их со штатными полями движка. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void SetVideoSettings(const FTacticsVideoSettings& NewSettings);

	// --- Камера ---------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Tactics|Settings")
	FTacticsCameraSettings GetCameraSettings() const;

	/** Пишет настройки камеры и сразу применяет их к активной камере боя. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void SetCameraSettings(const FTacticsCameraSettings& NewSettings, const UObject* WorldContext);

	/**
	 * Применить текущие настройки камеры к пешке-камере и контроллеру боя.
	 *
	 * Толчок, а не опрос: камера не читает настройки каждый кадр, поэтому смена
	 * значения обязана дойти до неё явно — из экрана настроек и со старта боя.
	 * Вне боя (меню, хаб) вызов безвреден: тактической камеры там просто нет.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	static void ApplyCameraSettings(const UObject* WorldContext);

	// --- Субтитры -------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Tactics|Settings")
	FTacticsSubtitleSettings GetSubtitleSettings() const;

	/**
	 * Пишет настройки субтитров и сразу отражает выключатель в движке
	 * (`GEngine->bSubtitlesEnabled`), чтобы любой движковый путь субтитров
	 * подчинялся тому же флажку, что и наш слой.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void SetSubtitleSettings(const FTacticsSubtitleSettings& NewSettings);

	// --- Язык -----------------------------------------------------------------

	/** Текущий язык игры (код культуры ICU). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Settings")
	static FString GetLanguage();

	/** Языки, поддерживаемые проектом (из Project Settings). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Settings")
	static TArray<FString> GetAvailableLanguages();

	/**
	 * Запоминает выбранный в меню язык БЕЗ применения.
	 *
	 * Смена языка перезагружает весь текст игры и перестраивает экраны, поэтому
	 * она происходит по «Применить», а не в момент клика по списку (так же
	 * устроен `PendingCulture` в Lyra).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void SetPendingLanguage(const FString& Culture);

	/** Язык, выбранный в меню: отложенный, если он есть, иначе текущий. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Settings")
	FString GetSelectedLanguage() const;

	/**
	 * Применяет отложенный язык и запоминает его между запусками.
	 *
	 * Персист идёт в `[Internationalization] Culture` файла `GameUserSettings.ini`
	 * — ровно туда, откуда движок читает язык при старте упакованной игры
	 * (`FTextLocalizationManager`). В редакторе это чтение не выполняется, то
	 * есть «язык запомнился» проверяется только в билде.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	bool ApplyPendingLanguage();

	/** Дефолты проекта: берутся из DA_TacticsAudio, если он назначен. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void ResetToProjectDefaults(const UObject* WorldContext);

	/**
	 * Первый запуск: подтянуть дизайнерские дефолты и фактическое состояние окна.
	 *
	 * Без этого `DefaultVolumes` из `DA_TacticsAudio` не влияли ни на что —
	 * побеждали C++-дефолты config-полей, а галочка полноэкранного режима
	 * стояла просто потому, что таков дефолт, а не потому что игра так идёт.
	 * Повторные запуски ничего не перетирают: игрок мог настроить своё.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void InitializeFromProjectIfNeeded(const UObject* WorldContext);

	// --- UGameUserSettings ----------------------------------------------------

	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
	virtual void SetToDefaults() override;
	virtual void LoadSettings(bool bForceReload = false) override;

	/**
	 * Нижний предел масштаба разрешения (нормализованный, движок мапит в
	 * 0..100% экрана). 0.5 = 50% разрешения — ниже уже не «экономия», а мыло
	 * и пиксельная обводка юнитов (стенсил рендерится во внутреннем разрешении).
	 */
	static constexpr float MinScreenScale = 0.5f;

protected:
	UPROPERTY(config) float MasterVolume = 1.f;
	UPROPERTY(config) float MusicVolume = 0.7f;
	UPROPERTY(config) float SfxVolume = 1.f;
	UPROPERTY(config) float UIVolume = 1.f;
	UPROPERTY(config) float VoiceVolume = 1.f;

	/** 0..3 (Низкое/Среднее/Высокое/Эпическое). Своё поле — движок отдаёт -1. */
	UPROPERTY(config) int32 QualityLevel = 2;

	UPROPERTY(config) float ScreenScale = 1.f;
	UPROPERTY(config) bool bFullscreenMode = true;
	UPROPERTY(config) bool bVerticalSync = true;

	/** Камера: дефолты те же, что у структуры (см. FTacticsCameraSettings). */
	UPROPERTY(config) float CameraFieldOfView = 65.f;
	UPROPERTY(config) float CameraRotationSensitivity = 1.f;
	UPROPERTY(config) float CameraPitchSensitivity = 1.f;
	UPROPERTY(config) bool bCameraInvertPitch = false;
	UPROPERTY(config) bool bCameraEdgeScroll = true;

	/** Субтитры: дефолты те же, что у структуры (см. FTacticsSubtitleSettings). */
	UPROPERTY(config) bool bSubtitlesEnabled = true;
	UPROPERTY(config) bool bSubtitleSpeakerNames = true;
	UPROPERTY(config) EXRU1SubtitleTextSize SubtitleTextSize = EXRU1SubtitleTextSize::Normal;
	UPROPERTY(config) EXRU1SubtitleBackdrop SubtitleBackdrop = EXRU1SubtitleBackdrop::Soft;

	/**
	 * Язык, выбранный в меню, но ещё не применённый. Не `config`: между
	 * запусками язык хранит сам движок в `[Internationalization]`, и второе
	 * место хранения неизбежно разошлось бы с первым.
	 */
	UPROPERTY(Transient) FString PendingCulture;

	/** Настройки уже инициализировались дефолтами проекта (первый запуск позади). */
	UPROPERTY(config) bool bInitializedFromProject = false;

private:
	/** Подсистема звука по GameInstance ИЛИ по любому объекту мира. */
	static const class UTacticsAudioSubsystem* FindAudioSubsystem(const UObject* WorldContext);
};
