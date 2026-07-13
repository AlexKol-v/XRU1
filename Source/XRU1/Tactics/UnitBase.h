#pragma once

#include "CoreMinimal.h"
#include "TDCombatant.h"
#include "TacticsTypes.h"
#include "UnitBase.generated.h"

class UActionPointsComponent;
class UCoverDetectionComponent;
class UGameplayAbility;

/**
 * Базовый тактический юнит. Наследует GAS-иерархию донора (ATDCombatant: ASC,
 * UTDAttributeSet, HUD над головой) и добавляет пошаговый слой: Action Points,
 * детекцию укрытий и набор классовых способностей.
 *
 * Blueprintable — из него сделаны 4 фиксированных класса ростера
 * (Assault / Sniper / Healer / Tank).
 */
UCLASS(Blueprintable)
class XRU1_API AUnitBase : public ATDCombatant
{
	GENERATED_BODY()

public:
	AUnitBase();

	UFUNCTION(BlueprintPure, Category = "Tactics|Unit")
	EUnitRole GetUnitRole() const { return UnitRole; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Unit")
	UActionPointsComponent* GetActionPoints() const { return ActionPoints; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Unit")
	UCoverDetectionComponent* GetCoverDetection() const { return CoverDetection; }

protected:
	virtual void BeginPlay() override;

	/** Выдаёт классовые способности через ASC (аналогично StartupAbilities родителя). */
	void GrantClassAbilities();

	/** Роль в ростере. Переопределяется в подклассах/BP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Unit")
	EUnitRole UnitRole = EUnitRole::Assault;

	/** Уникальные способности класса (снайп/лечение/штурм/удержание). Заполняется в BP-наследнике. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Unit")
	TArray<TSubclassOf<UGameplayAbility>> ClassAbilities;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Unit")
	TObjectPtr<UActionPointsComponent> ActionPoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Unit")
	TObjectPtr<UCoverDetectionComponent> CoverDetection;
};
