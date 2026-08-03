#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "TacticalQuestZone.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/**
 * Зона для тактического бойца игрока. В отличие от donor AQuestWaypoint она не
 * требует IsPlayerControlled(): в XRU1 игрок управляет camera pawn, а бойцами — AIController.
 */
UCLASS(Blueprintable)
class XRU1_API ATacticalQuestZone : public AActor
{
	GENERATED_BODY()

public:
	ATacticalQuestZone();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Quest")
	TObjectPtr<UBoxComponent> TriggerBox;

	/**
	 * Подсветка зоны на время шага, который её ждёт (декаль по размеру бокса).
	 * Включает/выключает `Tactical Objective` c RequiredTargetAnchor этой зоны;
	 * материал — `DA_Tutorial_Style.ZoneMarkerMaterial`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest")
	void SetHighlighted(bool bNewHighlighted);

protected:
	/** Декаль подсветки; создаётся лениво при первом включении. */
	UPROPERTY(Transient)
	TObjectPtr<class UDecalComponent> HighlightDecal;

	/** Точный канал Quest.Event.*, публикуемый после подтверждённого входа. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest", meta = (Categories = "Quest.Event"))
	FGameplayTag EventChannel;

	/** Один юнит не может многократно накрутить одну цель выходом/входом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bOneShotPerUnit = true;

	/** После первого принятого входа зона полностью отключает collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bDisableAfterFirstUnit = false;

	UFUNCTION(BlueprintImplementableEvent, Category = "Quest")
	void OnTacticalUnitEntered(AActor* Unit);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	TSet<TWeakObjectPtr<AActor>> TriggeredUnits;
};
