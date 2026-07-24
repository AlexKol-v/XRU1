#include "AIActionEvaluators.h"
#include "UnitAIController.h"
#include "UnitBase.h"
#include "GA_Attack.h"

// --- UAIEval_Retreat ----------------------------------------------------------

UAIEval_Retreat::UAIEval_Retreat()
{
	BasePriority = 120.f;
	DebugName = TEXT("Retreat");
}

bool UAIEval_Retreat::IsApplicable(const FAIDecisionContext& Context) const
{
	// Условия — дословно прежние (StepCombat п.1): не двигались в укрытие в этом
	// ходу, мало HP, стоим открытыми, есть чем оплатить.
	return !Context.bCoverMoveDoneThisTurn && Context.bLowHealth && Context.bExposed &&
		Context.ActionPointsLeft >= 1 && Context.Unit && Context.PrimaryThreat && Context.Controller;
}

float UAIEval_Retreat::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	FVector Point;
	FAICoverPointResult Details;
	if (!Context.Controller->FindCoverPoint(Context.Unit, Context.PrimaryThreat,
		Context.Unit->MoveRange * Context.ActionPointsLeft, /*bRetreat=*/true, Point,
		/*bAdvance=*/false, &Details))
	{
		return -1.f; // укрытия для отхода нет — вариант не предлагается
	}

	OutDecision.Kind = EAIActionKind::Move;
	OutDecision.Destination = Point;
	OutDecision.bIsCoverManeuver = true;
	OutDecision.AcceptanceRadius = 40.f;
	OutDecision.Reason = FString::Printf(TEXT("отступление (мало HP): %s"), *Details.Describe());
	return BasePriority;
}

// --- UAIEval_MoveToCover ------------------------------------------------------

UAIEval_MoveToCover::UAIEval_MoveToCover()
{
	BasePriority = 80.f;
	DebugName = TEXT("MoveToCover");
}

bool UAIEval_MoveToCover::IsApplicable(const FAIDecisionContext& Context) const
{
	// Нужно 2 AP: манёвр + выстрел. Смысл есть, только если открыт или отсюда
	// не выстрелить — иначе выгоднее стрелять с места.
	return !Context.bCoverMoveDoneThisTurn && Context.ActionPointsLeft >= 2 &&
		(Context.bExposed || !Context.bCanShootNow) &&
		Context.Unit && Context.PrimaryThreat && Context.Controller;
}

float UAIEval_MoveToCover::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	FVector Point;
	FAICoverPointResult Details;
	if (!Context.Controller->FindCoverPoint(Context.Unit, Context.PrimaryThreat,
		Context.Unit->MoveRange, /*bRetreat=*/false, Point, /*bAdvance=*/false, &Details))
	{
		return -1.f;
	}

	OutDecision.Kind = EAIActionKind::Move;
	OutDecision.Destination = Point;
	OutDecision.bIsCoverManeuver = true;
	OutDecision.AcceptanceRadius = 40.f;
	OutDecision.Reason = FString::Printf(TEXT("укрытие с линией огня (выстрел вторым AP): %s"),
		*Details.Describe());
	return BasePriority;
}

// --- UAIEval_Shoot ------------------------------------------------------------

UAIEval_Shoot::UAIEval_Shoot()
{
	// ⚠️ ВЫШЕ, чем у манёвра в укрытие (80). Так и должно быть: уверенный
	// выстрел важнее перебежки. Раньше приоритет был 60, и бот с шансом 93%
	// уходил перепозиционироваться, а потом стрелял с 28% — это видно в логах
	// боя прямо построчно. В XCOM порядок такой же: `ShootIfAvailable` стоит
	// перед движением, а движение — это то, что делают, когда стрелять НЕЧЕМ.
	//
	// Итоговый скор масштабируется качеством выстрела (см. ScoreAction), поэтому
	// ПЛОХОЙ выстрел манёвру всё-таки проигрывает — иначе бот застревал бы,
	// вечно паля по цели в полном укрытии с 20%.
	BasePriority = 100.f;
	DebugName = TEXT("Shoot");
}

bool UAIEval_Shoot::IsApplicable(const FAIDecisionContext& Context) const
{
	return Context.bCanShootNow && Context.PrimaryThreat && Context.ActionPointsLeft >= 1;
}

float UAIEval_Shoot::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	const float HitChance = UGA_Attack::ComputeAttackHitChance(Context.Unit, Context.PrimaryThreat);

	// Качество выстрела по тем же порогам, что и скоринг цели (A3): уверенный —
	// полный вес, средний — половина, сомнительный — четверть. Именно эта
	// шкала и решает «стрелять или сначала перебежать».
	const AUnitAIController* Controller = Context.Controller;
	float Quality = 1.f;
	if (Controller)
	{
		if (HitChance >= Controller->TargetHitChanceHighThreshold)      Quality = 1.f;
		else if (HitChance >= Controller->TargetHitChanceLowThreshold)  Quality = 0.55f;
		else                                                            Quality = 0.15f;
	}

	OutDecision.Kind = EAIActionKind::Shoot;
	OutDecision.Target = Context.PrimaryThreat;
	OutDecision.Reason = FString::Printf(TEXT("выстрел по %s (шанс %.0f%%)"),
		*GetNameSafe(Context.PrimaryThreat), HitChance);
	return BasePriority * Quality;
}

// --- UAIEval_AdvanceToCover ---------------------------------------------------

UAIEval_AdvanceToCover::UAIEval_AdvanceToCover()
{
	BasePriority = 40.f;
	DebugName = TEXT("AdvanceToCover");
}

bool UAIEval_AdvanceToCover::IsApplicable(const FAIDecisionContext& Context) const
{
	return !Context.bCoverMoveDoneThisTurn && Context.ActionPointsLeft >= 1 &&
		Context.Unit && Context.PrimaryThreat && Context.Controller;
}

float UAIEval_AdvanceToCover::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	FVector Point;
	FAICoverPointResult Details;
	if (!Context.Controller->FindCoverPoint(Context.Unit, Context.PrimaryThreat,
		Context.Unit->MoveRange * Context.ActionPointsLeft, /*bRetreat=*/false, Point,
		/*bAdvance=*/true, &Details))
	{
		return -1.f;
	}

	OutDecision.Kind = EAIActionKind::Move;
	OutDecision.Destination = Point;
	OutDecision.bIsCoverManeuver = true;
	OutDecision.AcceptanceRadius = 40.f;
	OutDecision.Reason = FString::Printf(TEXT("наступление к укрытию: %s"), *Details.Describe());
	return BasePriority;
}

// --- UAIEval_CloseDistance ----------------------------------------------------

UAIEval_CloseDistance::UAIEval_CloseDistance()
{
	BasePriority = 20.f;
	DebugName = TEXT("CloseDistance");
}

bool UAIEval_CloseDistance::IsApplicable(const FAIDecisionContext& Context) const
{
	return Context.PrimaryThreat && Context.Unit && Context.ActionPointsLeft >= 1;
}

float UAIEval_CloseDistance::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	OutDecision.Kind = EAIActionKind::Move;
	OutDecision.Destination = Context.PrimaryThreat->GetActorLocation();
	OutDecision.bIsCoverManeuver = false; // не манёвр: bCoverMoveDoneThisTurn не взводится
	OutDecision.AcceptanceRadius = Context.Unit->AttackRange * 0.8f;
	OutDecision.Reason = TEXT("сближение (укрытий по пути нет)");
	return BasePriority;
}
