#include "AnimNotify_UnitFootstep.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TacticsAudioSubsystem.h"
#include "UnitAudioDataAsset.h"
#include "UnitBase.h"

UAnimNotify_UnitFootstep::UAnimNotify_UnitFootstep()
{
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif
}

void UAnimNotify_UnitFootstep::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	AUnitBase* Unit = MeshComp ? Cast<AUnitBase>(MeshComp->GetOwner()) : nullptr;
	const UUnitAudioDataAsset* AudioProfile = Unit ? Unit->GetAudioProfile() : nullptr;
	UWorld* World = Unit ? Unit->GetWorld() : nullptr;
	if (!AudioProfile || !World)
	{
		return;
	}

	// Мёртвый/эвакуированный юнит шагов не издаёт: montage смерти может дойти до
	// notify уже после terminal-состояния.
	if (Unit->IsDead() || Unit->IsEvacuated())
	{
		return;
	}

	const FVector FootLocation = MeshComp->DoesSocketExist(FootSocket)
		? MeshComp->GetSocketLocation(FootSocket)
		: Unit->GetActorLocation();

	// Трейс делаем ТОЛЬКО если профиль реально различает поверхности. При «одном
	// звуке шага везде» (наш текущий случай) луч под каждую ногу — чистые траты.
	EPhysicalSurface Surface = SurfaceType_Default;
	if (AudioProfile->UsesSurfaceFootsteps())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(XRU1Footstep), /*bTraceComplex=*/true, Unit);
		// bReturnPhysicalMaterial — единственная причина этого трейса: без него мы
		// не узнаем, бетон под ногой или трава.
		Params.bReturnPhysicalMaterial = true;
		if (World->LineTraceSingleByChannel(Hit, FootLocation,
			FootLocation - FVector(0.f, 0.f, TraceDistance), ECC_Visibility, Params))
		{
			Surface = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
		}
	}

	if (UTacticsAudioSubsystem* Audio = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
	{
		Audio->PlayCueAtLocation(AudioProfile->FindFootstep(Surface), FootLocation,
			AudioProfile->ResolveAttenuation());
	}
}

#if WITH_EDITOR
FString UAnimNotify_UnitFootstep::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Шаг (%s)"), *FootSocket.ToString());
}
#endif
