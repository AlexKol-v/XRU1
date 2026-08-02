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

	/**
	 * С какой секунды начинать воспроизведение — «съедает» тишину в начале файла.
	 *
	 * Библиотечные SFX часто идут с паддингом: у `S_UI_Hover` первые 49 мс —
	 * тишина, а пик приходит только на 63-й миллисекунде, и наведение на кнопку
	 * ощущается запаздывающим. Переимпортировать обрезанный файл не всегда
	 * возможно (исходники бывают на другой машине), поэтому смещение — параметр
	 * звука, а не свойство ассета.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0", ClampMax = "2"))
	float StartOffset = 0.f;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float MasterVolume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float MusicVolume = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float SfxVolume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float UIVolume = 1.f;

	/** Голос «Купола» и реплики бойцов. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float VoiceVolume = 1.f;
};

/**
 * Пользовательские настройки КАМЕРЫ: угол обзора, чувствительность свободного
 * обзора (Alt+мышь и удержание Q/E), инверсия вертикали, панорама у края экрана.
 *
 * Лежит рядом со звуком и изображением по той же причине: это настройки
 * приложения, которые экран настроек читает и пишет одним способом. Набор
 * повторяет то, что игроки XCOM 2 добавляют себе камера-модами (Free Camera
 * Rotation), — там это тоже ini-параметры, а не игровой прогресс.
 */
USTRUCT(BlueprintType)
struct XRU1_API FTacticsCameraSettings
{
	GENERATED_BODY()

	/** Угол обзора тактической камеры (град). 65 — дефолт проекта, XCOM 2 играет на 50. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Camera", meta = (ClampMin = "40", ClampMax = "110"))
	float FieldOfView = 65.f;

	/** Множитель скорости вращения (удержание Q/E и Alt+мышь по горизонтали). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Camera", meta = (ClampMin = "0.1", ClampMax = "4"))
	float RotationSensitivity = 1.f;

	/** Множитель чувствительности наклона (Alt+мышь по вертикали). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Camera", meta = (ClampMin = "0.1", ClampMax = "4"))
	float PitchSensitivity = 1.f;

	/** Инвертировать вертикаль свободного обзора. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Camera")
	bool bInvertPitch = false;

	/** Панорама мышью у края экрана. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Camera")
	bool bEdgeScroll = true;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Video", meta = (ClampMin = "-1", ClampMax = "3"))
	int32 ScalabilityLevel = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Video", meta = (ClampMin = "0.25", ClampMax = "1"))
	float ResolutionScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Video")
	bool bFullscreen = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Video")
	bool bVSync = true;
};
