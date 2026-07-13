// Copyright Epic Games, Inc. All Rights Reserved.

#include "DialogueGameplayTags.h"

namespace DialogueGameplayTags
{
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Dialogue_Event, "Dialogue.Event", "Родительский канал событий диалога.");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Dialogue_Event_Choice, "Dialogue.Event.Choice", "Продвижение диалога: выбор варианта или продолжение реплики.");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Dialogue_Event_Started, "Dialogue.Event.Started", "Диалог начался.");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(Dialogue_Event_Ended, "Dialogue.Event.Ended", "Диалог завершился.");
}
