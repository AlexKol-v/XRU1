#include "HologramMapActor.h"

#include "MissionPointOfInterest.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

AHologramMapActor::AHologramMapActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	RotationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RotationRoot"));
	RotationRoot->SetupAttachment(Root);

	TerrainMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TerrainMesh"));
	TerrainMesh->SetupAttachment(RotationRoot);
	// По рельефу не кликают: клики принадлежат маркерам, иначе трейс контроллера
	// всегда упирался бы в карту.
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(Root);
	GlowLight->SetRelativeLocation(FVector(0.f, 0.f, -60.f));
	GlowLight->SetLightColor(FLinearColor(1.f, 0.45f, 0.1f));
	GlowLight->SetIntensity(6000.f);
	GlowLight->SetAttenuationRadius(2000.f);
	GlowLight->SetCastShadows(false);
}

void AHologramMapActor::BeginPlay()
{
	Super::BeginPlay();
	ResetView();
}

void AHologramMapActor::AddYawInput(float DeltaDegrees)
{
	if (FMath::IsNearlyZero(DeltaDegrees))
	{
		return;
	}
	CurrentYaw = FRotator::NormalizeAxis(CurrentYaw + DeltaDegrees);
	LastInputTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ApplyRotation();
}

void AHologramMapActor::ResetView()
{
	CurrentYaw = FRotator::NormalizeAxis(DefaultYaw);
	LastInputTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ApplyRotation();
}

void AHologramMapActor::ApplyRotation()
{
	if (RotationRoot)
	{
		RotationRoot->SetRelativeRotation(FRotator(0.f, CurrentYaw, 0.f));
	}
}

void AHologramMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IdleSpinSpeed <= 0.f)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() - LastInputTime < IdleSpinDelay)
	{
		return; // игрок только что крутил карту — не спорим с его вводом
	}

	CurrentYaw = FRotator::NormalizeAxis(CurrentYaw + IdleSpinSpeed * DeltaSeconds);
	ApplyRotation();
}

TArray<AMissionPointOfInterest*> AHologramMapActor::GetPointsOfInterest() const
{
	TArray<AMissionPointOfInterest*> Result;
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
	for (AActor* Actor : Attached)
	{
		if (AMissionPointOfInterest* POI = Cast<AMissionPointOfInterest>(Actor))
		{
			Result.Add(POI);
		}
	}
	return Result;
}
