#include "HubPlayerController.h"

#include "HologramMapActor.h"
#include "HubCameraPawn.h"
#include "HubHUDWidget.h"
#include "MissionBriefingWidget.h"
#include "MissionPointOfInterest.h"
#include "TacticsAudioSubsystem.h"
#include "GamePauseSubsystem.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "XRU1Log.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

AHubPlayerController::AHubPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;

	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	// Попапы маркеров живут на OnBeginCursorOver примитива — без этих флагов
	// AMissionPointOfInterest не получит ни одного события наведения.
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AHubPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}

	UWorld* World = GetWorld();
	for (TActorIterator<AHologramMapActor> It(World); It; ++It)
	{
		HologramMap = *It;
		break;
	}
	if (!HologramMap)
	{
		UE_LOG(LogXRU1UI, Warning,
			TEXT("[Hub] На уровне нет AHologramMapActor — вращать нечего, маркеры не найдутся"));
	}
	else
	{
		UE_LOG(LogXRU1UI, Display, TEXT("[Hub] карта '%s' найдена, точек интереса: %d"),
			*HologramMap->GetActorNameOrLabel(), HologramMap->GetPointsOfInterest().Num());
	}

	// Отсутствие HUD в хабе однажды было «невидимой» поломкой: контроллер
	// доходил до конца BeginPlay, а UI не появлялся и в логе не было причины.
	// Поэтому каждый шаг пути до виджета печатается явно.
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UGameUIManagerSubsystem* UIManager =
		GameInstance ? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
	UE_LOG(LogXRU1UI, Display,
		TEXT("[Hub] UI: контроллер='%s' GameInstance=%s UIManager=%s RootLayoutClass=%s HubHUDClass=%s"),
		*GetNameSafe(this), GameInstance ? TEXT("есть") : TEXT("НЕТ"),
		UIManager ? TEXT("есть") : TEXT("НЕТ"),
		*GetNameSafe(RootLayoutClass.Get()), *GetNameSafe(HubHUDClass.Get()));

	if (!UIManager)
	{
		UE_LOG(LogXRU1UI, Error,
			TEXT("[Hub] UGameUIManagerSubsystem недоступен — HUD хаба показать негде"));
	}
	else
	{
		UIManager->CreateLayout(this, RootLayoutClass);
		UPrimaryGameLayout* RootLayout = UIManager->GetRootLayout();
		if (!RootLayout)
		{
			UE_LOG(LogXRU1UI, Error, TEXT("[Hub] корневой лейаут не создан — HUD хаба не будет"));
		}
		else if (!HubHUDClass)
		{
			UE_LOG(LogXRU1UI, Error,
				TEXT("[Hub] HubHUDClass не назначен в Class Defaults контроллера — HUD хаба не будет"));
		}
		else
		{
			HubHUD = Cast<UHubHUDWidget>(RootLayout->PushWidgetToLayer(EUILayer::Game, HubHUDClass));
			UE_LOG(LogXRU1UI, Display, TEXT("[Hub] HUD '%s' на слое Game: %s"),
				*GetNameSafe(HubHUDClass.Get()),
				HubHUD ? TEXT("создан") : TEXT("НЕ создан (пустой стек слоя Game в лейауте?)"));
		}
	}

	// Мир нужен для трейса по маркерам, UI — для кнопок HUD.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	// Стартовое состояние HUD: выбора ещё нет.
	SelectPOI(nullptr);
}

void AHubPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}

	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AHubPlayerController::HandleRotatePressed);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AHubPlayerController::HandleRotateReleased);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AHubPlayerController::HandleSelectPressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_DoubleClick, this, &AHubPlayerController::HandleSelectDoubleClick);
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AHubPlayerController::HandleZoomIn);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AHubPlayerController::HandleZoomOut);
}

AHologramMapActor* AHubPlayerController::GetHologramMap() const
{
	return HologramMap;
}

AHubCameraPawn* AHubPlayerController::GetHubCamera() const
{
	return Cast<AHubCameraPawn>(GetPawn());
}

bool AHubPlayerController::IsInputBlockedByPause() const
{
	// PlayerController тикает и получает ввод даже на паузе, поэтому проверка
	// обязана быть явной: иначе карта крутится за открытым меню настроек.
	const UGameInstance* GameInstance = GetGameInstance();
	UGamePauseSubsystem* Pause = GameInstance
		? GameInstance->GetSubsystem<UGamePauseSubsystem>() : nullptr;
	const bool bBlocked = Pause && Pause->IsPaused();

	// Логируем только смену состояния: «хаб не слушается мыши» иначе выглядит
	// как поломка управления, хотя это просто незакрытая пауза.
	if (bBlocked != bLoggedInputBlocked)
	{
		bLoggedInputBlocked = bBlocked;
		UE_LOG(LogXRU1UI, Display, TEXT("[Hub] ввод %s | причины паузы: %s"),
			bBlocked ? TEXT("ЗАБЛОКИРОВАН") : TEXT("разрешён"),
			Pause ? *Pause->DescribeReasons() : TEXT("<нет подсистемы>"));
	}
	return bBlocked;
}

void AHubPlayerController::HandleRotatePressed()
{
	if (IsInputBlockedByPause())
	{
		return;
	}
	bRotating = true;
}

void AHubPlayerController::HandleRotateReleased()
{
	bRotating = false;
}

void AHubPlayerController::HandleZoomIn()
{
	if (IsInputBlockedByPause())
	{
		return;
	}
	if (AHubCameraPawn* CameraPawn = GetHubCamera())
	{
		CameraPawn->AddZoomStep(1.f);
	}
}

void AHubPlayerController::HandleZoomOut()
{
	if (IsInputBlockedByPause())
	{
		return;
	}
	if (AHubCameraPawn* CameraPawn = GetHubCamera())
	{
		CameraPawn->AddZoomStep(-1.f);
	}
}

AMissionPointOfInterest* AHubPlayerController::TracePOIUnderCursor() const
{
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex=*/false, Hit))
	{
		return nullptr;
	}
	// Маркер может быть собран из нескольких компонентов — адресуем владельца.
	return Cast<AMissionPointOfInterest>(Hit.GetActor());
}

void AHubPlayerController::HandleSelectPressed()
{
	if (IsInputBlockedByPause())
	{
		return;
	}
	// Вращение и выбор разведены по кнопкам, поэтому клик здесь всегда именно клик.
	if (AMissionPointOfInterest* POI = TracePOIUnderCursor())
	{
		SelectPOI(POI);
	}
}

void AHubPlayerController::HandleSelectDoubleClick()
{
	if (IsInputBlockedByPause())
	{
		return;
	}
	if (AMissionPointOfInterest* POI = TracePOIUnderCursor())
	{
		SelectPOI(POI);
		LaunchSelectedPOI();
	}
}

void AHubPlayerController::SelectPOI(AMissionPointOfInterest* POI)
{
	const bool bChanged = SelectedPOI != POI;

	// Выбранной может быть только одна точка: снимаем отметку с прежней, иначе
	// красными останутся обе.
	if (bChanged && SelectedPOI)
	{
		SelectedPOI->SetSelected(false);
	}
	SelectedPOI = POI;
	if (POI)
	{
		POI->SetSelected(true);
	}

	if (bChanged && POI)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UTacticsAudioSubsystem* Audio = GameInstance->GetSubsystem<UTacticsAudioSubsystem>())
				{
					Audio->PlayUIClick();
				}
			}
		}
	}

	if (HubHUD)
	{
		HubHUD->SetSelectedPOI(POI);
	}
	OnPOISelected.Broadcast(POI);
}

void AHubPlayerController::LaunchSelectedPOI()
{
	if (!SelectedPOI)
	{
		return;
	}

	// Брифинг — обязательный шаг между картой и боем, но только для доступной
	// точки: у заблокированной должен сработать штатный отказ POI (звук и хук
	// OnSelectionDenied), а не пустой экран с описанием недоступной миссии.
	if (BriefingScreenClass && !SelectedPOI->IsLocked())
	{
		const UGameInstance* GameInstance = GetGameInstance();
		UGameUIManagerSubsystem* UIManager = GameInstance
			? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
		UPrimaryGameLayout* RootLayout = UIManager ? UIManager->GetRootLayout() : nullptr;
		if (RootLayout)
		{
			UMissionBriefingWidget* Briefing = Cast<UMissionBriefingWidget>(
				RootLayout->PushWidgetToLayer(EUILayer::Menu, BriefingScreenClass));
			if (Briefing)
			{
				Briefing->SetupFromPOI(SelectedPOI);
				UE_LOG(LogXRU1UI, Display, TEXT("[Hub] открыт брифинг точки '%s'"),
					*SelectedPOI->GetActorNameOrLabel());
				return;
			}
		}
		UE_LOG(LogXRU1UI, Warning,
			TEXT("[Hub] брифинг '%s' показать не удалось — запускаю миссию напрямую"),
			*GetNameSafe(BriefingScreenClass.Get()));
	}

	// Гейт и сам запуск сценария живут в AMissionPointOfInterest: контроллер
	// только передаёт намерение игрока (BRIEF_HubHologram §6).
	SelectedPOI->SelectPointOfInterest();
}

void AHubPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HologramMap || IsInputBlockedByPause())
	{
		return;
	}

	if (bRotating)
	{
		float MouseX = 0.f;
		float MouseY = 0.f;
		GetInputMouseDelta(MouseX, MouseY);
		HologramMap->AddYawInput(-MouseX * MouseYawSensitivity);
		if (AHubCameraPawn* CameraPawn = GetHubCamera())
		{
			// Тянем вниз — смотрим сверху: инверсия привычна по картам стратегий.
			CameraPawn->AddPitchInput(MouseY * MousePitchSensitivity);
		}
	}

	// Q/E — тот же поворот карты без мыши.
	float KeyboardYaw = 0.f;
	if (IsInputKeyDown(EKeys::Q))
	{
		KeyboardYaw -= 1.f;
	}
	if (IsInputKeyDown(EKeys::E))
	{
		KeyboardYaw += 1.f;
	}
	if (!FMath::IsNearlyZero(KeyboardYaw))
	{
		HologramMap->AddYawInput(KeyboardYaw * KeyboardYawSpeed * DeltaSeconds);
	}
}
