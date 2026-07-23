#include "TacticalAbility.h"
#include "ActionPointsComponent.h"
#include "TacticsGameplayTags.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UTacticalAbility::UTacticalAbility()
{
	// Пошаговая одиночная игра: один инстанс на юнита, состояние живёт между активациями.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// Последняя линия защиты от одновременного запуска двух действий. Контроллер
	// управляет UI-режимами ДО активации (Attack/Ability targeting), а GAS
	// гарантирует взаимоисключение уже ВЫПОЛНЯЮЩИХСЯ способностей независимо от
	// того, откуда пришёл вызов: HUD, хоткей, GameplayEvent или AI.
	FGameplayTagContainer ActionTags;
	ActionTags.AddTag(TacticsGameplayTags::Ability_TacticalAction);
	SetAssetTags(ActionTags);
	BlockAbilitiesWithTag.AddTag(TacticsGameplayTags::Ability_TacticalAction);
}

bool UTacticalAbility::IsValidTargetActor_Implementation(
	AUnitBase* SourceUnit, AActor* TargetActor) const
{
	// Базовый контракт не навязывает правил конкретной способности, но не
	// разрешает отправлять событие без владельца или цели.
	return SourceUnit != nullptr && TargetActor != nullptr;
}

bool UTacticalAbility::CheckCost(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	// Сначала штатная стоимость GAS (CostGameplayEffectClass, если назначен).
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}

	// Лимит применений за миссию.
	if (MaxUsesPerMission > 0 && UsesRemaining <= 0)
	{
		return false;
	}

	if (ActionPointCost <= 0)
	{
		return true;
	}

	const UActionPointsComponent* ActionPoints = FindActionPoints(ActorInfo);
	return ActionPoints && ActionPoints->CanSpend(ActionPointCost);
}

void UTacticalAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	if (MaxUsesPerMission > 0)
	{
		// InstancedPerActor: состояние живёт на инстансе; const_cast — паттерн ApplyCost const-API.
		const_cast<UTacticalAbility*>(this)->UsesRemaining = FMath::Max(0, UsesRemaining - 1);
	}

	if (UActionPointsComponent* ActionPoints = FindActionPoints(ActorInfo))
	{
		if (ActionPointCost > 0)
		{
			ActionPoints->TrySpendActionPoint(ActionPointCost);
		}
		if (bConsumesAllRemainingAP)
		{
			// XCOM-правило: действие завершает активацию юнита.
			ActionPoints->SpendAllRemaining();
		}
	}
}

void UTacticalAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);
	// Новый аватар = новая миссия для этого инстанса — сброс лимита применений.
	UsesRemaining = MaxUsesPerMission;
}

void UTacticalAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Сначала GAS снимает BlockAbilitiesWithTag, только потом обновляем HUD.
	// Это единый lifecycle-путь и для мгновенных, и для длительных тактических GA:
	// кнопки не остаются серыми после Attack/Heal/RunAndGun и разблокируются после
	// завершения Overwatch/Hunker/Taunt.
	TWeakObjectPtr<AUnitBase> Unit = Cast<AUnitBase>(
		ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (Unit.IsValid())
	{
		Unit->NotifyUnitStateChanged();
	}
}

bool UTacticalAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// В бою активировать способности можно только в свою фазу хода.
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (Avatar)
	{
		// Мёртвый/тяжелораненый/эвакуированный юнит ничего не активирует
		// (страховка: HUD может запросить активацию до обновления серости кнопок).
		if (const AUnitBase* Unit = Cast<AUnitBase>(Avatar))
		{
			if (Unit->IsDead() || Unit->IsDowned() || Unit->IsEvacuated())
			{
				return false;
			}
		}
		if (const UWorld* World = Avatar->GetWorld())
		{
			if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
			{
				if (TurnManager->IsInCombat() && !TurnManager->IsUnitOnActiveSide(Avatar))
				{
					return false;
				}
			}
		}
	}
	return true;
}

UActionPointsComponent* UTacticalAbility::FindActionPoints(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	return Avatar ? Avatar->FindComponentByClass<UActionPointsComponent>() : nullptr;
}
