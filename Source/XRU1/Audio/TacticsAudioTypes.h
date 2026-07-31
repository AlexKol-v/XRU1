#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "TacticsAudioTypes.generated.h"

class USoundBase;

/**
 * Доменное событие юнита, у которого есть звук. Перечисление намеренно описывает
 * ПОДТВЕРЖДЁННЫЕ факты боя, а не «моменты монтажа»: звук вешается на ту же точку,
 * что и quest-событие, поэтому он не может прозвучать для отменённого действия.
 */
UENUM(BlueprintType)
enum class EUnitSoundEvent : uint8
{
	/** Выстрел разрешён (FireCommit), урон уже применён. */
	Fire,
	/** Реакционный выстрел наблюдения. */
	ReactionFire,
	/** Юнит получил урон и остался жив. */
	Hit,
	/** Юнит убит. */
	Death,
	/** Юнит перешёл в тяжёлое ранение. */
	Downed,
	/** Юнита подняли из тяжёлого ранения. */
	Revive,
	/** Лечение применено. */
	Heal,
	/** Активировано наблюдение. */
	OverwatchEnter,
	/** Активирована глухая оборона. */
	HunkerDown,
	/** Активирована провокация. */
	Taunt,
	/** Активирован рывок и удар. */
	RunAndGun,
	/** Юнит начал выполнять приказ перемещения. */
	MoveStart,
	/** Юнит полностью закончил перемещение и доводку позы. */
	MoveSettled,
	/** Юнит эвакуирован. */
	Evacuated,

	MAX UMETA(Hidden)
};

/**
 * Одна звуковая реплика с вариациями. Несколько вариантов и разброс высоты тона
 * убирают «пулемётную» повторяемость одного и того же WAV: это стандартная
 * практика, а не украшение — одинаковый выстрел 20 раз подряд читается как баг.
 */
USTRUCT(BlueprintType)
struct XRU1_API FTacticsSoundCue
{
	GENERATED_BODY()

	/** Варианты; выбирается случайный. Пустой массив = звук намеренно отсутствует. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TArray<TObjectPtr<USoundBase>> Variants;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0", ClampMax = "4"))
	float VolumeMultiplier = 1.f;

	/** Разброс высоты тона ±, доля от 1.0. 0 — без вариации. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0", ClampMax = "0.5"))
	float PitchVariance = 0.06f;

	bool IsValidCue() const { return Variants.Num() > 0; }

	/** Случайный вариант; nullptr, если реплика не заполнена. */
	USoundBase* PickVariant() const;
};

/**
 * Пользовательские громкости. Хранятся в слоте кампании и применяются через
 * SoundMix: один SoundMix с override по SoundClass — рекомендованный способ
 * управлять громкостью категорий, не трогая сами ассеты звуков.
 */
USTRUCT(BlueprintType)
struct XRU1_API FTacticsAudioSettings
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float MasterVolume = 1.f;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float MusicVolume = 0.7f;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float SfxVolume = 1.f;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float UIVolume = 1.f;

	/** Голос «Купола» и реплики бойцов. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float VoiceVolume = 1.f;
};

/**
 * Пользовательские настройки изображения. Применяются через UGameUserSettings,
 * а хранятся здесь, чтобы слот кампании оставался единственным местом, откуда
 * меню восстанавливает состояние.
 */
USTRUCT(BlueprintType)
struct XRU1_API FTacticsVideoSettings
{
	GENERATED_BODY()

	/** Общий уровень качества 0..3 (Low/Medium/High/Epic). -1 — не применялось. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Video", meta = (ClampMin = "-1", ClampMax = "3"))
	int32 ScalabilityLevel = 2;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Video", meta = (ClampMin = "0.25", ClampMax = "1"))
	float ResolutionScale = 1.f;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Video")
	bool bFullscreen = true;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Video")
	bool bVSync = true;
};
