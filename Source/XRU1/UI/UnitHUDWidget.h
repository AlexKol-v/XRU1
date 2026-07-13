#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "UnitHUDWidget.generated.h"

class UAbilitySystemComponent;
class UHorizontalBox;
class UUnitAttributeWidget;
class UUnitHUDLayoutData;
class UVerticalBox;

/**
 * Контейнерный CommonUI-виджет HUD'а юнита. Принимает UUnitHUDLayoutData
 * и UAbilitySystemComponent, строит UVerticalBox из UHorizontalBox'ов
 * (по одному на строку), вкладывает в них «детей» UUnitAttributeWidget
 * по убыванию (V, H) индексов.
 *
 * Сборка происходит однократно в InitFromLayout(), после чего виджет живёт
 * пассивно — каждый ребёнок самостоятельно подписан на ASC и обновляется
 * по событиям своих атрибутов.
 */
UCLASS(BlueprintType)
class XRU1_API UUnitHUDWidget : public UCommonUserWidget
{
    GENERATED_BODY()

public:
    /**
     * Строит дерево виджетов по DataAsset'у и подключает каждого ребёнка к ASC.
     * Безопасно вызывать несколько раз: предыдущий набор детей пересобирается с нуля.
     */
    UFUNCTION(BlueprintCallable, Category = "UI|Unit HUD")
    void InitFromLayout(UUnitHUDLayoutData* Layout, UAbilitySystemComponent* InASC);

protected:
    virtual void NativeOnInitialized() override;

    /** Корневой VBox — строки сверху вниз. */
    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> RootVBox;

    /** Все созданные ребёнки в порядке отрисовки. Хранятся, чтобы GC их не съел. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UUnitAttributeWidget>> ChildWidgets;
};
