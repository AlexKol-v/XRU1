#include "HubHUDWidget.h"

#include "HubPlayerController.h"
#include "MissionPointOfInterest.h"
#include "TacticsGameInstance.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UHubHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (Btn_Start)    { Btn_Start->OnClicked.AddUniqueDynamic(this, &UHubHUDWidget::HandleStartClicked);       RegisterButtonSounds(Btn_Start); }
	if (Btn_Settings) { Btn_Settings->OnClicked.AddUniqueDynamic(this, &UHubHUDWidget::HandleSettingsClicked); RegisterButtonSounds(Btn_Settings); }
	if (Btn_ToMenu)   { Btn_ToMenu->OnClicked.AddUniqueDynamic(this, &UHubHUDWidget::HandleToMenuClicked);     RegisterButtonSounds(Btn_ToMenu); }

	RefreshFromSelection();
}

void UHubHUDWidget::SetSelectedPOI(AMissionPointOfInterest* POI)
{
	SelectedPOI = POI;
	RefreshFromSelection();
}

void UHubHUDWidget::RefreshFromSelection()
{
	// Требования перечитываются каждый раз: миссия могла разблокироваться после
	// победы, а виджет пережил возвращение из боя.
	const FText LockedReason = SelectedPOI ? SelectedPOI->GetLockedReason() : FText::GetEmpty();
	const bool bLocked = !LockedReason.IsEmpty();

	if (Txt_POITitle)
	{
		Txt_POITitle->SetText(SelectedPOI
			? SelectedPOI->GetDisplayTitle()
			: NSLOCTEXT("XRU1.Hub", "NoSelection", "ВЫБЕРИТЕ ТОЧКУ НА КАРТЕ"));
	}
	if (Txt_POIDescription)
	{
		Txt_POIDescription->SetText(SelectedPOI
			? SelectedPOI->GetDisplayDescription()
			: NSLOCTEXT("XRU1.Hub", "NoSelectionHint",
				"ПКМ — вращение карты, колесо — приближение, ЛКМ — выбор точки."));
	}
	if (Txt_Status)
	{
		FText Status;
		if (!SelectedPOI)
		{
			Status = FText::GetEmpty();
		}
		else if (bLocked)
		{
			// Конкретная причина от миссии, а не общая фраза «требуется обучение».
			Status = LockedReason;
		}
		else
		{
			Status = NSLOCTEXT("XRU1.Hub", "ReadyStatus", "Отряд готов к переброске");
		}
		Txt_Status->SetText(Status);
	}
	if (Btn_Start)
	{
		Btn_Start->SetIsEnabled(SelectedPOI != nullptr && !bLocked);
	}

	OnSelectedPOIChanged(SelectedPOI, bLocked);
}

void UHubHUDWidget::HandleStartClicked()
{
	if (AHubPlayerController* HubController = GetOwningPlayer<AHubPlayerController>())
	{
		HubController->LaunchSelectedPOI();
	}
	else if (SelectedPOI)
	{
		SelectedPOI->SelectPointOfInterest();
	}
}

void UHubHUDWidget::HandleSettingsClicked()
{
	PushScreen(SettingsScreenClass);
}

void UHubHUDWidget::HandleToMenuClicked()
{
	if (UTacticsGameInstance* GameInstance = GetTacticsGameInstance())
	{
		GameInstance->TravelToMainMenu();
	}
}
