#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SubtitleTypes.h"
#include "TutorialPresentation.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * Один авторский «такт» обучения: реплика «Купола», субтитр, фокус камеры и
 * подсветка. FObjectiveProgress::Description хранит только короткую инструкцию,
 * поэтому режиссура живёт отдельной структурой и не размазывается по именам
 * акторов и Level Blueprint.
 */
USTRUCT(BlueprintType)
struct XRU1_API FTacticalTutorialBeat
{
	GENERATED_BODY()

	/** Идентификатор такта — для логов и повторного запуска. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FName BeatId;

	/** Кто говорит («Купол»). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FText Speaker;

	/** Текст субтитра. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial", meta = (MultiLine = "true"))
	FText Subtitle;

	/** Озвучка; проигрывает presentation-слой (BP), не задача StateTree. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TSoftObjectPtr<USoundBase> Voice;

	/** AnchorId, на который навести тактическую камеру. NAME_None — не трогать. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FName FocusAnchorId;

	// Поле HighlightAnchorIds убрано при аудите: его никто не читал. Подсветку
	// цели шага делают маркеры точек (политика шага) и зона квеста, а «куда
	// смотреть» — FocusAnchorId. Третий механизм подсветки не нужен.

	/** Длительность такта, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial", meta = (ClampMin = "0.1"))
	float Duration = 4.f;

	// --- Ответная реплика (диалоговый обмен) -----------------------------------
	// Половина тактов сценария — это ОБМЕН: боец реагирует, «Купол» объясняет
	// («Ай! За что?!» → «За то, что стоял столбом…»). Держать их в одном такте
	// правильнее, чем плодить состояния: последовательность реплик — это
	// презентация внутри шага, а не отдельная фаза обучения. Отдельным
	// состоянием остаётся только ПАУЗА, которая обязана задержать следующий шаг.
	// Пусто — обмена нет, такт кончается по Duration.

	/** Кто отвечает. Пусто — второй реплики нет. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Ответ")
	FText FollowUpSpeaker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Ответ", meta = (MultiLine = "true"))
	FText FollowUpSubtitle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Ответ")
	TSoftObjectPtr<USoundBase> FollowUpVoice;

	/** Длительность ответной реплики, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Ответ", meta = (ClampMin = "0"))
	float FollowUpDuration = 0.f;

	/** Есть ли вторая реплика (по тексту или по озвучке). */
	bool HasFollowUp() const
	{
		return !FollowUpSubtitle.IsEmpty() || !FollowUpVoice.IsNull();
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTutorialBeatStarted, FTacticalTutorialBeat, Beat);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTutorialBeatFinished, FTacticalTutorialBeat, Beat);

/**
 * Тонкий мост между StateTree-задачей такта и presentation-слоем (HUD/звук/VFX).
 * Механику такт не трогает: он только показывает реплику и наводит камеру, а
 * зачёт шага по-прежнему делает отдельная gameplay-objective.
 */
UCLASS()
class XRU1_API UTutorialPresentationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** HUD подписывается здесь и рисует спикера/субтитр. */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial")
	FOnTutorialBeatStarted OnBeatStarted;

	UPROPERTY(BlueprintAssignable, Category = "Tutorial")
	FOnTutorialBeatFinished OnBeatFinished;

	/** Запускает такт: наводит камеру на FocusAnchorId и оповещает presentation. */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void StartBeat(const FTacticalTutorialBeat& Beat);

	/** Закрывает такт; повторный вызов ничего не делает. */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void FinishBeat();

	UFUNCTION(BlueprintPure, Category = "Tutorial")
	bool IsBeatActive() const { return bBeatActive; }

	/**
	 * Игрок просит пропустить реплику (клик во время постановки). Флаг снимает
	 * задача такта: она одна владеет таймингом и знает, что дальше — ответная
	 * реплика или конец обмена.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void RequestSkipBeat();

	/** Забрать запрос пропуска (одноразово). */
	bool ConsumeSkipRequest();

	UFUNCTION(BlueprintPure, Category = "Tutorial")
	FTacticalTutorialBeat GetActiveBeat() const { return ActiveBeat; }

private:
	FTacticalTutorialBeat ActiveBeat;
	bool bBeatActive = false;

	/**
	 * Голос текущей реплики. Нужен, чтобы НОВАЯ реплика обрывала предыдущую:
	 * теперь озвучен каждый шаг, и быстрый игрок легко уходит из шага раньше,
	 * чем «Купол» договорил — без обрыва две реплики звучали бы разом.
	 * Естественное окончание такта голос НЕ рубит: фраза дотягивает хвост.
	 */
	TWeakObjectPtr<UAudioComponent> ActiveVoiceComponent;

	/**
	 * Субтитр текущей реплики в общем слое (`UXRU1SubtitleSubsystem`).
	 *
	 * Временем реплики владеет задача StateTree — она считает `Duration`, обмен
	 * репликами и пропуск игроком, — поэтому слой субтитров получает строку в
	 * режиме «снимет владелец»: собственных часов у него для обучения нет и
	 * разойтись с тактом ему нечем.
	 */
	FXRU1SubtitleHandle SubtitleHandle;

	/** Запрошен пропуск текущей реплики. */
	bool bSkipRequested = false;
};
