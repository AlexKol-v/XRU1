// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "QuestTypes.h"
#include "QuestRunnerActor.generated.h"

class UStateTreeComponent;
class UQuestInstance;
struct FStateTreeExecutionContext;

/**
 * Лёгкий невидимый актор-носитель исполнения одного квеста.
 * Несёт UStateTreeComponent, который исполняет ассет логики квеста
 * (стоковая схема UStateTreeComponentSchema). Регистрирует один листенер
 * GMS на родительский канал Quest.Event и ретранслирует квест-события
 * в State Tree. Завершение дерева опрашивается в Tick.
 */
UCLASS()
class STQUESTSYSTEM_API AQuestRunnerActor : public AActor
{
    GENERATED_BODY()

public:
    AQuestRunnerActor();

    /** Назначает инстанс, запускает State Tree и подписку на GMS. */
    void StartQuest(UQuestInstance* InInstance);

    /**
     * Запускает инстанс в режиме восстановления: задачи-цели берут стартовые
     * счётчики из снимка InProgress (через GetRestoredObjectiveCount), и дерево
     * «проматывается» к сохранённой позиции вместо старта с нуля.
     */
    void StartQuestRestoring(UQuestInstance* InInstance, const FQuestProgress& InProgress);

    /**
     * Стартовый счётчик цели для восстановления: 0, если восстановление не идёт.
     * Побочный эффект: помечает, что задачи всё ещё запрашивают сидинг — раннер
     * держит режим восстановления, пока запросы поступают.
     */
    int32 GetRestoredObjectiveCount(FGameplayTag ObjectiveId);

    /** Останавливает State Tree и снимает подписку GMS. */
    void StopQuest();

    /** Возвращает раннер, исполняющий State Tree данного контекста, или nullptr. */
    static AQuestRunnerActor* GetFromContext(FStateTreeExecutionContext& Context);

    /** Инстанс квеста, которым управляет этот раннер. */
    UQuestInstance* GetOwningInstance() const { return OwningInstance; }

    /** Учитывает прогресс цели в снимке квеста и оповещает подсистему. */
    void ReportObjectiveProgress(const FObjectiveProgress& ObjectiveProgress);

    /** Переподписывается на квест-события GMS (вызывается при переносе квеста). */
    void RefreshEventSubscription();

protected:
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** Колбэк GMS: ретранслирует квест-событие в State Tree квеста. */
    void HandleQuestEvent(FGameplayTag Channel, const FQuestEventData& Data);

    /** Пересчитывает долю выполнения квеста по снимку целей. */
    void RecomputeCompletionPercent();

    /** Компонент, исполняющий State Tree логики квеста. */
    UPROPERTY(VisibleAnywhere, Category = "Quest")
    TObjectPtr<UStateTreeComponent> QuestStateTree;

    /** Инстанс квеста, которым управляет этот раннер. */
    UPROPERTY()
    TObjectPtr<UQuestInstance> OwningInstance;

    /** Дескриптор GMS-листенера квест-событий. */
    FGameplayMessageListenerHandle EventListenerHandle;

    /** Завершён ли квест (защита от повторного уведомления подсистемы). */
    bool bFinished = false;

    /** Снимок прогресса для восстановления стартовых счётчиков целей. */
    FQuestProgress RestoredProgress;

    /** Идёт ли восстановление: задачи-цели сидят счётчики из RestoredProgress. */
    bool bRestoring = false;

    /** Запрашивали ли задачи сидинг с прошлого тика (сигнал «дерево ещё мотается»). */
    bool bSeedRequestedSinceTick = false;
};
