// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StateTreeReference.h"
#include "DialogueComponent.generated.h"

/**
 * Компонент NPC: хранит ассет State Tree диалога и запускает диалог с игроком
 * через UDialogueSubsystem. Вешается на NPC; вызывается из логики взаимодействия
 * (как UQuestGiverComponent выдаёт квесты).
 */
UCLASS(ClassGroup = "Dialogue", meta = (BlueprintSpawnableComponent))
class STQUESTSYSTEM_API UDialogueComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    /** Ассет State Tree диалога, который ведёт этот NPC. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    FStateTreeReference DialogueLogic;

    /**
     * Идентичность говорящего для этого NPC. Реплики ассета диалога, у которых
     * SpeakerId не задан явно, приписываются этому тегу — поэтому один ассет
     * диалога переиспользуется разными персонажами.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    FGameplayTag SpeakerId;

    /** Запускает диалог этого NPC с указанным игроком. */
    UFUNCTION(BlueprintCallable, Category = "Dialogue")
    void StartDialogueWith(AActor* Player);
};
