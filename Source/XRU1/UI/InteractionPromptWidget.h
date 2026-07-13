#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractionPromptWidget.generated.h"

class UTextBlock;
class UInteractionDetectorComponent;

/**
 * Виджет UI-подсказки «Нажмите F, чтобы …». Подписывается на OnFocusChanged детектора;
 * при наличии фокуса показывается, без фокуса — скрыт. UMG-наследник должен содержать
 * UTextBlock с именем PromptText (matched через meta=(BindWidget)).
 */
UCLASS(Abstract)
class XRU1_API UInteractionPromptWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "UI")
    void BindToDetector(UInteractionDetectorComponent* Detector);

protected:
    /** BindWidget — UE свяжет с TextBlock в WBP по имени. */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> PromptText;

private:
    UFUNCTION()
    void HandleFocusChanged(AActor* NewFocus, FText Prompt);
};
