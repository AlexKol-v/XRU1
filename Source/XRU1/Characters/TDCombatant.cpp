#include "TDCombatant.h"

#include "AbilitySystemComponent.h"
#include "Components/WidgetComponent.h"

#include "TDAttributeSet.h"
#include "UnitHUDLayoutData.h"
#include "UnitHUDWidget.h"

ATDCombatant::ATDCombatant()
{
    PrimaryActorTick.bCanEverTick = true;

    Attributes = CreateDefaultSubobject<UTDAttributeSet>(TEXT("Attributes"));

    HUDWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HUDWidgetComponent"));
    HUDWidgetComponent->SetupAttachment(RootComponent);
    HUDWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
    HUDWidgetComponent->SetDrawAtDesiredSize(false);
    HUDWidgetComponent->SetGenerateOverlapEvents(false);
    HUDWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HUDWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
}

void ATDCombatant::BeginPlay()
{
    Super::BeginPlay();
    SetupUnitHUD();
}

void ATDCombatant::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TickShieldDecay(DeltaSeconds);
}

void ATDCombatant::SetupUnitHUD()
{
    if (!HUDWidgetComponent || !HUDLayout || !HUDWidgetClass)
    {
        return;
    }

    HUDWidgetComponent->SetRelativeLocation(HUDLayout->RelativeLocation);
    HUDWidgetComponent->SetDrawSize(HUDLayout->DrawSize);
    HUDWidgetComponent->SetWidgetClass(HUDWidgetClass);

    // InitWidget гарантированно создаст инстанс класса, даже если компонент был сконструирован
    // до того, как SetWidgetClass смог его подтянуть.
    HUDWidgetComponent->InitWidget();

    if (UUnitHUDWidget* HUD = Cast<UUnitHUDWidget>(HUDWidgetComponent->GetUserWidgetObject()))
    {
        HUD->InitFromLayout(HUDLayout, GetAbilitySystemComponent());
    }
}

void ATDCombatant::TickShieldDecay(float DeltaSeconds)
{
    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    if (!ASC || !Attributes) return;

    const float Current = ASC->GetNumericAttribute(UTDAttributeSet::GetShieldAttribute());
    if (Current <= 0.f) return;

    const float New = FMath::Max(0.f, Current - DeltaSeconds);

    // SetNumericAttributeBase идёт через ASC и стреляет в AttributeValueChange-делегаты,
    // на которые подписаны HealthBar / ShieldIcon виджеты.
    ASC->SetNumericAttributeBase(UTDAttributeSet::GetShieldAttribute(), New);
}

float ATDCombatant::GetHealth()    const { return Attributes ? Attributes->GetHealth()    : 0.f; }
float ATDCombatant::GetMaxHealth() const { return Attributes ? Attributes->GetMaxHealth() : 0.f; }
float ATDCombatant::GetShield()    const { return Attributes ? Attributes->GetShield()    : 0.f; }
