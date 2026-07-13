#pragma once

#include "CoreMinimal.h"
#include "GASCharacter.h"
#include "TDCombatant.generated.h"

class UTDAttributeSet;
class UWidgetComponent;
class UUnitHUDLayoutData;
class UUnitHUDWidget;

/**
 * Боевой персонаж проекта TopDownCST: оснащён конкретным UTDAttributeSet,
 * HUD-виджетом над головой (Health/Shield bars) и тиком декремента Shield
 * через ASC. Используется как общий предок игрока и GAS-NPC, которым
 * нужны эти атрибуты.
 */
UCLASS(Abstract)
class XRU1_API ATDCombatant : public AGASCharacter
{
    GENERATED_BODY()

public:
    ATDCombatant();

    UFUNCTION(BlueprintPure, Category = "Attributes") float GetHealth() const;
    UFUNCTION(BlueprintPure, Category = "Attributes") float GetMaxHealth() const;
    UFUNCTION(BlueprintPure, Category = "Attributes") float GetShield() const;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    /** Создаёт виджет в HUDWidgetComponent и привязывает его к ASC. */
    void SetupUnitHUD();

    /** Декремент атрибута Shield по таймеру кадра через ASC, чтобы delegate'ы стрельнули. */
    void TickShieldDecay(float DeltaSeconds);

    UPROPERTY()
    TObjectPtr<UTDAttributeSet> Attributes;

    // --- HUD над головой -------------------------------------------------

    /** 3D-компонент-носитель HUD-виджета. Атачится к Mesh через offset из DataAsset'а. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    TObjectPtr<UWidgetComponent> HUDWidgetComponent;

    /** Контейнерный CommonUI-класс HUD'а. Дизайнер указывает в BP-наследнике. */
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UUnitHUDWidget> HUDWidgetClass;

    /** Раскладка слотов HUD'а (DataAsset). Может быть переопределена на инстансе. */
    UPROPERTY(EditAnywhere, Category = "UI")
    TObjectPtr<UUnitHUDLayoutData> HUDLayout;
};
