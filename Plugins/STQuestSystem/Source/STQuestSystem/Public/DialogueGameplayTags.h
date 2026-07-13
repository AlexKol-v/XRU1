// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Нативные теги диалоговой подсистемы — каналы событий продвижения диалога.
 * UI диалога продвигает реплику/выбор через UDialogueSubsystem, который
 * ретранслирует продвижение в State Tree диалога событием Dialogue.Event.Choice.
 */
namespace DialogueGameplayTags
{
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dialogue_Event);
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dialogue_Event_Choice);
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dialogue_Event_Started);
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dialogue_Event_Ended);
}
