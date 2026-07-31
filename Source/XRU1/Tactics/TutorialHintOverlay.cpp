#include "TutorialHintOverlay.h"

#include "TacticalPlayerController.h"
#include "TacticalHUDStyleData.h"
#include "TacticsGameInstance.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void STutorialHintOverlay::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;

	// Оверлей никогда не перехватывает мышь: это чисто информационный слой.
	SetVisibility(EVisibility::HitTestInvisible);

	// Позиция и размеры — из общей UI-темы (DA_TacticalHUDStyle), чтобы дизайнер
	// двигал подсказки без пересборки. Нет темы — дефолты из C++.
	FVector2D Offset(28.f, 110.f);
	int32 TitleSize = 12, TextSize = 17, DenialSize = 14;
	float WrapWidth = 560.f;
	if (const ATacticalPlayerController* Controller = Owner.Get())
	{
		const UTacticsGameInstance* GameInstance =
			Controller->GetGameInstance<UTacticsGameInstance>();
		if (const UTacticalHUDStyleData* Theme = GameInstance ? GameInstance->GetUITheme() : nullptr)
		{
			Offset = Theme->TutorialHintOffset;
			TitleSize = Theme->TutorialHintTitleFontSize;
			TextSize = Theme->TutorialHintTextFontSize;
			DenialSize = Theme->TutorialHintDenialFontSize;
			WrapWidth = Theme->TutorialHintWrapWidth;
		}
	}

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
