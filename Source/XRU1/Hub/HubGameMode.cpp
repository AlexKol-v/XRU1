#include "HubGameMode.h"

#include "HubCameraPawn.h"
#include "HubPlayerController.h"
#include "GameFramework/HUD.h"
#include "TacticsAudioSubsystem.h"

AHubGameMode::AHubGameMode()
{
	DefaultPawnClass = AHubCameraPawn::StaticClass();
	PlayerControllerClass = AHubPlayerController::StaticClass();
	// Пустой HUDClass заставляет движок спавнить AHUD без класса (эта же ошибка
	// уже ловилась в GM_Tactics) — задаём базовый явно.
	HUDClass = AHUD::StaticClass();
}

void AHubGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Возврат из миссии на карту сектора обязан вернуть спокойный трек хаба:
	// боевая петля, доигрывающая под оперативной картой, читается как баг.
	if (UTacticsAudioSubsystem* Audio = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
	{
		Audio->PlayHubMusic();
	}
}
