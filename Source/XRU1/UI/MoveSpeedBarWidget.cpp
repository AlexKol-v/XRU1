#include "MoveSpeedBarWidget.h"

#include "AbilitySystemComponent.h"
#include "TDAttributeSet.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UMoveSpeedBarWidget::UMoveSpeedBarWidget()
{
    BaseColor = FLinearColor::Green;
}

void UMoveSpeedBarWidget::BindDelegates()
{
    UAbilitySystemComponent* C = ASC.Get();
    if (!C) return;

    // MoveSpeed (cap) теперь не наш источник для бара — реальную скорость
    // читаем из CMC в NativeTick. Подписываемся только на MaxMoveSpeed
    // (knpb-меняется редко, через GE) — он наш знаменатель.
    MaxMoveSpeedHandle = C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetMaxMoveSpeedAttribute())
                          .AddUObject(this, &UMoveSpeedBarWidget::OnMaxMoveSpeedChanged);
}

void UMoveSpeedBarWidget::UnbindDelegates()
{
    if (UAbilitySystemComponent* C = ASC.Get())
    {
        C->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetMaxMoveSpeedAttribute()).Remove(MaxMoveSpeedHandle);
    }
}

void UMoveSpeedBarWidget::RefreshFromASC()
{
    Redraw();
}

void UMoveSpeedBarWidget::OnMaxMoveSpeedChanged(const FOnAttributeChangeData& /*Data*/)
{
    Redraw();
}

void UMoveSpeedBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    Redraw();
}

void UMoveSpeedBarWidget::Redraw()
{
    const UAbilitySystemComponent* C = ASC.Get();
    if (!C) return;

    float CurrentSpeed = 0.f;
    if (const AActor* Avatar = C->GetAvatarActor())
    {
        if (const ACharacter* Char = Cast<ACharacter>(Avatar))
        {
            if (const UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
            {
                // Size2D() — горизонтальная скорость; вертикальная (прыжок/гравитация)
                // не должна шевелить шкалу.
                CurrentSpeed = CMC->Velocity.Size2D();
            }
        }
    }

    const float Max = C->GetNumericAttribute(UTDAttributeSet::GetMaxMoveSpeedAttribute());
    UpdateBarVisual(CurrentSpeed, Max, BaseColor);
}
