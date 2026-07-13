// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestGameplayTags.h"

namespace QuestGameplayTags
{
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Quest_Event, "Quest.Event", "Родительский канал всех квест-событий.");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Quest_Event_Kill, "Quest.Event.Kill", "Событие убийства цели.");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Quest_Event_ItemCollected, "Quest.Event.ItemCollected", "Событие сбора предмета.");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Quest_Event_Reach, "Quest.Event.Reach", "Событие достижения точки на карте.");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Quest_Event_Custom, "Quest.Event.Custom", "Пользовательское квест-событие.");
}
