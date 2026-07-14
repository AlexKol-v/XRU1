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
	 * пути (обрезка хода юнита бюджетом AP). false — путь не построился.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat", meta = (WorldContext = "WorldContextObject"))
	static bool GetPointAlongPathBudget(UObject* WorldContextObject, const FVector& Start,
		const FVector& Goal, float PathBudget, FVector& OutPoint);

	/** Длина пути по навмешу между точками (< 0 — путь не построился). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat", meta = (WorldContext = "WorldContextObject"))
	static float GetNavPathLength(UObject* WorldContextObject, const FVector& Start, const FVector& Goal);
};
