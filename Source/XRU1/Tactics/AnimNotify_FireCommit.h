#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_FireCommit.generated.h"

/**
 * Единственный gameplay-момент выстрела в montage. Notify ставится на кадр
 * muzzle/recoil как Branching Point; stale/duplicate вызов отсекает ActionId GA.
 */
UCLASS(meta = (DisplayName = "XRU1 Fire Commit"))
class XRU1_API UAnimNotify_FireCommit : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_FireCommit();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void BranchingPointNotify(FBranchingPointNotifyPayload& BranchingPointPayload) override;

	virtual FString GetNotifyName_Implementation() const override;

private:
	void CommitFromAnimation(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation, int32 MontageInstanceId) const;
};
