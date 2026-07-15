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
		// Нормализуем сразу, иначе TargetYaw копится без ограничений (см. Tick).
		TargetYaw = FRotator::NormalizeAxis(TargetYaw + RotationStep * FMath::Sign(Direction));
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

	// Плавная доводка поворота к целевому значению по КРАТЧАЙШЕЙ дуге:
	// GetRelativeRotation() всегда возвращает Yaw, нормализованный в (-180,180],
	// поэтому обычный FInterpTo(Rot.Yaw, TargetYaw, ...) при накопленном TargetYaw
	// интерполировал «в лоб» по числам и после нескольких поворотов заставлял
	// камеру докручиваться на полный круг, чтобы догнать ушедшее далеко значение.
	FRotator Rot = SpringArm->GetRelativeRotation();
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(Rot.Yaw, TargetYaw);
	Rot.Yaw += FMath::FInterpTo(0.f, DeltaYaw, DeltaSeconds, InterpSpeed);
	SpringArm->SetRelativeRotation(Rot);

	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetZoom, DeltaSeconds, InterpSpeed);
}
