#include "InteractionDetectorComponent.h"

#include "Interactable.h"
#include "GameFramework/Actor.h"

UInteractionDetectorComponent::UInteractionDetectorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    // ВАЖНО: прямое присваивание, а не SetSphereRadius().
    // SetSphereRadius() уходит в USphereComponent::UpdateBodySetup() →
    // CreateShapeBodySetupIfNeeded() → NewObject<UBodySetup>(). Этот
    // NewObject вызывается внутри конструктора UObject (CDO-цепочка
    // APlayerCharacter → CreateDefaultSubobject → этот ctor) — и ловит
    // FObjectInitializer::AssertIfInConstructor. BodySetup всё равно будет
    // создан лениво в OnRegister, а реальную синхронизацию с BP-override
    // делает SetSphereRadius уже из BeginPlay (см. ниже).
    SphereRadius = DetectionRadius;

    SetCollisionProfileName(TEXT("OverlapAllDynamic"));
    SetGenerateOverlapEvents(true);
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void UInteractionDetectorComponent::BeginPlay()
{
    Super::BeginPlay();

    SetSphereRadius(DetectionRadius);
    OnComponentBeginOverlap.AddDynamic(this, &UInteractionDetectorComponent::HandleOverlapBegin);
    OnComponentEndOverlap  .AddDynamic(this, &UInteractionDetectorComponent::HandleOverlapEnd);
}

void UInteractionDetectorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                  FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (RefreshInterval <= KINDA_SMALL_NUMBER) return;

    TimeSinceRefresh += DeltaTime;
    if (TimeSinceRefresh >= RefreshInterval)
    {
        TimeSinceRefresh = 0.f;
        RecomputeFocus();
    }
}

void UInteractionDetectorComponent::HandleOverlapBegin(
    UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
    UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
    bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
    if (!OtherActor || !OtherActor->Implements<UInteractable>()) return;

    Candidates.AddUnique(OtherActor);
    RecomputeFocus();
}

void UInteractionDetectorComponent::HandleOverlapEnd(
    UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
    UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
    if (!OtherActor) return;

    Candidates.RemoveAll([OtherActor](const TWeakObjectPtr<AActor>& W)
    {
        return !W.IsValid() || W.Get() == OtherActor;
    });
    RecomputeFocus();
}

void UInteractionDetectorComponent::RecomputeFocus()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    AActor* Best = nullptr;
    float   BestSqr = TNumericLimits<float>::Max();
    const FVector OwnerLoc = Owner->GetActorLocation();

    for (int32 i = Candidates.Num() - 1; i >= 0; --i)
    {
        AActor* Cand = Candidates[i].Get();
        if (!Cand)
        {
            Candidates.RemoveAt(i);
            continue;
        }
        if (!IInteractable::Execute_CanInteract(Cand, Owner))
        {
            continue;
        }

        const float SqrDist = FVector::DistSquared(OwnerLoc, Cand->GetActorLocation());
        if (SqrDist < BestSqr)
        {
            BestSqr = SqrDist;
            Best    = Cand;
        }
    }

    AActor* Prev = FocusedActor.Get();
    if (Prev == Best) return;

    if (Prev)
    {
        IInteractable::Execute_OnUnfocused(Prev, Owner);
    }
    if (Best)
    {
        IInteractable::Execute_OnFocused(Best, Owner);
    }

    FocusedActor = Best;
    const FText Prompt = Best ? IInteractable::Execute_GetInteractionPrompt(Best) : FText::GetEmpty();
    OnFocusChanged.Broadcast(Best, Prompt);
}

bool UInteractionDetectorComponent::TryInteract()
{
    AActor* Target = FocusedActor.Get();
    if (!Target) return false;

    AActor* Owner = GetOwner();
    if (!IInteractable::Execute_CanInteract(Target, Owner))
    {
        return false;
    }

    IInteractable::Execute_Interact(Target, Owner);
    // После Interact объект мог удалиться (Pickup). Пересчёт обнулит фокус.
    RecomputeFocus();
    return true;
}
