// Copyright Epic Games, Inc. All Rights Reserved.

#include "DialogueWidget.h"

#include "DialogueSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UDialogueWidget::NativeOnActivated()
{
    Super::NativeOnActivated();

    if (UDialogueSubsystem* Subsystem = GetDialogueSubsystem())
    {
        Subsystem->OnLine.AddDynamic(this, &UDialogueWidget::HandleLine);
        Subsystem->OnChoices.AddDynamic(this, &UDialogueWidget::HandleChoices);
        Subsystem->OnDialogueEnded.AddDynamic(this, &UDialogueWidget::HandleDialogueEnded);

        // Если реплика/выборы уже отправлены до активации виджета — дотянуть их.
        Subsystem->ReplayCurrent();
    }
}

void UDialogueWidget::NativeOnDeactivated()
{
    if (UDialogueSubsystem* Subsystem = GetDialogueSubsystem())
    {
        Subsystem->OnLine.RemoveDynamic(this, &UDialogueWidget::HandleLine);
        Subsystem->OnChoices.RemoveDynamic(this, &UDialogueWidget::HandleChoices);
        Subsystem->OnDialogueEnded.RemoveDynamic(this, &UDialogueWidget::HandleDialogueEnded);
    }

    Super::NativeOnDeactivated();
}

void UDialogueWidget::SelectChoice(int32 ChoiceIndex)
{
    if (UDialogueSubsystem* Subsystem = GetDialogueSubsystem())
    {
        Subsystem->AdvanceChoice(ChoiceIndex);
    }
}

void UDialogueWidget::ContinueDialogue()
{
    SelectChoice(0);
}

UDialogueSubsystem* UDialogueWidget::GetDialogueSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UDialogueSubsystem>() : nullptr;
}

void UDialogueWidget::HandleLine(const FText& Line, FGameplayTag SpeakerId)
{
    DisplayLine(Line, SpeakerId);
}

void UDialogueWidget::HandleChoices(const TArray<FDialogueChoice>& Choices)
{
    DisplayChoices(Choices);
}

void UDialogueWidget::HandleDialogueEnded()
{
    DeactivateWidget();
}
