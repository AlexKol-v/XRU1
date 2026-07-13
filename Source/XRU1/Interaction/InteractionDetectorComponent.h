#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "InteractionDetectorComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnInteractionFocusChanged, AActor*, NewFocus, FText, Prompt);

/**
 * Сферический детектор интерактивных объектов на стороне игрока. На BeginOverlap собирает
 * кандидатов, реализующих IInteractable; на каждом изменении состава или периодически
 * пересчитывает «фокус» — ближайшего по дистанции кандидата, который проходит CanInteract.
 * При смене фокуса вызывает OnFocused/OnUnfocused и шлёт OnFocusChanged для UI.
 */
UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent))
class XRU1_API UInteractionDetectorComponent : public USphereComponent
{
    GENERATED_BODY()

public:
    UInteractionDetectorComponent();

    UPROPERTY(EditDefaultsOnly, Category = "Interaction",
              meta = (ClampMin = "50.0", ClampMax = "1000.0"))
    float DetectionRadius = 220.f;

    /** Если 0 — пересчитываем только по overlap-событиям. >0 — Tick на этой частоте. */
    UPROPERTY(EditDefaultsOnly, Category = "Interaction",
              meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RefreshInterval = 0.1f;

    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteractionFocusChanged OnFocusChanged;

    /** Активирует фокус. Возвращает true, если что-то реально сработало. */
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    bool TryInteract();

    UFUNCTION(BlueprintPure, Category = "Interaction")
    AActor* GetFocusedActor() const { return FocusedActor.Get(); }

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                                FActorComponentTickFunction* ThisTickFunction) override;

private:
    UFUNCTION()
    void HandleOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                             UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                             bool bFromSweep, const FHitResult& SweepResult);
    UFUNCTION()
    void HandleOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                           UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    void RecomputeFocus();

    /** Кандидаты, пришедшие через OnComponentBeginOverlap. */
    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<AActor>> Candidates;

    /** Текущий выбранный объект. */
    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> FocusedActor;

    float TimeSinceRefresh = 0.f;
};
