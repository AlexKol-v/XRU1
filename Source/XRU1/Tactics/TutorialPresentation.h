#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TutorialPresentation.generated.h"

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

	/** AnchorId акторов, которые следует подсветить на время такта. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TArray<FName> HighlightAnchorIds;

	/** Длительность такта, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial", meta = (ClampMin = "0.1"))
	float Duration = 4.f;
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

	UFUNCTION(BlueprintPure, Category = "Tutorial")
	FTacticalTutorialBeat GetActiveBeat() const { return ActiveBeat; }

private:
	FTacticalTutorialBeat ActiveBeat;
	bool bBeatActive = false;
};
