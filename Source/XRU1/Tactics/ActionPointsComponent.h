#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ActionPointsComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActionPointsChanged, int32, NewCurrent, int32, Max);

/**
 * Очки действия (Action Points) боевого юнита. По ТЗ каждый юнит имеет 2 AP за ход:
 * типичное действие (move / shoot) стоит 1 AP. Сбрасывается TurnManager'ом в начале
 * хода стороны.
 *
 * Привязка к GAS: способности юнитов наследуются от UTacticalAbility, который
 * проверяет (CheckCost) и списывает (ApplyCost) AP внутри CommitAbility — этот
 * компонент выступает хранилищем ресурса. Прямые вызовы TrySpendActionPoint
 * остаются для действий вне GAS (перемещение по приказу, ход AI).
 */
UCLASS(ClassGroup = (Tactics), meta = (BlueprintSpawnableComponent))
class XRU1_API UActionPointsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UActionPointsComponent();

	/** Максимум очков действия за ход (по ТЗ = 2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|ActionPoints", meta = (ClampMin = "1"))
	int32 MaxActionPoints = 2;

	/** Текущий остаток очков действия в этом ходу. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|ActionPoints")
	int32 CurrentActionPoints = 2;

	/** Срабатывает при любом изменении CurrentActionPoints (для UI/HUD). */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|ActionPoints")
	FOnActionPointsChanged OnActionPointsChanged;

	/** Хватает ли очков на действие стоимостью Cost. */
	UFUNCTION(BlueprintPure, Category = "Tactics|ActionPoints")
	bool CanSpend(int32 Cost = 1) const;

	/** Пытается списать Cost очков. Возвращает false, если не хватает (ничего не списывает). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|ActionPoints")
	bool TrySpendActionPoint(int32 Cost = 1);

	/** Возвращает очки в исходное состояние — вызывается TurnManager'ом в начале хода стороны. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|ActionPoints")
	void ResetForNewTurn();

	/** Сжигает весь остаток AP (атака в XCOM завершает активацию; «пропустить ход»). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|ActionPoints")
	void SpendAllRemaining();

	/** Выдаёт дополнительные очки сверх максимума («Рывок и удар» штурмовика). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|ActionPoints")
	void GrantExtraPoints(int32 Amount = 1);

	UFUNCTION(BlueprintPure, Category = "Tactics|ActionPoints")
	bool HasActionsLeft() const { return CurrentActionPoints > 0; }

protected:
	virtual void BeginPlay() override;

	void BroadcastChanged();
};
