#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_UnitFootstep.generated.h"

/**
 * Шаг бойца: звук выбирается по РЕАЛЬНОЙ поверхности под ногой, а не по одному
 * общему WAV. Трейс вниз от сокета стопы в момент касания — стандартный способ:
 * анимация знает КОГДА, а физматериал знает ПО ЧЕМУ.
 *
 * Notify не знает про конкретные звуки: он спрашивает профиль
 * `UUnitAudioDataAsset` у юнита, поэтому четыре класса звучат по-разному без
 * единой правки анимаций.
 */
UCLASS(meta = (DisplayName = "XRU1 Footstep"))
class XRU1_API UAnimNotify_UnitFootstep : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_UnitFootstep();

	/** Сокет стопы, от которого идёт трейс (обычно foot_l / foot_r). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep")
	FName FootSocket = TEXT("foot_l");

	/** Длина трейса вниз от сокета, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep", meta = (ClampMin = "5"))
	float TraceDistance = 60.f;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif
};
