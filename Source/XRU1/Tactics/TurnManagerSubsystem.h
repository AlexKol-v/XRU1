#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/TimerHandle.h"
#include "TacticsTypes.h"
#include "TurnManagerSubsystem.generated.h"

class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTurnStarted, ETurnPhase, Phase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatEnded, bool, bPlayerWon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTurnLimitExpired);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyUnitActivated, AActor*, Unit);

/**
 * Менеджер пошагового боя уровня. Хранит очередь юнитов и текущую фазу
 * (ход игрока / ход врага), рулит переключением ходов и жизненным циклом боя.
 *
 * Ход врага исполняется здесь же: юниты вражеской стороны действуют по одному
 * (AUnitAIController::ExecuteUnitTurn), после последнего ход возвращается игроку.
 * После каждого выстрела (см. UTacticsCombatStatics::ResolveShot) проверяются
 * условия конца боя: сторона без живых юнитов проигрывает.
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

	/** Проверяет условия конца боя: если одна из сторон полностью мертва — EndCombat. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Turns")
	void CheckCombatOutcome();

	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	ETurnPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	bool IsInCombat() const { return bInCombat; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	int32 GetTurnNumber() const { return TurnNumber; }

	/** Юниты активной стороны в этом ходу. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	const TArray<AActor*>& GetActiveSideUnits() const;

	/** Ходит ли сейчас сторона, которой принадлежит юнит. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	bool IsUnitOnActiveSide(const AActor* Unit) const;

	/** Живые юниты стороны, противоположной юниту (цели для его атак/движения). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	TArray<AActor*> GetOpposingUnits(const AActor* Unit) const;

	/** Все юниты стороны юнита, включая его самого (живые, для Squadsight и HUD). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	TArray<AActor*> GetSideUnits(const AActor* Unit) const;

	/** Юниты стороны игрока как есть (включая мёртвых/эвакуированных). Для подписок HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	TArray<AActor*> GetPlayerSideUnits() const;

	/** Юниты вражеской стороны как есть (включая мёртвых). Для подписок и счётчика HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	TArray<AActor*> GetEnemySideUnits() const;

	/** Живые враги на поле (критерий «жив» общий с CheckCombatOutcome). Для HUD/результата. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	int32 GetAliveEnemyCount() const;

	// --- Таймер ходов (бомба миссии; GDD §5.7) -------------------------------

	/**
	 * Лимит ходов игрока (0 = без лимита). По завершении лимитного хода игрока —
	 * OnTurnLimitExpired и поражение. GameMode снимает лимит при обезвреживании.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Turns")
	void SetTurnLimit(int32 NewLimit) { TurnLimit = FMath::Max(0, NewLimit); }

	/** Сколько ходов игрока осталось до взрыва (-1 = лимита нет). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Turns")
	int32 GetTurnsRemaining() const { return TurnLimit > 0 ? FMath::Max(0, TurnLimit - TurnNumber + 1) : -1; }

	/**
	 * «Победа при уничтожении всех врагов». Для миссий с целью (бомба/эвакуация)
	 * GameMode выключает флаг и объявляет победу сам.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Tactics|Turns")
	bool bAutoWinWhenEnemiesDead = true;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Turns")
	FOnTurnStarted OnTurnStarted;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Turns")
	FOnCombatEnded OnCombatEnded;

	/** Истёк лимит ходов (бомба взорвалась) — срабатывает ПЕРЕД OnCombatEnded(false). */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|Turns")
	FOnTurnLimitExpired OnTurnLimitExpired;

	/**
	 * Вражеский юнит начал свой ход (шлётся перед ExecuteUnitTurn). Контроллер
	 * игрока фокусирует на нём камеру (XCOM: камера летит к действующему врагу).
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|Turns")
	FOnEnemyUnitActivated OnEnemyUnitActivated;

protected:
	/** Открывает ход указанной стороны: сбрасывает её AP и шлёт OnTurnStarted. */
	void BeginPhase(ETurnPhase Phase);

	/** Сбрасывает Action Points у всех юнитов стороны (через UActionPointsComponent). */
	void ResetActionPointsForSide(const TArray<TObjectPtr<AActor>>& Units);

	// --- Исполнение хода вражеской стороны ---------------------------------

	/** Запускает последовательный проход по вражеским юнитам. */
	void StartEnemyTurnProcessing();

	/** Отдаёт ход следующему живому вражескому юниту; когда юниты кончились — EndTurn. */
	void ProcessNextEnemyUnit();

	/**
	 * Запускает ExecuteUnitTurn текущего юнита. Отделено от ProcessNextEnemyUnit
	 * паузой, за которую камера игрока долетает до действующего врага (XCOM-темп).
	 */
	void ActivateCurrentEnemyUnit();

	/** Колбэк AI-контроллера: юнит закончил действия, переходим к следующему. */
	void HandleEnemyUnitFinished();

	/** Останавливает проход по вражеским юнитам (конец боя / принудительный EndTurn). */
	void StopEnemyTurnProcessing();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> PlayerSide;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> EnemySide;

	ETurnPhase CurrentPhase = ETurnPhase::None;
	bool bInCombat = false;
	int32 TurnNumber = 0;

	/** Лимит ходов игрока (0 = нет). */
	int32 TurnLimit = 0;

	/** Индекс обрабатываемого вражеского юнита в EnemySide. */
	int32 EnemyTurnIndex = 0;

	/** Пауза между действиями вражеских юнитов — чтобы игрок успевал читать ход. */
	float EnemyStepInterval = 0.5f;

	/** Пауза между «камера полетела к юниту» и его действиями (полёт камеры). */
	float EnemyActivationDelay = 0.35f;

	FTimerHandle EnemyStepTimerHandle;

private:
	// Кэш для GetActiveSideUnits (возврат по ссылке).
	mutable TArray<AActor*> ActiveSideCache;
};
