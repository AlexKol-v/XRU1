#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HubGameMode.generated.h"

/**
 * GameMode уровня хаба: камера-обзор вместо персонажа и собственный контроллер.
 * Отдельный от GM_Tactics — в хабе нет боя, ходов и тактического ввода.
 */
UCLASS(Blueprintable)
class XRU1_API AHubGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHubGameMode();

protected:
	virtual void BeginPlay() override;
};
