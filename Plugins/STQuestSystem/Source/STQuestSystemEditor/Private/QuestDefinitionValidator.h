// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "QuestDefinitionValidator.generated.h"

/**
 * Валидатор определений квестов. Авто-обнаруживается DataValidation на старте
 * редактора (см. EditorValidatorBase). Ловит частые ошибки авторинга: пустой
 * QuestId, отсутствующий ассет логики, тривиальные циклы цепочки.
 */
UCLASS()
class UQuestDefinitionValidator : public UEditorValidatorBase
{
    GENERATED_BODY()

protected:
    virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
    virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
