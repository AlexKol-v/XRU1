#pragma once

#include "CoreMinimal.h"
#include "UnitBase.h"
#include "UnitClasses.generated.h"

/**
 * Фиксированный ростер из 4 классов (без кастомизации экипировки по ТЗ).
 * Каждый класс — тонкий Blueprintable-наследник AUnitBase. Нативный
 * конструктор задаёт роль, имя, базовые статы и классовую способность по GDD;
 * BP-наследник может переопределить defaults, а StartupEffects — добавить
 * ситуативные модификаторы поверх уже инициализированных GAS-атрибутов.
 */

/** Штурмовик: ближний натиск, мобильность. */
UCLASS(Blueprintable)
class XRU1_API AUnit_Assault : public AUnitBase
{
	GENERATED_BODY()
public:
	AUnit_Assault();
};

/** Снайпер: дальний прицельный урон. */
UCLASS(Blueprintable)
class XRU1_API AUnit_Sniper : public AUnitBase
{
	GENERATED_BODY()
public:
	AUnit_Sniper();
};

/** Хилер: поддержка и лечение отряда. */
UCLASS(Blueprintable)
class XRU1_API AUnit_Healer : public AUnitBase
{
	GENERATED_BODY()
public:
	AUnit_Healer();
};

/** Танк: высокая живучесть, стягивание урона. */
UCLASS(Blueprintable)
class XRU1_API AUnit_Tank : public AUnitBase
{
	GENERATED_BODY()
public:
	AUnit_Tank();
};
