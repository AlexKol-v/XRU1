#pragma once

#include "CoreMinimal.h"
#include "AIActionEvaluator.h"
#include "AIActionEvaluators.generated.h"

/**
 * Стандартный набор оценщиков вражеского AI.
 *
 * Базовые потолки задают порядок дешёвого отсечения, но итоговый выбор уже
 * утилитарный: Shoot масштабируется качеством выстрела, а позиционные действия
 * проходят собственный поиск и могут отказаться:
 *
 *   Retreat 120 > Shoot 100 > Hunker 85 > MoveToCover 80
 *   > Overwatch 45 > AdvanceToCover 40 > CloseDistance 20
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

	/**
	 * Надбавка к скору, когда боец СТОИТ ОТКРЫТЫМ против главной угрозы.
	 *
	 * `BasePriority` (80) заведомо ниже уверенного выстрела (`UAIEval_Shoot`,
	 * до 100), поэтому без надбавки открытый бот с двумя ОД всегда стрелял с
	 * места и оставался в поле до конца хода противника (выстрел сжигает остаток
	 * ОД). 30 выводит манёвр на 110 — выше выстрела, но ниже отступления (120),
	 * так что раненый по-прежнему уходит, а не лезет в перестрелку.
	 *
	 * 0 — вернуть прежнее поведение «стреляй, если можешь, откуда стоишь».
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluator|MoveToCover", meta = (ClampMin = "0"))
	float ExposedUrgencyBonus = 30.f;

	virtual bool IsApplicable(const FAIDecisionContext& Context) const override;
	virtual float ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const override;

	/** Потолок обязан включать надбавку, иначе отсечение перебора срежет победителя. */
	virtual float GetMaxPossibleScore() const override
	{
		return (BasePriority + ExposedUrgencyBonus) * Weight;
	}
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

	/**
	 * AI-4: предлагает выстрел по КАЖДОЙ доступной цели, а не только по
	 * `PrimaryThreat`. Это ровно тот случай, ради которого заведены полные
	 * предложения, и он дешёвый: цена варианта — один расчёт шанса попадания.
	 *
	 * Итог сравнивается общей шкалой вместе с манёврами, поэтому «выстрелить в
	 * менее приоритетного, но доступного» перестал быть невыразимым решением.
	 */
	virtual void ProposeActions(const FAIDecisionContext& Context,
		TArray<FAIDecision>& OutProposals) const override;

private:
	/** Общая формула качества выстрела: скор и текст причины по одной цели. */
	float ScoreShotAt(const FAIDecisionContext& Context, AActor* Target,
		FAIDecision& OutDecision) const;
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
 * ГЛУХАЯ ОБОРОНА (A7/W2). Мало HP, юнит В УКРЫТИИ против главной угрозы, но
 * ответить не может — вжаться и удвоить укрытие вместо бессмысленной перебежки
 * через простреливаемое место.
 *
 * ⚠️ Стоит ВЫШЕ манёвра (85 против 80) и НИЖЕ отступления (120) намеренно:
 * пока укрытие работает, сидеть в нём выгоднее, чем бежать; но если HP совсем
 * мало и юнит открыт — приоритет у отхода.
 *
 * ⚠️ Проверяет ДВА условия укрытия (см. .cpp): «работает против главной угрозы»
 * — это тактический смысл — и `BestCoverAround != None`, ровно то, что требует
 * `UGA_HunkerDown::CanActivateAbility`. Иначе бот изредка выбирал бы действие,
 * которое способность тут же отклонит, и сжигал бы активацию впустую.
 */
UCLASS()
class XRU1_API UAIEval_HunkerDown : public UAIActionEvaluator
{
	GENERATED_BODY()
public:
	UAIEval_HunkerDown();
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

/**
 * НАБЛЮДЕНИЕ (A7/W2). Врага видно, но выстрел невозможен (нет линии огня/
 * дальности) либо лимит атакующих запретил атаку — удерживать позицию вместо
 * бессмысленной пробежки под ответ.
 *
 * ⚠️ **Требует W1.** До неё овервотч бота не выстрелил бы НИ РАЗУ: реакция
 * висела на «врага увидел», а не на «видимый враг сдвинулся».
 *
 * ⚠️ **Рандомизация обязательна** (`ActivationChance`, XCOM `RandSelector`).
 * Детерминированный овервотч читается наизусть: игрок один раз выясняет, что
 * враг без линии огня ВСЕГДА встаёт в наблюдение, и дальше просто не входит в
 * его сектор. Розыгрыш делается ОДИН раз за активацию по стабильному зерну
 * (см. .cpp) — `ScoreAction` обязана быть чистой и воспроизводимой.
 *
 * ДВА основания встать в наблюдение, и они дают РАЗНЫЙ скор:
 *  - «не могу стрелять» (нет линии огня) → `IdleOverwatchScore` = 30, ниже
 *    наступления к укрытию (40): сначала пытаемся занять позицию для выстрела;
 *    плюс розыгрыш `ActivationChance`;
 *  - «не разрешено стрелять» (A8, лимит атакующих исчерпан) → полный
 *    `BasePriority` = 45, БЕЗ розыгрыша: юнит обязан удерживать позицию, иначе
 *    лимит атакующих превратился бы в «беги на игрока безоружным».
 */
UCLASS()
class XRU1_API UAIEval_Overwatch : public UAIActionEvaluator
{
	GENERATED_BODY()
public:
	UAIEval_Overwatch();
	virtual bool IsApplicable(const FAIDecisionContext& Context) const override;
	virtual float ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const override;
	virtual float GetMaxPossibleScore() const override
	{
		return FMath::Max(BasePriority, IdleOverwatchScore) * Weight;
	}

	/**
	 * Вероятность встать в наблюдение, когда стрелять НЕ ПО КОМУ (0..1). 0 —
	 * поведение выключено, 1 — детерминированный овервотч. На занятую ветку A8
	 * не влияет: там наблюдение обязательно.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluator", meta = (ClampMin = "0", ClampMax = "1"))
	float ActivationChance = 0.33f;

	/** Скор наблюдения в обычном случае «стрелять не по кому». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evaluator", meta = (ClampMin = "0"))
	float IdleOverwatchScore = 30.f;
};
