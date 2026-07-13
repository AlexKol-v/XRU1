// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "QuestRequirement.h"
#include "QuestGiverComponent.generated.h"

class UQuestDefinition;

/** Запись о выдаваемом NPC квесте с условиями выдачи. */
USTRUCT(BlueprintType)
struct FQuestGiverEntry
{
    GENERATED_BODY()

    /** Определение выдаваемого квеста. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    TObjectPtr<UQuestDefinition> Quest;

    /** Условия, при которых этот NPC может выдать квест. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    TArray<FQuestRequirement> Conditions;
};

/**
 * Компонент NPC: хранит список выдаваемых квестов с условиями и выдаёт их
 * игроку — как правило, по взаимодействию или через диалог.
 */
UCLASS(ClassGroup = "Quest", meta = (BlueprintSpawnableComponent))
class STQUESTSYSTEM_API UQuestGiverComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UQuestGiverComponent();

    /** Квесты, которые этот NPC может выдать. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
    TArray<FQuestGiverEntry> IssuableQuests;

    /**
     * Возвращает квесты, которые можно предложить игроку сейчас: ещё не взятые
     * (состояние Inactive) и с выполненными условиями выдачи.
     */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    TArray<UQuestDefinition*> GetAvailableQuests(AActor* Player) const;

    /** Есть ли у NPC хотя бы один квест, который можно предложить игроку сейчас. */
    UFUNCTION(BlueprintPure, Category = "Quest")
    bool HasQuestToOffer(AActor* Player) const;

    /** Выдаёт квест игроку: делает его доступным и запускает. */
    UFUNCTION(BlueprintCallable, Category = "Quest")
    void GrantQuest(FGameplayTag QuestId, AActor* Player);
};
