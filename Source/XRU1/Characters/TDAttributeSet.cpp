#include "TDAttributeSet.h"

#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "TacticsGameplayTags.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UTDAttributeSet::UTDAttributeSet()
{
    InitHealth(100.f);
    InitMaxHealth(100.f);
    InitMoveSpeed(600.f);
    InitMaxMoveSpeed(600.f);
    InitShield(0.f);
}

bool UTDAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
    if (!Super::PreGameplayEffectExecute(Data))
    {
        return false;
    }

    if (Data.EvaluatedData.Attribute == GetHealthAttribute())
    {
        float& Magnitude = Data.EvaluatedData.Magnitude;
        if (Magnitude < 0.f)
        {
            // Mitigation меняет единственную применяемую дельту ДО Health delegate.
            // Старый refund в Post успевал сообщить временный полный/смертельный урон,
            // после чего лишь возвращал HP и мог оставить ложный Downed/Death state.
            if (GetShield() > 0.f)
            {
                Magnitude = 0.f;
            }
            else if (Data.Target.HasMatchingGameplayTag(TacticsGameplayTags::State_Taunting))
            {
                Magnitude *= 0.5f;
            }
        }
    }

    return true;
}

void UTDAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);

    if (Data.EvaluatedData.Attribute == GetHealthAttribute())
    {
        // Magnitude уже прошёл shield/taunt mitigation в Pre; здесь только границы Health.
        const float ClampedHealth = FMath::Clamp(GetHealth(), 0.f, GetMaxHealth());
        if (ClampedHealth != GetHealth())
        {
            SetHealth(ClampedHealth);
        }
    }
    else if (Data.EvaluatedData.Attribute == GetMoveSpeedAttribute())
    {
        if (ACharacter* Owner = Cast<ACharacter>(Data.Target.GetAvatarActor()))
        {
            Owner->GetCharacterMovement()->MaxWalkSpeed = GetMoveSpeed();
        }
    }
    else if (Data.EvaluatedData.Attribute == GetShieldAttribute())
    {
        // Накопительные подборы щита — не даём шкале уйти в минус.
        SetShield(FMath::Max(GetShield(), 0.f));
    }
}
