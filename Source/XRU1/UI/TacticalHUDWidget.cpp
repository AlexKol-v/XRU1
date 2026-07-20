#include "TacticalHUDWidget.h"
#include "TacticalPlayerController.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "TacticsCombatStatics.h"
#include "CoverDetectionComponent.h"
#include "GA_Attack.h"
#include "Engine/World.h"

ATacticalPlayerController* UTacticalHUDWidget::GetTacticalController() const
{
	return Cast<ATacticalPlayerController>(GetOwningPlayer());
}

UTurnManagerSubsystem* UTacticalHUDWidget::GetTurnManager() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
}

TArray<AUnitBase*> UTacticalHUDWidget::GetSquad() const
{
	const ATacticalPlayerController* Controller = GetTacticalController();
	return Controller ? Controller->GetSquad() : TArray<AUnitBase*>();
}

float UTacticalHUDWidget::GetHitChanceOnTarget(AActor* Target) const
{
	const ATacticalPlayerController* Controller = GetTacticalController();
	const AUnitBase* Shooter = Controller ? Controller->GetSelectedUnit() : nullptr;
	if (!Shooter || !Target || !UGA_Attack::CanTargetActor(Shooter, Target))
	{
		return -1.f;
	}
	return UTacticsCombatStatics::ComputeHitChance(Shooter, Target, Shooter->BaseAim);
}

ECoverType UTacticalHUDWidget::GetTargetCoverAgainstSelected(AActor* Target) const
{
	const ATacticalPlayerController* Controller = GetTacticalController();
	const AUnitBase* Shooter = Controller ? Controller->GetSelectedUnit() : nullptr;
	if (!Shooter || !Target)
	{
		return ECoverType::None;
	}
	const UCoverDetectionComponent* Cover = Target->FindComponentByClass<UCoverDetectionComponent>();
	return Cover ? Cover->GetCoverAgainst(Shooter) : ECoverType::None;
}

int32 UTacticalHUDWidget::GetAliveEnemyCount() const
{
	// Критерий «жив» живёт в TurnManager (общий с условием конца боя).
	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	return TurnManager ? TurnManager->GetAliveEnemyCount() : 0;
}

void UTacticalHUDWidget::SubscribeToUnitStates()
{
	UTurnManagerSubsystem* TurnManager = GetTurnManager();
	if (!TurnManager)
	{
		return;
	}

	// Смерть/ранение/эвакуация любого юнита обновляет портреты и счётчик врагов
	// одним событием OnUnitsStateChanged.
	auto SubscribeSide = [this](const TArray<AActor*>& Side)
	{
		for (AActor* Actor : Side)
		{
			AUnitBase* Unit = Cast<AUnitBase>(Actor);
			if (Unit && !StateSubscribedUnits.Contains(Unit))
			{
				Unit->OnUnitStateChanged.AddDynamic(this, &UTacticalHUDWidget::HandleUnitStateChanged);
				StateSubscribedUnits.Add(Unit);
			}
		}
	};
	SubscribeSide(TurnManager->GetPlayerSideUnits());
	SubscribeSide(TurnManager->GetEnemySideUnits());
}

void UTacticalHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UTurnManagerSubsystem* TurnManager = GetTurnManager())
	{
		TurnManager->OnTurnStarted.AddDynamic(this, &UTacticalHUDWidget::HandleTurnStarted);
		TurnManager->OnCombatEnded.AddDynamic(this, &UTacticalHUDWidget::HandleCombatEnded);

		// Первичная инициализация индикаторов (бой мог начаться до создания HUD;
		// подписка на юнитов — внутри HandleTurnStarted).
		HandleTurnStarted(TurnManager->GetCurrentPhase());
		OnUnitsStateChanged();
	}
	if (ATacticalPlayerController* Controller = GetTacticalController())
	{
		Controller->OnSelectedUnitChanged.AddDynamic(this, &UTacticalHUDWidget::HandleSelectedUnitChanged);
		Controller->OnHoveredUnitChanged.AddDynamic(this, &UTacticalHUDWidget::HandleHoveredUnitChanged);
		HandleSelectedUnitChanged(Controller->GetSelectedUnit());
		HandleHoveredUnitChanged(Controller->GetHoveredUnit());
	}
}

void UTacticalHUDWidget::NativeDestruct()
{
	if (UTurnManagerSubsystem* TurnManager = GetTurnManager())
	{
		TurnManager->OnTurnStarted.RemoveDynamic(this, &UTacticalHUDWidget::HandleTurnStarted);
		TurnManager->OnCombatEnded.RemoveDynamic(this, &UTacticalHUDWidget::HandleCombatEnded);
	}
	if (ATacticalPlayerController* Controller = GetTacticalController())
	{
		Controller->OnSelectedUnitChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleSelectedUnitChanged);
		Controller->OnHoveredUnitChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleHoveredUnitChanged);
	}
	for (const TWeakObjectPtr<AUnitBase>& Unit : StateSubscribedUnits)
	{
		if (AUnitBase* Alive = Unit.Get())
		{
			Alive->OnUnitStateChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleUnitStateChanged);
		}
	}
	StateSubscribedUnits.Reset();
	Super::NativeDestruct();
}

void UTacticalHUDWidget::HandleTurnStarted(ETurnPhase Phase)
{
	// Ленивая идемпотентная подписка: покрывает «HUD создан до StartCombat»
	// и юнитов, добавленных в бой после создания HUD.
	SubscribeToUnitStates();

	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	OnPhaseChanged(Phase,
		TurnManager ? TurnManager->GetTurnNumber() : 0,
		TurnManager ? TurnManager->GetTurnsRemaining() : -1);
}

void UTacticalHUDWidget::HandleCombatEnded(bool bPlayerWon)
{
	OnCombatFinished(bPlayerWon);
}

void UTacticalHUDWidget::HandleSelectedUnitChanged(AUnitBase* Selected)
{
	OnSelectedUnitChanged(Selected);
}

void UTacticalHUDWidget::HandleHoveredUnitChanged(AUnitBase* Hovered)
{
	OnHoveredUnitChanged(Hovered);
}

void UTacticalHUDWidget::HandleUnitStateChanged()
{
	OnUnitsStateChanged();
}
