#include "InteractionPromptWidget.h"

#include "Components/TextBlock.h"
#include "InteractionDetectorComponent.h"

void UInteractionPromptWidget::BindToDetector(UInteractionDetectorComponent* Detector)
{
    if (!Detector) return;

    Detector->OnFocusChanged.RemoveDynamic(this, &UInteractionPromptWidget::HandleFocusChanged);
    Detector->OnFocusChanged.AddDynamic   (this, &UInteractionPromptWidget::HandleFocusChanged);

    SetVisibility(ESlateVisibility::Hidden);
}

void UInteractionPromptWidget::HandleFocusChanged(AActor* NewFocus, FText Prompt)
{
    if (!PromptText) return;

    if (NewFocus && !Prompt.IsEmpty())
    {
        PromptText->SetText(Prompt);
        SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    else
    {
        SetVisibility(ESlateVisibility::Hidden);
    }
}
