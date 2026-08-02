#include "ShotTracerActor.h"

#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

AShotTracerActor::AShotTracerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Effect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
	Effect->SetupAttachment(RootComponent);
	Effect->bAutoActivate = false;
}

float AShotTracerActor::Launch(const UObject* WorldContext, UNiagaraSystem* System,
	const FVector& Start, const FVector& End, float Speed)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const float Distance = FVector::Dist(Start, End);
	if (!World || !System || Distance < 1.f || Speed <= 1.f)
	{
		return 0.f;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShotTracerActor* Tracer = World->SpawnActor<AShotTracerActor>(
		AShotTracerActor::StaticClass(), Start, (End - Start).Rotation(), Params);
	if (!Tracer)
	{
		return 0.f;
	}

	Tracer->TargetLocation = End;
	Tracer->FlightSpeed = Speed;
	Tracer->LifeLeft = FMath::Min(3.f, Distance / Speed + 0.5f);
	if (Tracer->Effect)
	{
		Tracer->Effect->SetAsset(System);
		Tracer->Effect->Activate(true);
	}
	return Distance / Speed;
}

void AShotTracerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	LifeLeft -= DeltaSeconds;
	const FVector Current = GetActorLocation();
	const FVector ToTarget = TargetLocation - Current;
	const float Step = FlightSpeed * DeltaSeconds;

	if (LifeLeft <= 0.f || ToTarget.SizeSquared() <= FMath::Square(Step))
	{
		SetActorLocation(TargetLocation);
		// Компонент отцеплять не нужно: у шлейфа короткий срок жизни, и он
		// доигрывает вместе с актором.
		Destroy();
		return;
	}
	SetActorLocation(Current + ToTarget.GetSafeNormal() * Step);
}
