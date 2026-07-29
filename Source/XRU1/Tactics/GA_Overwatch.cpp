#include "GA_Overwatch.h"
#include "TacticsGameplayTags.h"
#include "TacticsGameplayEffects.h"
#include "TacticsCombatStatics.h"
#include "TurnManagerSubsystem.h"
#include "GA_Attack.h"
#include "UnitBase.h"
#include "CoverDetectionComponent.h"
#include "UnitAIController.h"
#include "TacticalPlayerController.h"
#include "TacticalQuestEvents.h"
#include "AbilitySystemComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogTacticsOverwatchAction, Log, All);

namespace
{
	/** Один presentation-slot на мир: наблюдатели отрабатывают последовательно. */
	TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<UGA_Overwatch>> GReactionOwners;

	bool TryAcquireReactionSlot(UGA_Overwatch* Ability)
	{
		UWorld* World = Ability ? Ability->GetWorld() : nullptr;
		if (!World)
		{
			return false;
		}

		const TWeakObjectPtr<UWorld> WorldKey(World);
		if (const TWeakObjectPtr<UGA_Overwatch>* Existing = GReactionOwners.Find(WorldKey))
		{
			if (Existing->IsValid() && Existing->Get() != Ability)
			{
				return false;
			}
		}
		GReactionOwners.Add(WorldKey, Ability);
		return true;
	}

	void ReleaseReactionSlot(UGA_Overwatch* Ability)
	{
		UWorld* World = Ability ? Ability->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}

		const TWeakObjectPtr<UWorld> WorldKey(World);
		if (const TWeakObjectPtr<UGA_Overwatch>* Existing = GReactionOwners.Find(WorldKey))
		{
			if (!Existing->IsValid() || Existing->Get() == Ability)
			{
				GReactionOwners.Remove(WorldKey);
			}
		}
	}
}

UGA_Overwatch::UGA_Overwatch()
{
	// Пока способность активна, юнит несёт тег State.Overwatch;
	// он же блокирует повторную активацию (двойной Overwatch невозможен).
	ActivationOwnedTags.AddTag(TacticsGameplayTags::State_Overwatch);
	ActivationBlockedTags.AddTag(TacticsGameplayTags::State_Overwatch);

	// XCOM-правило: наблюдение завершает активацию юнита (остаток AP сгорает).
	bConsumesAllRemainingAP = true;

	DamageEffect = UGE_ShotDamage::StaticClass();
}

void UGA_Overwatch::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerData)
{
	// CommitAbility проверит и спишет 1 AP (UTacticalAbility::CheckCost/ApplyCost).
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ReactionShotsUsed = 0;
	BoundPerception = GetOwnerPerception();
	if (BoundPerception)
	{
		BoundPerception->OnTargetPerceptionUpdated.AddDynamic(this, &UGA_Overwatch::HandlePerceptionUpdated);
	}

	MoveStartLocations.Reset();
	ReactedThisMove.Reset();

	// Способность снимается сама, когда ход возвращается нашей стороне или бой кончается.
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (UWorld* World = Avatar ? Avatar->GetWorld() : nullptr)
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->OnTurnStarted.AddDynamic(this, &UGA_Overwatch::HandleTurnStarted);
			TurnManager->OnCombatEnded.AddDynamic(this, &UGA_Overwatch::HandleCombatEnded);
		}

		// ВТОРОЙ ТРИГГЕР (W1): движение уже видимого врага. Без него бот,
		// заставший отряд в поле зрения, не реагировал бы вообще — стимула
		// «увидел» больше не будет.
		World->GetTimerManager().SetTimer(ReactionCheckTimer, this,
			&UGA_Overwatch::CheckMovingTargets, FMath::Max(0.02f, ReactionCheckInterval), /*bLoop=*/true);
	}

	if (AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
	{
		Unit->NotifyUnitStateChanged();
		if (UTacticalQuestEvents::IsPlayerSideUnit(Unit, Unit))
		{
			UTacticalQuestEvents::BroadcastQuestEvent(Unit,
				TacticalQuestTags::Event_Tactical_Ability_Overwatch_Activated, Unit);
		}
	}
}

void UGA_Overwatch::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Сначала закрываем все источники новых реакций, затем отпускаем mover.
	if (BoundPerception)
	{
		BoundPerception->OnTargetPerceptionUpdated.RemoveDynamic(this, &UGA_Overwatch::HandlePerceptionUpdated);
		BoundPerception = nullptr;
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (UWorld* World = Avatar ? Avatar->GetWorld() : nullptr)
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->OnTurnStarted.RemoveDynamic(this, &UGA_Overwatch::HandleTurnStarted);
			TurnManager->OnCombatEnded.RemoveDynamic(this, &UGA_Overwatch::HandleCombatEnded);
		}
		World->GetTimerManager().ClearTimer(ReactionCheckTimer);
	}

	if (ReactionAction.IsActive())
	{
		const FTacticalFireActionContext FinishedAction = ReactionAction;
		const FGuid ActionId = FinishedAction.ActionId;
		const bool bShotCommitted = FinishedAction.bShotCommitted;
		ClearReactionActionWatchdog();
		// Снимаем action/slot до внешних callback от montage, mover и camera.
		ReactionAction.Reset();
		ReleaseReactionSlot(this);
		StopReactionMontage(FinishedAction);
		ResumeReactionMover();
		EndReactionPresentation();
		OnReactionActionTerminated(ActionId, bShotCommitted, /*bAborted=*/true);
	}
	else
	{
		ResumeReactionMover();
		ReleaseReactionSlot(this);
	}
	MoveStartLocations.Reset();
	ReactedThisMove.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Overwatch::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	// Реагируем только на факт «увидел» (успешно воспринятый стимул).
	// Всё остальное — общие правила реакции, они в TryReactTo.
	if (Stimulus.WasSuccessfullySensed())
	{
		TryReactTo(Actor);
	}
}

bool UGA_Overwatch::TryReactTo(AActor* Target)
{
	if (!Target || ReactionShotsUsed >= MaxReactionShots || ReactionAction.IsActive())
	{
		return false;
	}

	// Одна реакция на одно непрерывное перемещение цели: иначе появление врага
	// (перцепция) и его же движение дали бы два выстрела за один шаг.
	if (ReactedThisMove.Contains(TObjectKey<AActor>(Target)))
	{
		return false;
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return false;
	}

	// Реакция — только в ЧУЖУЮ фазу хода (в свою юнит стреляет обычными действиями).
	if (const UWorld* World = Avatar->GetWorld())
	{
		if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			if (TurnManager->IsInCombat() && TurnManager->IsUnitOnActiveSide(Avatar))
			{
				return false;
			}
		}
	}

	// Реакция подчиняется ОБЩЕМУ правилу выстрела (враждебность, живость,
	// дальность оружия, линия огня) — тому же, что у игрока и у AI. Радиус
	// перцепции шире дальности стрельбы: без этого юнит реагировал бы на цели,
	// в которые физически не может попасть.
	const AUnitBase* ShooterUnit = Cast<AUnitBase>(Avatar);
	if (!ShooterUnit || !DamageEffect || !UGA_Attack::CanTargetActor(ShooterUnit, Target)
		|| !UTacticsCombatStatics::HasLineOfSight(ShooterUnit, Target))
	{
		return false;
	}

	// Эпизод помечается до BP callback: синхронный abort/end не должен после
	// возврата из события повторно добавить stale запись.
	const TObjectKey<AActor> TargetKey(Target);
	ReactedThisMove.Add(TargetKey);
	if (!BeginReactionAction(Target))
	{
		ReactedThisMove.Remove(TargetKey);
		return false;
	}
	return true;
}

void UGA_Overwatch::CheckMovingTargets()
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	const UTurnManagerSubsystem* TurnManager =
		World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager || ReactionShotsUsed >= MaxReactionShots || ReactionAction.IsActive())
	{
		return;
	}

	// Ранний выход в свою фазу: TryReactTo отсёк бы всё равно, но тогда мы зря
	// прогоняли бы линию огня по каждому врагу каждые 0.1 с своего же хода.
	if (TurnManager->IsInCombat() && TurnManager->IsUnitOnActiveSide(Avatar))
	{
		return;
	}

	for (AActor* Enemy : TurnManager->GetOpposingUnits(Avatar))
	{
		if (!Enemy || !UTacticsCombatStatics::IsUnitAlive(Enemy))
		{
			continue;
		}
		const TObjectKey<AActor> Key(Enemy);

		// Встал — перемещение закончилось: следующее будет считаться заново, и по
		// нему снова можно отработать (за пределами лимита MaxReactionShots).
		if (!UTacticsCombatStatics::IsUnitInTransit(Enemy))
		{
			MoveStartLocations.Remove(Key);
			ReactedThisMove.Remove(Key);
			continue;
		}

		// Первый кадр движения — запоминаем точку старта, но не стреляем: нужно,
		// чтобы цель прошла заметное расстояние (аналог клетки XCOM).
		const FVector* Start = MoveStartLocations.Find(Key);
		if (!Start)
		{
			MoveStartLocations.Add(Key, Enemy->GetActorLocation());
			continue;
		}
		if (FVector::Dist2D(*Start, Enemy->GetActorLocation()) < ReactionMinTravel)
		{
			continue;
		}

		if (TryReactTo(Enemy))
		{
			return; // выстрел мог завершить способность — дальше идти нельзя
		}
	}
}

void UGA_Overwatch::HandleTurnStarted(ETurnPhase Phase)
{
	// Ход вернулся нашей стороне — режим наблюдения заканчивается.
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}
	if (const UWorld* World = Avatar->GetWorld())
	{
		if (const UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			if (TurnManager->IsUnitOnActiveSide(Avatar))
			{
				EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
			}
		}
	}
}

void UGA_Overwatch::HandleCombatEnded(bool /*bPlayerWon*/)
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

bool UGA_Overwatch::GetReactionActionInProgressFor(const AUnitBase* Unit, FGuid& OutActionId)
{
	OutActionId.Invalidate();
	if (!Unit || !Unit->OverwatchAbilityClass)
	{
		return false;
	}

	UAbilitySystemComponent* ASC = const_cast<AUnitBase*>(Unit)->GetAbilitySystemComponent();
	FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromClass(Unit->OverwatchAbilityClass) : nullptr;
	const UGA_Overwatch* Overwatch = nullptr;
	if (Spec && Spec->IsActive())
	{
		Overwatch = Cast<UGA_Overwatch>(Spec->GetPrimaryInstance());
		if (!Overwatch || !Overwatch->ReactionAction.IsActive())
		{
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			{
				const UGA_Overwatch* Candidate = Cast<UGA_Overwatch>(Instance);
				if (Candidate && Candidate->ReactionAction.IsActive())
				{
					Overwatch = Candidate;
					break;
				}
			}
		}
	}
	if (!Overwatch || !Overwatch->ReactionAction.IsActive())
	{
		return false;
	}

	OutActionId = Overwatch->ReactionAction.ActionId;
	return true;
}

bool UGA_Overwatch::AcceptFireCommitMontageInstance(
	const FGuid& ActionId, int32 MontageInstanceId)
{
	return ReactionAction.Matches(ActionId) &&
		ReactionAction.TryBindMontageInstance(MontageInstanceId);
}

UAnimMontage* UGA_Overwatch::GetReactionActionPresentation(const FGuid& ActionId,
	EFiringStance& OutStance, FVector& OutHomeRootLocation,
	FVector& OutPresentationRootLocation) const
{
	OutStance = EFiringStance::Open;
	OutHomeRootLocation = FVector::ZeroVector;
	OutPresentationRootLocation = FVector::ZeroVector;
	if (!ReactionAction.Matches(ActionId))
	{
		return nullptr;
	}

	OutStance = ReactionAction.FiringStance;
	OutHomeRootLocation = ReactionAction.HomeRootLocation;
	OutPresentationRootLocation = ReactionAction.PresentationRootLocation;
	return ReactionAction.FireMontage.Get();
}

bool UGA_Overwatch::BeginReactionAction(AActor* Target)
{
	AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo());
	if (!Unit || !Target || !TryAcquireReactionSlot(this))
	{
		return false;
	}

	EFiringStance FiringStance = EFiringStance::Open;
	FVector FiringEyeLocation = FVector::ZeroVector;
	FVector PresentationRootLocation = Unit->GetActorLocation();
	UAnimMontage* FireMontage = Unit->GetFireMontageFor(
		Target, FiringStance, FiringEyeLocation, PresentationRootLocation);
	if (!DamageEffect || !FireMontage)
	{
		UE_LOG(LogTacticsOverwatchAction, Error,
			TEXT("[ReactionAction] %s: не назначен DamageEffect/fire montage"),
			*GetNameSafe(Unit));
		ReleaseReactionSlot(this);
		return false;
	}

	const float Aim = FMath::Max(0.f,
		UGA_Attack::ComputeEffectiveAim(Unit, Target) - ReactionAimPenalty);
	const float ResolvedHitChance = UTacticsCombatStatics::ComputeHitChance(Unit, Target, Aim);
	ReactionAction.Begin(Unit, Target, FiringEyeLocation, ResolvedHitChance,
		Unit->ShotDamage, Unit->AttackRange, DamageEffect);
	const UCoverDetectionComponent* Cover = Unit->GetCoverDetection();
	ReactionAction.SetPresentation(FireMontage, FiringStance, Unit->GetActorLocation(),
		PresentationRootLocation,
		Cover ? Cover->ActiveCoverAnchor : FVector::ZeroVector,
		Cover ? Cover->ActiveCoverNormal : FVector::ZeroVector,
		Cover ? Cover->ActiveCoverWallId : 0,
		Cover ? Cover->ActiveCoverRevision : 0,
		/*bUsedSquadsight=*/false);
	const FGuid ActionId = ReactionAction.ActionId;

	// Сначала публикуем immutable action snapshot, затем зовём внешнюю camera/presentation-
	// систему: синхронный callback уже увидит корректный ActionId и общий barrier.
	if (const UWorld* World = Unit->GetWorld())
	{
		if (ATacticalPlayerController* PC = Cast<ATacticalPlayerController>(World->GetFirstPlayerController()))
		{
			if (!PC->TryBeginReactionShot(Unit, Target))
			{
				ReactionAction.Reset();
				ReleaseReactionSlot(this);
				return false;
			}
		}
	}

	// Пауза принадлежит reaction action и снимается только terminal callback.
	PausedReactionMover = nullptr;
	if (const APawn* TargetPawn = Cast<APawn>(Target))
	{
		if (AUnitAIController* TargetAI = Cast<AUnitAIController>(TargetPawn->GetController()))
		{
			if (TargetAI->SetMovementPaused(true))
			{
				PausedReactionMover = Target;
			}
		}
	}

	if (UWorld* World = Unit->GetWorld())
	{
		World->GetTimerManager().SetTimer(ReactionActionWatchdogTimer,
			FTimerDelegate::CreateUObject(this, &UGA_Overwatch::HandleReactionActionTimeout, ActionId),
			FMath::Max(1.f, ReactionActionTimeout), /*bLoop=*/false);
	}

	UE_LOG(LogTacticsOverwatchAction, Log,
		TEXT("[ReactionAction] Begin id=%s shooter=%s target=%s chance=%.1f paused=%d"),
		*ActionId.ToString(EGuidFormats::Digits), *GetNameSafe(Unit), *GetNameSafe(Target),
		ResolvedHitChance, PausedReactionMover.IsValid() ? 1 : 0);
	OnReactionActionStarted(Target, ActionId);
	return true;
}

bool UGA_Overwatch::FireCommit(const FGuid& ActionId, bool& bOutHit)
{
	bOutHit = false;
	if (!ReactionAction.CanCommit(ActionId))
	{
		UE_LOG(LogTacticsOverwatchAction, Warning,
			TEXT("[ReactionAction] Reject stale/duplicate commit id=%s active=%s committed=%d"),
			*ActionId.ToString(EGuidFormats::Digits),
			*ReactionAction.ActionId.ToString(EGuidFormats::Digits),
			ReactionAction.bShotCommitted ? 1 : 0);
		return false;
	}
	if (!IsFrozenReactionCommitValid())
	{
		UE_LOG(LogTacticsOverwatchAction, Warning,
			TEXT("[ReactionAction] Reject invalid frozen solution id=%s shooter=%s target=%s"),
			*ActionId.ToString(EGuidFormats::Digits), *GetNameSafe(ReactionAction.Shooter.Get()),
			*GetNameSafe(ReactionAction.Target.Get()));
		return false;
	}

	AActor* Shooter = ReactionAction.Shooter.Get();
	AActor* Target = ReactionAction.Target.Get();
	const FVector ShotOrigin = ReactionAction.FiringEyeLocation;
	const float HitChance = ReactionAction.ResolvedHitChance;
	const float Damage = ReactionAction.Damage;
	const TSubclassOf<UGameplayEffect> EffectClass = ReactionAction.DamageEffectClass;

	ReactionAction.MarkCommitStarted();
	++ReactionShotsUsed;
	bOutHit = UTacticsCombatStatics::ResolveShotMechanics(
		Shooter, Target, HitChance, Damage, EffectClass, ShotOrigin);
	if (ReactionAction.Matches(ActionId))
	{
		ReactionAction.SetCommitResult(bOutHit);
		OnReactionShot(Target, bOutHit);
		if (UTacticalQuestEvents::IsPlayerSideUnit(Shooter, Shooter))
		{
			UTacticalQuestEvents::BroadcastQuestEvent(Shooter,
				TacticalQuestTags::Event_Tactical_Combat_Attack_Overwatch, Shooter);
		}
	}

	UE_LOG(LogTacticsOverwatchAction, Log,
		TEXT("[ReactionAction] Commit id=%s hit=%d used=%d/%d"),
		*ActionId.ToString(EGuidFormats::Digits), bOutHit ? 1 : 0,
		ReactionShotsUsed, MaxReactionShots);
	return true;
}

bool UGA_Overwatch::CompleteReactionAction(const FGuid& ActionId)
{
	if (!ReactionAction.Matches(ActionId))
	{
		return false;
	}
	if (!ReactionAction.bShotCommitted)
	{
		return AbortReactionAction(ActionId);
	}

	ClearReactionActionWatchdog();
	ReactionAction.Reset();
	ReleaseReactionSlot(this);
	ResumeReactionMover();
	EndReactionPresentation();
	OnReactionActionTerminated(ActionId, /*bShotCommitted=*/true, /*bAborted=*/false);

	if (ReactionShotsUsed >= MaxReactionShots)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
	CheckCombatOutcomeAfterReaction();
	return true;
}

bool UGA_Overwatch::AbortReactionAction(const FGuid& ActionId)
{
	if (!ReactionAction.Matches(ActionId))
	{
		return false;
	}

	const FTacticalFireActionContext FinishedAction = ReactionAction;
	const bool bShotCommitted = FinishedAction.bShotCommitted;
	ClearReactionActionWatchdog();
	ReactionAction.Reset();
	ReleaseReactionSlot(this);
	StopReactionMontage(FinishedAction);
	ResumeReactionMover();
	EndReactionPresentation();
	OnReactionActionTerminated(ActionId, bShotCommitted, /*bAborted=*/true);

	// После commit квота уже потрачена; parent снимается только после cleanup.
	if (bShotCommitted && ReactionShotsUsed >= MaxReactionShots)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
	if (bShotCommitted)
	{
		CheckCombatOutcomeAfterReaction();
	}
	return true;
}

void UGA_Overwatch::HandleReactionActionTimeout(FGuid ActionId)
{
	if (ReactionAction.Matches(ActionId))
	{
		UE_LOG(LogTacticsOverwatchAction, Error,
			TEXT("[ReactionAction] Watchdog abort id=%s phase=%d committed=%d"),
			*ActionId.ToString(EGuidFormats::Digits), static_cast<int32>(ReactionAction.Phase),
			ReactionAction.bShotCommitted ? 1 : 0);
		AbortReactionAction(ActionId);
	}
}

void UGA_Overwatch::ClearReactionActionWatchdog()
{
	if (AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		if (UWorld* World = Avatar->GetWorld())
		{
			World->GetTimerManager().ClearTimer(ReactionActionWatchdogTimer);
		}
	}
}

void UGA_Overwatch::ResumeReactionMover()
{
	const TWeakObjectPtr<AActor> Mover = PausedReactionMover;
	PausedReactionMover = nullptr;
	if (const APawn* MoverPawn = Cast<APawn>(Mover.Get()))
	{
		if (AUnitAIController* MoverAI = Cast<AUnitAIController>(MoverPawn->GetController()))
		{
			MoverAI->SetMovementPaused(false);
		}
	}
}

bool UGA_Overwatch::IsFrozenReactionCommitValid() const
{
	const AUnitBase* Shooter = Cast<AUnitBase>(ReactionAction.Shooter.Get());
	const AActor* Target = ReactionAction.Target.Get();
	if (!Shooter || !Target || Shooter->IsDead() || Shooter->IsDowned() || Shooter->IsEvacuated()
		|| !UTacticsCombatStatics::IsUnitAlive(Target)
		|| !UTacticsCombatStatics::AreHostile(Shooter, Target))
	{
		return false;
	}
	if (FVector::Dist(ReactionAction.FiringEyeLocation, Target->GetActorLocation()) > ReactionAction.MaxRange)
	{
		return false;
	}
	return UTacticsCombatStatics::HasLineOfSightFromFrozenOrigin(
		Shooter->GetWorld(), ReactionAction.FiringEyeLocation, Target);
}

void UGA_Overwatch::EndReactionPresentation() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (ATacticalPlayerController* PC = World
		? Cast<ATacticalPlayerController>(World->GetFirstPlayerController())
		: nullptr)
	{
		PC->EndReactionShotPresentation();
	}
}

void UGA_Overwatch::StopReactionMontage(
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

void UGA_Overwatch::CheckCombatOutcomeAfterReaction() const
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

UAIPerceptionComponent* UGA_Overwatch::GetOwnerPerception() const
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	if (!Info)
	{
		return nullptr;
	}

	// Perception обычно живёт на AIController пешки; ищем и там, и на самом аватаре.
	if (const APawn* Pawn = Cast<APawn>(Info->AvatarActor.Get()))
	{
		if (AController* C = Pawn->GetController())
		{
			if (UAIPerceptionComponent* P = C->FindComponentByClass<UAIPerceptionComponent>())
			{
				return P;
			}
		}
	}
	if (AActor* Avatar = Info->AvatarActor.Get())
	{
		return Avatar->FindComponentByClass<UAIPerceptionComponent>();
	}
	return nullptr;
}
