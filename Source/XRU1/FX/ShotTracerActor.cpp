#include "ShotTracerActor.h"

#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "UnitVfxDataAsset.h"

AShotTracerActor::AShotTracerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Effect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
	Effect->SetupAttachment(RootComponent);
	Effect->bAutoActivate = false;
}

float AShotTracerActor::Launch(const UObject* WorldContext, const UUnitVfxDataAsset* Profile,
	const FVector& Start, const FVector& End)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const float Distance = FVector::Dist(Start, End);
	if (!World || !Profile || !Profile->Tracer || Distance < 1.f || Profile->TracerSpeed <= 1.f)
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

	const float FlightTime = Distance / Profile->TracerSpeed;
	Tracer->TargetLocation = End;
	Tracer->FlightSpeed = Profile->TracerSpeed;
	Tracer->TrailLinger = FMath::Max(0.f, Profile->TracerTrailDuration);
	Tracer->LifeLeft = FMath::Min(3.f, FlightTime + 0.5f);
	if (Tracer->Effect)
	{
		Tracer->Effect->SetAsset(Profile->Tracer);
		// Параметры — строго ДО активации: систему интересует геометрия выстрела
		// в момент спавна, после активации она уже летит по своим дефолтам.
		Profile->ApplyTracerParameters(Tracer->Effect, Start, End);
		Tracer->Effect->Activate(true);
	}
	return FlightTime;
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
		// Прилетели: эмиссию гасим, а уже рождённым частицам даём дожить свой
		// шлейф. Тик больше не нужен — актор снимет себя сам по LifeSpan.
		if (Effect)
		{
			Effect->Deactivate();
		}
		SetActorTickEnabled(false);
		SetLifeSpan(FMath::Max(0.05f, TrailLinger));
		return;
	}
	SetActorLocation(Current + ToTarget.GetSafeNormal() * Step);
}
