#include "SubtitleOverlay.h"

#include "SubtitleSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "XRU1.Subtitles"

void SXRU1SubtitleOverlay::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;

	// Информационный слой: мышь и клавиатура проходят насквозь всегда.
	SetVisibility(EVisibility::HitTestInvisible);

	if (Owner.IsValid())
	{
		CachedStyle = Owner->GetResolvedStyle();
	}

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Bottom)
	[
		// Отступ снизу задаёт SBox: сам якорь (нижний край) неизменен, меняется
		// только высота — «чуть выше низа экрана» в роликах и меню, «выше панели
		// способностей» в бою.
		SNew(SBox)
		.Padding(this, &SXRU1SubtitleOverlay::GetBottomPadding)
		[
			SNew(SBorder)
			.Visibility(this, &SXRU1SubtitleOverlay::GetContentVisibility)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(this, &SXRU1SubtitleOverlay::GetBackdropColor)
			.Padding(this, &SXRU1SubtitleOverlay::GetBackdropPadding)
			[
				SNew(SVerticalBox)

				// Имя говорящего отдельной строкой: его показ — настройка игрока,
				// поэтому оно не может быть частью переводимого текста реплики.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Visibility(this, &SXRU1SubtitleOverlay::GetSpeakerVisibility)
					.Text(this, &SXRU1SubtitleOverlay::GetSpeakerText)
					.Font(this, &SXRU1SubtitleOverlay::GetSpeakerFont)
					.ColorAndOpacity(this, &SXRU1SubtitleOverlay::GetSpeakerColor)
					.Justification(ETextJustify::Center)
				]

				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(this, &SXRU1SubtitleOverlay::GetLineText)
					.Font(this, &SXRU1SubtitleOverlay::GetLineFont)
					.ColorAndOpacity(this, &SXRU1SubtitleOverlay::GetLineColor)
					.Justification(ETextJustify::Center)
					.WrapTextAt(this, &SXRU1SubtitleOverlay::GetWrapWidth)
				]

				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Visibility(this, &SXRU1SubtitleOverlay::GetHintVisibility)
					.Text(this, &SXRU1SubtitleOverlay::GetHintText)
					.Font(this, &SXRU1SubtitleOverlay::GetHintFont)
					.ColorAndOpacity(this, &SXRU1SubtitleOverlay::GetHintColor)
					.Justification(ETextJustify::Center)
				]
			]
		]
	];
}

void SXRU1SubtitleOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (Owner.IsValid())
	{
		CachedStyle = Owner->GetResolvedStyle();
	}
}

FText SXRU1SubtitleOverlay::GetSpeakerText() const
{
	return Owner.IsValid() ? Owner->GetActiveLine().Speaker : FText::GetEmpty();
}

FText SXRU1SubtitleOverlay::GetLineText() const
{
	return Owner.IsValid() ? Owner->GetActiveLine().Text : FText::GetEmpty();
}

FText SXRU1SubtitleOverlay::GetHintText() const
{
	return LOCTEXT("SkipHint", "[Пробел — пропустить]");
}

EVisibility SXRU1SubtitleOverlay::GetContentVisibility() const
{
	// Оверлей живёт в viewport и рисуется даже когда мир остановлен — Slate про
	// паузу не знает. Поэтому спрашиваем подсистему: на паузе реплика не звучит
	// и её часы стоят, значит строке на экране делать нечего. Строка при этом
	// НЕ снята и вернётся вместе с голосом (`IsDisplaySuppressed`).
	const bool bHasLine = Owner.IsValid() && !Owner->GetVisibleLine().IsEmpty();
	return bHasLine ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SXRU1SubtitleOverlay::GetSpeakerVisibility() const
{
	const bool bShow = CachedStyle.bShowSpeaker
		&& Owner.IsValid() && !Owner->GetActiveLine().Speaker.IsEmpty();
	return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SXRU1SubtitleOverlay::GetHintVisibility() const
{
	// Подсказку рисует слой, а не автор реплики. Пока авторские тексты обучения
	// содержат её сами, флаг темы держит её выключенной — иначе подсказка
	// удвоится (см. docs/14_SUBTITLES.md).
	const bool bShow = CachedStyle.bShowSkipHint
		&& Owner.IsValid() && Owner->GetActiveLine().bSkippable;
	return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

FMargin SXRU1SubtitleOverlay::GetBottomPadding() const
{
	return FMargin(0.f, 0.f, 0.f, FMath::Max(0.f, CachedStyle.BottomOffset));
}

FMargin SXRU1SubtitleOverlay::GetBackdropPadding() const
{
	return CachedStyle.BackdropPadding;
}

FSlateColor SXRU1SubtitleOverlay::GetBackdropColor() const
{
	return FSlateColor(CachedStyle.BackdropColor);
}

FSlateColor SXRU1SubtitleOverlay::GetSpeakerColor() const
{
	return FSlateColor(CachedStyle.SpeakerColor);
}

FSlateColor SXRU1SubtitleOverlay::GetLineColor() const
{
	return FSlateColor(CachedStyle.LineColor);
}

FSlateColor SXRU1SubtitleOverlay::GetHintColor() const
{
	return FSlateColor(CachedStyle.HintColor);
}

FSlateFontInfo SXRU1SubtitleOverlay::GetSpeakerFont() const
{
	return FCoreStyle::GetDefaultFontStyle("Bold", CachedStyle.SpeakerFontSize);
}

FSlateFontInfo SXRU1SubtitleOverlay::GetLineFont() const
{
	return FCoreStyle::GetDefaultFontStyle("Regular", CachedStyle.LineFontSize);
}

FSlateFontInfo SXRU1SubtitleOverlay::GetHintFont() const
{
	return FCoreStyle::GetDefaultFontStyle("Regular", CachedStyle.HintFontSize);
}

float SXRU1SubtitleOverlay::GetWrapWidth() const
{
	return FMath::Max(200.f, CachedStyle.WrapWidth);
}

#undef LOCTEXT_NAMESPACE
