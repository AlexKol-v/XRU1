#pragma once

#include "CoreMinimal.h"
#include "AIActionEvaluator.h"
#include "AIActionEvaluators.generated.h"

/**
 * Стандартный набор оценщиков вражеского AI.
 *
 * ⚠️ Фаза A9 — это КАРКАС, а не смена поведения. Базовые приоритеты подобраны
 * так, чтобы вместе с отсечением по верхней границе воспроизвести прежний
 * приоритетный список StepCombat ОДИН В ОДИН:
 *
 *   Retreat 100 > MoveToCover 80 > Shoot 60 > AdvanceToCover 40 > CloseDistance 20
 *
 * Пока каждый оценщик возвращает свой BasePriority целиком, набор ведёт себя
 * как «первое подходящее правило» — то есть ровно как раньше, и по числу
 * дорогих запросов тоже (отсечение обрывает перебор на первом сработавшем).
 *
 * Настоящий утилити-скоринг (шанс попадания, укрытие против всех угроз, фланг,
 * высота, кучность) въезжает в фазах A3/A5: они меняют ТЕЛА ScoreAction, не
 * трогая ни каркас, ни друг друга.
 */

/**
 * ОТСТУПЛЕНИЕ: мало HP и стоим открытыми — уходим в укрытие подальше от угрозы.
 * Бюджет — все оставшиеся AP: выживание важнее выстрела.
 */
UCLASS()
class XRU1_API UAIEval_Retreat : public UAIActionEvaluator
{
	GENERATED_BODY()
public:
	UAIEval_Retreat();
	virtual bool IsApplicable(const FAIDecisionContext& Context) const override;
	virtual float ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const override;
};

/**
 * МАНЁВР В УКРЫТИЕ С ЛИНИЕЙ ОГНЯ на 1 AP — выстрел вторым AP (XCOM).
 * Нужно 2 AP и смысл лишь если открыт или отсюда не выстрелить.
 */
UCLASS()
class XRU1_API UAIEval_MoveToCover : public UAIActionEvaluator
{
	GENERATED_BODY()
public:
	UAIEval_MoveToCover();
	virtual bool IsApplicable(const FAIDecisionContext& Context) const override;
	virtual float ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const override;
};

/**
 * ВЫСТРЕЛ с текущей позиции. Право на выстрел даёт ОБЩИЙ с игроком предикат
 * (UGA_Attack::CanTargetActor), поэтому AI не стреляет там, где HUD игрока
 * показал бы «нет линии».
 */
UCLASS()
class XRU1_API UAIEval_Shoot : public UAIActionEvaluator
{
	GENERATED_BODY()
public:
	UAIEval_Shoot();
	virtual bool IsApplicable(const FAIDecisionContext& Context) const override;
	virtual float ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const override;
};

/**
 * НАСТУПЛЕНИЕ К УКРЫТИЮ: стрелять нельзя — сближаемся, но заканчиваем ход в
 * укрытии, а не открытой пробежкой (XCOM: наступать от укрытия к укрытию).
 */
UCLASS()
class XRU1_API UAIEval_AdvanceToCover : public UAIActionEvaluator
{
	GENERATED_BODY()
public:
	UAIEval_AdvanceToCover();
	virtual bool IsApplicable(const FAIDecisionContext& Context) const override;
	virtual float ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const override;
};

/**
 * СБЛИЖЕНИЕ (последний резерв): укрытий по пути нет — идём прямо к цели.
 * Единственный оценщик без дорогих запросов — он и страхует от «ход завис».
 */
UCLASS()
class XRU1_API UAIEval_CloseDistance : public UAIActionEvaluator
{
	GENERATED_BODY()
public:
	UAIEval_CloseDistance();
	virtual bool IsApplicable(const FAIDecisionContext& Context) const override;
	virtual float ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const override;
};
