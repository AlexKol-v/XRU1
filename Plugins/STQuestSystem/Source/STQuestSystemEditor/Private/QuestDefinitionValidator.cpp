// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestDefinitionValidator.h"

#include "Misc/DataValidation.h"
#include "QuestDefinition.h"

#define LOCTEXT_NAMESPACE "STQuestEditor"

bool UQuestDefinitionValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const
{
    return InObject && InObject->IsA<UQuestDefinition>();
}

EDataValidationResult UQuestDefinitionValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
    const UQuestDefinition* Quest = Cast<UQuestDefinition>(InAsset);
    if (!Quest)
    {
        return EDataValidationResult::NotValidated;
    }

    bool bAnyError = false;

    if (!Quest->QuestId.IsValid())
    {
        AssetFails(InAsset, LOCTEXT("EmptyQuestId", "QuestId не задан — квест не попадёт в реестр подсистемы."));
        bAnyError = true;
    }

    if (Quest->QuestLogic.GetStateTree() == nullptr)
    {
        AssetFails(InAsset, LOCTEXT("NoLogic", "QuestLogic (ассет State Tree) не задан — квест не сможет исполняться."));
        bAnyError = true;
    }

    // Тривиальный цикл: цепочка ссылается на сам квест.
    for (const FGameplayTag& Link : Quest->ChainLinks.UnlockOnComplete)
    {
        if (Link == Quest->QuestId)
        {
            AssetFails(InAsset, LOCTEXT("SelfChainComplete", "ChainLinks.UnlockOnComplete ссылается на сам квест (цикл)."));
            bAnyError = true;
        }
    }
    for (const FGameplayTag& Link : Quest->ChainLinks.UnlockOnFail)
    {
        if (Link == Quest->QuestId)
        {
            AssetFails(InAsset, LOCTEXT("SelfChainFail", "ChainLinks.UnlockOnFail ссылается на сам квест (цикл)."));
            bAnyError = true;
        }
    }

    if (bAnyError)
    {
        return EDataValidationResult::Invalid;
    }

    AssetPasses(InAsset);
    return EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
