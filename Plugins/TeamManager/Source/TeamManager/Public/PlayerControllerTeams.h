// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PlayerControllerTeams.generated.h"

/**
 * 
 */
UCLASS()
class TEAMMANAGER_API APlayerControllerTeams : public APlayerController
{
	GENERATED_BODY()
	
public:
	APlayerControllerTeams();
	
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	void SetActualTeamID(AController* ControllerRef, uint8 NewTeamId);
	
	UFUNCTION(BlueprintCallable, Category = "TeamManager")
	uint8 GetActualTeamID(AController* ControllerRef);
	
private:
	uint8 ActualTeamID;
};
