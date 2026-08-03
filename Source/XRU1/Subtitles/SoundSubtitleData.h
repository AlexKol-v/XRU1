#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "SubtitleTypes.h"
#include "SoundSubtitleData.generated.h"

/**
 * Субтитр, живущий НА САМОМ ассете озвучки.
 *
 * Вешается в редакторе звука: `Sound Wave` → Advanced → `Asset User Data` →
 * `+` → «Субтитр озвучки». После этого любая реплика, сыгранная через
 * `UTacticsAudioSubsystem::PlayVoice2D`, покажет текст сама — без правок кода,
 * виджетов и вызывающих экранов. Это и есть масштабируемость слоя: добавление
 * новой озвученной реплики в игру не требует программиста.
 *
 * Форма полей повторяет движковый `FSubtitleAssetData` (плагин
 * SubtitlesAndClosedCaptions): если плагин когда-нибудь выйдет из
 * Experimental, перенос данных будет механическим.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Субтитр озвучки"))
class XRU1_API USoundSubtitleData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** Кто говорит. Пусто — реплика без атрибуции. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Субтитры")
	FText Speaker;

	/** Текст реплики. Пусто — субтитра у этого звука нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Субтитры", meta = (MultiLine = "true"))
	FText Text;

	/**
	 * Сколько держать строку ПОСЛЕ конца звука, с.
	 *
	 * Нужно на длинных фразах с обрывистым хвостом: глазу не хватает того же
	 * времени, что уху. 0 — снимать ровно по окончании звука.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Субтитры", meta = (ClampMin = "0", ClampMax = "5"))
	float HoldAfterSound = 0.f;

	bool HasSubtitle() const { return !Text.IsEmpty(); }

	/** Собирает строку слоя из данных ассета. */
	FXRU1SubtitleLine MakeLine(FName SourceId) const
	{
		FXRU1SubtitleLine Line;
		Line.Speaker = Speaker;
		Line.Text = Text;
		Line.SourceId = SourceId;
		return Line;
	}
};
