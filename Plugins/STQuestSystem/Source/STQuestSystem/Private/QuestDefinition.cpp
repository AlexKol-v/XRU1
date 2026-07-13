// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestDefinition.h"

FPrimaryAssetId UQuestDefinition::GetPrimaryAssetId() const
{
    // Тип «Quest» должен совпадать с PrimaryAssetTypesToScan в DefaultGame.ini —
    // по нему Asset Manager перечисляет все определения квестов для реестра.
    return FPrimaryAssetId(FPrimaryAssetType(TEXT("Quest")), QuestId.GetTagName());
}
