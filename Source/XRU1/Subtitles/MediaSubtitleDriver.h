#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "SubtitleTypes.h"
#include "UObject/Object.h"
#include "MediaSubtitleDriver.generated.h"

class UMediaPlayer;
class USubtitleTrackDataAsset;

/**
 * Ведёт таймлайн субтитров по времени медиаплеера.
 *
 * Единственный источник истины о времени здесь — сам ролик
 * (`UMediaPlayer::GetTime()`). Своего счётчика драйвер не держит намеренно:
 * ролик может стартовать с задержкой (`OpenSource` асинхронный), буферизоваться
 * и быть остановлен игроком, и любой параллельный счётчик с ним разойдётся.
 *
 * Тик идёт через core ticker, а НЕ через таймер мира: экраны меню держат паузу
 * игры (`UGamePauseSubsystem`), а видео при этом продолжает играть — на таймере
 * мира титры замерли бы, а картинка нет.
 */
UCLASS()
class XRU1_API UMediaSubtitleDriver : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Начинает вести титры. Повторный вызов перезапускает ведение.
	 * `Track == nullptr` — штатная ситуация «у ролика нет титров»: драйвер
	 * просто ничего не делает.
	 */
	void Start(UMediaPlayer* InPlayer, USubtitleTrackDataAsset* InTrack, FName InSourceId);

	/** Прекращает ведение и снимает свою строку. Идемпотентно. */
	void Stop();

	virtual void BeginDestroy() override;

private:
	/** Кадр ведения: сверяет время ролика с таймлайном. */
	bool TickDriver(float DeltaTime);

	TWeakObjectPtr<UMediaPlayer> Player;

	/** Ассет держим ссылкой: он мог быть загружен только ради титров. */
	UPROPERTY(Transient)
	TObjectPtr<USubtitleTrackDataAsset> Track;

	FTSTicker::FDelegateHandle TickerHandle;

	/** Индекс показанной реплики таймлайна; INDEX_NONE — сейчас тишина. */
	int32 CurrentCueIndex = INDEX_NONE;

	/** Дескриптор своей строки: снимаем только её. */
	FXRU1SubtitleHandle ActiveHandle;

	FName SourceId;
};
