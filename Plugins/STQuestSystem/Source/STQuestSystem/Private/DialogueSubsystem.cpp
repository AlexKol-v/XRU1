// Copyright Epic Games, Inc. All Rights Reserved.

#include "DialogueSubsystem.h"

#include "DialogueRunnerActor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UDialogueSubsystem::StartDialogue(AActor* InNPC, AActor* InPlayer, const FStateTreeReference& DialogueLogic, FGameplayTag NPCSpeakerId)
{
    // Один активный диалог за раз — предыдущий завершаем.
    if (ActiveRunner)
    {
        EndDialogue();
    }

    const UGameInstance* GameInstance = GetGameInstance();
    UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    if (!World)
    {
        return;
    }

    ActiveRunner = World->SpawnActor<ADialogueRunnerActor>();
    if (!ActiveRunner)
    {
        return;
    }

    // Сбрасываем кэш прошлого диалога, СНАЧАЛА показываем окно (OnDialogueStarted
    // → UI пушится и подписывается на OnLine/OnChoices), и только ПОТОМ запускаем
    // дерево — тогда первая реплика дойдёт до уже подписанного виджета. На случай
    // отложенной активации виджета его дотягивает ReplayCurrent (см. ниже).
    CurrentLine = FText::GetEmpty();
    CurrentChoices.Reset();
    bShowingChoices = false;

    OnDialogueStarted.Broadcast();
    ActiveRunner->StartDialogue(DialogueLogic, InNPC, InPlayer, NPCSpeakerId);
}

void UDialogueSubsystem::AdvanceChoice(int32 ChoiceIndex)
{
    if (ActiveRunner)
    {
        ActiveRunner->SendChoiceEvent(ChoiceIndex);
    }
}

void UDialogueSubsystem::EndDialogue()
{
    if (!ActiveRunner)
    {
        return;
    }

    // Снимаем ссылку до уничтожения — HandleDialogueFinished не зайдёт повторно.
    ADialogueRunnerActor* Runner = ActiveRunner;
    ActiveRunner = nullptr;

    Runner->StopDialogue();
    Runner->Destroy();

    OnDialogueEnded.Broadcast();
}

bool UDialogueSubsystem::IsDialogueActive() const
{
    return ActiveRunner != nullptr;
}

void UDialogueSubsystem::BroadcastLine(const FText& Line, FGameplayTag SpeakerId)
{
    CurrentLine = Line;
    CurrentSpeaker = SpeakerId;
    bShowingChoices = false;
    OnLine.Broadcast(Line, SpeakerId);
}

void UDialogueSubsystem::BroadcastChoices(const TArray<FDialogueChoice>& Choices)
{
    CurrentChoices = Choices;
    bShowingChoices = true;
    OnChoices.Broadcast(Choices);
}

void UDialogueSubsystem::ReplayCurrent()
{
    // UI, активированный уже после отправки, дотягивает текущую реплику/выборы.
    if (bShowingChoices)
    {
        OnChoices.Broadcast(CurrentChoices);
    }
    else if (!CurrentLine.IsEmpty())
    {
        OnLine.Broadcast(CurrentLine, CurrentSpeaker);
    }
}

void UDialogueSubsystem::HandleDialogueFinished(ADialogueRunnerActor* Runner)
{
    if (Runner == ActiveRunner)
    {
        EndDialogue();
    }
}
