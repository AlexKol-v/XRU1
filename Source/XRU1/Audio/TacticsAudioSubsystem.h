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

	/** Ассет микшера (из GameInstance). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Audio")
	UTacticsAudioSettingsDataAsset* GetAudioSettingsAsset() const;

private:
	/** Ставит override громкости одного SoundClass в общий SoundMix. */
	void ApplyClassVolume(USoundClass* SoundClass, float Volume);

	FTacticsAudioSettings AppliedSettings;
	bool bMixPushed = false;
};
