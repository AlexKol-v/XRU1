#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMontage.h"
#include "CoverTypes.h"

class AActor;
class UGameplayEffect;

/**
 * Минимальная фаза транзакции выстрела. Контекст живёт внутри инстанса GA:
 * активация только резервирует действие, а механика применяется на FireCommit.
 */
enum class ETacticalFireActionPhase : uint8
{
	Inactive,
	AwaitingFireCommit,
	AwaitingPresentationCompletion
};

/**
 * Неизменяемая часть одного обычного или реакционного выстрела плюс guards.
 * Контекст замораживает и механику, и presentation/cover-anchor: latent callback не
 * пересчитывает цель, край стены или стойку посреди того же ActionId.
 */
struct FTacticalFireActionContext
{
	FGuid ActionId;
	TWeakObjectPtr<AActor> Shooter;
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<UAnimMontage> FireMontage;
	FVector FiringEyeLocation = FVector::ZeroVector;
	FVector HomeRootLocation = FVector::ZeroVector;
	FVector PresentationRootLocation = FVector::ZeroVector;
	FVector CoverAnchor = FVector::ZeroVector;
	FVector CoverNormal = FVector::ZeroVector;
	float ResolvedHitChance = 0.f;
	float Damage = 0.f;
	float MaxRange = 0.f;
	TSubclassOf<UGameplayEffect> DamageEffectClass;
	int64 CoverWallId = 0;
	int32 CoverRevision = 0;
	/** Instance конкретного запуска montage; привязывается первым валидным branching-point notify. */
	int32 MontageInstanceId = INDEX_NONE;
	int32 ActionPointsBefore = INDEX_NONE;
	EFiringStance FiringStance = EFiringStance::Open;
	ETacticalFireActionPhase Phase = ETacticalFireActionPhase::Inactive;
	bool bUsedSquadsight = false;
	bool bCostCommitted = false;
	bool bShotCommitted = false;
	bool bLastShotHit = false;

	void Begin(AActor* InShooter, AActor* InTarget, const FVector& InFiringEyeLocation,
		float InResolvedHitChance, float InDamage, float InMaxRange,
		TSubclassOf<UGameplayEffect> InDamageEffectClass, int32 InActionPointsBefore = INDEX_NONE)
	{
		ActionId = FGuid::NewGuid();
		Shooter = InShooter;
		Target = InTarget;
		FiringEyeLocation = InFiringEyeLocation;
		ResolvedHitChance = FMath::Clamp(InResolvedHitChance, 0.f, 100.f);
		Damage = InDamage;
		MaxRange = InMaxRange;
		DamageEffectClass = InDamageEffectClass;
		ActionPointsBefore = InActionPointsBefore;
		Phase = ETacticalFireActionPhase::AwaitingFireCommit;
		bCostCommitted = false;
		bShotCommitted = false;
		bLastShotHit = false;
	}

	/**
	 * Неизменяемый presentation-срез строится один раз до оплаты AP. BP не
	 * пересчитывает стойку/край стены после latent callback и потому не может
	 * внезапно выбрать другой угол укрытия посреди того же ActionId.
	 */
	void SetPresentation(UAnimMontage* InFireMontage, EFiringStance InFiringStance,
		const FVector& InHomeRootLocation, const FVector& InPresentationRootLocation,
		const FVector& InCoverAnchor, const FVector& InCoverNormal,
		int64 InCoverWallId, int32 InCoverRevision, bool bInUsedSquadsight)
	{
		FireMontage = InFireMontage;
		FiringStance = InFiringStance;
		HomeRootLocation = InHomeRootLocation;
		PresentationRootLocation = InPresentationRootLocation;
		CoverAnchor = InCoverAnchor;
		CoverNormal = InCoverNormal;
		CoverWallId = InCoverWallId;
		CoverRevision = InCoverRevision;
		bUsedSquadsight = bInUsedSquadsight;
	}

	bool IsActive() const
	{
		return Phase != ETacticalFireActionPhase::Inactive && ActionId.IsValid();
	}

	bool Matches(const FGuid& InActionId) const
	{
		return IsActive() && InActionId.IsValid() && ActionId == InActionId;
	}

	bool CanCommit(const FGuid& InActionId) const
	{
		return Matches(InActionId)
			&& Phase == ETacticalFireActionPhase::AwaitingFireCommit
			&& !bShotCommitted;
	}

	bool TryBindMontageInstance(int32 InMontageInstanceId)
	{
		if (!IsActive() || InMontageInstanceId == INDEX_NONE)
		{
			return false;
		}
		if (MontageInstanceId == INDEX_NONE)
		{
			MontageInstanceId = InMontageInstanceId;
		}
		return MontageInstanceId == InMontageInstanceId;
	}

	void MarkCommitStarted()
	{
		bShotCommitted = true;
		Phase = ETacticalFireActionPhase::AwaitingPresentationCompletion;
	}

	void SetCommitResult(bool bHit)
	{
		bLastShotHit = bHit;
	}

	void Reset()
	{
		*this = FTacticalFireActionContext();
	}
};
