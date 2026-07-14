#include "TacticsGameplayTags.h"

namespace TacticsGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cover_Half, "Cover.Half", "Половинчатое укрытие: частичный бонус к защите.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cover_Full, "Cover.Full", "Полное укрытие: максимальный бонус к защите.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Overwatch, "State.Overwatch", "Юнит в режиме наблюдения (Overwatch).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_HunkeredDown, "State.HunkeredDown", "Глухая оборона: бонус укрытия удвоен.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Taunting, "State.Taunting", "Провокация: приоритетная цель, урон снижен.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Downed, "State.Downed", "Тяжело ранен: не действует, ждёт медика.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage, "Data.Damage", "SetByCaller: величина урона выстрела.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Heal, "Data.Heal", "SetByCaller: величина лечения.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Attack, "Event.Attack", "Приказ атаки: активирует GA_Attack с целью.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Heal, "Event.Heal", "Приказ лечения: активирует GA_Heal с целью.");
}
