// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "QuestTypes.h"
#include "QuestNotificationWidget.generated.h"

class UQuestSubsystem;

/**
 * Виджет-тост уведомлений о событиях квестов. Подписан на делегат
 * OnQuestNotification подсистемы и показывает каждое уведомление через
 * событие разметки OnShowNotification.
 */
UCLASS()
class STQUESTSYSTEM_API UQuestNotificationWidget : public UCommonUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeDestruct() override;

    /** Реализуется в разметке: показывает тост уведомления. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Quest")
    void OnShowNotification(const FQuestNotification& Notification);

private:
    /** Возвращает подсистему квестов или nullptr. */
    UQuestSubsystem* GetQuestSubsystem() const;

    UFUNCTION()
    void HandleQuestNotification(FQuestNotification Notification);
};
