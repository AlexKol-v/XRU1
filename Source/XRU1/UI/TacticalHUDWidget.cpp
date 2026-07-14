#include "TacticalHUDWidget.h"
#include "TacticalPlayerController.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "TacticsCombatStatics.h"
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

void UTacticalHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UTurnManagerSubsystem* TurnManager = GetTurnManager())
	{
		TurnManager->OnTurnStarted.AddDynamic(this, &UTacticalHUDWidget::HandleTurnStarted);
		TurnManager->OnCombatEnded.AddDynamic(this, &UTacticalHUDWidget::HandleCombatEnded);

		// Первичная инициализация индикаторов (бой мог начаться до создания HUD).
		HandleTurnStarted(TurnManager->GetCurrentPhase());
	}
	if (ATacticalPlayerController* Controller = GetTacticalController())
	{
		Controller->OnSelectedUnitChanged.AddDynamic(this, &UTacticalHUDWidget::HandleSelectedUnitChanged);
		HandleSelectedUnitChanged(Controller->GetSelectedUnit());
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
	}
	Super::NativeDestruct();
}

void UTacticalHUDWidget::HandleTurnStarted(ETurnPhase Phase)
{
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
