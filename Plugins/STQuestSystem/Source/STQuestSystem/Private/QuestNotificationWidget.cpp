// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestNotificationWidget.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "QuestSubsystem.h"

UQuestSubsystem* UQuestNotificationWidget::GetQuestSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
}

void UQuestNotificationWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->OnQuestNotification.AddDynamic(this, &UQuestNotificationWidget::HandleQuestNotification);
    }
}

void UQuestNotificationWidget::NativeDestruct()
{
    if (UQuestSubsystem* Subsystem = GetQuestSubsystem())
    {
        Subsystem->OnQuestNotification.RemoveDynamic(this, &UQuestNotificationWidget::HandleQuestNotification);
    }

    Super::NativeDestruct();
}

void UQuestNotificationWidget::HandleQuestNotification(FQuestNotification Notification)
{
    OnShowNotification(Notification);
}
