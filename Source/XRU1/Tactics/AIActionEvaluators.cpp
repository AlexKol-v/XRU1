#include "AIActionEvaluators.h"
#include "UnitAIController.h"
#include "UnitBase.h"
#include "GA_Attack.h"
#include "CoverDetectionComponent.h" // BestCoverAround — предусловие глухой обороны
#include "TacticsGameplayTags.h"     // State.Taunting — hard contract провокации
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Misc/Crc.h"
#include "Math/RandomStream.h"

// --- UAIEval_Retreat ----------------------------------------------------------

UAIEval_Retreat::UAIEval_Retreat()
{
	BasePriority = 120.f;
	DebugName = TEXT("Retreat");
}

bool UAIEval_Retreat::IsApplicable(const FAIDecisionContext& Context) const
{
	// Условия — те же, что в StepCombat: не двигались в укрытие в этом
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

	// ОТКРЫТЫЙ БОЕЦ СНАЧАЛА ВСТАЁТ В УКРЫТИЕ, а стреляет вторым очком.
	//
	// Без этой надбавки уверенный выстрел (Shoot: 100 × качество) всегда бил
	// манёвр (80), и бот с двумя ОД оставался стрелять из чистого поля
	// («стоят и стреляют»). Ошибка усиливается тем, что у
	// нас `GA_Attack.bConsumesAllRemainingAP = true`: выстрел с места сжигает и
	// второе очко, то есть выбор здесь не «сначала стрельнуть, потом уйти», а
	// строго «или укрытие, или открытая позиция до конца хода противника».
	//
	// Надбавка даётся ТОЛЬКО когда боец реально открыт против главной угрозы.
	// В рабочем укрытии Shoot по-прежнему выигрывает — это правило XCOM
	// «стреляй, если можешь», и ломать его незачем.
	const float Urgency = Context.bExposed ? ExposedUrgencyBonus : 0.f;
	OutDecision.Reason = FString::Printf(TEXT("укрытие с линией огня (выстрел вторым AP)%s: %s"),
		Context.bExposed ? TEXT(", боец открыт") : TEXT(""), *Details.Describe());
	return BasePriority + Urgency;
}

// --- UAIEval_Shoot ------------------------------------------------------------

UAIEval_Shoot::UAIEval_Shoot()
{
	// ⚠️ ВЫШЕ, чем у манёвра в укрытие (80). Так и должно быть: уверенный
	// выстрел важнее перебежки. С приоритетом ниже манёвра бот с шансом 93%
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
	// При достигнутом лимите атакующих выстрел запрещён — юнит проваливается
	// в занятую ветку (наблюдение/перемещение), а не стоит столбом.
	return Context.bCanShootNow && !Context.bAttackThrottled &&
		Context.PrimaryThreat && Context.ActionPointsLeft >= 1;
}

float UAIEval_Shoot::ScoreShotAt(const FAIDecisionContext& Context, AActor* Target,
	FAIDecision& OutDecision) const
{
	const float HitChance = UGA_Attack::ComputeAttackHitChance(Context.Unit, Target);

	// Качество выстрела по тем же порогам, что и скоринг цели: уверенный —
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
	OutDecision.Target = Target;
	OutDecision.Reason = FString::Printf(TEXT("выстрел по %s (шанс %.0f%%)"),
		*GetNameSafe(Target), HitChance);
	return BasePriority * Quality;
}

float UAIEval_Shoot::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	return ScoreShotAt(Context, Context.PrimaryThreat, OutDecision);
}

void UAIEval_Shoot::ProposeActions(const FAIDecisionContext& Context,
	TArray<FAIDecision>& OutProposals) const
{
	// Главная угроза идёт первой и всегда: она уже прошла полный аддитивный
	// скоринг цели (провокация, фланг, добивание), и её приоритет — контракт GDD.
	FAIDecision Primary;
	const float PrimaryScore = ScoreShotAt(Context, Context.PrimaryThreat, Primary);
	if (PrimaryScore > 0.f)
	{
		Primary.Score = PrimaryScore;
		OutProposals.Add(Primary);
	}

	// ⚠️ TAUNT — ЖЁСТКИЙ КОНТРАКТ (GDD §7). Если главной целью стал ДОСТУПНЫЙ
	// провоцирующий в радиусе провокации, альтернативы не предлагаются вовсе:
	// «обязан бить именно его» не должно вырождаться в «сильно предпочитает».
	if (PrimaryScore > 0.f && Context.Unit && Context.Controller && Context.PrimaryThreat)
	{
		if (const UAbilitySystemComponent* ASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Context.PrimaryThreat))
		{
			const float TauntDist = FVector::Dist(
				Context.Unit->GetActorLocation(), Context.PrimaryThreat->GetActorLocation());
			if (ASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Taunting) &&
				TauntDist <= Context.Controller->TauntPriorityRadius)
			{
				return;
			}
		}
	}

	// Остальные видимые угрозы — как отдельные предложения. Дороже одного
	// расчёта шанса попадания на цель это не стоит.
	for (const TObjectPtr<AActor>& Threat : Context.VisibleThreats)
	{
		AActor* Candidate = Threat.Get();
		if (!Candidate || Candidate == Context.PrimaryThreat)
		{
			continue;
		}
		if (!UGA_Attack::CanTargetActor(Context.Unit, Candidate))
		{
			continue; // тот же предикат, что решает выстрел игрока
		}
		FAIDecision Alternative;
		const float Score = ScoreShotAt(Context, Candidate, Alternative);
		if (Score > 0.f)
		{
			Alternative.Score = Score;
			OutProposals.Add(MoveTemp(Alternative));
		}
	}
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

// --- UAIEval_HunkerDown -------------------------------------------------------

UAIEval_HunkerDown::UAIEval_HunkerDown()
{
	// Выше манёвра (80): пока укрытие РАБОТАЕТ, вжаться в него выгоднее, чем
	// перебегать под ответным огнём. Ниже отступления (120): если юнит открыт,
	// удваивать нечего и надо уходить.
	BasePriority = 85.f;
	DebugName = TEXT("HunkerDown");
}

bool UAIEval_HunkerDown::IsApplicable(const FAIDecisionContext& Context) const
{
	if (!Context.bLowHealth || Context.bCanShootNow || Context.bExposed ||
		Context.ActionPointsLeft < 1 || !Context.Unit || !Context.Unit->HunkerAbilityClass)
	{
		return false;
	}

	// ⚠️ ДВА РАЗНЫХ УСЛОВИЯ УКРЫТИЯ, и нужны оба.
	//  - `!bExposed` — «укрытие работает ПРОТИВ ГЛАВНОЙ УГРОЗЫ» (луч к стрелку);
	//    это тактический смысл: удваивать нечего, если конкретный враг нас
	//    и так простреливает.
	//  - `BestCoverAround != None` — ровно то, что проверяет
	//    `UGA_HunkerDown::CanActivateAbility` (через тег Cover.Half/Full).
	// Совпадают они почти всегда, но не тождественны: луч к угрозе и 8 лучей по
	// кругу — разные выборки направлений. Без второй проверки бот изредка
	// выбирал бы действие, которое способность тут же отклоняет, и сжигал
	// активацию впустую.
	const UCoverDetectionComponent* Cover = Context.Unit->GetCoverDetection();
	return Cover && Cover->BestCoverAround != ECoverType::None;
}

float UAIEval_HunkerDown::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	OutDecision.Kind = EAIActionKind::Hunker;
	OutDecision.Reason = TEXT("глухая оборона (мало HP, укрытие держит, ответить нечем)");
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
	// Есть выстрел — сближаться незачем. Правило XCOM: бегут те, кому стрелять
	// НЕЧЕМ. Без этого условия боец с целью в прицеле выбирал перебежку (20)
	// вместо выстрела (15), перебежка не строилась, и ход уходил в никуда.
	if (Context.bCanShootNow && !Context.bAttackThrottled)
	{
		return false;
	}

	return Context.PrimaryThreat && Context.Unit && Context.ActionPointsLeft >= 1;
}

float UAIEval_CloseDistance::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	// ⚠️ Цель — НЕ позиция врага, а точка на идеальной дистанции боя от него по
	// линии «враг → я». Позицию врага с радиусом приёмки
	// `AttackRange * 0.8` (≈2400 см) сюда отдавать нельзя: `MoveToLocation` с
	// таким радиусом всегда отвечает `AlreadyAtGoal`, и юнит СЖИГАЕТ очко
	// действия, не сделав ни шага.
	// Радиус приёмки обычный, а «не подходить вплотную» выражено целью.
	const FVector ThreatPos = Context.PrimaryThreat->GetActorLocation();
	FVector Away = (Context.Unit->GetActorLocation() - ThreatPos).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = Context.Unit->GetActorForwardVector().GetSafeNormal2D();
	}

	OutDecision.Kind = EAIActionKind::Move;
	OutDecision.Destination = ThreatPos + Away * FMath::Max(50.f, Context.Unit->IdealCombatRange);
	OutDecision.bIsCoverManeuver = false; // не манёвр: bCoverMoveDoneThisTurn не взводится
	OutDecision.AcceptanceRadius = 40.f;
	OutDecision.Reason = TEXT("сближение до боевой дистанции (укрытий по пути нет)");
	return BasePriority;
}

// --- UAIEval_Overwatch --------------------------------------------------------

UAIEval_Overwatch::UAIEval_Overwatch()
{
	// ПОТОЛОК 45 — выше наступления к укрытию (40), но достижим только в
	// «занятой» ветке: юнит, которому лимит атакующих ЗАПРЕТИЛ стрелять,
	// обязан удерживать позицию, а не бежать вперёд (иначе лимит превращается в
	// «беги на игрока безоружным»).
	// В обычном случае («стрелять не по кому») скор = IdleOverwatchScore = 30,
	// то есть ниже наступления: сначала пробуем занять позицию для выстрела.
	BasePriority = 45.f;
	DebugName = TEXT("Overwatch");
}

bool UAIEval_Overwatch::IsApplicable(const FAIDecisionContext& Context) const
{
	if (Context.ActionPointsLeft < 1 || Context.VisibleThreats.Num() == 0 ||
		!Context.Unit || !Context.Unit->OverwatchAbilityClass)
	{
		return false;
	}
	// Два РАЗНЫХ основания встать в наблюдение:
	//  - «не могу» (нет линии огня / вне дальности) — классический случай;
	//  - «не разрешено» (лимит атакующих) — занятая ветка XCOM.
	return !Context.bCanShootNow || Context.bAttackThrottled;
}

float UAIEval_Overwatch::ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
{
	OutDecision.Kind = EAIActionKind::Overwatch;

	// Занятая ветка лимита — БЕЗ розыгрыша и с полным приоритетом: юниту прямо
	// запретили стрелять, «иногда вместо этого побегу вперёд» здесь означало бы
	// подставиться под отряд без возможности ответить.
	if (Context.bAttackThrottled)
	{
		OutDecision.Reason = TEXT("наблюдение (лимит атакующих исчерпан — держим позицию)");
		return BasePriority;
	}

	if (ActivationChance <= 0.f)
	{
		return -1.f;
	}

	// РОЗЫГРЫШ (аналог RandSelector XCOM). Контекст несёт стабильное зерно шага:
	// повторная оценка того же решения даёт тот же ответ независимо от FPS.
	if (ActivationChance < 1.f)
	{
		const uint32 Seed = HashCombine(Context.DecisionSeed,
			FCrc::StrCrc32(*GetClass()->GetPathName()));
		if (FRandomStream(static_cast<int32>(Seed)).GetFraction() >= ActivationChance)
		{
			OutDecision.Reason = TEXT("наблюдение — розыгрыш не выпал");
			return -1.f;
		}
	}

	OutDecision.Reason = TEXT("наблюдение (стрелять не по кому — ждём движения)");
	return IdleOverwatchScore;
}
