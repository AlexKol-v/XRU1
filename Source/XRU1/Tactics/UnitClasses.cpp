#include "UnitClasses.h"

// Дефолты статов — по GDD §7 (HP задаётся атрибутами/StartupEffects в BP).
// Юниты игрока при 0 HP падают ранеными, а не умирают.

AUnit_Assault::AUnit_Assault()
{
	UnitRole = EUnitRole::Assault;
	BaseAim = 75.f;
	ShotDamage = 25.f;
	AttackRange = 1100.f;
	bCanBeDowned = true;
}

AUnit_Sniper::AUnit_Sniper()
{
	UnitRole = EUnitRole::Sniper;
	BaseAim = 85.f;
	ShotDamage = 40.f;
	AttackRange = 2000.f;
	bHasSquadsight = true; // «Прицел отряда» — пассивка класса
	bCanBeDowned = true;
}

AUnit_Healer::AUnit_Healer()
{
	UnitRole = EUnitRole::Healer;
	BaseAim = 70.f;
	ShotDamage = 25.f;
	AttackRange = 1100.f;
	bCanBeDowned = true;
}

AUnit_Tank::AUnit_Tank()
{
	UnitRole = EUnitRole::Tank;
	BaseAim = 70.f;
	ShotDamage = 25.f;
	AttackRange = 900.f;
	bCanBeDowned = true;
}
