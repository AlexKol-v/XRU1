// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "QuestWaypoint.generated.h"

class USphereComponent;
class UPrimitiveComponent;

/**
 * Точка-актор на карте. При входе пешки игрока в объём-триггер броадкастит
 * квест-событие на канале WaypointTag. Цель «достичь точки» — это
 * FQuestTask_TrackObjective, чей EventChannel и ObjectiveId равны WaypointTag.
 * Тег точки авторится дочерним к Quest.Event.Reach.
 */
UCLASS()
class STQUESTSYSTEM_API AQuestWaypoint : public AActor
{
    GENERATED_BODY()

public:
    AQuestWaypoint();

    /** Тег точки — канал квест-события достижения и идентификатор цели. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
    FGameplayTag WaypointTag;

    /** Возвращает все зарегистрированные точки в мире. */
    static const TArray<TWeakObjectPtr<AQuestWaypoint>>& GetAllWaypoints() { return AllWaypoints; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** Колбэк перекрытия объёма-триггера. */
    UFUNCTION()
    void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    /** Объём-триггер достижения точки. */
    UPROPERTY(VisibleAnywhere, Category = "Quest")
    TObjectPtr<USphereComponent> TriggerSphere;

    /** Глобальный реестр точек — для трекинг-компонента игрока. */
    static TArray<TWeakObjectPtr<AQuestWaypoint>> AllWaypoints;
};
