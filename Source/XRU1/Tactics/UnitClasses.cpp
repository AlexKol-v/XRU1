#include "UnitClasses.h"
#include "TacticalClassAbilities.h"

// Дефолты статов — по GDD §7. BaseMaxHealth до BeginPlay переносится в GAS-атрибуты.
// Юниты игрока при 0 HP падают ранеными, а не умирают.

AUnit_Assault::AUnit_Assault()
{
	UnitRole = EUnitRole::Assault;
	UnitDisplayName = NSLOCTEXT("XRU1Units", "AssaultName", "Клин");
	BaseMaxHealth = 100.f;
	BaseAim = 75.f;
	ShotDamage = 25.f;
	ClassAbilityClass = UGA_RunAndGun::StaticClass();
	// AttackRange — дефолт из AUnitBase (3000, см. комментарий там): не переопределяем.
	bCanBeDowned = true;
}

AUnit_Sniper::AUnit_Sniper()
{
	UnitRole = EUnitRole::Sniper;
	UnitDisplayName = NSLOCTEXT("XRU1Units", "SniperName", "Оса");
	BaseMaxHealth = 80.f;
	BaseAim = 85.f;
	ShotDamage = 40.f;
	AttackRange = 5000.f; // снайпер — дальше всех (GDD §7), но и это лишь технический cap
	bHasSquadsight = true; // «Прицел отряда» — пассивка класса
	ClassAbilityClass = nullptr;
	bCanBeDowned = true;
}

AUnit_Healer::AUnit_Healer()
{
	UnitRole = EUnitRole::Healer;
	UnitDisplayName = NSLOCTEXT("XRU1Units", "HealerName", "Шприц");
	BaseMaxHealth = 90.f;
	BaseAim = 70.f;
	ShotDamage = 25.f;
	ClassAbilityClass = UGA_Heal::StaticClass();
	// AttackRange — дефолт из AUnitBase (3000): не переопределяем.
	bCanBeDowned = true;
}

AUnit_Tank::AUnit_Tank()
{
	UnitRole = EUnitRole::Tank;
	UnitDisplayName = NSLOCTEXT("XRU1Units", "TankName", "Молот");
	BaseMaxHealth = 150.f;
	BaseAim = 70.f;
	ShotDamage = 25.f;
	ClassAbilityClass = UGA_Taunt::StaticClass();
	// AttackRange — дефолт из AUnitBase (3000): не переопределяем (раньше был
	// короче остальных — 900, без всякой причины короче даже AI SightRadius).
	bCanBeDowned = true;
}
