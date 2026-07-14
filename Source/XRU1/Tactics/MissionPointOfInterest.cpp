#include "MissionPointOfInterest.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
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
	PopupWidget->SetVisibility(bHovered);
	OnHoverChanged.Broadcast(bHovered);
}

bool AMissionPointOfInterest::IsLocked() const
{
	if (RequiredCompletedMission == NAME_None)
	{
		return false;
	}
	if (const UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>())
	{
		if (GI->CurrentSave)
		{
			return !GI->CurrentSave->IsMissionCompleted(RequiredCompletedMission);
		}
	}
	// Кампании нет (прямой запуск хаба в PIE) — не блокируем.
	return false;
}

void AMissionPointOfInterest::SelectPointOfInterest()
{
	// Гейт: миссия недоступна, пока не пройдено требуемое (обучение).
	if (IsLocked())
	{
		OnSelectionDenied();
		return;
	}

	if (LevelToLoad.IsNull())
	{
		return;
	}

	// Запоминаем в кампании, из какой точки хаба ушли (для «Продолжить»).
	if (UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>())
	{
		if (GI->CurrentSave)
		{
			GI->CurrentSave->LastHubPointOfInterest = MissionId;
			GI->SaveCampaign();
		}
	}

	UGameplayStatics::OpenLevelBySoftObjectPtr(this, LevelToLoad);
}
