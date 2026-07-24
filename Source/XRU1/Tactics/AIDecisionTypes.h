#pragma once

#include "CoreMinimal.h"
#include "AIDecisionTypes.generated.h"

class AActor;
class AUnitBase;
class AUnitAIController;

/**
 * Что именно AI решил сделать в эту активацию. Одна активация = одно действие
 * (правило XCOM: дерево гоняется заново на каждое очко действия, а не «я в
 * состоянии Flanking, значит и дальше фланкирую»).
 * См. docs/12_AI_REFERENCE.md §I.5 и docs/13_AI_STATE_MACHINE_PLAN.md ADR-1.
 */
UENUM(BlueprintType)
enum class EAIActionKind : uint8
{
	/** Делать нечего — активация завершается. Терминальный фолбэк, всегда доступен. */
	Skip      UMETA(DisplayName = "Skip"),
	/** Выстрел по Target с текущей позиции. */
	Shoot     UMETA(DisplayName = "Shoot"),
	/** Перемещение в Destination. */
	Move      UMETA(DisplayName = "Move"),
	/** Встать в режим наблюдения (реализуется в фазе W2). */
	Overwatch UMETA(DisplayName = "Overwatch"),
	/** Глухая оборона (реализуется в фазе W2). */
	Hunker    UMETA(DisplayName = "Hunker")
};

/**
 * РЕШЕНИЕ AI — что делать и с чем. Заполняется оценщиком, исполняется отдельно
 * (AUnitAIController::ExecuteDecision). Разделение «решил» и «сделал» нужно,
 * чтобы решение можно было залогировать и показать в отладке ДО того, как оно
 * изменит мир, — без этого утилити-архитектура неотлаживаема (ADR-1.6).
 */
USTRUCT(BlueprintType)
struct FAIDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tactics|AI")
	EAIActionKind Kind = EAIActionKind::Skip;

	/** Цель действия (для Shoot). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|AI")
	TObjectPtr<AActor> Target = nullptr;

	/** Точка назначения (для Move). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|AI")
	FVector Destination = FVector::ZeroVector;

	/** Итоговый скор победившего варианта (для лога и отладки). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|AI")
	float Score = 0.f;

	/**
	 * Перемещение — это МАНЁВР в укрытие (один выбор на ход, продолжается на
	 * следующем шаге через PendingManeuverPoint), а не простое сближение.
	 * Различие важно: манёвр взводит bCoverMoveDoneThisTurn, сближение — нет.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|AI")
	bool bIsCoverManeuver = false;

	/** Радиус приёмки для Move (у манёвра он жёсткий, у сближения — широкий). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|AI")
	float AcceptanceRadius = 40.f;

	/**
	 * Человекочитаемая причина для `xru1.AI.LogCombat`. На логику не влияет
	 * никогда — только вывод. Намеренно не UPROPERTY: в сейв не идёт.
	 */
	FString Reason;
};

/**
 * ЧТО ИМЕННО ДАЁТ выбранная точка — «осознанность» укрытия.
 *
 * Юнит не должен выбирать позицию «по среднему баллу»: в момент выбора он обязан
 * знать, ОТ КОГО он там закрыт и КОГО оттуда простреливает. Это же и позволяет
 * объяснить решение в логе, а не гадать по числу.
 *
 * Аналог в XCOM — поля `ai_tile_score`: `fCoverValue` считается по каждому врагу
 * (сколько флангуют / half / full), а `fEnemyVisibility` — сколько врагов видно
 * с этой клетки.
 */
USTRUCT()
struct FAICoverPointResult
{
	GENERATED_BODY()

	/** Итоговая точка (уже с учётом прилипания к стене). */
	FVector Point = FVector::ZeroVector;

	/** От скольких угроз точка закрыта (укрытие работает против них). */
	int32 ThreatsCovered = 0;

	/** Скольким угрозам точка открыта — именно они и будут стрелять. */
	int32 ThreatsExposed = 0;

	/** Скольких угроз оттуда видно (можно ответить). */
	int32 ThreatsVisible = 0;

	/** Скольких угроз оттуда фланкируем. */
	int32 ThreatsFlanked = 0;

	/** Итоговый скор точки. */
	float Score = 0.f;

	/** Короткое человекочитаемое пояснение для лога. */
	FString Describe() const
	{
		return FString::Printf(TEXT("закрыт от %d/%d, вижу %d, фланкую %d"),
			ThreatsCovered, ThreatsCovered + ThreatsExposed, ThreatsVisible, ThreatsFlanked);
	}
};

/**
 * СНИМОК МИРА на одну активацию юнита. Считается ОДИН раз и передаётся всем
 * оценщикам — иначе каждый заново собирал бы списки видимых врагов и перебор
 * стал бы квадратичным (инвариант 1b в docs/13_AI_STATE_MACHINE_PLAN.md).
 *
 * Все поля — только чтение для оценщиков. Оценщик, который что-то здесь меняет,
 * ломает воспроизводимость решения.
 */
USTRUCT()
struct FAIDecisionContext
{
	GENERATED_BODY()

	/** Кто решает. */
	UPROPERTY()
	TObjectPtr<AUnitBase> Unit = nullptr;

	/** Его контроллер — оценщикам нужен для поиска позиций и навигации. */
	UPROPERTY()
	TObjectPtr<AUnitAIController> Controller = nullptr;

	/**
	 * Главная угроза: провоцирующий (танк) в радиусе провокации, иначе
	 * ближайший видимый. В фазе A3 выбор заменится аддитивным скорингом.
	 */
	UPROPERTY()
	TObjectPtr<AActor> PrimaryThreat = nullptr;

	/** Все живые видимые враги — нужны для скоринга укрытия против ВСЕХ (фаза A5). */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> VisibleThreats;

	/** Остаток очков действия на момент решения. */
	int32 ActionPointsLeft = 0;

	/** Юнит открыт против PrimaryThreat (укрытие против него не работает). */
	bool bExposed = false;

	/** Юнит может выстрелить по PrimaryThreat прямо сейчас (общий предикат с игроком). */
	bool bCanShootNow = false;

	/** HP ниже порога отступления (RetreatHealthFraction). */
	bool bLowHealth = false;

	/** Манёвр в укрытие в этом ходу уже выбран (один выбор на ход, XCOM). */
	bool bCoverMoveDoneThisTurn = false;
};
