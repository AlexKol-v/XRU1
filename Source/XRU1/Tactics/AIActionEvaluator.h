#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Class.h" // GetClass()->GetFName() в GetDebugName
#include "AIDecisionTypes.h"
#include "AIActionEvaluator.generated.h"

/**
 * ОДИН ВАРИАНТ ДЕЙСТВИЯ AI: оценивает сам себя и сам заполняет решение.
 *
 * Почему объект, а не ветка в StepCombat (ADR-1 в docs/13_AI_STATE_MACHINE_PLAN.md):
 * новое поведение (граната, перезарядка, подавление) добавляется НАСЛЕДНИКОМ и
 * записью в набор — существующие оценщики при этом не правятся вообще. В
 * стейт-машине то же изменение потребовало бы переходов из каждого состояния и
 * в каждое.
 *
 * ⚠️ ScoreAction обязана быть ЧИСТОЙ: ничего не менять в мире. Иначе результат
 * зависит от порядка вызова оценщиков и решение невоспроизводимо.
 *
 * BP-наследование сейчас не включено намеренно (обычные virtual вместо
 * BlueprintNativeEvent): нужды нет, а лишняя рефлексия — лишний риск. Когда
 * дизайнеру понадобится свой оценщик в BP — достаточно пометить два метода
 * BlueprintNativeEvent, вызовы не меняются.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType)
class XRU1_API UAIActionEvaluator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Множитель итогового скора — «характер» юнита. 0 полностью выключает
	 * вариант, не удаляя его из набора (удобно для отладки: «а если он не будет
	 * отступать?»).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|AI", meta = (ClampMin = "0"))
	float Weight = 1.f;

	/**
	 * Базовый приоритет варианта. Здесь он же — ВЕРХНЯЯ ГРАНИЦА скора (см.
	 * GetMaxPossibleScore): по ней работает отсечение перебора.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|AI")
	float BasePriority = 0.f;

	/** Имя для лога `xru1.AI.LogCombat`. Пусто — берётся имя класса. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|AI")
	FName DebugName;

	/**
	 * Дешёвый отсев ДО дорогой оценки: нет AP, нет цели, способность недоступна.
	 * Здесь запрещено делать трейсы и запросы навигации — для этого ScoreAction.
	 */
	virtual bool IsApplicable(const FAIDecisionContext& Context) const { return true; }

	/**
	 * Оценка варианта и заполнение решения. Возвращает скор ДО умножения на
	 * Weight (умножает вызывающий) либо отрицательное число — «нельзя».
	 *
	 * ⚠️ КОНТРАКТ: возвращаемое значение не должно превышать BasePriority,
	 * иначе отсечение по верхней границе начнёт терять лучшие варианты.
	 */
	virtual float ScoreAction(const FAIDecisionContext& Context, FAIDecision& OutDecision) const
	{
		return -1.f;
	}

	/**
	 * Верхняя граница скора этого варианта. Нужна для отсечения перебора: набор
	 * сортируется по ней убыванием, и как только текущий лучший скор её достиг,
	 * остальные варианты можно не считать вовсе.
	 *
	 * Это не микрооптимизация. Поиск позиции (FindCoverPoint) перебирает 48
	 * точек с трейсами LOS на каждую; без отсечения три «двигательных» оценщика
	 * утроили бы время хода врага — ровно тот риск, что отмечен в Ф5.
	 */
	virtual float GetMaxPossibleScore() const { return BasePriority * Weight; }

	/** Имя для лога. */
	FName GetDebugName() const { return DebugName.IsNone() ? GetClass()->GetFName() : DebugName; }
};
