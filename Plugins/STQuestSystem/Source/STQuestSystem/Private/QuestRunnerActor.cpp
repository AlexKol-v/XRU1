// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuestRunnerActor.h"

#include "Components/ActorComponent.h"
#include "Components/StateTreeComponent.h"
#include "Engine/GameInstance.h"
#include "QuestDefinition.h"
#include "QuestGameplayTags.h"
#include "QuestInstance.h"
#include "QuestSubsystem.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeTypes.h"

AQuestRunnerActor::AQuestRunnerActor()
{
    PrimaryActorTick.bCanEverTick = true;

    QuestStateTree = CreateDefaultSubobject<UStateTreeComponent>(TEXT("QuestStateTree"));
    // Запуск дерева — вручную из StartQuest, ПОСЛЕ назначения ассета логики.
    QuestStateTree->SetStartLogicAutomatically(false);
}

AQuestRunnerActor* AQuestRunnerActor::GetFromContext(FStateTreeExecutionContext& Context)
{
    // Владельцем контекста может быть либо сам раннер, либо его State Tree-компонент.
    UObject* Owner = Context.GetOwner();
    if (AQuestRunnerActor* Runner = Cast<AQuestRunnerActor>(Owner))
    {
        return Runner;
    }
    if (const UActorComponent* Component = Cast<UActorComponent>(Owner))
    {
        return Cast<AQuestRunnerActor>(Component->GetOwner());
    }
    return nullptr;
}

void AQuestRunnerActor::StartQuest(UQuestInstance* InInstance)
{
    OwningInstance = InInstance;
    if (!OwningInstance || !OwningInstance->Definition || !QuestStateTree)
    {
        return;
    }

    // Назначаем ассет State Tree логики квеста и подписываемся на квест-события.
    QuestStateTree->SetStateTreeReference(OwningInstance->Definition->QuestLogic);
    RefreshEventSubscription();
    QuestStateTree->StartLogic();
}

void AQuestRunnerActor::StartQuestRestoring(UQuestInstance* InInstance, const FQuestProgress& InProgress)
{
    // Включаем восстановление ДО запуска дерева — EnterState задач сразу
    // запросит стартовые счётчики через GetRestoredObjectiveCount.
    RestoredProgress = InProgress;
    bRestoring = true;
    bSeedRequestedSinceTick = true;
    StartQuest(InInstance);
}

int32 AQuestRunnerActor::GetRestoredObjectiveCount(FGameplayTag ObjectiveId)
{
    if (!bRestoring)
    {
        return 0;
    }

    // Любой запрос означает, что дерево ещё мотается к сохранённой позиции.
    bSeedRequestedSinceTick = true;

    for (const FObjectiveProgress& Objective : RestoredProgress.Objectives)
    {
        if (Objective.ObjectiveId == ObjectiveId)
        {
            return Objective.Current;
        }
    }
    return 0;
}

void AQuestRunnerActor::RefreshEventSubscription()
{
    // Снимаем прежнюю подписку — важно при переносе квеста другому владельцу.
    if (EventListenerHandle.IsValid())
    {
        EventListenerHandle.Unregister();
    }

    // Один листенер на родительский канал Quest.Event с частичным совпадением:
    // он ловит Quest.Event.Kill, Quest.Event.ItemCollected, Quest.Event.Reach и т.д.
    if (UGameplayMessageSubsystem::HasInstance(this))
    {
        UGameplayMessageSubsystem& Messages = UGameplayMessageSubsystem::Get(this);
        EventListenerHandle = Messages.RegisterListener<FQuestEventData>(
            QuestGameplayTags::Quest_Event,
            [this](FGameplayTag Channel, const FQuestEventData& Data)
            {
                HandleQuestEvent(Channel, Data);
            },
            EGameplayMessageMatch::PartialMatch);
    }
}

void AQuestRunnerActor::HandleQuestEvent(FGameplayTag Channel, const FQuestEventData& Data)
{
    if (!QuestStateTree)
    {
        return;
    }

    // Ретранслируем квест-событие в State Tree. Вклад Amount учитываем
    // повторением: задача-цель инкрементирует счётчик на каждое событие.
    FStateTreeEvent Event;
    Event.Tag = Channel;

    const int32 Repeats = FMath::Max(1, Data.Amount);
    for (int32 Index = 0; Index < Repeats; ++Index)
    {
        QuestStateTree->SendStateTreeEvent(Event);
    }
}

void AQuestRunnerActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bFinished || !QuestStateTree)
    {
        return;
    }

    // Завершаем режим восстановления, когда дерево промоталось к сохранённой
    // позиции: за прошлый тик ни одна задача не запросила стартовый счётчик.
    if (bRestoring)
    {
        if (bSeedRequestedSinceTick)
        {
            bSeedRequestedSinceTick = false;
        }
        else
        {
            bRestoring = false;
        }
    }

    // Опрашиваем статус исполнения дерева — так раннер узнаёт о завершении
    // квеста без привязки к делегату с неизвестной сигнатурой.
    const EStateTreeRunStatus Status = QuestStateTree->GetStateTreeRunStatus();
    if (Status == EStateTreeRunStatus::Succeeded || Status == EStateTreeRunStatus::Failed)
    {
        bFinished = true;

        if (const UGameInstance* GameInstance = GetGameInstance())
        {
            if (UQuestSubsystem* Subsystem = GameInstance->GetSubsystem<UQuestSubsystem>())
            {
                Subsystem->NotifyQuestFinished(OwningInstance, Status == EStateTreeRunStatus::Succeeded);
            }
        }
    }
}

void AQuestRunnerActor::StopQuest()
{
    if (EventListenerHandle.IsValid())
    {
        EventListenerHandle.Unregister();
    }

    if (QuestStateTree)
    {
        QuestStateTree->StopLogic(TEXT("Quest stopped"));
    }
}

void AQuestRunnerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopQuest();
    Super::EndPlay(EndPlayReason);
}

void AQuestRunnerActor::ReportObjectiveProgress(const FObjectiveProgress& ObjectiveProgress)
{
    if (!OwningInstance || !ObjectiveProgress.ObjectiveId.IsValid())
    {
        return;
    }

    // Upsert снимка цели по её идентификатору.
    TArray<FObjectiveProgress>& Objectives = OwningInstance->Progress.Objectives;
    FObjectiveProgress* Existing = Objectives.FindByPredicate(
        [&ObjectiveProgress](const FObjectiveProgress& Item)
        {
            return Item.ObjectiveId == ObjectiveProgress.ObjectiveId;
        });
    if (Existing)
    {
        *Existing = ObjectiveProgress;
    }
    else
    {
        Objectives.Add(ObjectiveProgress);
    }

    RecomputeCompletionPercent();

    // Оповещаем подсистему — UI обновляется через делегат.
    if (const UGameInstance* GameInstance = GetGameInstance())
    {
        if (UQuestSubsystem* Subsystem = GameInstance->GetSubsystem<UQuestSubsystem>())
        {
            Subsystem->NotifyObjectiveProgress(OwningInstance->GetQuestId());
        }
    }
}

void AQuestRunnerActor::RecomputeCompletionPercent()
{
    if (!OwningInstance)
    {
        return;
    }

    const TArray<FObjectiveProgress>& Objectives = OwningInstance->Progress.Objectives;
    if (Objectives.Num() == 0)
    {
        OwningInstance->Progress.CompletionPercent = 0.f;
        return;
    }

    // Доля выполнения — среднее по целям с учётом ЧАСТИЧНОГО прогресса внутри
    // цели (Current/Required), а не только полностью завершённых. Иначе цель
    // «убей 2/3» давала бы 0% до завершения и скачок к 100%. Завершённая цель = 1.0.
    float ProgressSum = 0.f;
    for (const FObjectiveProgress& Objective : Objectives)
    {
        if (Objective.State == EObjectiveState::Completed)
        {
            ProgressSum += 1.f;
        }
        else if (Objective.State == EObjectiveState::Active && Objective.Required > 0)
        {
            ProgressSum += FMath::Clamp(
                static_cast<float>(Objective.Current) / static_cast<float>(Objective.Required), 0.f, 1.f);
        }
    }
    OwningInstance->Progress.CompletionPercent = ProgressSum / static_cast<float>(Objectives.Num());
}
