#pragma once

#include "CoreMinimal.h"
#include "TacticsTypes.generated.h"

/**
 * Канонические GenericTeamId тактического режима. BP-юниты и GameMode должны
 * использовать те же значения; игровые проверки не должны размножать magic 1/2.
 */
namespace TacticsTeamIds
{
	inline constexpr uint8 Player = 1;
	inline constexpr uint8 Enemy = 2;
}

/** Уровень сложности, выбирается при старте новой игры. Хранится в UTacticsSaveGame. */
UENUM(BlueprintType)
enum class EDifficultyLevel : uint8
{
	Easy   UMETA(DisplayName = "Easy"),
	Medium UMETA(DisplayName = "Medium"),
	Hard   UMETA(DisplayName = "Hard")
};

/** Роль юнита в фиксированном ростере из 4 классов. */
UENUM(BlueprintType)
enum class EUnitRole : uint8
{
	Assault UMETA(DisplayName = "Assault"),
	Sniper  UMETA(DisplayName = "Sniper"),
	Healer  UMETA(DisplayName = "Healer"),
	Tank    UMETA(DisplayName = "Tank")
};

/**
 * Как боец обходит маршрут патруля из нескольких точек.
 *
 * Разница видна только на НЕЗАМКНУТОМ маршруте (точки идут линией — вдоль
 * дороги, стены, коридора). `Loop` после последней точки уводит бойца к первой,
 * то есть заставляет пробежать всю линию обратно вхолостую; `PingPong`
 * разворачивает его и ведёт назад по тем же точкам. Для кольцевого маршрута
 * (последняя точка рядом с первой) режимы эквивалентны.
 */
UENUM(BlueprintType)
enum class EPatrolRouteMode : uint8
{
	/** Кольцо: после последней точки — снова к первой. */
	Loop     UMETA(DisplayName = "Кольцо (замкнутый маршрут)"),

	/** Туда-обратно: на концах маршрута боец разворачивается. */
	PingPong UMETA(DisplayName = "Туда-обратно (незамкнутый маршрут)")
};

/** Чей сейчас ход в пошаговом бою. */
UENUM(BlueprintType)
enum class ETurnPhase : uint8
{
	None    UMETA(DisplayName = "None"),
	Player  UMETA(DisplayName = "Player Turn"),
	Enemy   UMETA(DisplayName = "Enemy Turn")
};

/** Какая контекстная интеракция (клавиша F) доступна выбранному юниту. Для кнопки HUD. */
UENUM(BlueprintType)
enum class EInteractionKind : uint8
{
	None       UMETA(DisplayName = "Недоступна"),
	DefuseBomb UMETA(DisplayName = "Обезвредить заряд"),
	Evacuate   UMETA(DisplayName = "Эвакуация")
};

/**
 * Почему юнит не может атаковать конкретную цель прямо сейчас (или может —
 * Valid). Схлопывать «слишком далеко» и «нет линии огня» в один
 * bool/-1 нельзя: HUD всегда писал бы «Нет линии огня» даже при реальной
 * причине «дальность». Детализация нужна панели цели и
 * серости AttackBtn.
 */
UENUM(BlueprintType)
enum class EAttackTargetStatus : uint8
{
	Valid          UMETA(DisplayName = "Можно стрелять"),
	NotHostile     UMETA(DisplayName = "Не враг"),
	Dead           UMETA(DisplayName = "Цель мертва"),
	OutOfRange     UMETA(DisplayName = "Слишком далеко"),
	NoLineOfSight  UMETA(DisplayName = "Нет линии огня"),
	/** Геометрия чиста, но цель дальше собственного обзора, а прицела отряда нет. */
	OutOfSight     UMETA(DisplayName = "Цель вне обзора")
};

/**
 * Форс исхода ОДНОГО выстрела для сценарного шага обучения (A4 «попадание на
 * 30», A7 «промах», B4 «урон уполовинен провокацией»).
 *
 * Это не подмена урона мимо системы: override меняет только входные числа уже
 * зафиксированного FTacticalFireActionContext, а roll, GE, HitReact, камера и
 * quest-события остаются общим attack pipeline.
 */
USTRUCT(BlueprintType)
struct FScriptedShotOverride
{
	GENERATED_BODY()

	/** Итоговый шанс попадания, %. 100 — гарантированное попадание, 0 — промах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Scripted",
		meta = (ClampMin = "0", ClampMax = "100"))
	float HitChancePercent = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Scripted")
	bool bOverrideHitChance = true;

	/** Базовый урон до укрытия/провокации. Разброс ±10% общего pipeline сохраняется. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Scripted",
		meta = (ClampMin = "0"))
	float Damage = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|Scripted")
	bool bOverrideDamage = true;

	bool IsMeaningful() const { return bOverrideHitChance || bOverrideDamage; }
};
