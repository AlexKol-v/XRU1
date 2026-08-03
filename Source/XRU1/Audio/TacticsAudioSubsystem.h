#pragma once

#include "CoreMinimal.h"
#include "AudioDeviceHandle.h"
#include "Containers/Ticker.h" // сторож музыки живёт на системном тикере
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
	/**
	 * Стартовые громкости новой кампании и значения кнопки «Сбросить».
	 * Здесь, а не в C++-дефолтах структуры: подбор громкости на слух — работа
	 * дизайнера, ради неё не должно требоваться пересобирать проект.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Mixer")
	FTacticsAudioSettings DefaultVolumes;

	/**
	 * SoundMix проекта. Пользовательские громкости через него НЕ идут (они
	 * ставятся прямо в SoundClass); микс держится активным для будущих эффектов
	 * вроде приглушения боя под реплику. Он же должен стоять в
	 * Project Settings → Audio → Default Base Sound Mix.
	 */
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

	// --- Голос -------------------------------------------------------------

	/**
	 * Реплика «Купола» при первом входе в хаб: что это за место и что делать
	 * дальше. Живёт здесь, а не в уровне: озвучка — данные, а не логика карты.
	 * Пусто — хаб молчит.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Voice")
	TObjectPtr<USoundBase> HubArrivalVoice;

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
	virtual void Deinitialize() override;

	/** Применяет громкости к SoundClass'ам. Зовётся при старте мира и из меню настроек. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio")
	void ApplyAudioSettings(const FTacticsAudioSettings& Settings);

	/** Текущие применённые громкости. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Audio")
	const FTacticsAudioSettings& GetAudioSettings() const { return AppliedSettings; }

	/** Перечитывает громкости из UTacticsUserSettings и применяет их. */
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
	 *
	 * `bAutoSubtitle` — показать субтитр по данным САМОГО ассета озвучки
	 * (`USoundSubtitleData` в его `Asset User Data`). Так новая озвученная
	 * реплика получает текст без правок кода: брифинг, экран результата и
	 * вводная хаба обслуживаются одним этим вызовом. Выключается там, где текст
	 * ведёт другой источник со своими часами (такт обучения).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Voice")
	UAudioComponent* PlayVoice2D(USoundBase* Voice, float VolumeMultiplier = 1.f,
		bool bAutoSubtitle = true);

	/**
	 * Вводная Купола про оперативную карту — РОВНО ОДИН раз на кампанию.
	 *
	 * Признак «уже слышал» лежит в слоте (`UTacticsSaveGame::bHubBriefed`), а не
	 * только в сессии: «Продолжить» приводит игрока, который хаб уже видел.
	 * Сессионный флаг остаётся страховкой от повтора внутри одного запуска —
	 * GameMode пересоздаётся на каждый заход в хаб и сам ничего не помнит.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio|Voice")
	void PlayHubArrivalVoiceOnce();

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

	/** Стартовые громкости проекта: из ассета микшера, иначе C++-дефолты. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Audio")
	FTacticsAudioSettings GetDefaultAudioSettings() const;

	/**
	 * Полный дамп состояния звука в лог: ассет микшера, мир и аудио-устройство,
	 * базовый микс, активные миксы с их override'ами, иерархия SoundClass,
	 * сохранённые и применённые громкости, текущий музыкальный компонент.
	 * Консоль: `xru1.Audio.Dump`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio")
	void DumpAudioState();

	/**
	 * Пауза боевого звука: реплика замирает и продолжится с того же места,
	 * эффекты и голос глушатся. Интерфейс и музыка остаются слышимыми — иначе
	 * меню паузы выглядит «сломавшим звук».
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Audio")
	void SetGameplayAudioPaused(bool bPaused);

private:
	/** Ставит громкость одного SoundClass (прямо в его Properties). */
	void ApplyClassVolume(USoundClass* SoundClass, float Volume);

	/** Возвращает ассетам их исходные громкости (нужно редактору после PIE). */
	void RestoreOriginalClassVolumes();

	/** Громкости SoundClass, какими они были до вмешательства подсистемы. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<USoundClass>, float> OriginalClassVolumes;

	/** Мир получил аудио-устройство — переприменяем к нему громкости. */
	void HandleWorldRegisteredToAudioDevice(const UWorld* World, Audio::FDeviceId DeviceId);

	/** Мир, к аудио-устройству которого применяются микс и громкости. */
	UWorld* GetAudioWorld() const;

	/** Последний мир, зарегистрированный с аудио-устройством. */
	TWeakObjectPtr<UWorld> AudioWorld;

	FDelegateHandle AudioDeviceRegisteredHandle;

	/**
	 * Сторож музыки: системный тикер, живущий и на паузе. Пишет в лог ТОЛЬКО
	 * смену состояния «играет ↔ молчит» вместе с подозреваемыми (мир на паузе,
	 * приглушение, судьба компонента) — жалоба «музыка молча пропала» иначе не
	 * расследуется постфактум.
	 */
	FTSTicker::FDelegateHandle MusicWatchdogHandle;
	bool bMusicWasPlaying = false;

	/** Музыку остановили НАМЕРЕННО (стингер исхода) — сторожу молчать. */
	bool bMusicStoppedIntentionally = false;

	/** Тик сторожа; всегда возвращает true (тикер живёт до Deinitialize). */
	bool TickMusicWatchdog(float DeltaTime);

	FTacticsAudioSettings AppliedSettings;

	/** Приглушены ли боевые категории паузой (Sfx/Voice = 0). */
	bool bGameplayAudioPaused = false;

	/** Реплика прибытия в хаб уже звучала в этой сессии. */
	bool bHubArrivalVoicePlayed = false;

	/** Активный музыкальный компонент; переживает travel (persist across level). */
	/**
	 * ⚠️ СИЛЬНАЯ ссылка обязательна. Компонент музыки создаётся с
	 * `bAutoDestroy=false` и не принадлежит ни одному актору: слабая ссылка не
	 * удерживает его от сборщика мусора, и музыка молча пропадала на первой же
	 * подгрузке ассета (в логе — `музыка МОЛЧИТ | компонент=нет` рядом с
	 * FlushAsyncLoading). Для звуков с bAutoDestroy=true это не нужно — их
	 * держит аудио-движок, пока они играют.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> MusicComponent;

	TWeakObjectPtr<USoundBase> CurrentMusicTrack;

	/** Активная реплика: следующая обрывает её, чтобы фразы не накладывались. */
	/**
	 * Голос создаётся с `bAutoDestroy=true` — пока звучит, его держит
	 * аудио-движок. Сильная ссылка нужна, чтобы после конца фразы указатель
	 * гарантированно вёл на «мёртвый» объект (IsValid=false), а не на память,
	 * переиспользованную другим звуком.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> VoiceComponent;
};
