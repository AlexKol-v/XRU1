#include "GA_Attack.h"
#include "UnitBase.h"
#include "CoverDetectionComponent.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "TacticsCombatStatics.h"
#include "CoverTuningDataAsset.h"
#include "TurnManagerSubsystem.h"
#include "ActionPointsComponent.h"
#include "UnitAIController.h"
#include "TacticalPlayerController.h"
#include "TacticalQuestEvents.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogTacticsAttackAction, Log, All);

UGA_Attack::UGA_Attack()
{
	InstancingPolicy =
		EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	// Активация приходит событием Event.Attack с целью в payload.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = TacticsGameplayTags::Event_Attack;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);

	// XCOM-правило: выстрел стоит 1 AP и завершает активацию юнита.
	ActionPointCost = 1;
	bConsumesAllRemainingAP = true;

	DamageEffect = UGE_ShotDamage::StaticClass();
}

float UGA_Attack::ComputeEffectiveAim(const AUnitBase* Shooter, const AActor* Target)
{
	if (!Shooter)
	{
		return 0.f;
	}

	float Aim = Shooter->BaseAim;
	if (Target)
	{
		// Модификаторы XCOM 2 (GDD §5.4). Считаются ЗДЕСЬ и только здесь:
		// через ComputeEffectiveAim идут выстрел игрока, AI, Overwatch (со своим
		// штрафом поверх) и HUD-прогноз — расходиться им негде.

		// 1) Дистанция: профиль оружия (кривая юнита или встроенная винтовка).
		const float Distance = FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation());
		Aim += UTacticsCombatStatics::GetAimDistanceModifier(Shooter, Distance);

		// 2) Высота — СИММЕТРИЧНО: стрелок заметно выше цели → +20, заметно ниже
		// → −20. (В XCOM 2 штрафа снизу нет, только бонус сверху; симметрия —
		// осознанное отклонение, зафиксировано в GDD §5.4: позиция на высоте
		// должна читаться как преимущество с обеих сторон.)
		const UCoverTuningDataAsset* Tuning = UTacticsCombatStatics::GetCoverTuning(Shooter->GetWorld());
		const float HeightDelta = Shooter->GetActorLocation().Z - Target->GetActorLocation().Z;
		if (HeightDelta >= Tuning->HeightAdvantageZ)
		{
			Aim += Tuning->HeightAdvantageAimBonus;
		}
		else if (Tuning->bSymmetricHeightPenalty && HeightDelta <= -Tuning->HeightAdvantageZ)
		{
			Aim -= Tuning->HeightAdvantageAimBonus;
		}

		// 3) Squadsight-выстрел без собственной LOS — штраф. Берём из CDO
		// способности атаки ЭТОГО юнита: HUD и выстрел считают одно и то же
		// даже при перенастроенном BP-наследнике GA_Attack.
		if (!UTacticsCombatStatics::HasLineOfSight(Shooter, Target))
		{
			const UGA_Attack* AttackCDO = nullptr;
			if (Shooter->AttackAbilityClass && Shooter->AttackAbilityClass->IsChildOf(UGA_Attack::StaticClass()))
			{
				AttackCDO = Shooter->AttackAbilityClass->GetDefaultObject<UGA_Attack>();
			}
			Aim -= AttackCDO
				? AttackCDO->SquadsightAimPenalty
				: GetDefault<UGA_Attack>()->SquadsightAimPenalty;
		}
	}
	return FMath::Max(0.f, Aim);
}

float UGA_Attack::ComputeAttackHitChance(const AUnitBase* Shooter, const AActor* Target)
{
	if (!CanTargetActor(Shooter, Target))
	{
		return -1.f;
	}
	return UTacticsCombatStatics::ComputeHitChance(Shooter, Target, ComputeEffectiveAim(Shooter, Target));
}

EAttackTargetStatus UGA_Attack::GetTargetStatus(const AUnitBase* Shooter, const AActor* Target)
{
	if (!Shooter || !Target || !UTacticsCombatStatics::AreHostile(Shooter, Target))
	{
		return EAttackTargetStatus::NotHostile;
	}
	if (!UTacticsCombatStatics::IsUnitAlive(Target))
	{
		return EAttackTargetStatus::Dead;
	}

	const float Distance = FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation());
	if (Distance > Shooter->AttackRange)
	{
		return EAttackTargetStatus::OutOfRange;
	}

	// Прямая видимость либо Squadsight (цель видит любой союзник снайпера).
	if (UTacticsCombatStatics::HasLineOfSight(Shooter, Target))
	{
		return EAttackTargetStatus::Valid;
	}
	if (Shooter->bHasSquadsight && UTacticsCombatStatics::SquadHasLineOfSight(Shooter, Target))
	{
		return EAttackTargetStatus::Valid;
	}
	return EAttackTargetStatus::NoLineOfSight;
}

bool UGA_Attack::CanTargetActor(const AUnitBase* Shooter, const AActor* Target)
{
	return GetTargetStatus(Shooter, Target) == EAttackTargetStatus::Valid;
}

bool UGA_Attack::HasAnyValidTarget(const AUnitBase* Shooter)
{
	if (!Shooter)
	{
		return false;
	}
	const UWorld* World = Shooter->GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return false;
	}
	for (const AActor* Enemy : TurnManager->GetOpposingUnits(Shooter))
	{
		if (CanTargetActor(Shooter, Enemy))
		{
			return true;
		}
	}
	return false;
}

void UGA_Attack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerData)
{
	AUnitBase* Shooter = Cast<AUnitBase>(GetAvatarActorFromActorInfo());
	AActor* Target = TriggerData ? const_cast<AActor*>(TriggerData->Target.Get()) : nullptr;

	// Все проверки ДО Commit: при провале AP не списываются.
	if (!Shooter || !DamageEffect || !CanTargetActor(Shooter, Target))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Snapshot строится ДО оплаты AP. Контекст публикуется до CommitAbility,
	// чтобы синхронный OnActionPointsChanged уже видел action barrier.
	const UActionPointsComponent* ActionPoints = FindActionPoints(ActorInfo);
	const int32 ActionPointsBefore = ActionPoints ? ActionPoints->CurrentActionPoints : INDEX_NONE;
	const float ResolvedHitChance = ComputeAttackHitChance(Shooter, Target);
	EFiringStance FiringStance = EFiringStance::Open;
	FVector FiringEyeLocation = FVector::ZeroVector;
	FVector PresentationRootLocation = Shooter->GetActorLocation();
	UAnimMontage* FireMontage = Shooter->GetFireMontageFor(
		Target, FiringStance, FiringEyeLocation, PresentationRootLocation);
	if (!FireMontage)
	{
		UE_LOG(LogTacticsAttackAction, Error,
			TEXT("[FireAction] %s: для стойки %d не назначен fire montage"),
			*GetNameSafe(Shooter), static_cast<int32>(FiringStance));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const bool bUsedSquadsight = !UTacticsCombatStatics::HasLineOfSight(Shooter, Target)
		&& Shooter->bHasSquadsight
		&& UTacticsCombatStatics::SquadHasLineOfSight(Shooter, Target);
	FireAction.Begin(Shooter, Target, FiringEyeLocation, ResolvedHitChance,
		Shooter->ShotDamage, Shooter->AttackRange, DamageEffect, ActionPointsBefore);
	const UCoverDetectionComponent* Cover = Shooter->GetCoverDetection();
	FireAction.SetPresentation(FireMontage, FiringStance, Shooter->GetActorLocation(),
		PresentationRootLocation,
		Cover ? Cover->ActiveCoverAnchor : FVector::ZeroVector,
		Cover ? Cover->ActiveCoverNormal : FVector::ZeroVector,
		Cover ? Cover->ActiveCoverWallId : 0,
		Cover ? Cover->ActiveCoverRevision : 0,
		bUsedSquadsight);
	const FGuid ActionId = FireAction.ActionId;

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FireAction.Reset();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	FireAction.bCostCommitted = true;

	if (UWorld* World = Shooter->GetWorld())
	{
		World->GetTimerManager().SetTimer(FireActionWatchdogTimer,
			FTimerDelegate::CreateUObject(this, &UGA_Attack::HandleFireActionTimeout, ActionId),
			FMath::Max(1.f, FireActionTimeout), /*bLoop=*/false);
	}

	UE_LOG(LogTacticsAttackAction, Log,
		TEXT("[FireAction] Begin id=%s shooter=%s target=%s chance=%.1f"),
		*ActionId.ToString(EGuidFormats::Digits), *GetNameSafe(Shooter), *GetNameSafe(Target),
		ResolvedHitChance);

	// Здесь НЕТ ResolveShot и EndAbility: BP/C++ coordinator должен доиграть
	// StepOut/montage/ReturnToAnchor и вызвать terminal API.
	NotifyShotPresentation(Shooter, Target);
	OnFireActionStarted(Target, ActionId);
}

void UGA_Attack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (FireAction.IsActive())
	{
		const FTacticalFireActionContext FinishedAction = FireAction;
		const FGuid ActionId = FinishedAction.ActionId;
		const bool bShotCommitted = FinishedAction.bShotCommitted;
		ClearFireActionWatchdog();
		// Сначала атомарно закрываем ActionId. Любой синхронный callback от остановки montage
		// или возврата AP уже увидит inactive-context и не сможет повторить terminal/refund.
		FireAction.Reset();
		StopFireActionMontage(FinishedAction);
		if (!bShotCommitted)
		{
			RefundPreCommitActionPoints(FinishedAction);
		}
		EndShotPresentation();
		OnFireActionTerminated(ActionId, bShotCommitted, /*bAborted=*/true);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UAnimMontage* UGA_Attack::GetFireActionPresentation(const FGuid& ActionId,
	EFiringStance& OutStance, FVector& OutHomeRootLocation,
	FVector& OutPresentationRootLocation) const
{
	OutStance = EFiringStance::Open;
	OutHomeRootLocation = FVector::ZeroVector;
	OutPresentationRootLocation = FVector::ZeroVector;
	if (!FireAction.Matches(ActionId))
	{
		return nullptr;
	}

	OutStance = FireAction.FiringStance;
	OutHomeRootLocation = FireAction.HomeRootLocation;
	OutPresentationRootLocation = FireAction.PresentationRootLocation;
	return FireAction.FireMontage.Get();
}

bool UGA_Attack::GetAttackActionInProgressFor(const AUnitBase* Unit, FGuid& OutActionId)
{
	OutActionId.Invalidate();
	if (!Unit || !Unit->AttackAbilityClass)
	{
		return false;
	}

	UAbilitySystemComponent* ASC = const_cast<AUnitBase*>(Unit)->GetAbilitySystemComponent();
	FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromClass(Unit->AttackAbilityClass) : nullptr;
	const UGA_Attack* Attack = nullptr;
	if (Spec && Spec->IsActive())
	{
		Attack = Cast<UGA_Attack>(Spec->GetPrimaryInstance());
		if (!Attack || !Attack->FireAction.IsActive())
		{
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			{
				const UGA_Attack* Candidate = Cast<UGA_Attack>(Instance);
				if (Candidate && Candidate->FireAction.IsActive())
				{
					Attack = Candidate;
					break;
				}
			}
		}
	}
	if (!Attack || !Attack->FireAction.IsActive())
	{
		return false;
	}

	OutActionId = Attack->FireAction.ActionId;
	return true;
}

bool UGA_Attack::AcceptFireCommitMontageInstance(
	const FGuid& ActionId, int32 MontageInstanceId)
{
	return FireAction.Matches(ActionId) &&
		FireAction.TryBindMontageInstance(MontageInstanceId);
}

bool UGA_Attack::FireCommit(const FGuid& ActionId, bool& bOutHit)
{
	bOutHit = false;
	if (!FireAction.CanCommit(ActionId))
	{
		UE_LOG(LogTacticsAttackAction, Warning,
			TEXT("[FireAction] Reject stale/duplicate commit id=%s active=%s committed=%d"),
			*ActionId.ToString(EGuidFormats::Digits),
			*FireAction.ActionId.ToString(EGuidFormats::Digits), FireAction.bShotCommitted ? 1 : 0);
		return false;
	}
	if (!IsFrozenFireCommitValid())
	{
		UE_LOG(LogTacticsAttackAction, Warning,
			TEXT("[FireAction] Reject invalid frozen solution id=%s shooter=%s target=%s"),
			*ActionId.ToString(EGuidFormats::Digits), *GetNameSafe(FireAction.Shooter.Get()),
			*GetNameSafe(FireAction.Target.Get()));
		return false;
	}

	AActor* Shooter = FireAction.Shooter.Get();
	AActor* Target = FireAction.Target.Get();
	const FVector ShotOrigin = FireAction.FiringEyeLocation;
	const float HitChance = FireAction.ResolvedHitChance;
	const float Damage = FireAction.Damage;
	const TSubclassOf<UGameplayEffect> EffectClass = FireAction.DamageEffectClass;

	// Guard ставится ДО callbacks/GE: смерть последней цели не должна позволить
	// reentrant notify повторно применить урон или вернуть AP.
	FireAction.MarkCommitStarted();
	if (const APawn* ShooterPawn = Cast<APawn>(Shooter))
	{
		if (Cast<AUnitAIController>(ShooterPawn->GetController()))
		{
			if (UWorld* World = Shooter->GetWorld())
			{
				if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
				{
					// A8 throttle считается по необратимому commit, а не по AP/reservation.
					TurnManager->NotifyUnitAttacked(Shooter);
				}
			}
		}
	}
	bOutHit = UTacticsCombatStatics::ResolveShotMechanics(
		Shooter, Target, HitChance, Damage, EffectClass, ShotOrigin);
	if (FireAction.Matches(ActionId))
	{
		FireAction.SetCommitResult(bOutHit);
		OnShotFired(Target, bOutHit);
		// Пока STQuestSystem отбрасывает payload/Source, публикуем автоматически
		// только действия стороны игрока. Scripted enemy shot подтверждает его
		// orchestration-task, иначе любой обычный выстрел врага закрыл бы tutorial.
		if (UTacticalQuestEvents::IsPlayerSideUnit(Shooter, Shooter))
		{
			UTacticalQuestEvents::BroadcastQuestEvent(Shooter,
				FireAction.bUsedSquadsight
					? TacticalQuestTags::Event_Tactical_Combat_Attack_Squadsight
					: TacticalQuestTags::Event_Tactical_Combat_Attack_Normal,
				Shooter);
		}
	}

	UE_LOG(LogTacticsAttackAction, Log,
		TEXT("[FireAction] Commit id=%s hit=%d chance=%.1f"),
		*ActionId.ToString(EGuidFormats::Digits), bOutHit ? 1 : 0, HitChance);
	return true;
}

bool UGA_Attack::CompleteFireAction(const FGuid& ActionId)
{
	if (!FireAction.Matches(ActionId))
	{
		return false;
	}
	if (!FireAction.bShotCommitted)
	{
		// Montage закончился без FireCommit notify: это pre-commit abort, не miss.
		return AbortFireAction(ActionId);
	}

	ClearFireActionWatchdog();
	FireAction.Reset();
	EndShotPresentation();
	OnFireActionTerminated(ActionId, /*bShotCommitted=*/true, /*bAborted=*/false);
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	CheckCombatOutcomeAfterAction();
	return true;
}

bool UGA_Attack::AbortFireAction(const FGuid& ActionId)
{
	if (!FireAction.Matches(ActionId))
	{
		return false;
	}

	const FTacticalFireActionContext FinishedAction = FireAction;
	const bool bShotCommitted = FinishedAction.bShotCommitted;
	ClearFireActionWatchdog();
	FireAction.Reset();
	StopFireActionMontage(FinishedAction);
	if (!bShotCommitted)
	{
		RefundPreCommitActionPoints(FinishedAction);
	}
	EndShotPresentation();
	OnFireActionTerminated(ActionId, bShotCommitted, /*bAborted=*/true);
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	if (bShotCommitted)
	{
		CheckCombatOutcomeAfterAction();
	}
	return true;
}

void UGA_Attack::HandleFireActionTimeout(FGuid ActionId)
{
	if (FireAction.Matches(ActionId))
	{
		UE_LOG(LogTacticsAttackAction, Error,
			TEXT("[FireAction] Watchdog abort id=%s phase=%d committed=%d"),
			*ActionId.ToString(EGuidFormats::Digits), static_cast<int32>(FireAction.Phase),
			FireAction.bShotCommitted ? 1 : 0);
		AbortFireAction(ActionId);
	}
}

void UGA_Attack::ClearFireActionWatchdog()
{
	if (AActor* Shooter = FireAction.Shooter.Get())
	{
		if (UWorld* World = Shooter->GetWorld())
		{
			World->GetTimerManager().ClearTimer(FireActionWatchdogTimer);
		}
	}
}

void UGA_Attack::RefundPreCommitActionPoints(
	const FTacticalFireActionContext& FinishedAction)
{
	if (!FinishedAction.bCostCommitted || FinishedAction.bShotCommitted ||
		FinishedAction.ActionPointsBefore < 0)
	{
		return;
	}

	AUnitBase* Shooter = Cast<AUnitBase>(FinishedAction.Shooter.Get());
	if (!Shooter || Shooter->IsDead() || Shooter->IsDowned() || Shooter->IsEvacuated())
	{
		return;
	}
	if (UActionPointsComponent* ActionPoints = Shooter->GetActionPoints())
	{
		const int32 Refund = FMath::Max(0,
			FinishedAction.ActionPointsBefore - ActionPoints->CurrentActionPoints);
		if (Refund > 0)
		{
			ActionPoints->GrantExtraPoints(Refund);
		}
	}
	if (MaxUsesPerMission > 0)
	{
		UsesRemaining = FMath::Min(MaxUsesPerMission, UsesRemaining + 1);
	}
}

bool UGA_Attack::IsFrozenFireCommitValid() const
{
	const AUnitBase* Shooter = Cast<AUnitBase>(FireAction.Shooter.Get());
	const AActor* Target = FireAction.Target.Get();
	if (!Shooter || !Target || Shooter->IsDead() || Shooter->IsDowned() || Shooter->IsEvacuated()
		|| !UTacticsCombatStatics::IsUnitAlive(Target)
		|| !UTacticsCombatStatics::AreHostile(Shooter, Target))
	{
		return false;
	}
	if (FVector::Dist(FireAction.FiringEyeLocation, Target->GetActorLocation()) > FireAction.MaxRange)
	{
		return false;
	}
	if (UTacticsCombatStatics::HasLineOfSightFromFrozenOrigin(
		Shooter->GetWorld(), FireAction.FiringEyeLocation, Target))
	{
		return true;
	}

	// Squadsight — зафиксированное правило именно этой транзакции. Мы не
	// превращаем обычный выстрел в Squadsight задним числом, но сохраняем
	// разрешённый GDD путь, если союзник всё ещё видит живую цель на commit.
	return FireAction.bUsedSquadsight && Shooter->bHasSquadsight
		&& UTacticsCombatStatics::SquadHasLineOfSight(Shooter, Target);
}

void UGA_Attack::NotifyShotPresentation(AActor* Shooter, AActor* Target) const
{
	const UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (ATacticalPlayerController* PC = World
		? Cast<ATacticalPlayerController>(World->GetFirstPlayerController())
		: nullptr)
	{
		PC->NotifyShotFired(Shooter, Target);
	}
}

void UGA_Attack::EndShotPresentation() const
{
	const AActor* Shooter = GetAvatarActorFromActorInfo();
	const UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (ATacticalPlayerController* PC = World
		? Cast<ATacticalPlayerController>(World->GetFirstPlayerController())
		: nullptr)
	{
		PC->EndShotPresentation();
	}
}

void UGA_Attack::StopFireActionMontage(
	const FTacticalFireActionContext& FinishedAction) const
{
	const AUnitBase* Shooter = Cast<AUnitBase>(FinishedAction.Shooter.Get());
	UAnimMontage* Montage = FinishedAction.FireMontage.Get();
	UAnimInstance* AnimInstance = Shooter && Shooter->GetMesh()
		? Shooter->GetMesh()->GetAnimInstance()
		: nullptr;
	if (AnimInstance && Montage && AnimInstance->Montage_IsActive(Montage))
	{
		AnimInstance->Montage_Stop(0.1f, Montage);
	}
}

void UGA_Attack::CheckCombatOutcomeAfterAction() const
{
	const AActor* Shooter = GetAvatarActorFromActorInfo();
	const UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>()
		: nullptr)
	{
		TurnManager->CheckCombatOutcome();
	}
}
