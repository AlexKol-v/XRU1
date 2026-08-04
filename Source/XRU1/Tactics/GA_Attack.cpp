#include "GA_Attack.h"
#include "UnitBase.h"
#include "CoverDetectionComponent.h"
#include "ScenarioActorRegistry.h"
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

bool UGA_Attack::IsSquadsightShot(const AUnitBase* Shooter, const AActor* Target)
{
	// ЕДИНОЕ определение «стреляю по наводке отряда»: цель дальше СОБСТВЕННОГО
	// обзора бойца. Оно же решает допуск цели в GetTargetStatus, оно же метит
	// транзакцию выстрела, оно же начисляет штраф к точности — три места с
	// одинаковым условием разъезжались бы при первой же правке.
	return Shooter && Target && Shooter->bHasSquadsight
		&& FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation())
			> UTacticsCombatStatics::SquadVisionRange;
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

		// 3) Squadsight-выстрел — штраф (GDD §5.4: «цель дальше собственного
		// обзора, обнаружена союзником» = −10). Берём из CDO способности атаки
		// ЭТОГО юнита: HUD и выстрел считают одно и то же даже при
		// перенастроенном BP-наследнике GA_Attack.
		//
		// ⚠️ Условие — ДИСТАНЦИЯ, а не `!HasLineOfSight`. На отсутствии линии огня
		// эта ветка была недостижима: `ComputeEffectiveAim` зовётся только после
		// `CanTargetActor`, а тот требует геометрию ВСЕГДА (сквозь стены не
		// стреляет никто) — штраф не применялся ни разу, вопреки GDD.
		if (IsSquadsightShot(Shooter, Target))
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
	// Staged-актор сценария физически стоит на карте, но скрыт и не в бою:
	// целиться в невидимую голограмму следующей секции нельзя ни игроку, ни AI.
	if (!UTacticalScenarioSubsystem::IsActorScenarioActive(Target))
	{
		return EAttackTargetStatus::Dead;
	}

	const float Distance = FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation());
	if (Distance > Shooter->AttackRange)
	{
		return EAttackTargetStatus::OutOfRange;
	}

	// Геометрическая линия огня обязательна ВСЕГДА — сквозь стены не стреляет
	// никто (модель XCOM 2). Squadsight ниже расширяет только ОБНАРУЖЕНИЕ,
	// а не геометрию.
	//
	// ⚠️ Спрашиваем именно ОГНЕВОЕ РЕШЕНИЕ (`FindFiringSolution`), а не видимость
	// (`HasLineOfSight`). Это тот же перебор, который потом даст замороженную
	// точку выстрела, и та же проверка, которой activation отклоняет действие, —
	// одна истина на HUD, AI и активацию. Пока здесь стояла видимость, её более
	// широкий набор точек (корпус стрелка, быстрый путь без огневых позиций)
	// показывал игроку шанс на выстрел, который activation тут же отклонял
	// («Reject at activation: из замороженной позиции нет линии огня»).
	FVector FiringEye = FVector::ZeroVector;
	EFiringStance Stance = EFiringStance::Open;
	if (!UTacticsCombatStatics::FindFiringSolution(Shooter, Target, FiringEye, Stance))
	{
		return EAttackTargetStatus::NoLineOfSight;
	}

	// Обнаружение: цель в СОБСТВЕННОМ обзоре бойца (тот же радиус, что у тумана
	// войны), либо «Прицел отряда» — цель дальше своего обзора, но её видит
	// любой союзник. Так снайпер бьёт через полкарты по вскрытым отрядом целям,
	// но никогда — по тем, кого не видит никто.
	if (Distance > UTacticsCombatStatics::SquadVisionRange)
	{
		if (!IsSquadsightShot(Shooter, Target) ||
			!UTacticsCombatStatics::SquadHasLineOfSight(Shooter, Target))
		{
			return EAttackTargetStatus::OutOfSight;
		}
	}
	return EAttackTargetStatus::Valid;
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

	// Все проверки ДО Commit: при провале AP не списываются. Сценарный выстрел
	// НЕ обходит LOS: видимость симметрична (Ф5 — цель выглядывает из-за края
	// так же, как стрелок), поэтому если боец может стрелять по голограмме из
	// своего укрытия, голограмма обязана видеть его через тот же peek-луч.
	if (!Shooter || !DamageEffect || !CanTargetActor(Shooter, Target))
	{
		// Отказ активации обязан называть причину: немое «не активировалась»
		// стоило дня отладки. Печатаем статус и число точек каждой стороны —
		// по нему сразу видно, чей peek-набор не построился.
		if (Shooter && Target)
		{
			const EAttackTargetStatus Status = GetTargetStatus(Shooter, Target);
			int32 ShooterPoints = 0, TargetPoints = 0;
			if (const UWorld* World = Shooter->GetWorld())
			{
				const float EyeOffset =
					UTacticsCombatStatics::GetCoverTuning(World)->EyeHeightOffset;
				TArray<FVector, TInlineAllocator<4>> Points;
				UTacticsCombatStatics::GetFiringPositions(World, Shooter,
					Shooter->GetActorLocation() + FVector(0.f, 0.f, EyeOffset),
					Target->GetActorLocation(), Points);
				ShooterPoints = Points.Num();
				UTacticsCombatStatics::GetTargetExposedPoints(World, Target,
					Shooter->GetActorLocation() + FVector(0.f, 0.f, EyeOffset), Points);
				TargetPoints = Points.Num();
			}
			UE_LOG(LogTacticsAttackAction, Warning,
				TEXT("[FireAction] ОТКАЗ активации %s → %s: статус=%s, огневых точек стрелка=%d, exposed-точек цели=%d"),
				*GetNameSafe(Shooter), *GetNameSafe(Target),
				Status == EAttackTargetStatus::Dead ? TEXT("Dead/Inactive")
					: Status == EAttackTargetStatus::OutOfRange ? TEXT("OutOfRange")
					: Status == EAttackTargetStatus::NoLineOfSight ? TEXT("NoLineOfSight")
					: TEXT("NotHostile/другое"),
				ShooterPoints, TargetPoints);
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Snapshot строится ДО оплаты AP. Контекст публикуется до CommitAbility,
	// чтобы синхронный OnActionPointsChanged уже видел action barrier.
	const UActionPointsComponent* ActionPoints = FindActionPoints(ActorInfo);
	const int32 ActionPointsBefore = ActionPoints ? ActionPoints->CurrentActionPoints : INDEX_NONE;

	// Сценарный форс шага обучения меняет только входные числа snapshot'а: roll,
	// GE урона, HitReact, камера и quest-события остаются общим pipeline.
	float ResolvedHitChance = ComputeAttackHitChance(Shooter, Target);
	float ResolvedDamage = Shooter->ShotDamage;
	FScriptedShotOverride ScriptedShot;
	bConsumedScriptedShotValid = false;
	if (Shooter->ConsumePendingScriptedShot(Target, ScriptedShot))
	{
		if (ScriptedShot.bOverrideHitChance)
		{
			ResolvedHitChance = ScriptedShot.HitChancePercent;
		}
		if (ScriptedShot.bOverrideDamage)
		{
			ResolvedDamage = ScriptedShot.Damage;
		}
		// Abort до commit вернёт форс юниту — сорванный монтаж не должен
		// сжигать учебное «гарантированное попадание» (см. GA_Overwatch v2.9).
		ConsumedScriptedShot = ScriptedShot;
		bConsumedScriptedShotValid = true;
		UE_LOG(LogTacticsAttackAction, Log,
			TEXT("[FireAction] Scripted shot %s → %s: chance=%.0f damage=%.0f"),
			*GetNameSafe(Shooter), *GetNameSafe(Target), ResolvedHitChance, ResolvedDamage);
	}

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

	// Squadsight-выстрел = цель дальше собственного обзора (обнаружена союзником);
	// геометрию и союзную видимость уже гарантировал GetTargetStatus выше.
	const bool bUsedSquadsight = IsSquadsightShot(Shooter, Target);
	FireAction.Begin(Shooter, Target, FiringEyeLocation, ResolvedHitChance,
		ResolvedDamage, Shooter->AttackRange, DamageEffect, ActionPointsBefore);
	const UCoverDetectionComponent* Cover = Shooter->GetCoverDetection();
	FireAction.SetPresentation(FireMontage, FiringStance, Shooter->GetActorLocation(),
		PresentationRootLocation,
		Cover ? Cover->ActiveCoverAnchor : FVector::ZeroVector,
		Cover ? Cover->ActiveCoverNormal : FVector::ZeroVector,
		Cover ? Cover->ActiveCoverWallId : 0,
		Cover ? Cover->ActiveCoverRevision : 0,
		bUsedSquadsight);
	const FGuid ActionId = FireAction.ActionId;

	// Замороженное решение проверяется ЗДЕСЬ — до оплаты AP и до монтажа, тем же
	// предикатом, что и commit. Иначе слепое решение (гонка после выбора цели)
	// оплачивало монтаж, отклонялось на commit, возвращало AP — и детерминированный
	// AI повторял его вечно: «монтаж → reject → refund → повтор» (лог 2026-07-30).
	if (!IsFrozenFireCommitValid())
	{
		UE_LOG(LogTacticsAttackAction, Warning,
			TEXT("[FireAction] Reject at activation: из замороженной позиции нет линии огня shooter=%s target=%s"),
			*GetNameSafe(Shooter), *GetNameSafe(Target));
		FireAction.Reset();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

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

	// Полный состав презентации в ОДНОЙ строке: по ней при разборе записи видно,
	// почему выстрел выглядел так, а не иначе — стойка объясняет «выстрелил не
	// вставая из-за укрытия» (Open вместо OverCover), отклонение корпуса —
	// сколько предстоит довернуть, settle — откуда пауза перед montage.
	UE_LOG(LogTacticsAttackAction, Display,
		TEXT("[FireAction] Begin id=%s shooter=%s target=%s chance=%.1f стойка=%s montage=%s дистанция=%.0f отклонение корпуса=%.1f° settle=%.2f с"),
		*ActionId.ToString(EGuidFormats::Digits), *GetNameSafe(Shooter), *GetNameSafe(Target),
		ResolvedHitChance,
		FiringStance == EFiringStance::OverCover ? TEXT("OverCover")
			: FiringStance == EFiringStance::StepOut ? TEXT("StepOut") : TEXT("Open"),
		*GetNameSafe(FireMontage),
		FVector::Dist(Shooter->GetActorLocation(), Target->GetActorLocation()),
		UTacticsCombatStatics::GetFacingErrorDegrees(Shooter, Target->GetActorLocation()),
		PreShotCameraSettleDelay);

	// Здесь НЕТ ResolveShot и EndAbility: BP/C++ coordinator должен доиграть
	// StepOut/montage/ReturnToAnchor и вызвать terminal API.
	//
	// ⚠️ Презентация стартует В ТОТ ЖЕ КАДР, что и команда игрока: раньше здесь
	// стоял таймер на PreShotCameraSettleDelay, и между нажатием и первым
	// движением бойца висела мёртвая пауза («нажимаю — микропауза — юнит
	// выходит»). Теперь ждёт только стрелковая анимация: остаток времени на
	// наводку камеры затянут в фазу доворота (GetCameraSettleRemaining), а
	// выход на огневую точку идёт параллельно наезду кадра.
	PresentationStartTime = Shooter->GetWorld() ? Shooter->GetWorld()->GetTimeSeconds() : 0.0;
	NotifyShotPresentation(Shooter, Target);

	// Доворот стреляющего в ОТКРЫТОМ ПОЛЕ стартует ВМЕСТЕ с камерой: пока кадр
	// едет, боец разворачивается, и к старту montage угол уже сведён.
	//
	// Другие стойки доворачиваются позже и по своим причинам: StepOut — после
	// прибытия на огневую точку (по дороге корпус ведёт path following),
	// OverCover — уже стоя, во время подъёма из-за укрытия (порядок «встал →
	// довернулся → выстрелил», см. ScheduleAimTurnAfterRise).
	if (FiringStance == EFiringStance::Open)
	{
		StartAimTurnTowardsTarget(ActionId, TEXT("вместе с наводкой камеры"));
	}
	UE_LOG(LogTacticsAttackAction, Display,
		TEXT("[FireAction] Презентация стартует сразу; montage подождёт кадр ещё %.2f с id=%s"),
		PreShotCameraSettleDelay, *ActionId.ToString(EGuidFormats::Digits));
	OnFireActionStarted(Target, ActionId);
}

float UGA_Attack::GetCameraSettleRemaining(const FGuid& ActionId) const
{
	if (!FireAction.Matches(ActionId) || PreShotCameraSettleDelay <= 0.f)
	{
		return 0.f;
	}
	const UWorld* World = GetWorld();
	if (!World || PresentationStartTime <= 0.0)
	{
		return 0.f;
	}
	// Отсчёт от НАЧАЛА транзакции: пока боец бежал на огневую точку, кадр уже
	// ехал, поэтому у StepOut остаток обычно нулевой и montage не ждёт ничего.
	//
	// ⚠️ Из ожидания вычитается время ДО ВЫСТРЕЛА ВНУТРИ montage: кадру нужно
	// успеть к моменту выстрела, а не к первому кадру анимации. Без этого боец
	// у полуукрытия неподвижно ждал всю паузу и только потом начинал вставать —
	// «что-то делает, потом встаёт и стреляет» (запись PIE 2026-08-03).
	const double Elapsed = World->GetTimeSeconds() - PresentationStartTime;
	const float MontageLeadIn = FindFireCommitTime(FireAction.FireMontage.Get());
	// Нижняя граница ритма: даже когда анимация сама даёт кадру достаточно
	// времени до выстрела, презентация не начинается в тот же миг, что и
	// команда — иначе выстрел с места вдвое короче выхода из-за угла.
	const float RequiredLeadIn = FMath::Max(
		PreShotCameraSettleDelay - MontageLeadIn, MinMontageLeadIn);
	return FMath::Max(0.f, RequiredLeadIn - static_cast<float>(Elapsed));
}

float UGA_Attack::GetPresentationHoldDelay(const FGuid& ActionId) const
{
	// Держать кадр незачем, если выстрела не было: сорванный montage должен
	// возвращать бойца в укрытие сразу, а не после паузы «на чтение урона».
	if (!FireAction.Matches(ActionId) || !FireAction.bShotCommitted)
	{
		return 0.f;
	}
	// Убитая цель держится дольше: игрок должен увидеть падение, а не только
	// цифру урона (та же логика, что в CompleteFireAction).
	const AActor* ShotTarget = FireAction.Target.Get();
	const bool bTargetKilled = ShotTarget && !UTacticsCombatStatics::IsUnitAlive(ShotTarget);
	return bTargetKilled ? PostKillHoldDelay : PostShotHoldDelay;
}

void UGA_Attack::FinishPostShotHold(FGuid ActionId)
{
	// Кадр удержан — доводим отложенный терминал той же транзакции.
	if (FireAction.Matches(ActionId))
	{
		PostHoldDoneActionId = ActionId;
		CompleteFireAction(ActionId);
	}
}

void UGA_Attack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Отложенные таймеры презентации не должны пережить транзакцию.
	//
	// ⚠️ Мир берём у СПОСОБНОСТИ, а не через GetAvatarActorFromActorInfo():
	// при выходе из PIE GAS отменяет способности уже после сброса ActorInfo, и
	// тот путь ловил ensure(CurrentActorInfo) с пятисекундным дампом стека в лог
	// (поймано в записи 2026-08-03).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PostHoldTimer);
	}

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
		ReleasePresentationStanding(); // боец садится вместе с уходом камеры
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

	// Этот запрос — первый шаг BP-ветки презентации. Его наличие в логе значит
	// «BP получил план», а следующая за ним ветка ([AimTurn] или AI Move To)
	// показывает, куда ветка ушла. Без этой строки зависшая презентация видна
	// только по watchdog'у, и непонятно, дошла ли она вообще до BP.
	UE_LOG(LogTacticsAttackAction, Display,
		TEXT("[FireAction] BP запросил план id=%s: стойка=%s montage=%s"),
		*ActionId.ToString(EGuidFormats::Digits),
		OutStance == EFiringStance::OverCover ? TEXT("OverCover")
			: OutStance == EFiringStance::StepOut ? TEXT("StepOut") : TEXT("Open"),
		*GetNameSafe(FireAction.FireMontage.Get()));
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

	// Последняя проверка читаемости кадра: если плавный доворот не довёл угол,
	// корпус доворачивается здесь — стрелять «в спину» нельзя ни при каких
	// сбоях презентации. В штатном выстреле это no-op (отклонение ~0°).
	EnsureFacingAtCommit(ActionId);

	// Guard ставится ДО callbacks/GE: смерть последней цели не должна позволить
	// reentrant notify повторно применить урон или вернуть AP.
	FireAction.MarkCommitStarted();
	bConsumedScriptedShotValid = false; // форс исполнен — возврат не нужен
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
					// Тот же момент фиксирует цель для сведения огня отряда.
					TurnManager->NotifyUnitTargeted(Target);
				}
			}
		}
	}
	bOutHit = UTacticsCombatStatics::ResolveShotMechanics(
		Shooter, Target, HitChance, Damage, EffectClass, ShotOrigin);
	if (FireAction.Matches(ActionId))
	{
		FireAction.SetCommitResult(bOutHit);
		// Звук выстрела — здесь, а не на старте montage: до commit действие ещё
		// могло быть прервано, и выстрел без урона звучал бы как попадание.
		if (AUnitBase* ShooterUnit = Cast<AUnitBase>(Shooter))
		{
			ShooterUnit->PlayUnitSound(EUnitSoundEvent::Fire);
			// Тот же commit рисует выстрел: трассер и вспышка не могут появиться
			// у действия, которое так и не состоялось.
			ShooterUnit->PlayShotVfx(Target, bOutHit, ShotOrigin);
		}
		OnShotFired(Target, bOutHit);
		// Автоматически публикуем только атаки стороны игрока: обычный выстрел
		// врага не должен закрывать шаг обучения. Scripted enemy shot подтверждает
		// его собственная orchestration-task по своему ActionId.
		if (UTacticalQuestEvents::IsPlayerSideUnit(Shooter, Shooter))
		{
			UTacticalQuestEvents::BroadcastQuestEventEx(Shooter,
				FireAction.bUsedSquadsight
					? TacticalQuestTags::Event_Tactical_Combat_Attack_Squadsight
					: TacticalQuestTags::Event_Tactical_Combat_Attack_Normal,
				Shooter, Target);
		}
	}

	// Отклонение корпуса В МОМЕНТ выстрела — приёмочная метрика доворота:
	// «стреляет ли боец туда, куда смотрит». Ненулевое значение здесь означает,
	// что фаза AimTurn не успела или её кто-то перебил.
	UE_LOG(LogTacticsAttackAction, Display,
		TEXT("[FireAction] Commit id=%s hit=%d chance=%.1f отклонение корпуса от цели=%.1f°"),
		*ActionId.ToString(EGuidFormats::Digits), bOutHit ? 1 : 0, HitChance,
		Target ? UTacticsCombatStatics::GetFacingErrorDegrees(Shooter, Target->GetActorLocation()) : 0.f);
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

	// Удержание кадра после выстрела: терминал откладывается, транзакция и
	// презентация остаются живыми — цифры урона читаются, потом ход едет дальше.
	// Убитая цель держится дольше: игрок должен увидеть падение, а не только
	// цифру урона. Смерть определяем по самой цели, а не по «нанесли много» —
	// добивание чужим уроном в тот же кадр тоже считается.
	const AActor* ShotTarget = FireAction.Target.Get();
	const bool bTargetKilled = ShotTarget && !UTacticsCombatStatics::IsUnitAlive(ShotTarget);
	const float HoldDelay = bTargetKilled ? PostKillHoldDelay : PostShotHoldDelay;

	if (HoldDelay > 0.f && PostHoldDoneActionId != ActionId)
	{
		if (const AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			if (UWorld* World = Avatar->GetWorld())
			{
				UE_LOG(LogTacticsAttackAction, Display,
					TEXT("[FireAction] Кадр удерживается %.2f с после выстрела (%s) id=%s"),
					HoldDelay, bTargetKilled ? TEXT("цель убита") : TEXT("post-hold"),
					*ActionId.ToString(EGuidFormats::Digits));
				World->GetTimerManager().SetTimer(PostHoldTimer,
					FTimerDelegate::CreateUObject(this, &UGA_Attack::FinishPostShotHold, ActionId),
					HoldDelay, /*bLoop=*/false);
				return true;
			}
		}
	}

	ClearFireActionWatchdog();
	FireAction.Reset();
	EndShotPresentation();
	ReleasePresentationStanding(); // боец садится вместе с уходом камеры
	UE_LOG(LogTacticsAttackAction, Display,
		TEXT("[FireAction] Терминал: выстрел завершён штатно id=%s → камера возвращается"),
		*ActionId.ToString(EGuidFormats::Digits));
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
	UE_LOG(LogTacticsAttackAction, Display,
		TEXT("[FireAction] ABORT id=%s committed=%d shooter=%s — транзакция прервана"),
		*ActionId.ToString(EGuidFormats::Digits), bShotCommitted ? 1 : 0,
		*GetNameSafe(FinishedAction.Shooter.Get()));
	ClearFireActionWatchdog();
	FireAction.Reset();
	StopFireActionMontage(FinishedAction);
	if (!bShotCommitted)
	{
		RefundPreCommitActionPoints(FinishedAction);
	}
	EndShotPresentation();
	ReleasePresentationStanding(); // боец садится вместе с уходом камеры
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
		// Watchdog срабатывает только на сломанной презентации, поэтому печатаем
		// ВСЁ, что позволяет понять, на каком шаге она встала: привязался ли
		// montage instance (значит montage реально играл) и какая была стойка —
		// StepOut-ветка идёт через AI Move To и висит иначе, чем стрельба с места.
		UE_LOG(LogTacticsAttackAction, Error,
			TEXT("[FireAction] Watchdog abort id=%s phase=%d committed=%d стойка=%s montage=%s instance=%d shooter=%s"),
			*ActionId.ToString(EGuidFormats::Digits), static_cast<int32>(FireAction.Phase),
			FireAction.bShotCommitted ? 1 : 0,
			FireAction.FiringStance == EFiringStance::OverCover ? TEXT("OverCover")
				: FireAction.FiringStance == EFiringStance::StepOut ? TEXT("StepOut") : TEXT("Open"),
			*GetNameSafe(FireAction.FireMontage.Get()), FireAction.MontageInstanceId,
			*GetNameSafe(FireAction.Shooter.Get()));
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
	// Вместе с AP возвращается и потреблённый учебный форс: сорванная до
	// commit атака не должна сжигать «гарантированное попадание» (v2.9).
	if (!FinishedAction.bShotCommitted && bConsumedScriptedShotValid)
	{
		if (AUnitBase* ShooterUnit = Cast<AUnitBase>(FinishedAction.Shooter.Get()))
		{
			ShooterUnit->SetPendingScriptedShot(ConsumedScriptedShot, FinishedAction.Target.Get());
			UE_LOG(LogTacticsAttackAction, Display,
				TEXT("[FireAction] Форс возвращён %s после сорванной атаки"),
				*GetNameSafe(ShooterUnit));
		}
		bConsumedScriptedShotValid = false;
	}

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
	// Геометрия из замороженной позиции обязательна для ЛЮБОГО выстрела —
	// squadsight расширяет обнаружение, а не разрешает стрелять сквозь стены
	// (модель XCOM 2). Отдельной squadsight-ветки без геометрии больше нет.
	return UTacticsCombatStatics::HasLineOfSightFromFrozenOrigin(
		Shooter->GetWorld(), FireAction.FiringEyeLocation, Target);
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
	// Мир — у способности: этот путь идёт из EndAbility, который при выходе из
	// PIE вызывается уже без ActorInfo (см. фикс ensure там же).
	const UWorld* World = GetWorld();
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
	const UWorld* World = GetWorld();
	if (UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>()
		: nullptr)
	{
		TurnManager->CheckCombatOutcome();
	}
}
