#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ATacticalPlayerController;

/**
 * Оверлей подсказок обучения: трекер целей квеста + причина отказа Action Gate.
 *
 * Речь «Купола» здесь больше не рисуется: субтитры переехали в общий слой
 * (`UXRU1SubtitleSubsystem`), который живёт и в меню, и в хабе, и в бою.
 * Здесь остаётся только то, что относится к ЗАДАЧЕ шага, а не к реплике.
 *
 * Нарисован на чистом Slate и добавляется в viewport из контроллера — не требует
 * UMG-вёрстки. WBP-трекер целей остаётся production-заменой; этот оверлей —
 * рабочий минимум, который агент может поддерживать без ручной работы
 * в Designer (слой обучения — docs/03_ARCHITECTURE.md §9).
 *
 * Данные тянутся атрибутами каждый кадр из контроллера: Slate сам перерисует
 * только изменившийся текст, отдельная система событий не нужна.
 */
class XRU1_API STutorialHintOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STutorialHintOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ATacticalPlayerController>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TWeakObjectPtr<ATacticalPlayerController> Owner;

	FText GetQuestTitle() const;
	FText GetObjectiveLines() const;
	FText GetDenialText() const;
	EVisibility GetTitleVisibility() const;
	EVisibility GetObjectiveVisibility() const;
	EVisibility GetDenialVisibility() const;
};
