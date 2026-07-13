// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestDefinitionFactory.h"

#include "AssetTypeCategories.h"
#include "QuestDefinition.h"

UQuestDefinitionFactory::UQuestDefinitionFactory()
{
    SupportedClass = UQuestDefinition::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}

UObject* UQuestDefinitionFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName,
    EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    return NewObject<UQuestDefinition>(InParent, InClass, InName, Flags);
}

uint32 UQuestDefinitionFactory::GetMenuCategories() const
{
    return EAssetTypeCategories::Gameplay;
}

FText UQuestDefinitionFactory::GetDisplayName() const
{
    return NSLOCTEXT("QuestDefinitionFactory", "QuestDefinitionDisplayName", "Quest Definition");
}
