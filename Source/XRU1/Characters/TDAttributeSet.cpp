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

void UTDAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);

    if (Data.EvaluatedData.Attribute == GetHealthAttribute())
    {
        const float Magnitude = Data.EvaluatedData.Magnitude;
        // Щит активен — отыгрываем входящий урон обратно. Положительные дельты (heal) пропускаем.
        if (GetShield() > 0.f && Magnitude < 0.f)
        {
            SetHealth(GetHealth() - Magnitude);
        }
        // «Провокация» танка (GDD §7): −50% входящего урона — половину возвращаем.
        else if (Magnitude < 0.f &&
            Data.Target.HasMatchingGameplayTag(TacticsGameplayTags::State_Taunting))
        {
            SetHealth(GetHealth() - Magnitude * 0.5f);
        }
        SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
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
