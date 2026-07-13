// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Нативные теги системы квестов — каналы квест-событий для шины GMS.
 * Геймплейный код броадкастит сообщения FQuestEventData на этих каналах,
 * а задачи-цели State Tree на них подписаны (через раннер квеста).
 */
namespace QuestGameplayTags
{
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Quest_Event);
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Quest_Event_Kill);
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Quest_Event_ItemCollected);
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Quest_Event_Reach);
    STQUESTSYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Quest_Event_Custom);
}
