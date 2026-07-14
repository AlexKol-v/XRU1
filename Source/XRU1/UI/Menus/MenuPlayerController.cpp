#include "MenuPlayerController.h"
#include "MenuWidgets.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

AMenuPlayerController::AMenuPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void AMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}

	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UGameUIManagerSubsystem* UIManager = GameInstance ? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
	if (!UIManager)
	{
		return;
	}

	UIManager->CreateLayout(this, RootLayoutClass);

	UPrimaryGameLayout* RootLayout = UIManager->GetRootLayout();
	if (RootLayout && InitialScreenClass)
	{
		RootLayout->PushWidgetToLayer(EUILayer::Menu, InitialScreenClass);
	}

	// В меню игрового мира нет — ввод целиком отдаём UI.
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}
