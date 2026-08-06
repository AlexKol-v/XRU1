#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "MissionVfxSubsystem.generated.h"

class UNiagaraSystem;
struct FQuestEventData;

/**
 * Спавнит Niagara-эффекты по ПОДТВЕРЖДЁННЫМ событиям сценария.
 *
 * Зеркало `UMissionVoiceDirectorSubsystem`: тот же вход (шина `Quest.Event`),
 * та же философия — эффект появляется на факте, который уже случился в
 * механике, а не на нажатии кнопки. Из-за этого слой не умеет «соврать»:
 * искры обезвреживания невозможны без зачтённого шага обезвреживания.
 *
 * Какие каналы к каким полям `UMissionVfxDataAsset` привязаны — см. сам ассет.
 * Подсистема пассивна: нет назначенного ассета (CDO пуст) — ничего не спавнит.
 */
UCLASS()
class XRU1_API UMissionVfxSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Подписка живёт здесь, а не в Initialize: подсистема мира создаётся до
	 * того, как мир готов целиком, и молча несостоявшаяся подписка (нет шины —
	 * нет эффектов, и ни одной строки в логе) уже стоила прогона 2026-08-06.
	 * К BeginPlay мира шина существует гарантированно, а результат подписки
	 * печатается явно.
	 */
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Только миры с геймплеем: в editor-preview мирах подписка не нужна. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

private:
	/** Событие с шины квестов; фильтрация по точному каналу внутри. */
	void HandleQuestEvent(FGameplayTag Channel, const FQuestEventData& Payload);

	/** Разовый эффект в точке актора (nullptr-безопасно по всем аргументам). */
	void SpawnAtActor(UNiagaraSystem* System, const UObject* AtObject, const TCHAR* Why) const;

	/** Столб дыма на всех зонах эвакуации (прикреплён к актору зоны). */
	void IgniteEvacZoneSmoke(UNiagaraSystem* System);

	FGameplayMessageListenerHandle EventListenerHandle;
};
