// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "QuestDefinitionFactory.generated.h"

/**
 * Фабрика ассетов UQuestDefinition. Добавляет пункт создания «Quest Definition»
 * в Content Browser и создаёт ассет определения квеста.
 */
UCLASS()
class UQuestDefinitionFactory : public UFactory
{
    GENERATED_BODY()

public:
    UQuestDefinitionFactory();

    virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName,
        EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

    /** Категория пункта в меню Content Browser → Add (иначе — Miscellaneous). */
    virtual uint32 GetMenuCategories() const override;

    /**
     * Подпись пункта в меню Add. Без override берётся имя ближайших AssetTypeActions
     * по иерархии — для наследника UPrimaryDataAsset это «Data Asset».
     */
    virtual FText GetDisplayName() const override;
};
