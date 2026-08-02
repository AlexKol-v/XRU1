#include "MissionPointOfInterest.h"
#include "TacticalScenarioDataAsset.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
#include "POIPopupWidget.h"
#include "Kismet/GameplayStatics.h"

AMissionPointOfInterest::AMissionPointOfInterest()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Root);

	HoverBounds = CreateDefaultSubobject<USphereComponent>(TEXT("HoverBounds"));
	HoverBounds->SetupAttachment(Root);
	HoverBounds->SetSphereRadius(150.f);
	HoverBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HoverBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	HoverBounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	PopupWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("PopupWidget"));
	PopupWidget->SetupAttachment(Root);
	PopupWidget->SetWidgetSpace(EWidgetSpace::Screen);
	PopupWidget->SetDrawAtDesiredSize(true);
	PopupWidget->SetVisibility(false);
	// Попап ОБЯЗАН быть прозрачным для курсора. Иначе он перекрывает HoverBounds,
	// приходит OnEndCursorOver, попап гаснет, курсор снова на маркере — и точка
	// мигает с частотой кадров (классическая петля WidgetComponent в UE).
	PopupWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PopupWidget->SetGenerateOverlapEvents(false);
	// Screen-space виджет не должен ни ловить ввод, ни забирать фокус окна:
	// сам виджет дополнительно ставит себе HitTestInvisible (UPOIPopupWidget).
	PopupWidget->SetWindowFocusable(false);
}

void AMissionPointOfInterest::BeginPlay()
{
	Super::BeginPlay();

	if (PopupWidgetClass)
	{
		PopupWidget->SetWidgetClass(PopupWidgetClass);
	}

	HoverBounds->OnBeginCursorOver.AddDynamic(this, &AMissionPointOfInterest::HandleBeginCursorOver);
	HoverBounds->OnEndCursorOver.AddDynamic(this, &AMissionPointOfInterest::HandleEndCursorOver);
}

void AMissionPointOfInterest::HandleBeginCursorOver(UPrimitiveComponent* /*TouchedComponent*/)
{
	SetHovered(true);
}

void AMissionPointOfInterest::HandleEndCursorOver(UPrimitiveComponent* /*TouchedComponent*/)
{
	SetHovered(false);
}

void AMissionPointOfInterest::SetHovered(bool bHovered)
{
	if (bIsHovered == bHovered)
	{
		return;
	}
	bIsHovered = bHovered;

	// Данные попапа обновляются при каждом показе: гейт RequiredCompletedMission
	// мог открыться после возвращения из пройденного туториала.
	if (bHovered)
	{
		if (UPOIPopupWidget* Popup = Cast<UPOIPopupWidget>(PopupWidget->GetUserWidgetObject()))
		{
			Popup->SetupFromPOI(GetDisplayTitle(), GetDisplayDescription(), GetLockedReason());
		}
	}

	PopupWidget->SetVisibility(bHovered);
	OnVisualStateChanged();
	OnHoverChanged.Broadcast(bHovered);
}

void AMissionPointOfInterest::SetSelected(bool bInSelected)
{
	if (bIsSelected == bInSelected)
	{
		return;
	}
	bIsSelected = bInSelected;
	OnVisualStateChanged();
}

bool AMissionPointOfInterest::IsCompleted() const
{
	const UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>();
	const UTacticsSaveGame* Save = GI ? GI->CurrentSave : nullptr;
	if (!Save)
	{
		return false;
	}
	// Идентификатор миссии принадлежит сценарию; MissionId остаётся legacy-путём
	// для точек, у которых сценарий не назначен.
	const FName Id = Scenario && !Scenario->ScenarioId.IsNone() ? Scenario->ScenarioId : MissionId;
	return !Id.IsNone() && Save->IsMissionCompleted(Id);
}

bool AMissionPointOfInterest::IsLocked() const
{
	const UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>();
	const UTacticsSaveGame* Save = GI ? GI->CurrentSave : nullptr;

	// Требования принадлежат миссии: точка на карте — только её представитель.
	if (Scenario)
	{
		return !Scenario->ArePrerequisitesMet(Save);
	}

	// Legacy-путь для точек без Scenario Data Asset.
	if (RequiredCompletedMission == NAME_None)
	{
		return false;
	}
	if (Save)
	{
		return !Save->IsMissionCompleted(RequiredCompletedMission);
	}
	return false;
}

FText AMissionPointOfInterest::GetLockedReason() const
{
	if (Scenario)
	{
		const UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>();
		return Scenario->GetLockedReason(GI ? GI->CurrentSave : nullptr);
	}
	if (!IsLocked())
	{
		return FText::GetEmpty();
	}
	return FText::Format(
		NSLOCTEXT("XRU1.POI", "LockedLegacy", "Недоступно: сначала пройдите «{0}»"),
		FText::FromName(RequiredCompletedMission));
}

FText AMissionPointOfInterest::GetDisplayTitle() const
{
	if (!Title.IsEmpty())
	{
		return Title;
	}
	return Scenario ? Scenario->GetDisplayNameSafe() : FText::GetEmpty();
}

FText AMissionPointOfInterest::GetDisplayDescription() const
{
	if (!Description.IsEmpty())
	{
		return Description;
	}
	return Scenario ? Scenario->BriefingText : FText::GetEmpty();
}

void AMissionPointOfInterest::SelectPointOfInterest()
{
	// Гейт: миссия недоступна, пока не пройдено требуемое (обучение).
	if (IsLocked())
	{
		OnSelectionDenied();
		return;
	}

	UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>();
	if (Scenario)
	{
		if (!GI || Scenario->ScenarioId.IsNone() || GI->SharedCombatLevel.IsNull())
		{
			OnSelectionDenied();
			return;
		}

		if (!GI->StartCombatScenario(Scenario))
		{
			OnSelectionDenied();
			return;
		}
		// OpenLevel уже принят, но GameInstance/Save ещё доступны в этом callstack.
		if (GI->CurrentSave)
		{
			GI->CurrentSave->LastHubPointOfInterest = Scenario->ScenarioId;
			GI->SaveCampaign();
		}
		return;
	}

	if (LevelToLoad.IsNull())
	{
		OnSelectionDenied();
		return;
	}

	// Legacy-путь для старых карт, ещё не переведённых на общую Scenario-схему.
	if (GI && GI->CurrentSave)
	{
		GI->CurrentSave->LastHubPointOfInterest = MissionId;
		GI->SaveCampaign();
	}

	UGameplayStatics::OpenLevelBySoftObjectPtr(this, LevelToLoad);
}
