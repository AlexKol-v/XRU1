// Copyright Epic Games, Inc. All Rights Reserved.

#include "DialogueRunnerActor.h"

#include "Components/ActorComponent.h"
#include "Components/StateTreeComponent.h"
#include "DialogueGameplayTags.h"
#include "DialogueSubsystem.h"
#include "Engine/GameInstance.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeTypes.h"

ADialogueRunnerActor::ADialogueRunnerActor()
{
    PrimaryActorTick.bCanEverTick = true;

    DialogueStateTree = CreateDefaultSubobject<UStateTreeComponent>(TEXT("DialogueStateTree"));
    // Запуск дерева — вручную из StartDialogue, ПОСЛЕ назначения ассета логики.
    DialogueStateTree->SetStartLogicAutomatically(false);
}

ADialogueRunnerActor* ADialogueRunnerActor::GetFromContext(FStateTreeExecutionContext& Context)
{
    // Владельцем контекста может быть либо сам раннер, либо его State Tree-компонент.
    UObject* Owner = Context.GetOwner();
    if (ADialogueRunnerActor* Runner = Cast<ADialogueRunnerActor>(Owner))
    {
        return Runner;
    }
    if (const UActorComponent* Component = Cast<UActorComponent>(Owner))
    {
        return Cast<ADialogueRunnerActor>(Component->GetOwner());
    }
    return nullptr;
}

UDialogueSubsystem* ADialogueRunnerActor::GetDialogueSubsystem() const
{
    const UGameInstance* GameInstance = GetGameInstance();
    return GameInstance ? GameInstance->GetSubsystem<UDialogueSubsystem>() : nullptr;
}

void ADialogueRunnerActor::StartDialogue(const FStateTreeReference& DialogueLogic, AActor* InNPC, AActor* InPlayer, FGameplayTag InNPCSpeakerId)
{
    NPC = InNPC;
    Player = InPlayer;
    NPCSpeakerId = InNPCSpeakerId;
    if (!DialogueStateTree)
    {
        return;
    }

    DialogueStateTree->SetStateTreeReference(DialogueLogic);
    DialogueStateTree->StartLogic();
}

void ADialogueRunnerActor::SendChoiceEvent(int32 ChoiceIndex)
{
    PendingChoiceIndex = ChoiceIndex;
    if (!DialogueStateTree)
    {
        return;
    }

    FStateTreeEvent Event;
    Event.Tag = DialogueGameplayTags::Dialogue_Event_Choice;
    DialogueStateTree->SendStateTreeEvent(Event);
}

void ADialogueRunnerActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bFinished || !DialogueStateTree)
    {
        return;
    }

    // Опрашиваем статус дерева — так раннер узнаёт о завершении диалога.
    const EStateTreeRunStatus Status = DialogueStateTree->GetStateTreeRunStatus();
    if (Status == EStateTreeRunStatus::Succeeded || Status == EStateTreeRunStatus::Failed)
    {
        bFinished = true;
        if (UDialogueSubsystem* Subsystem = GetDialogueSubsystem())
        {
            Subsystem->HandleDialogueFinished(this);
        }
    }
}

void ADialogueRunnerActor::StopDialogue()
{
    if (DialogueStateTree)
    {
        DialogueStateTree->StopLogic(TEXT("Dialogue stopped"));
    }
}

void ADialogueRunnerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopDialogue();
    Super::EndPlay(EndPlayReason);
}
