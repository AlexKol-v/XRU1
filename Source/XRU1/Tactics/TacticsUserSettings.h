#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
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

	/** Настройки уже инициализировались дефолтами проекта (первый запуск позади). */
	UPROPERTY(config) bool bInitializedFromProject = false;

private:
	/** Подсистема звука по GameInstance ИЛИ по любому объекту мира. */
	static const class UTacticsAudioSubsystem* FindAudioSubsystem(const UObject* WorldContext);
};
