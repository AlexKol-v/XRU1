#include "AnimNotify_FireCommit.h"
#include "UnitBase.h"
#include "GA_Attack.h"
#include "GA_Overwatch.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/ActiveMontageInstanceScope.h"

DEFINE_LOG_CATEGORY_STATIC(LogFireCommitNotify, Log, All);

namespace
{
	template <typename TAbility>
	TAbility* FindActiveAbilityInstance(AUnitBase* Unit, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		UAbilitySystemComponent* ASC = Unit ? Unit->GetAbilitySystemComponent() : nullptr;
		FGameplayAbilitySpec* Spec = ASC && AbilityClass
			? ASC->FindAbilitySpecFromClass(AbilityClass)
			: nullptr;
		if (!Spec || !Spec->IsActive())
		{
			return nullptr;
		}
		if (TAbility* Primary = Cast<TAbility>(Spec->GetPrimaryInstance()))
		{
			return Primary;
		}
		for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
		{
			if (TAbility* Ability = Cast<TAbility>(Instance))
			{
				return Ability;
			}
		}
		return nullptr;
	}
}

UAnimNotify_FireCommit::UAnimNotify_FireCommit()
{
	// Gameplay commit обязан исполняться синхронно внутри montage update, а не
	// через очередь обычных notify в конце кадра.
	bIsNativeBranchingPoint = true;
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif
}

void UAnimNotify_FireCommit::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	int32 MontageInstanceId = INDEX_NONE;
	if (const UE::Anim::FAnimNotifyMontageInstanceContext* Context =
		EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>())
	{
		MontageInstanceId = Context->MontageInstanceID;
	}
	CommitFromAnimation(MeshComp, Animation, MontageInstanceId);
}

void UAnimNotify_FireCommit::BranchingPointNotify(
	FBranchingPointNotifyPayload& BranchingPointPayload)
{
	CommitFromAnimation(BranchingPointPayload.SkelMeshComponent,
		BranchingPointPayload.SequenceAsset, BranchingPointPayload.MontageInstanceID);
}

void UAnimNotify_FireCommit::CommitFromAnimation(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, int32 MontageInstanceId) const
{

	AUnitBase* Unit = MeshComp ? Cast<AUnitBase>(MeshComp->GetOwner()) : nullptr;
	if (!Unit)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	FString RejectReason = TEXT("no active matching fire action");
	auto IsExpectedMontageInstance = [AnimInstance, Animation, MontageInstanceId, &RejectReason](
		UAnimMontage* ExpectedMontage)
	{
		if (!ExpectedMontage)
		{
			RejectReason = TEXT("expected montage is null");
			return false;
		}
		if (!AnimInstance)
		{
			RejectReason = TEXT("anim instance is null");
			return false;
		}
		// Gameplay-notify без instance id неоднозначен: queued notify старого запуска того же
		// montage asset не имеет права commit'нуть уже следующую атаку/цель.
		if (MontageInstanceId == INDEX_NONE)
		{
			RejectReason = TEXT("missing montage instance id");
			return false;
		}

		FAnimMontageInstance* Triggering = AnimInstance->GetMontageInstanceForID(MontageInstanceId);
		if (!Triggering)
		{
			RejectReason = FString::Printf(TEXT("montage instance %d not found"), MontageInstanceId);
			return false;
		}
		if (Triggering->Montage != ExpectedMontage)
		{
			RejectReason = FString::Printf(TEXT("montage mismatch expected=%s actual=%s payload=%s"),
				*GetNameSafe(ExpectedMontage), *GetNameSafe(Triggering->Montage), *GetNameSafe(Animation));
			return false;
		}
		return true;
	};

	bool bHit = false;
	if (UGA_Attack* Attack = FindActiveAbilityInstance<UGA_Attack>(Unit, Unit->AttackAbilityClass))
	{
		const FGuid ActionId = Attack->GetActiveFireActionId();
		EFiringStance Stance = EFiringStance::Open;
		FVector HomeRoot;
		FVector PresentationRoot;
		UAnimMontage* ExpectedMontage = Attack->GetFireActionPresentation(
			ActionId, Stance, HomeRoot, PresentationRoot);
		if (!ActionId.IsValid())
		{
			RejectReason = TEXT("attack action id is invalid");
		}
		else if (!IsExpectedMontageInstance(ExpectedMontage))
		{
		}
		else if (!Attack->AcceptFireCommitMontageInstance(ActionId, MontageInstanceId))
		{
			RejectReason = TEXT("attack montage instance was rejected by action guard");
		}
		else if (Attack->FireCommit(ActionId, bHit))
		{
			return;
		}
		else
		{
			RejectReason = TEXT("attack FireCommit returned false");
		}
	}

	if (UGA_Overwatch* Overwatch =
		FindActiveAbilityInstance<UGA_Overwatch>(Unit, Unit->OverwatchAbilityClass))
	{
		const FGuid ActionId = Overwatch->GetActiveReactionActionId();
		EFiringStance Stance = EFiringStance::Open;
		FVector HomeRoot;
		FVector PresentationRoot;
		UAnimMontage* ExpectedMontage = Overwatch->GetReactionActionPresentation(
			ActionId, Stance, HomeRoot, PresentationRoot);
		if (!ActionId.IsValid())
		{
			RejectReason = TEXT("reaction action id is invalid");
		}
		else if (!IsExpectedMontageInstance(ExpectedMontage))
		{
		}
		else if (!Overwatch->AcceptFireCommitMontageInstance(ActionId, MontageInstanceId))
		{
			RejectReason = TEXT("reaction montage instance was rejected by action guard");
		}
		else
		{
			Overwatch->FireCommit(ActionId, bHit);
			return;
		}
	}

	UE_LOG(LogFireCommitNotify, Warning,
		TEXT("[FireCommit] Отклонён notify: unit=%s animation=%s instance=%d"),
		*GetNameSafe(Unit), *GetNameSafe(Animation), MontageInstanceId);
	UE_LOG(LogFireCommitNotify, Warning,
		TEXT("[FireCommit] reject reason=%s"), *RejectReason);
}

FString UAnimNotify_FireCommit::GetNotifyName_Implementation() const
{
	return TEXT("XRU1 Fire Commit");
}
