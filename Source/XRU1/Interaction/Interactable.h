#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UInteractable : public UInterface
{
    GENERATED_BODY()
};

class XRU1_API IInteractable
{
    GENERATED_BODY()

public:
    /** Главный «ткни меня»-метод. Реализация по умолчанию ничего не делает. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
    void Interact(AActor* Instigator);

    /** Пройдёт ли проверка «можно ли сейчас» (заперто? использовано? чужая команда?). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
    bool CanInteract(AActor* Instigator) const;

    /** Текст для UI-подсказки: «Нажмите F, чтобы открыть». */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
    FText GetInteractionPrompt() const;

    /** Объект попал под фокус детектора — обычно включаем подсветку. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
    void OnFocused(AActor* Instigator);

    /** Фокус ушёл — выключаем подсветку. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
    void OnUnfocused(AActor* Instigator);

    /** Дефолтная реализация: ничего. */
    virtual void Interact_Implementation(AActor* /*Instigator*/) {}

    /** Дефолтная реализация: разрешено всегда. Наследник с состоянием — переопределяет. */
    virtual bool CanInteract_Implementation(AActor* /*Instigator*/) const { return true; }

    /** Дефолтная подсказка — пусто. */
    virtual FText GetInteractionPrompt_Implementation() const { return FText::GetEmpty(); }

    virtual void OnFocused_Implementation(AActor* /*Instigator*/) {}
    virtual void OnUnfocused_Implementation(AActor* /*Instigator*/) {}
};
