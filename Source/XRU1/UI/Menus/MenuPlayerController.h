#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Templates/SubclassOf.h"
#include "MenuPlayerController.generated.h"

class UMenuScreenBase;
class UPrimaryGameLayout;

/**
 * Контроллер уровня главного меню: в BeginPlay поднимает корневой
 * UPrimaryGameLayout (через UGameUIManagerSubsystem), проталкивает стартовый
 * экран (UMainMenuWidget) на слой Menu и переводит ввод в режим UI.
 * Ставится PlayerController'ом в GameMode уровня меню.
 */
UCLASS(Abstract)
class XRU1_API AMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMenuPlayerController();

protected:
	virtual void BeginPlay() override;

	/** Класс корневого UI-слоя (BP с четырьмя стеками-слоями). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UPrimaryGameLayout> RootLayoutClass;

	/** Стартовый экран меню (обычно BP-наследник UMainMenuWidget). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UMenuScreenBase> InitialScreenClass;
};
