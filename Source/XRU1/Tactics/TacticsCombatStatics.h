#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TacticsCombatStatics.generated.h"

class UGameplayEffect;
class AUnitBase;

/**
 * Общие боевые расчёты выстрела: шанс попадания с учётом укрытия цели
 * (относительно стрелка, как в XCOM), глухой обороны и применение урона через
 * GAS. Используются реакцией Overwatch, вражеским AI и способностями атаки.
 */
UCLASS()
class XRU1_API UTacticsCombatStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Жив ли юнит (Health > 0). Тяжело раненые и мёртвые — «не живы». */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsUnitAlive(const AActor* Unit);

	/** Эвакуирован ли юнит (покинул поле живым). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsUnitEvacuated(const AActor* Unit);

	/** Тяжело ранен (Downed): лежит, ждёт медика. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsUnitDowned(const AActor* Unit);

	/** Враждебны ли акторы друг другу (по IGenericTeamAgentInterface / TeamManager). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool AreHostile(const AActor* A, const AActor* B);

	/**
	 * Итоговый шанс попадания (0..100): BaseHitChance минус защита укрытия цели
	 * ПРОТИВ этого стрелка (при глухой обороне цели — удвоенная). Зажат [5..95].
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static float ComputeHitChance(const AActor* Shooter, const AActor* Target, float BaseHitChance);

	/**
	 * Разыгрывает выстрел: бросок на попадание против укрытия цели; при попадании
	 * применяет DamageEffectClass (SetByCaller Data.Damage = -Damage ± 10%) к ASC
	 * цели, оповещает врагов поблизости шумом (NotifyCombatNoise) и просит
	 * TurnManager проверить конец боя. BaseHitChance 100/0 — скриптовые выстрелы
	 * туториала. Возвращает true при попадании.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat")
	static bool ResolveShot(AActor* Shooter, AActor* Target, float BaseHitChance, float Damage,
		TSubclassOf<UGameplayEffect> DamageEffectClass);

	/**
	 * Дальность визуального контакта отряда (см): на неё смотрят Squadsight
	 * снайпера и «видит ли отряд действующего врага» для камеры. Один порог на
	 * оба случая — иначе камера и правила стрельбы расходятся.
	 */
	static constexpr float SquadVisionRange = 2500.f;

	/** Есть ли прямая линия видимости между юнитами (трейс по Visibility). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool HasLineOfSight(const AActor* Viewer, const AActor* Target);

	/** Видит ли цель ХОТЬ ОДИН живой союзник юнита (для Squadsight снайпера). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool SquadHasLineOfSight(const AActor* Unit, const AActor* Target);

	/**
	 * Шум боя в точке: враги источника в радиусе Radius переходят в режим
	 * разведки (Investigate, идут на звук) — XCOM-жёлтая тревога от выстрелов.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat")
	static void NotifyCombatNoise(AActor* Instigator, const FVector& Location, float Radius = 2000.f);

	/**
	 * Точка на пути по навмешу от Start до Goal, не дальше PathBudget по длине
	 * пути (обрезка хода юнита бюджетом AP). Путь дополнительно УСЕКАЕТСЯ там,
	 * где бегущий не протиснется мимо стоящего юнита (навмеш о них не знает) —
	 * не отказ, а «дойти насколько можно»: иначе AI впустую пропускал бы ход,
	 * упёршись в союзника. false — путь вообще не построился.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat", meta = (WorldContext = "WorldContextObject"))
	static bool GetPointAlongPathBudget(UObject* WorldContextObject, const AActor* Mover, const FVector& Start,
		const FVector& Goal, float PathBudget, FVector& OutPoint);

	/**
	 * Длина пути по навмешу между точками (< 0 — путь не построился).
	 * ЧИСТАЯ навигационная метрика без учёта юнитов: достижимость с учётом
	 * занятости — задача поля дистанций (AMoveRangeVisualizer), см. §2.4
	 * 03_CODE_OVERVIEW. Используется там, где занятость уже проверена отдельно.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat", meta = (WorldContext = "WorldContextObject"))
	static float GetNavPathLength(UObject* WorldContextObject, const FVector& Start, const FVector& Goal);

	/**
	 * ЕДИНСТВЕННЫЙ источник правила стоимости перемещения (GDD §5.3):
	 * путь ≤ MoveRange — 1 AP, ≤ 2×MoveRange — 2 AP, дальше — нельзя.
	 * Возвращает 0, если приказ неоплатен (не хватает AP или слишком далеко).
	 * Зона хода, превью пути и валидация клика обязаны звать ЭТО, иначе
	 * пороги разъезжаются и «зона показывает одно, клик делает другое».
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static int32 GetMoveCostForDistance(const AUnitBase* Unit, float PathLength, int32 AvailableActionPoints);

	// --- Занятость: юниты НЕ мутируют навмеш (XCOM-подход) --------------------
	//
	// Навмеш статичен; «нельзя встать в юнита / зона огибает юнитов» решается
	// на уровне ЗАПРОСОВ дисками занятости. Никаких асинхронных перестроек
	// тайлов — зона хода и валидация приказов считаются синхронно и точно.

	/**
	 * Радиус «занятой клетки» вокруг юнита (см) — аналог занятого тайла XCOM.
	 * ВНУТРИ него нельзя ЗАКОНЧИТЬ перемещение (валидация цели приказа, волна
	 * зоны хода). Для «пробежать МИМО» нужен больший просвет — см.
	 * GetTransitClearance: тело бегущего тоже занимает место.
	 */
	static constexpr float UnitObstacleRadius = 60.f;

	/**
	 * Просвет ЦЕНТР-В-ЦЕНТР, нужный, чтобы Mover протиснулся мимо стоящего
	 * юнита: занятая клетка + собственный радиус капсулы бегущего (берётся с
	 * актора, а не константой — BP может переопределить капсулу). «Встать
	 * рядом» можно ближе, чем «пробежать мимо» — это разные правила.
	 */
	static float GetTransitClearance(const AActor* Mover);

	/**
	 * Позиции дисков занятости: живые неэвакуированные юниты обеих сторон,
	 * кроме Ignored (сам ходящий) и кроме БЕГУЩИХ (их позиция переходная —
	 * диск встанет по финишу, AIController дёрнет перестройку зоны).
	 */
	static void GetUnitObstacles(UWorld* World, const AActor* Ignored, TArray<FVector>& OutPositions);

	/**
	 * Точка занята чужим юнитом? (в пределах UnitObstacleRadius от диска).
	 * Для валидации точки приказа и превью пути.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat", meta = (WorldContext = "WorldContextObject"))
	static bool IsLocationBlockedByUnit(UObject* WorldContextObject, const FVector& Location, const AActor* Ignored);

	/**
	 * Длина вдоль пути до первого места, где просвет между полилинией и диском
	 * занятости меньше Clearance (бегущий там не протиснется). < 0 — путь чист
	 * целиком. Obstacles передаются ГОТОВЫМИ: в горячих циклах (уточнение поля
	 * зоны хода) сбор дисков делается один раз снаружи, а не на каждый вызов.
	 *
	 * ВАЖНО: «путь задевает юнита» != «дойти нельзя» — навмеш строит прямую и в
	 * чистом поле пройдёт сквозь одиночного бойца, которого Detour Crowd спокойно
	 * обходит на бегу. Ответ на «достижимо ли» даёт ВОЛНА поля дистанций
	 * (она знает про обходы), а это — метрика «докуда точно дойдём по прямой».
	 */
	static double FindPathClearanceLimit(const TArray<FVector>& PathPoints,
		const TArray<FVector>& Obstacles, float Clearance);

	/**
	 * Выталкивает цель перемещения из диска занятости на его край (клик «в»
	 * стоящего юнита или впритык). false — вытолкнуть некуда (цель остаётся
	 * заблокированной), приказ стоит отклонить.
	 */
	static bool AdjustGoalOutOfUnits(UWorld* World, const AActor* Mover, FVector& InOutGoal);
};
