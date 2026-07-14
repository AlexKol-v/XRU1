#include "TacticalCameraPawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

ATacticalCameraPawn::ATacticalCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Root);
	SpringArm->TargetArmLength = TargetZoom;
	SpringArm->SetRelativeRotation(FRotator(-55.f, TargetYaw, 0.f));
	SpringArm->bDoCollisionTest = false; // камера не должна «прилипать» к стенам укрытий

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
}

void ATacticalCameraPawn::AddPanInput(const FVector2D& Input)
{
	if (Input.IsNearlyZero())
	{
		return;
	}

	// Направления берём от текущего yaw камеры, движение — в плоскости земли.
	const FRotator YawRot(0.f, SpringArm->GetRelativeRotation().Yaw, 0.f);
	const FVector Forward = YawRot.RotateVector(FVector::ForwardVector);
	const FVector Right = YawRot.RotateVector(FVector::RightVector);

	const float Delta = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	AddActorWorldOffset((Forward * Input.Y + Right * Input.X) * PanSpeed * Delta);
}

void ATacticalCameraPawn::AddRotationStep(float Direction)
{
	if (!FMath::IsNearlyZero(Direction))
	{
		TargetYaw += RotationStep * FMath::Sign(Direction);
	}
}

void ATacticalCameraPawn::AddZoomInput(float Input)
{
	TargetZoom = FMath::Clamp(TargetZoom - Input * ZoomStep, MinZoom, MaxZoom);
}

void ATacticalCameraPawn::FocusOnActor(const AActor* Target)
{
	if (Target)
	{
		SetActorLocation(FVector(Target->GetActorLocation().X, Target->GetActorLocation().Y, GetActorLocation().Z));
	}
}

void ATacticalCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Плавная доводка поворота и зума к целевым значениям.
	FRotator Rot = SpringArm->GetRelativeRotation();
	Rot.Yaw = FMath::FInterpTo(Rot.Yaw, TargetYaw, DeltaSeconds, InterpSpeed);
	SpringArm->SetRelativeRotation(Rot);

	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetZoom, DeltaSeconds, InterpSpeed);
}
