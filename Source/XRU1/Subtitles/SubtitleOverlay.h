#pragma once

#include "CoreMinimal.h"
#include "SubtitleTypes.h"
#include "Widgets/SCompoundWidget.h"

class UXRU1SubtitleSubsystem;

/**
 * Встроенный дисплей субтитров: подложка, имя говорящего, текст, подсказка
 * пропуска. Живёт в viewport поверх корневого лейаута CommonUI, поэтому виден
 * и в меню, и в интро, и в бою.
 *
 * Виджет намеренно ГЛУПЫЙ: он не решает ни что показывать, ни как долго, ни
 * каким кеглем — всё это ему отдаёт `UXRU1SubtitleSubsystem::GetResolvedStyle()`
 * одной структурой. Из-за этого его можно заменить на WBP (флаг
 * `bUseBuiltInDisplay` в настройках проекта), не трогая ни подсистему, ни
 * источники реплик.
 *
 * Чистый Slate, а не UMG — по той же причине, что и `STutorialHintOverlay`:
 * оверлей не требует ручной вёрстки в редакторе и работает сразу после сборки.
 */
class XRU1_API SXRU1SubtitleOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SXRU1SubtitleOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UXRU1SubtitleSubsystem>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
		const float InDeltaTime) override;

private:
	TWeakObjectPtr<UXRU1SubtitleSubsystem> Owner;

	/**
	 * Стиль пересчитывается раз в кадр, а не на каждый атрибут: Slate дёргает
	 * геттеры многократно за проход, а сведение темы с настройками — не
	 * бесплатная операция.
	 */
	FXRU1SubtitleStyle CachedStyle;

	FText GetSpeakerText() const;
	FText GetLineText() const;
	FText GetHintText() const;

	EVisibility GetContentVisibility() const;
	EVisibility GetSpeakerVisibility() const;
	EVisibility GetHintVisibility() const;

	FMargin GetBottomPadding() const;
	FMargin GetBackdropPadding() const;
	FSlateColor GetBackdropColor() const;
	FSlateColor GetSpeakerColor() const;
	FSlateColor GetLineColor() const;
	FSlateColor GetHintColor() const;
	FSlateFontInfo GetSpeakerFont() const;
	FSlateFontInfo GetLineFont() const;
	FSlateFontInfo GetHintFont() const;
	float GetWrapWidth() const;
};
