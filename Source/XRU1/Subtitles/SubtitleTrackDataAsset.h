#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SubtitleTypes.h"
#include "SubtitleTrackDataAsset.generated.h"

/** Одна реплика таймлайна: текст, привязанный ко времени носителя. */
USTRUCT(BlueprintType)
struct XRU1_API FXRU1SubtitleCue
{
	GENERATED_BODY()

	/** Момент появления, с от начала носителя (ролика). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры", meta = (ClampMin = "0"))
	float StartTime = 0.f;

	/**
	 * Сколько держать, с. 0 — до начала следующей реплики (а для последней —
	 * до конца носителя). Так авторy достаточно проставить только моменты входа.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры", meta = (ClampMin = "0"))
	float Duration = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры")
	FText Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Субтитры", meta = (MultiLine = "true"))
	FText Text;
};

/**
 * Таймлайн субтитров к носителю со своим временем — сейчас это интро-ролик.
 *
 * Почему не титры, вшитые в видео: вшитые не переводятся, не масштабируются и
 * не отключаются. Почему не caption-трек внутри mp4: `UMediaPlayer` умеет
 * ВЫБРАТЬ такой трек, но не отдаёт его текст наружу (оверлей-сэмплы живут
 * ниже уровня `MediaAssets`), то есть показать его нечем.
 *
 * Ассет не знает, кто его ведёт: время подаёт драйвер (`UMediaSubtitleDriver`
 * — по `UMediaPlayer::GetTime()`), поэтому тем же ассетом можно озвучить
 * любой другой носитель со своим временем.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Таймлайн субтитров"))
class XRU1_API USubtitleTrackDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Реплики. Порядок в массиве не важен: поиск идёт по времени. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Субтитры", meta = (TitleProperty = "StartTime"))
	TArray<FXRU1SubtitleCue> Cues;

	/**
	 * Индекс реплики, активной в момент `TimeSeconds`, либо INDEX_NONE.
	 *
	 * Реплики могут идти в любом порядке и с нулевой длительностью («до
	 * следующей»), поэтому это честный перебор, а не бинарный поиск: реплик в
	 * ролике десятки, а не тысячи, зато данные не обязаны быть отсортированы —
	 * автору не нужно помнить о порядке строк в массиве.
	 */
	int32 FindCueIndex(float TimeSeconds) const;

	/** Строка слоя из реплики по индексу. */
	FXRU1SubtitleLine MakeLine(int32 CueIndex, FName SourceId) const;
};
