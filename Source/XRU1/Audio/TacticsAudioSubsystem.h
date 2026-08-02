#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TacticsAudioTypes.h"
#include "TacticsAudioSubsystem.generated.h"

class AActor;
class USoundClass;
class USoundMix;
class UUnitAudioDataAsset;

/**
 * Ассет-описание микшера проекта. Ссылки лежат в одном месте, чтобы код не знал
 * путей к SoundClass и не ломался при переносе ассетов.
 */
UCLASS(BlueprintType)
class XRU1_API UTacticsAudioSettingsDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** SoundMix, через который применяются пользовательские громкости. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Mixer")
	TObjectPtr<USoundMix> UserVolumeMix;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Mixer")
	TObjectPtr<USoundClass> MasterClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Mixer")
	TObjectPtr<USoundClass> MusicClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Mixer")
	TObjectPtr<USoundClass> SfxClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Mixer")
	TObjectPtr<USoundClass> UIClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Mixer")
	TObjectPtr<USoundClass> VoiceClass;

	/** Общие 2D-звуки интерфейса: наведение, клик, отказ, подтверждение приказа. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	FTacticsSoundCue UIHover;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	FTacticsSoundCue UIClick;

	/** Отказ команды: игрок должен слышать, что действие отклонено, а не «ничего не произошло». */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	FTacticsSoundCue UIDenied;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	FTacticsSoundCue UnitSelected;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	FTacticsSoundCue TurnStartedPlayer;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	FTacticsSoundCue TurnStartedEnemy;

	// --- Музыка ------------------------------------------------------------
	//
	// Один трек на состояние игры. Держим ссылки здесь, а не в GameMode/меню:
	// иначе «какая музыка играет» пришлось бы искать по нескольким Blueprint.

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Music")
	TObjectPtr<USoundBase> MenuMusic;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Music")
	TObjectPtr<USoundBase> HubMusic;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Music")
	TObjectPtr<USoundBase> CombatMusic;

	/** Короткий стингер победы; играется поверх затухающего боевого трека. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Music")
	TObjectPtr<USoundBase> VictoryStinger;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Music")
	TObjectPtr<USoundBase> DefeatStinger;

	/** Длительность кроссфейда между треками, с. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Music", meta = (ClampMin = "0"))
	float MusicFadeTime = 2.f;

	// --- Мир ---------------------------------------------------------------

	/** Тик таймера заряда в последние ходы (GDD §13). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|World")
	FTacticsSoundCue BombTick;

	/** Один шаг обезвреживания (1/2). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|World")
	FTacticsSoundCue BombDefuseStep;

	/** Заряд снят. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|World")
	FTacticsSoundCue BombDisarmed;

	/** Активация зоны эвакуации (сирена/дым). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|World")
	FTacticsSoundCue EvacZoneActivated;

	/** Боец покинул поле через зону эвакуации. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|World")
	FTacticsSoundCue EvacUnit;
};

/**
 * Единственная точка воспроизведения звука проекта.
 *
 * Зачем подсистема, а не вызовы UGameplayStatics по месту: громкость категорий,
 * ассет микшера и логирование звуковых событий должны быть в одном месте.
 * Иначе «почему не слышно выстрел» превращается в поиск по десяткам Blueprint.
 */
UCLASS()
class XRU1_API UTacticsAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Применяет громкости к SoundMix. Вызывается при старте и из меню настроек. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio")
	void ApplyAudioSettings(const FTacticsAudioSettings& Settings);

	/** Текущие применённые громкости. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Audio")
	const FTacticsAudioSettings& GetAudioSettings() const { return AppliedSettings; }

	/** Перечитывает громкости из слота кампании и применяет их. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio")
	void ApplyAudioSettingsFromSave();

	/** 3D-звук в точке мира. Ничего не делает для незаполненной реплики. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio")
	void PlayCueAtLocation(const FTacticsSoundCue& Cue, const FVector& Location,
		class USoundAttenuation* Attenuation = nullptr);

	/** 3D-звук, привязанный к актору (движется вместе с ним). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio")
	void PlayCueAttached(const FTacticsSoundCue& Cue, AActor* Actor,
		class USoundAttenuation* Attenuation = nullptr);

	/** 2D-звук интерфейса. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio")
	void PlayCue2D(const FTacticsSoundCue& Cue);

	// --- Готовые интерфейсные реплики ----------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|UI")
	void PlayUIHover();

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|UI")
	void PlayUIClick();

	/** Команда отклонена (нет AP, шаг обучения не разрешает, нет цели). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|UI")
	void PlayUIDenied();

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|UI")
	void PlayUnitSelected();

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|UI")
	void PlayTurnStarted(bool bPlayerTurn);

	// --- Мировые события -----------------------------------------------------

	/** Тик таймера заряда (2D: игрок должен слышать его независимо от камеры). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|World")
	void PlayBombTick();

	/** Шаг обезвреживания у самого заряда; bComplete — заряд снят. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|World")
	void PlayBombDefuse(const FVector& Location, bool bComplete);

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|World")
	void PlayEvacZoneActivated(const FVector& Location);

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|World")
	void PlayEvacUnit(const FVector& Location);

	// --- Голос ---------------------------------------------------------------

	/**
	 * Реплика «Купола»/бойца (2D, поверх боя). Возвращает компонент, чтобы
	 * следующая реплика могла оборвать предыдущую: две накладывающиеся фразы
	 * читаются как баг, а не как «живой эфир».
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Voice")
	UAudioComponent* PlayVoice2D(USoundBase* Voice, float VolumeMultiplier = 1.f);

	// --- Музыка --------------------------------------------------------------

	/** Кроссфейд на новый трек. Повторный вызов с тем же треком ничего не делает. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Music")
	void PlayMusic(USoundBase* Track, float FadeInTime = -1.f);

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Music")
	void PlayMenuMusic();

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Music")
	void PlayHubMusic();

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Music")
	void PlayCombatMusic();

	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Music")
	void StopMusic(float FadeOutTime = -1.f);

	/** Стингер исхода: гасит боевой трек и играет короткую точку. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Music")
	void PlayOutcomeStinger(bool bVictory);

	/** Ассет микшера (из GameInstance). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Audio")
	UTacticsAudioSettingsDataAsset* GetAudioSettingsAsset() const;

private:
	/** Ставит override громкости одного SoundClass в общий SoundMix. */
	void ApplyClassVolume(USoundClass* SoundClass, float Volume);

	FTacticsAudioSettings AppliedSettings;
	bool bMixPushed = false;

	/** Активный музыкальный компонент; переживает travel (persist across level). */
	TWeakObjectPtr<class UAudioComponent> MusicComponent;
	TWeakObjectPtr<USoundBase> CurrentMusicTrack;

	/** Активная реплика: следующая обрывает её, чтобы фразы не накладывались. */
	TWeakObjectPtr<class UAudioComponent> VoiceComponent;
};
