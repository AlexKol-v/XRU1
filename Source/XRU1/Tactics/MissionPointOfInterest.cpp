#include "MissionPointOfInterest.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"

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

void AMissionPointOfInterest::SelectPointOfInterest()
{
	// TODO(next phase): открыть брифинг и загрузить LevelToLoad через UGameplayStatics::OpenLevelBySoftObjectPtr,
	// обновив прогресс кампании (UTacticsSaveGame / UTacticsGameInstance).
}
