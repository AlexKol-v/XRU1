#include "HubCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

AHubCameraPawn::AHubCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Root);
	SpringArm->TargetArmLength = DefaultArmLength;
	// Камера не должна «прыгать» при столкновении boom с геометрией зала и не
	// подчиняется повороту контроллера: наклоном управляет только наш ввод.
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
}

void AHubCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	ResetView();
}

void AHubCameraPawn::ResetView()
{
	DesiredArmLength = FMath::Clamp(DefaultArmLength, MinArmLength, MaxArmLength);
	if (SpringArm)
	{
		SpringArm->TargetArmLength = DesiredArmLength;
		SpringArm->SetRelativeRotation(FRotator(FMath::Clamp(DefaultPitch, MinPitch, MaxPitch), 0.f, 0.f));
	}
}

void AHubCameraPawn::AddPitchInput(float DeltaDegrees)
{
	if (!SpringArm || FMath::IsNearlyZero(DeltaDegrees))
	{
		return;
	}
	FRotator Rotation = SpringArm->GetRelativeRotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + DeltaDegrees, MinPitch, MaxPitch);
	SpringArm->SetRelativeRotation(Rotation);
}

void AHubCameraPawn::AddZoomStep(float Steps)
{
	if (FMath::IsNearlyZero(Steps))
	{
		return;
	}
	// Steps > 0 — приближение, значит длина boom уменьшается.
	DesiredArmLength = FMath::Clamp(DesiredArmLength - Steps * ZoomStep, MinArmLength, MaxArmLength);
}

void AHubCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (SpringArm && !FMath::IsNearlyEqual(SpringArm->TargetArmLength, DesiredArmLength, 0.5f))
	{
		SpringArm->TargetArmLength = FMath::FInterpTo(
			SpringArm->TargetArmLength, DesiredArmLength, DeltaSeconds, ZoomInterpSpeed);
	}
}
