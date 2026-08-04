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
#include "CombatFeedbackSubsystem.h"
#include "AbilitySystemComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"

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
	DisplayName = NSLOCTEXT("XRU1", "AbilityOverwatchName", "Наблюдение");
	Description = NSLOCTEXT("XRU1", "AbilityOverwatchDesc",
		"Боец берёт сектор под прицел и стреляет по первому, кто войдёт в его линию огня.");

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
		Unit->PlayUnitSound(EUnitSoundEvent::OverwatchEnter);
		// Всплывающее «НАБЛЮДЕНИЕ» над взведённым бойцом (09_UI_HUD §4).
		if (UCombatFeedbackSubsystem* Feedback = UCombatFeedbackSubsystem::Get(Unit))
		{
			Feedback->ShowStatusText(Unit,
				NSLOCTEXT("XRU1.Feedback", "OverwatchArmed", "НАБЛЮДЕНИЕ"));
		}
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

	// ⚠️ Мир — у способности, не через аватара: при выходе из PIE GAS отменяет
	// способности уже после сброса ActorInfo, и обращение к аватару ловит
	// ensure(CurrentActorInfo) (см. тот же фикс в UGA_Attack::EndAbility).
	if (UWorld* World = GetWorld())
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->OnTurnStarted.RemoveDynamic(this, &UGA_Overwatch::HandleTurnStarted);
			TurnManager->OnCombatEnded.RemoveDynamic(this, &UGA_Overwatch::HandleCombatEnded);
		}
		World->GetTimerManager().ClearTimer(ReactionCheckTimer);
		World->GetTimerManager().ClearTimer(ReactionPresentationDelayTimer);
		World->GetTimerManager().ClearTimer(ReactionPostHoldTimer);
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
		ReleasePresentationStanding(); // боец садится вместе с уходом камеры
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
	//
	// ⚠️ Линия огня спрашивается ТОЛЬКО через `CanTargetActor` — второй,
	// параллельной проверки здесь быть не должно. Прежний дубль
	// (`HasLineOfSight`) отвечал на вопрос видимости, а не огневого решения:
	// реакция запускалась по одному предикату, а замороженную точку выстрела
	// потом отклонял другой («Reject invalid frozen solution», лог PIE 2026-08-04).
	const AUnitBase* ShooterUnit = Cast<AUnitBase>(Avatar);
	if (!ShooterUnit || !DamageEffect || !UGA_Attack::CanTargetActor(ShooterUnit, Target))
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

		// Встал — точка старта следующего перемещения посчитается заново.
		// ReactedThisMove здесь НЕ чистим (v2.9): пауза мовера реакцией
		// выглядела как остановка, метка стиралась, и по той же цели уходила
		// вторая реакция без форса. Одна реакция на цель за фазу.
		if (!UTacticsCombatStatics::IsUnitInTransit(Enemy))
		{
			MoveStartLocations.Remove(Key);
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
	float ResolvedHitChance = UTacticsCombatStatics::ComputeHitChance(Unit, Target, Aim);
	float ReactionDamage = Unit->ShotDamage;

	// Сценарный форс обучения действует и на реакционный выстрел: «следующий
	// выстрел бойца» — это и Overwatch-реакция (шаг C1 туториала требует
	// гарантированные «пол-HP» по Holo_D). Форс меняет только числа snapshot'а,
	// roll/GE/камера/quest-события остаются общим pipeline — как в GA_Attack.
	FScriptedShotOverride ScriptedShot;
	bConsumedScriptedShotValid = false;
	if (Unit->ConsumePendingScriptedShot(Target, ScriptedShot))
	{
		if (ScriptedShot.bOverrideHitChance)
		{
			ResolvedHitChance = ScriptedShot.HitChancePercent;
		}
		if (ScriptedShot.bOverrideDamage)
		{
			ReactionDamage = ScriptedShot.Damage;
		}
		// Запоминаем потреблённый форс: abort сорванного монтажа ДО commit
		// обязан вернуть его юниту, иначе повторная попытка идёт с общим
		// шансом и учебное «гарантированное попадание» превращается в промах.
		ConsumedScriptedShot = ScriptedShot;
		bConsumedScriptedShotValid = true;
		UE_LOG(LogTacticsOverwatchAction, Log,
			TEXT("[ReactionAction] Scripted reaction %s → %s: chance=%.0f damage=%.0f"),
			*GetNameSafe(Unit), *GetNameSafe(Target), ResolvedHitChance, ReactionDamage);
	}

	ReactionAction.Begin(Unit, Target, FiringEyeLocation, ResolvedHitChance,
		ReactionDamage, Unit->AttackRange, DamageEffect);
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

	// Состав презентации реакции печатается тем же набором полей, что и у обычной
	// атаки: стойка объясняет, встаёт ли боец из укрытия, отклонение корпуса —
	// сколько предстоит довернуть до выстрела (см. фазу [AimTurn]).
	UE_LOG(LogTacticsOverwatchAction, Display,
		TEXT("[ReactionAction] Begin id=%s shooter=%s target=%s chance=%.1f paused=%d стойка=%s montage=%s дистанция=%.0f отклонение корпуса=%.1f° settle=%.2f с"),
		*ActionId.ToString(EGuidFormats::Digits), *GetNameSafe(Unit), *GetNameSafe(Target),
		ResolvedHitChance, PausedReactionMover.IsValid() ? 1 : 0,
		FiringStance == EFiringStance::OverCover ? TEXT("OverCover")
			: FiringStance == EFiringStance::StepOut ? TEXT("StepOut") : TEXT("Open"),
		*GetNameSafe(FireMontage),
		FVector::Dist(Unit->GetActorLocation(), Target->GetActorLocation()),
		UTacticsCombatStatics::GetFacingErrorDegrees(Unit, Target->GetActorLocation()),
		PreReactionCameraSettleDelay);

	// Доворот реагирующего с места идёт ВНУТРИ паузы наводки камеры — так же,
	// как у обычной атаки. StepOut доворачивается позже, latent-фазой BP: до
	// прибытия на огневую точку разворачивать бойца бессмысленно.
	if (FiringStance == EFiringStance::Open)
	{
		StartAimTurnTowardsTarget(ActionId, TEXT("вместе с наводкой камеры реакции"));
	}

	// Цель уже замерла, камера наведена — короткая пауза, ПОТОМ выстрел:
	// иначе кадр «дёргается и сразу едет дальше» (фидбэк по реакции Танка).
	// Таймер живёт в ИГРОВОМ времени, а slow-mo реакции уже включён — без
	// умножения на дилатацию «0.9 с» растягивались в несколько реальных секунд
	// («овервотч ушёл в паузу», лог 2026-08-02).
	if (PreReactionCameraSettleDelay > 0.f)
	{
		if (UWorld* World = Unit->GetWorld())
		{
			const float Dilation = FMath::Max(0.05f, UGameplayStatics::GetGlobalTimeDilation(World));
			World->GetTimerManager().SetTimer(ReactionPresentationDelayTimer,
				FTimerDelegate::CreateUObject(this, &UGA_Overwatch::StartReactionPresentation, ActionId),
				PreReactionCameraSettleDelay * Dilation, /*bLoop=*/false);
			return true;
		}
	}
	OnReactionActionStarted(Target, ActionId);
	return true;
}

float UGA_Overwatch::GetPresentationHoldDelay(const FGuid& ActionId) const
{
	// Сорванная реакция кадр не держит — возврат в укрытие сразу (как у атаки).
	if (!ReactionAction.Matches(ActionId) || !ReactionAction.bShotCommitted)
	{
		return 0.f;
	}
	// Симметрично атаке: смерть цели — это анимация падения, её держим дольше.
	const AActor* ReactionTarget = ReactionAction.Target.Get();
	const bool bTargetKilled = ReactionTarget && !UTacticsCombatStatics::IsUnitAlive(ReactionTarget);
	return bTargetKilled ? PostReactionKillHoldDelay : PostReactionHoldDelay;
}

void UGA_Overwatch::StartReactionPresentation(FGuid ActionId)
{
	// Реакцию могли abort'нуть за время паузы — устаревший таймер молчит.
	if (ReactionAction.Matches(ActionId))
	{
		UE_LOG(LogTacticsOverwatchAction, Display,
			TEXT("[ReactionAction] Presentation start id=%s (после паузы %.2f с)"),
			*ActionId.ToString(EGuidFormats::Digits), PreReactionCameraSettleDelay);
		OnReactionActionStarted(ReactionAction.Target.Get(), ActionId);
	}
	else
	{
		UE_LOG(LogTacticsOverwatchAction, Warning,
			TEXT("[ReactionAction] Presentation start id=%s ОТМЕНЁН: транзакция уже закрыта"),
			*ActionId.ToString(EGuidFormats::Digits));
	}
}

void UGA_Overwatch::FinishReactionPostHold(FGuid ActionId)
{
	if (ReactionAction.Matches(ActionId))
	{
		UE_LOG(LogTacticsOverwatchAction, Display,
			TEXT("[ReactionAction] Post-hold %.2f с истёк id=%s → терминал"),
			PostReactionHoldDelay, *ActionId.ToString(EGuidFormats::Digits));
		ReactionPostHoldDoneId = ActionId;
		CompleteReactionAction(ActionId);
	}
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

	// Та же страховка, что у обычной атаки (см. EnsureFacingAtCommit).
	EnsureFacingAtCommit(ActionId);

	ReactionAction.MarkCommitStarted();
	bConsumedScriptedShotValid = false; // форс исполнен — возврат больше не нужен
	++ReactionShotsUsed;
	bOutHit = UTacticsCombatStatics::ResolveShotMechanics(
		Shooter, Target, HitChance, Damage, EffectClass, ShotOrigin);
	if (ReactionAction.Matches(ActionId))
	{
		ReactionAction.SetCommitResult(bOutHit);
		if (AUnitBase* ShooterUnit = Cast<AUnitBase>(Shooter))
		{
			ShooterUnit->PlayUnitSound(EUnitSoundEvent::ReactionFire);
			ShooterUnit->PlayShotVfx(Target, bOutHit, ShotOrigin);
		}
		OnReactionShot(Target, bOutHit);
		if (UTacticalQuestEvents::IsPlayerSideUnit(Shooter, Shooter))
		{
			UTacticalQuestEvents::BroadcastQuestEventEx(Shooter,
				TacticalQuestTags::Event_Tactical_Combat_Attack_Overwatch, Shooter, Target);
		}
	}

	// Отклонение корпуса в момент реакции — та же приёмочная метрика, что у атаки.
	UE_LOG(LogTacticsOverwatchAction, Display,
		TEXT("[ReactionAction] Commit id=%s hit=%d used=%d/%d отклонение корпуса от цели=%.1f°"),
		*ActionId.ToString(EGuidFormats::Digits), bOutHit ? 1 : 0,
		ReactionShotsUsed, MaxReactionShots,
		Target ? UTacticsCombatStatics::GetFacingErrorDegrees(Shooter, Target->GetActorLocation()) : 0.f);
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

	// Удержание кадра: цель остаётся замершей, урон читается — потом резюм.
	// Slow-mo ещё активен (его снимает терминал) — умножаем на дилатацию.
	// Убитая реакцией цель держится дольше (симметрично GA_Attack): падение —
	// это анимация, а не мгновенная цифра.
	const AActor* ReactionTarget = ReactionAction.Target.Get();
	const bool bTargetKilled = ReactionTarget && !UTacticsCombatStatics::IsUnitAlive(ReactionTarget);
	const float HoldDelay = bTargetKilled ? PostReactionKillHoldDelay : PostReactionHoldDelay;

	if (HoldDelay > 0.f && ReactionPostHoldDoneId != ActionId)
	{
		if (const AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			if (UWorld* World = Avatar->GetWorld())
			{
				const float Dilation = FMath::Max(0.05f, UGameplayStatics::GetGlobalTimeDilation(World));
				UE_LOG(LogTacticsOverwatchAction, Display,
					TEXT("[ReactionAction] Кадр удерживается %.2f с (%s) id=%s"),
					HoldDelay, bTargetKilled ? TEXT("цель убита") : TEXT("post-hold"),
					*ActionId.ToString(EGuidFormats::Digits));
				World->GetTimerManager().SetTimer(ReactionPostHoldTimer,
					FTimerDelegate::CreateUObject(this, &UGA_Overwatch::FinishReactionPostHold, ActionId),
					HoldDelay * Dilation, /*bLoop=*/false);
				return true;
			}
		}
	}

	UE_LOG(LogTacticsOverwatchAction, Display,
		TEXT("[ReactionAction] Complete id=%s: камера/мовер/худы возвращаются"),
		*ActionId.ToString(EGuidFormats::Digits));
	ClearReactionActionWatchdog();
	ReactionAction.Reset();
	ReleaseReactionSlot(this);
	ResumeReactionMover();
	EndReactionPresentation();
	ReleasePresentationStanding(); // боец садится вместе с уходом камеры
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

	// Abort раньше был немым — источник сорванной транзакции не находился по логу.
	UE_LOG(LogTacticsOverwatchAction, Display,
		TEXT("[ReactionAction] ABORT id=%s phase=%d committed=%d shooter=%s target=%s"),
		*ActionId.ToString(EGuidFormats::Digits), static_cast<int32>(FinishedAction.Phase),
		bShotCommitted ? 1 : 0, *GetNameSafe(FinishedAction.Shooter.Get()),
		*GetNameSafe(FinishedAction.Target.Get()));

	// Сорванная ДО выстрела реакция возвращает потреблённый форс: учебное
	// «гарантированное попадание» не должно сгорать на прерванном монтаже.
	if (!bShotCommitted && bConsumedScriptedShotValid)
	{
		if (AUnitBase* ShooterUnit = Cast<AUnitBase>(FinishedAction.Shooter.Get()))
		{
			ShooterUnit->SetPendingScriptedShot(ConsumedScriptedShot, FinishedAction.Target.Get());
			UE_LOG(LogTacticsOverwatchAction, Display,
				TEXT("[ReactionAction] Форс возвращён %s после сорванной реакции"),
				*GetNameSafe(ShooterUnit));
		}
	}
	bConsumedScriptedShotValid = false;
	ClearReactionActionWatchdog();
	ReactionAction.Reset();
	ReleaseReactionSlot(this);
	StopReactionMontage(FinishedAction);
	ResumeReactionMover();
	EndReactionPresentation();
	ReleasePresentationStanding(); // боец садится вместе с уходом камеры
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
	// Мир — у способности: путь идёт из EndAbility, где ActorInfo при выходе
	// из PIE уже сброшен (ensure(CurrentActorInfo), запись 2026-08-03).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReactionActionWatchdogTimer);
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
	const UWorld* World = GetWorld();
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
	const UWorld* World = GetWorld();
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
