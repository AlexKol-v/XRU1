#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "TacticalScenarioDirector.generated.h"

class UTacticalScenarioDataAsset;

/**
 * Единственная точка старта логического сценария на persistent-карте.
 * Level Blueprint не участвует: BP-наследник загружает указанный sublevel,
 * а после On Level Loaded вызывает StartConfiguredQuest().
 */
UCLASS(Blueprintable)
class XRU1_API ATacticalScenarioDirector : public AActor
{
	GENERATED_BODY()

public:
	ATacticalScenarioDirector();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Scenario")
	TObjectPtr<UTacticalScenarioDataAsset> ActiveScenario;

	/** Поколение конкретного запуска общей карты; сохранять вместе с async callbacks. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Scenario")
	int32 ScenarioRunId = 0;

	/** Grace для StateTree после Result event до нативного terminal fallback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Quest",
		meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float QuestFinalizationGracePeriod = 0.25f;

	/** Вызывается один раз после чтения ActiveScenario из GameInstance. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Scenario")
	void OnScenarioSelected(UTacticalScenarioDataAsset* Scenario);

	/** Вызывается через дополнительный tick после Scenario.Ready; здесь открывать input/gate. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Scenario")
	void OnScenarioReady();

	/**
	 * Вызывать только после загрузки scenario sublevel и регистрации его акторов.
	 * QuestOwner по умолчанию — camera pawn игрока, что совместимо с quest debugger.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scenario|Quest")
	bool StartConfiguredQuest(AActor* QuestOwner = nullptr);

	/**
	 * Единая точка подтверждённого исхода. Вызывать из GameMode только после
	 * проверки его правил победы/поражения, до открытия result screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scenario|Quest")
	bool FinalizeConfiguredScenario(bool bSuccess);

	/** Защита latent callbacks: относится ли этот Director к текущему run GameInstance. */
	UFUNCTION(BlueprintPure, Category = "Scenario")
	bool IsCurrentScenarioRun() const;

	/** Quest уже подтверждённо перешёл в нужный terminal state. */
	UFUNCTION(BlueprintPure, Category = "Scenario|Quest")
	bool IsScenarioFinalized() const { return bScenarioFinalized; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Разносит PlayerStarted и Scenario.Ready по разным ticks. */
	void BroadcastReadyEvent();

	/** Даёт StateTree один полный tick обработать Ready до открытия первого gate. */
	void OpenScenarioReadyGate();

	/** Разносит objective/result по ticks и после grace применяет terminal fallback. */
	void CompletePendingFinalization();

	FGameplayTag ActiveQuestId;
	FTimerHandle ReadyEventTimerHandle;
	FTimerHandle FinalizationTimerHandle;
	int32 PendingFinalizationRunId = 0;
	int32 FinalizationStage = 0;
	bool bReadyEventBroadcast = false;
	bool bReadyEventPending = false;
	bool bReadyGateOpened = false;
	bool bScenarioFinalized = false;
	bool bFinalizationPending = false;
	bool bPendingFinalizationSuccess = false;
};
