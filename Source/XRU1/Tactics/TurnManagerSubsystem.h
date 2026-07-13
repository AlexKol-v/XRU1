#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TacticsTypes.h"
#include "TurnManagerSubsystem.generated.h"

class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTurnStarted, ETurnPhase, Phase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatEnded, bool, bPlayerWon);

/**
 * Менеджер пошагового боя уровня. Хранит очередь юнитов и текущую фазу
 * (ход игрока / ход врага), рулит переключением ходов и жизненным циклом боя.
 *
 * Каркас: очередь, фаза, делегаты и базовые переходы заложены; правила победы,
 * инициатива и AI-ходы врага дозаполняются на следующем этапе.
 */
UCLASS()
class XRU1_API UTurnManagerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Начинает бой с заданными сторонами. Сбрасывает AP, ставит фазу игрока, шлёт OnTurnStarted. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Turns")
	void StartCombat(const TArray<AActor*>& PlayerUnits, const TArray<AActor*>& EnemyUnits);

	/** Завершает ход текущей стороны и передаёт ход другой (Player <-> Enemy). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Turns")
	void EndTurn();

	/** Принудительно завершает бой (напр. по условию победы). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Turns")
	void EndCombat(bool bPlayerWon);

	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	ETurnPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	bool IsInCombat() const { return bInCombat; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	int32 GetTurnNumber() const { return TurnNumber; }

	/** Юниты активной стороны в этом ходу. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	const TArray<AActor*>& GetActiveSideUnits() const;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Turns")
	FOnTurnStarted OnTurnStarted;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Turns")
	FOnCombatEnded OnCombatEnded;

protected:
	/** Открывает ход указанной стороны: сбрасывает её AP и шлёт OnTurnStarted. */
	void BeginPhase(ETurnPhase Phase);

	/** Сбрасывает Action Points у всех юнитов стороны (через UActionPointsComponent). */
	void ResetActionPointsForSide(const TArray<TObjectPtr<AActor>>& Units);

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> PlayerSide;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> EnemySide;

	ETurnPhase CurrentPhase = ETurnPhase::None;
	bool bInCombat = false;
	int32 TurnNumber = 0;

private:
	// Кэш для GetActiveSideUnits (возврат по ссылке).
	mutable TArray<AActor*> ActiveSideCache;
};
