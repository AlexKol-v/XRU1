#include "TutorialHintOverlay.h"

#include "TacticalPlayerController.h"
#include "TutorialStyleData.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void STutorialHintOverlay::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;

	// Оверлей никогда не перехватывает мышь: это чисто информационный слой.
	SetVisibility(EVisibility::HitTestInvisible);

	// Позиция и размеры — из презентации обучения (DA_Tutorial_Style), чтобы
	// дизайнер двигал подсказки без пересборки. Get() никогда не вернёт nullptr:
	// без назначенного ассета берётся CDO с теми же дефолтами.
	const UTutorialStyleData* Style = UTutorialStyleData::Get(Owner.Get());
	const FVector2D Offset = Style->HintOffset;
	const int32 TitleSize = Style->HintTitleFontSize;
	const int32 TextSize = Style->HintTextFontSize;
	const int32 DenialSize = Style->HintDenialFontSize;
	const float WrapWidth = Style->HintWrapWidth;

	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", TitleSize);
	const FSlateFontInfo ObjectiveFont = FCoreStyle::GetDefaultFontStyle("Bold", TextSize);
	const FSlateFontInfo DenialFont = FCoreStyle::GetDefaultFontStyle("Regular", DenialSize);
	const FVector2D Shadow(1.0, 1.0);
	const FLinearColor ShadowColor(0.f, 0.f, 0.f, 0.85f);

	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	// Слева под верхним краем: центр занят баннером фазы, левый угол — дебагом.
	.Padding(FMargin(Offset.X, Offset.Y, 0.f, 0.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(TitleFont)
			.ColorAndOpacity(FLinearColor(0.55f, 0.75f, 0.95f))
			.ShadowOffset(Shadow)
			.ShadowColorAndOpacity(ShadowColor)
			.Text(this, &STutorialHintOverlay::GetQuestTitle)
			.Visibility(this, &STutorialHintOverlay::GetTitleVisibility)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Font(ObjectiveFont)
			.ColorAndOpacity(FLinearColor(0.95f, 0.96f, 1.f))
			.ShadowOffset(Shadow)
			.ShadowColorAndOpacity(ShadowColor)
			.Text(this, &STutorialHintOverlay::GetObjectiveLines)
			.Visibility(this, &STutorialHintOverlay::GetObjectiveVisibility)
			.WrapTextAt(WrapWidth)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Font(DenialFont)
			.ColorAndOpacity(FLinearColor(1.f, 0.72f, 0.25f))
			.ShadowOffset(Shadow)
			.ShadowColorAndOpacity(ShadowColor)
			.Text(this, &STutorialHintOverlay::GetDenialText)
			.Visibility(this, &STutorialHintOverlay::GetDenialVisibility)
			.WrapTextAt(WrapWidth)
		]
		// Субтитр реплики отсюда убран: речь рисует общий слой субтитров
		// (`UXRU1SubtitleSubsystem`) внизу экрана — он один на всю игру, виден
		// вне боя и настраивается игроком. Здесь остаётся только задача шага.
	];
}

FText STutorialHintOverlay::GetQuestTitle() const
{
	const ATacticalPlayerController* Controller = Owner.Get();
	return Controller ? Controller->GetTutorialQuestTitle() : FText::GetEmpty();
}

FText STutorialHintOverlay::GetObjectiveLines() const
{
	const ATacticalPlayerController* Controller = Owner.Get();
	return Controller ? Controller->GetTutorialObjectiveLines() : FText::GetEmpty();
}

FText STutorialHintOverlay::GetDenialText() const
{
	const ATacticalPlayerController* Controller = Owner.Get();
	return Controller ? Controller->GetTutorialDenialText() : FText::GetEmpty();
}

EVisibility STutorialHintOverlay::GetTitleVisibility() const
{
	return GetQuestTitle().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

EVisibility STutorialHintOverlay::GetObjectiveVisibility() const
{
	return GetObjectiveLines().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

EVisibility STutorialHintOverlay::GetDenialVisibility() const
{
	return GetDenialText().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}
