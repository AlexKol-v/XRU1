#include "TurnManagerSubsystem.h"
#include "ActionPointsComponent.h"
#include "TacticsCombatStatics.h"
#include "UnitAIController.h"
#include "UnitBase.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UTurnManagerSubsystem::StartCombat(const TArray<AActor*>& PlayerUnits, const TArray<AActor*>& EnemyUnits)
{
	PlayerSide.Reset();
	for (AActor* A : PlayerUnits)
	{
		if (A) { PlayerSide.Add(A); }
	}

	EnemySide.Reset();
	for (AActor* A : EnemyUnits)
	{
		if (A) { EnemySide.Add(A); }
	}

	bInCombat = true;
	TurnNumber = 1;
	BeginPhase(ETurnPhase::Player);
}

void UTurnManagerSubsystem::EndTurn()
{
	if (!bInCombat)
	{
		return;
	}

	StopEnemyTurnProcessing();

	switch (CurrentPhase)
	{
	case ETurnPhase::Player:
		// Таймер бомбы: игрок завершил свой N-й (лимитный) ход — взрыв, поражение.
		if (TurnLimit > 0 && TurnNumber >= TurnLimit)
		{
			OnTurnLimitExpired.Broadcast();
			EndCombat(false);
			return;
		}
		BeginPhase(ETurnPhase::Enemy);
		break;
	case ETurnPhase::Enemy:
		++TurnNumber;
		BeginPhase(ETurnPhase::Player);
		break;
	default:
		break;
	}
}

void UTurnManagerSubsystem::EndCombat(bool bPlayerWon)
{
	if (!bInCombat)
	{
		return;
	}

	StopEnemyTurnProcessing();
	bInCombat = false;
	CurrentPhase = ETurnPhase::None;
	OnCombatEnded.Broadcast(bPlayerWon);
}

void UTurnManagerSubsystem::CheckCombatOutcome()
{
	if (!bInCombat)
	{
		return;
	}

	auto AnyAlive = [](const TArray<TObjectPtr<AActor>>& Side)
	{
		for (const TObjectPtr<AActor>& Unit : Side)
		{
			if (Unit && UTacticsCombatStatics::IsUnitAlive(Unit))
			{
				return true;
			}
		}
		return false;
	};
	auto AnyEvacuated = [](const TArray<TObjectPtr<AActor>>& Side)
	{
		for (const TObjectPtr<AActor>& Unit : Side)
		{
			if (Unit && UTacticsCombatStatics::IsUnitEvacuated(Unit))
			{
				return true;
			}
		}
		return false;
	};

	const bool bEnemiesAlive = AnyAlive(EnemySide);
	// Эвакуированные живы: полная эвакуация отряда — не поражение (победу объявляет GameMode).
	const bool bPlayersAlive = AnyAlive(PlayerSide) || AnyEvacuated(PlayerSide);

	if (!bPlayersAlive)
	{
		EndCombat(false);
	}
	else if (!bEnemiesAlive && bAutoWinWhenEnemiesDead)
	{
		// Для миссий с целью (бомба/эвакуация) флаг выключен — зачистка не завершает бой.
		EndCombat(true);
	}
}

void UTurnManagerSubsystem::BeginPhase(ETurnPhase Phase)
{
	CurrentPhase = Phase;
	ResetActionPointsForSide(Phase == ETurnPhase::Player ? PlayerSide : EnemySide);
	OnTurnStarted.Broadcast(Phase);

	// Ход врага исполняет сам менеджер: юниты действуют по одному.
	if (Phase == ETurnPhase::Enemy)
	{
		StartEnemyTurnProcessing();
	}
}

void UTurnManagerSubsystem::ResetActionPointsForSide(const TArray<TObjectPtr<AActor>>& Units)
{
	for (const TObjectPtr<AActor>& Unit : Units)
	{
		if (!Unit) { continue; }
		if (UActionPointsComponent* AP = Unit->FindComponentByClass<UActionPointsComponent>())
		{
			AP->ResetForNewTurn();
		}
	}
}

void UTurnManagerSubsystem::StartEnemyTurnProcessing()
{
	EnemyTurnIndex = 0;
	// Небольшая пауза перед первым действием: даём HUD показать смену фазы.
	GetWorld()->GetTimerManager().SetTimer(EnemyStepTimerHandle, this,
		&UTurnManagerSubsystem::ProcessNextEnemyUnit, EnemyStepInterval, false);
}

void UTurnManagerSubsystem::ProcessNextEnemyUnit()
{
	if (!bInCombat || CurrentPhase != ETurnPhase::Enemy)
	{
		return;
	}

	// Ищем следующего живого юнита с AI-контроллером; мёртвых и «безмозглых» пропускаем.
	while (EnemySide.IsValidIndex(EnemyTurnIndex))
	{
		AActor* Unit = EnemySide[EnemyTurnIndex];
		APawn* Pawn = Cast<APawn>(Unit);
		AUnitAIController* AI = Pawn ? Cast<AUnitAIController>(Pawn->GetController()) : nullptr;

		if (Unit && AI && UTacticsCombatStatics::IsUnitAlive(Unit))
		{
			// Действующий юнит не вырезает навмеш под собой (иначе не построит
			// свой путь); камера игрока летит к нему (подписан контроллер).
			if (AUnitBase* UnitBase = Cast<AUnitBase>(Unit))
			{
				UnitBase->SetNavObstacleEnabled(false);
			}
			OnEnemyUnitActivated.Broadcast(Unit);

			// Пауза перед действиями: навмеш латает дыру, камера долетает.
			GetWorld()->GetTimerManager().SetTimer(EnemyStepTimerHandle, this,
				&UTurnManagerSubsystem::ActivateCurrentEnemyUnit, EnemyActivationDelay, false);
			return;
		}
		++EnemyTurnIndex;
	}

	// Все вражеские юниты отходили — ход возвращается игроку.
	EndTurn();
}

void UTurnManagerSubsystem::ActivateCurrentEnemyUnit()
{
	if (!bInCombat || CurrentPhase != ETurnPhase::Enemy || !EnemySide.IsValidIndex(EnemyTurnIndex))
	{
		return;
	}

	AActor* Unit = EnemySide[EnemyTurnIndex];
	APawn* Pawn = Cast<APawn>(Unit);
	AUnitAIController* AI = Pawn ? Cast<AUnitAIController>(Pawn->GetController()) : nullptr;
	if (Unit && AI && UTacticsCombatStatics::IsUnitAlive(Unit))
	{
		AI->ExecuteUnitTurn(FSimpleDelegate::CreateUObject(
			this, &UTurnManagerSubsystem::HandleEnemyUnitFinished));
	}
	else
	{
		// Юнит выбыл за время паузы (овервотч) — дальше по очереди.
		HandleEnemyUnitFinished();
	}
}

void UTurnManagerSubsystem::HandleEnemyUnitFinished()
{
	// Юнит отходил — снова вырезает навмеш на новой позиции.
	if (EnemySide.IsValidIndex(EnemyTurnIndex))
	{
		if (AUnitBase* UnitBase = Cast<AUnitBase>(EnemySide[EnemyTurnIndex].Get()))
		{
			UnitBase->SetNavObstacleEnabled(true);
		}
	}
	++EnemyTurnIndex;
	CheckCombatOutcome();
	if (!bInCombat)
	{
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(EnemyStepTimerHandle, this,
		&UTurnManagerSubsystem::ProcessNextEnemyUnit, EnemyStepInterval, false);
}

void UTurnManagerSubsystem::StopEnemyTurnProcessing()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EnemyStepTimerHandle);
	}
	// Прерванный посреди хода юнит (конец боя) не должен остаться без выреза.
	if (CurrentPhase == ETurnPhase::Enemy && EnemySide.IsValidIndex(EnemyTurnIndex))
	{
		if (AUnitBase* UnitBase = Cast<AUnitBase>(EnemySide[EnemyTurnIndex].Get()))
		{
			UnitBase->SetNavObstacleEnabled(true);
		}
	}
	EnemyTurnIndex = 0;
}

const TArray<AActor*>& UTurnManagerSubsystem::GetActiveSideUnits() const
{
	ActiveSideCache.Reset();
	const TArray<TObjectPtr<AActor>>& Side = (CurrentPhase == ETurnPhase::Enemy) ? EnemySide : PlayerSide;
	for (const TObjectPtr<AActor>& A : Side)
	{
		ActiveSideCache.Add(A.Get());
	}
	return ActiveSideCache;
}

bool UTurnManagerSubsystem::IsUnitOnActiveSide(const AActor* Unit) const
{
	if (!Unit || CurrentPhase == ETurnPhase::None)
	{
		return false;
	}
	const TArray<TObjectPtr<AActor>>& Side = (CurrentPhase == ETurnPhase::Enemy) ? EnemySide : PlayerSide;
	return Side.Contains(Unit);
}

TArray<AActor*> UTurnManagerSubsystem::GetSideUnits(const AActor* Unit) const
{
	TArray<AActor*> Result;
	if (!Unit)
	{
		return Result;
	}

	const TArray<TObjectPtr<AActor>>* Side = nullptr;
	if (PlayerSide.Contains(Unit))
	{
		Side = &PlayerSide;
	}
	else if (EnemySide.Contains(Unit))
	{
		Side = &EnemySide;
	}

	if (Side)
	{
		for (const TObjectPtr<AActor>& A : *Side)
		{
			if (A && UTacticsCombatStatics::IsUnitAlive(A))
			{
				Result.Add(A.Get());
			}
		}
	}
	return Result;
}

TArray<AActor*> UTurnManagerSubsystem::GetOpposingUnits(const AActor* Unit) const
{
	TArray<AActor*> Result;
	if (!Unit)
	{
		return Result;
	}

	const TArray<TObjectPtr<AActor>>* Opposing = nullptr;
	if (PlayerSide.Contains(Unit))
	{
		Opposing = &EnemySide;
	}
	else if (EnemySide.Contains(Unit))
	{
		Opposing = &PlayerSide;
	}

	if (Opposing)
	{
		for (const TObjectPtr<AActor>& A : *Opposing)
		{
			if (A && UTacticsCombatStatics::IsUnitAlive(A))
			{
				Result.Add(A.Get());
			}
		}
	}
	return Result;
}
