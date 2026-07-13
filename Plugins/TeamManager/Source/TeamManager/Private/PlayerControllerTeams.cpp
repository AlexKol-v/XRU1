// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerControllerTeams.h"

APlayerControllerTeams::APlayerControllerTeams()
{
}

void APlayerControllerTeams::SetActualTeamID(AController* ControllerRef, uint8 NewTeamId)
{
	if (ControllerRef)
	{
		ActualTeamID = NewTeamId;
	}
}

uint8 APlayerControllerTeams::GetActualTeamID(AController* ControllerRef)
{
	if (ControllerRef)
	{
		return ActualTeamID;
	}
	return 0;
}
