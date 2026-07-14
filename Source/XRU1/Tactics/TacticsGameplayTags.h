#pragma once

#include "NativeGameplayTags.h"

/**
 * Нативные GameplayTags тактического ядра. Объявлены в C++ (а не в
 * DefaultGameplayTags.ini), чтобы код ссылался на них напрямую, без строковых
 * литералов, а редактор видел их в выпадающих списках автоматически.
 */
namespace TacticsGameplayTags
{
	/** Юнит за половинчатым укрытием (низкая стена). Выдаётся UGE_CoverHalf. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cover_Half);

	/** Юнит за полным укрытием (высокая стена). Выдаётся UGE_CoverFull. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cover_Full);

	/** Юнит в режиме наблюдения (Overwatch) — ждёт реакционного выстрела. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Overwatch);

	/** Юнит в глухой обороне: бонус его укрытия удвоен до его следующего хода. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_HunkeredDown);

	/** Танк провоцирует: враги предпочитают его целью, входящий урон снижен. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Taunting);

	/** Юнит тяжело ранен (Downed): лежит, не действует, ждёт медика. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Downed);

	/** SetByCaller-тег величины урона для UGE_ShotDamage. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);

	/** SetByCaller-тег величины лечения для UGE_Heal. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Heal);

	/** GameplayEvent: приказ атаки (payload.Target = цель). Триггерит GA_Attack. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Attack);

	/** GameplayEvent: приказ лечения (payload.Target = союзник). Триггерит GA_Heal. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Heal);
}
