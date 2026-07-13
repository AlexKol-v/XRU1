#include "UnitClasses.h"

AUnit_Assault::AUnit_Assault()
{
	UnitRole = EUnitRole::Assault;
}

AUnit_Sniper::AUnit_Sniper()
{
	UnitRole = EUnitRole::Sniper;
}

AUnit_Healer::AUnit_Healer()
{
	UnitRole = EUnitRole::Healer;
}

AUnit_Tank::AUnit_Tank()
{
	UnitRole = EUnitRole::Tank;
}
