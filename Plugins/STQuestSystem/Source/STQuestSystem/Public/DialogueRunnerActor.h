// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "StateTreeReference.h"
#include "DialogueRunnerActor.generated.h"

class UStateTreeComponent;
class UDialogueSubsystem;
struct FStateTreeExecutionContext;

/**
 * Невидимый актор-носитель исполнения одного диалога. Несёт UStateTreeComponent
 * со стоковой схемой; диалог авторится как ассет State Tree. Продвижение выборов
 * приходит не из GMS, а вызовом UDialogueSubsystem::AdvanceChoice, который через
 * раннер шлёт в дерево событие Dialogue.Event.Choice. Завершение дерева
 * опрашивается в Tick и сообщается подсистеме.
 */
UCLASS()
class STQUESTSYSTEM_API ADialogueRunnerActor : public AActor
{
    GENERATED_BODY()

public:
    ADialogueRunnerActor();

    /** Запускает State Tree диалога. NPC и Player — участники реплик/действий. */
    void StartDialogue(const FStateTreeReference& DialogueLogic, AActor* InNPC, AActor* InPlayer, FGameplayTag InNPCSpeakerId);

    /** Останавливает State Tree диалога. */
    void StopDialogue();

    /** Кладёт индекс выбора и шлёт в дерево событие продвижения. */
    void SendChoiceEvent(int32 ChoiceIndex);

    /** Раннер, исполняющий State Tree данного контекста, или nullptr. */
    static ADialogueRunnerActor* GetFromContext(FStateTreeExecutionContext& Context);

    /** NPC — собеседник игрока в этом диалоге. */
    AActor* GetNPCActor() const { return NPC.Get(); }

    /** Игрок, ведущий этот диалог. */
    AActor* GetPlayerActor() const { return Player.Get(); }

    /** Индекс последнего выбора, переданного игроком (читается задачей выборов). */
    int32 GetPendingChoiceIndex() const { return PendingChoiceIndex; }

    /** Тег говорящего NPC — для реплик без явного SpeakerId в ассете. */
    FGameplayTag GetNPCSpeakerId() const { return NPCSpeakerId; }

    /** Подсистема диалогов текущего GameInstance. */
    UDialogueSubsystem* GetDialogueSubsystem() const;

protected:
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** Компонент, исполняющий State Tree диалога. */
    UPROPERTY(VisibleAnywhere, Category = "Dialogue")
    TObjectPtr<UStateTreeComponent> DialogueStateTree;

    /** NPC-собеседник. */
    TWeakObjectPtr<AActor> NPC;

    /** Игрок, ведущий диалог. */
    TWeakObjectPtr<AActor> Player;

    /** Тег говорящего NPC — задаётся при старте, читается задачей-репликой. */
    FGameplayTag NPCSpeakerId;

    /** Индекс последнего выбора игрока. */
    int32 PendingChoiceIndex = INDEX_NONE;

    /** Защита от повторного уведомления подсистемы о завершении. */
    bool bFinished = false;
};
