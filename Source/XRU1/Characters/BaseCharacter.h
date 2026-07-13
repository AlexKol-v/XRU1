#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "BaseCharacter.generated.h"

/**
 * Скелетный базовый класс для всех персонажей проекта TopDownCST.
 * Содержит только Team-роль для AI Perception — единственная ответственность,
 * имеющая смысл для произвольного персонажа (декоративного, боевого, NPC).
 *
 * Боевая логика, GAS-плата и UI-обвязка лежат в наследниках:
 *   - AGASCharacter   — добавляет ASC и стартовые способности/эффекты.
 *   - ATDCombatant    — добавляет UTDAttributeSet, Shield-тик и HUD над головой.
 */
UCLASS(Abstract)
class XRU1_API ABaseCharacter
    : public ACharacter
    , public IGenericTeamAgentInterface
{
    GENERATED_BODY()

public:
    ABaseCharacter();

    // IGenericTeamAgentInterface — сообщаем AI-перцепции «свой/чужой».
    virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
    virtual FGenericTeamId GetGenericTeamId() const override;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(EditDefaultsOnly, Category = "Team", meta = (ClampMin = "0", ClampMax = "255"))
    uint8 DefaultTeamId = 255; // 255 == FGenericTeamId::NoTeam

private:
    FGenericTeamId TeamId = FGenericTeamId::NoTeam;
};
