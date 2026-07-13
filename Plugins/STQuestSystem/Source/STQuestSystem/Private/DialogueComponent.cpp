// Copyright Epic Games, Inc. All Rights Reserved.

#include "DialogueComponent.h"

#include "DialogueSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UDialogueComponent::StartDialogueWith(AActor* Player)
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    UDialogueSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UDialogueSubsystem>() : nullptr;
    if (Subsystem)
    {
        Subsystem->StartDialogue(GetOwner(), Player, DialogueLogic, SpeakerId);
    }
}
