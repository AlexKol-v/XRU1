#include "MissionVfxSubsystem.h"

#include "MissionVfxDataAsset.h"
#include "XRU1Log.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MissionObjectives.h" // AEvacZone — носитель столба дыма
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "QuestGameplayTags.h"
#include "QuestTypes.h"
#include "TacticalQuestEvents.h"

void UMissionVfxSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMissionVfxSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Один листенер на родительский канал — как у директора реплик: эффекты
	// висят на разных leaf-каналах, отдельная подписка на каждый — лишний
	// реестр, который разъедется при первой правке.
	if (!UGameplayMessageSubsystem::HasInstance(this))
	{
		UE_LOG(LogXRU1UI, Error,
			TEXT("[VFX] нет GameplayMessageSubsystem — эффекты событий миссии выключены"));
		return;
	}

	UGameplayMessageSubsystem& Messages = UGameplayMessageSubsystem::Get(this);
	EventListenerHandle = Messages.RegisterListener<FQuestEventData>(
		QuestGameplayTags::Quest_Event,
		[this](FGameplayTag Channel, const FQuestEventData& Data)
		{
			HandleQuestEvent(Channel, Data);
		},
		EGameplayMessageMatch::PartialMatch);

	// Диагностика резолва прямо на старте: «эффектов нет и непонятно почему» —
	// худший режим отказа, дешевле одной строки лога он не ловится.
	const UMissionVfxDataAsset* Vfx = UMissionVfxDataAsset::Get(&InWorld);
	UE_LOG(LogXRU1UI, Display, TEXT("[VFX] слой событий миссии подписан (мир %s), ассет: %s"),
		*InWorld.GetName(),
		Vfx == GetDefault<UMissionVfxDataAsset>() ? TEXT("<CDO — назначь MissionVfx в GameInstance!>")
			: *Vfx->GetName());
}

void UMissionVfxSubsystem::Deinitialize()
{
	if (EventListenerHandle.IsValid())
	{
		EventListenerHandle.Unregister();
	}
	Super::Deinitialize();
}

void UMissionVfxSubsystem::HandleQuestEvent(FGameplayTag Channel, const FQuestEventData& Payload)
{
	const UMissionVfxDataAsset* Vfx = UMissionVfxDataAsset::Get(GetWorld());

	// Каналы сравниваются точно (leaf): родительские дубли одного события не
	// должны спавнить эффект дважды.
	if (Channel == TacticalQuestTags::Event_Tactical_Ability_Heal_Normal)
	{
		// Лечение показывается на ЦЕЛИ лечения (при self-heal Source == Target).
		SpawnAtActor(Vfx->HealEffect, Payload.Target ? Payload.Target : Payload.Source,
			TEXT("лечение"));
	}
	else if (Channel == TacticalQuestTags::Event_Tactical_Ability_Heal_Revive)
	{
		SpawnAtActor(Vfx->ReviveEffect, Payload.Target ? Payload.Target : Payload.Source,
			TEXT("подъём бойца"));
	}
	else if (Channel == TacticalQuestTags::Event_Tactical_Objective_Defuse_Progressed)
	{
		// Target — заряд (объект, НАД которым подтверждён результат).
		SpawnAtActor(Vfx->DefuseProgressEffect, Payload.Target ? Payload.Target : Payload.Source,
			TEXT("шаг обезвреживания"));
	}
	else if (Channel == TacticalQuestTags::Event_Tactical_Objective_Defuse_Completed)
	{
		SpawnAtActor(Vfx->DefuseCompleteEffect, Payload.Target ? Payload.Target : Payload.Source,
			TEXT("заряд снят"));
		// Снятый заряд открывает эвакуацию — зона обозначает себя дымом, как
		// обещает реплика («южные ворота, синий дым»).
		IgniteEvacZoneSmoke(Vfx->EvacZoneSmokeEffect);
	}
	else if (Channel == TacticalQuestTags::Event_Tactical_Objective_Evac_Unit)
	{
		// Source — эвакуировавшийся боец; эффект остаётся в точке его ухода.
		SpawnAtActor(Vfx->EvacUnitEffect, Payload.Source, TEXT("эвакуация бойца"));
	}
}

void UMissionVfxSubsystem::SpawnAtActor(UNiagaraSystem* System, const UObject* AtObject,
	const TCHAR* Why) const
{
	const AActor* Actor = Cast<AActor>(AtObject);
	UWorld* World = GetWorld();
	if (!System || !Actor || !World)
	{
		return; // незаполненное поле ассета — штатно; объект не актор — молчим
	}

	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, System, Actor->GetActorLocation(),
		Actor->GetActorRotation(), FVector(1.f), /*bAutoDestroy=*/true);

	// Разовый эффект обязан погаснуть сам: зацикленную систему bAutoDestroy не
	// убьёт никогда. Именно DestroyComponent, а не Deactivate: деактивация лишь
	// останавливает спавн НОВЫХ частиц, а существующие доживают свой срок — у
	// «аур» набора частица (ромб эвакуации, молния) живёт практически вечно, и
	// после Deactivate эффект продолжает висеть.
	const float Lifetime = UMissionVfxDataAsset::Get(World)->OneShotLifetimeSeconds;
	if (Component && Lifetime > 0.f)
	{
		FTimerHandle ExpireTimer;
		World->GetTimerManager().SetTimer(ExpireTimer,
			FTimerDelegate::CreateWeakLambda(Component, [Component]()
			{
				Component->DestroyComponent();
			}),
			Lifetime, /*bLoop=*/false);
	}

	// Display, а не Verbose: событий мало (единицы за миссию), а вопрос «показался
	// ли эффект и на ком» — первый при любой жалобе на VFX.
	UE_LOG(LogXRU1UI, Display, TEXT("[VFX] %s: %s на %s"),
		Why, *System->GetName(), *GetNameSafe(Actor));
}

void UMissionVfxSubsystem::IgniteEvacZoneSmoke(UNiagaraSystem* System)
{
	UWorld* World = GetWorld();
	if (!System || !World)
	{
		return;
	}

	// Дым прикреплён к АКТОРУ зоны, а не заспавнен в точке: со смертью зоны
	// (retry перезагружает sublevel) компонент умирает сам, и уборка не нужна.
	// Повторное событие на ЖИВОЙ зоне дым не дублирует — идемпотентность
	// проверяется по самой зоне, а не флагом подсистемы: подсистема переживает
	// retry, а зона нет, и флаг после retry молча оставил бы зону без дыма.
	int32 Ignited = 0;
	for (TActorIterator<AEvacZone> It(World); It; ++It)
	{
		TInlineComponentArray<UNiagaraComponent*> Existing(*It);
		const bool bAlreadyBurning = Existing.ContainsByPredicate(
			[System](const UNiagaraComponent* Component)
			{
				return Component && Component->GetAsset() == System;
			});
		if (bAlreadyBurning)
		{
			continue;
		}
		if (UNiagaraComponent* Smoke = UNiagaraFunctionLibrary::SpawnSystemAttached(
			System, It->GetRootComponent(), NAME_None, FVector::ZeroVector,
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, /*bAutoDestroy=*/false))
		{
			// Масштаб обязателен: аура размером с бойца на зоне 10×10 м невидима.
			Smoke->SetWorldScale3D(FVector(
				UMissionVfxDataAsset::Get(World)->EvacZoneEffectScale));
			++Ignited;
		}
	}
	UE_LOG(LogXRU1UI, Display, TEXT("[VFX] дым эвакуации зажжён на %d зоне(ах)"), Ignited);
}
