// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "STDialogueTypes.h"
#include "DialogueSubsystem.generated.h"

class ADialogueRunnerActor;
struct FStateTreeReference;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDialogueStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDialogueEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueLine, const FText&, Line, FGameplayTag, SpeakerId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueChoices, const TArray<FDialogueChoice>&, Choices);

/**
 * Подсистема жизненного цикла диалога. Спаунит ADialogueRunnerActor с ассетом
 * State Tree диалога, принимает продвижение выборов от UI (AdvanceChoice) и
 * ретранслирует реплики/выборы в UI через делегаты. Один активный диалог за раз.
 */
UCLASS()
class STQUESTSYSTEM_API UDialogueSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** Запускает диалог: спаунит раннер, запускает дерево, оповещает UI. */
    void StartDialogue(AActor* InNPC, AActor* InPlayer, const FStateTreeReference& DialogueLogic, FGameplayTag NPCSpeakerId);

    /** Передаёт выбор игрока (или продолжение реплики) в активный диалог. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void AdvanceChoice(int32 ChoiceIndex);

    /** Принудительно завершает активный диалог. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void EndDialogue();

    /** Идёт ли сейчас диалог. */
    UFUNCTION(BlueprintPure, Category = "Dialogue")
    bool IsDialogueActive() const;

    /** Ретранслятор реплики из задачи State Tree (через раннер). */
    void BroadcastLine(const FText& Line, FGameplayTag SpeakerId);

    /** Ретранслятор набора выборов из задачи State Tree (через раннер). */
    void BroadcastChoices(const TArray<FDialogueChoice>& Choices);

    /**
     * Повторно выдаёт текущую реплику/выборы — для UI, который активировался уже
     * после их отправки (виджет вызывает это при активации). Закрывает гонку
     * «реплика отправлена раньше, чем виджет подписался».
     */
    void ReplayCurrent();

    /** Колбэк раннера при завершении дерева диалога. */
    void HandleDialogueFinished(ADialogueRunnerActor* Runner);

    /** Диалог начался. */
    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueStarted OnDialogueStarted;

    /** Диалог завершился. */
    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueEnded OnDialogueEnded;

    /** Показать реплику NPC. */
    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueLine OnLine;

    /** Показать варианты выбора. */
    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueChoices OnChoices;

private:
    /** Текущий активный раннер диалога (единственный). */
    UPROPERTY(Transient)
    TObjectPtr<ADialogueRunnerActor> ActiveRunner;

    /** Кэш текущей реплики — чтобы дотянуть её в UI, открывшийся позже. */
    FText CurrentLine;

    /** Кэш говорящего текущей реплики. */
    FGameplayTag CurrentSpeaker;

    /** Кэш текущих выборов. */
    TArray<FDialogueChoice> CurrentChoices;

    /** Что сейчас показано: true — выборы, false — реплика. */
    bool bShowingChoices = false;
};
