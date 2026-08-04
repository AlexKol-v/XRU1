#include "TacticalAbility.h"
#include "ActionPointsComponent.h"
#include "TacticsGameplayTags.h"
#include "TacticalFireActionContext.h"
#include "TacticsCombatStatics.h"
#include "AnimNotify_FireCommit.h"
#include "Animation/AnimMontage.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "LatentActions.h"

DEFINE_LOG_CATEGORY_STATIC(LogTacticsPresentation, Log, All);

namespace
{
	/** Человекочитаемое имя стойки для логов доворота/выстрела. */
	const TCHAR* FiringStanceToString(EFiringStance Stance)
	{
		switch (Stance)
		{
		case EFiringStance::OverCover: return TEXT("OverCover (поверх укрытия)");
		case EFiringStance::StepOut:   return TEXT("StepOut (выход из-за угла)");
		default:                       return TEXT("Open (на месте, не вставая)");
		}
	}
}

/**
 * Ожидание доворота стрелка перед стартом стрелкового montage.
 *
 * Сам поворот ведёт `AUnitBase` (единственный владелец yaw); latent-действие
 * только ждёт его окончания и продолжает BP-ветку. Своей логики поворота здесь
 * нет намеренно — иначе появился бы второй владелец вращения.
 */
class FTacticalAimTurnLatentAction : public FPendingLatentAction
{
public:
	FTacticalAimTurnLatentAction(const FLatentActionInfo& LatentInfo, UTacticalAbility* InAbility,
		const FGuid& InActionId, AUnitBase* InShooter, float InMaxWait, bool bInSkipWait,
		float InPlannedDuration, float InSettleDelay, const FString& InShooterName)
		: ExecutionFunction(LatentInfo.ExecutionFunction)
		, OutputLink(LatentInfo.Linkage)
		, CallbackTarget(LatentInfo.CallbackTarget)
		, Ability(InAbility)
		, ActionId(InActionId)
		, Shooter(InShooter)
		, MaxWait(FMath::Max(0.f, InMaxWait))
		, bSkipWait(bInSkipWait)
		, PlannedDuration(InPlannedDuration)
		, SettleRemaining(FMath::Max(0.f, InSettleDelay))
		, ShooterName(InShooterName)
	{
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		Elapsed += Response.ElapsedTime();

		UTacticalAbility* OwnerAbility = Ability.Get();
		AUnitBase* Unit = Shooter.Get();

		// Транзакция закрылась, пока шёл доворот (abort/watchdog/смерть цели).
		// Ветку BP НЕ продолжаем: montage устаревшего действия не нужен, а саму
		// транзакцию уже закрыл C++ — обязанностей у этой ветки не осталось.
		//
		// ⚠️ Условие ровно одно — «транзакция уже не текущая». Пропавший стрелок
		// сюда НЕ входит: доворачивать в этом случае некому, но обрывать живую
		// ветку нельзя — она бы висела до watchdog.
		if (!OwnerAbility || !OwnerAbility->IsPresentationActionCurrent(ActionId))
		{
			UE_LOG(LogTacticsPresentation, Warning,
				TEXT("[AimTurn] %s: транзакция id=%s закрыта за %.2f с ожидания — ветка презентации остановлена, montage не запускается"),
				*ShooterName, *ActionId.ToString(EGuidFormats::Digits), Elapsed);
			Response.DoneIf(true);
			return;
		}

		// ФАЗА 2 — микропауза стабилизации: поворот кончился, но тело ещё гасит
		// инерцию. Стартовать montage в этот же кадр = «выстрел на ходу».
		// Сюда же затянут остаток «камера доезжает»: само действие им больше не
		// задерживается, ждёт только стрелковая анимация.
		if (bSettling)
		{
			SettleRemaining -= Response.ElapsedTime();
			const float CameraRemaining = OwnerAbility->GetCameraSettleRemaining(ActionId);
			if (SettleRemaining > 0.f || CameraRemaining > 0.f)
			{
				return;
			}
			UE_LOG(LogTacticsPresentation, Display,
				TEXT("[AimTurn] %s: пауза стабилизации окончена → montage id=%s"),
				*ShooterName, *ActionId.ToString(EGuidFormats::Digits));
			TriggerMontage(Response);
			return;
		}

		// ФАЗА 1 — сам доворот (пропавший стрелок = поворачивать нечего).
		const bool bTurnFinished = !Unit || !Unit->IsTurningInPlace();
		const bool bTimedOut = Elapsed >= MaxWait;
		if (!bSkipWait && !bTurnFinished && !bTimedOut)
		{
			return;
		}

		if (!bSkipWait)
		{
			const float FacingError = UTacticsCombatStatics::GetFacingErrorDegrees(
				Unit, ResolveTargetLocation(OwnerAbility));
			if (bTimedOut && !bTurnFinished)
			{
				UE_LOG(LogTacticsPresentation, Warning,
					TEXT("[AimTurn] %s: ЛИМИТ ожидания %.2f с исчерпан (план %.2f с), остаточное отклонение %.1f° — montage стартует принудительно id=%s"),
					*ShooterName, MaxWait, PlannedDuration, FacingError,
					*ActionId.ToString(EGuidFormats::Digits));
			}
			else
			{
				UE_LOG(LogTacticsPresentation, Display,
					TEXT("[AimTurn] %s: доворот завершён за %.2f с (план %.2f с), отклонение от цели %.1f°, пауза стабилизации %.2f с id=%s"),
					*ShooterName, Elapsed, PlannedDuration, FacingError, SettleRemaining,
					*ActionId.ToString(EGuidFormats::Digits));
			}
		}

		if (SettleRemaining > 0.f || OwnerAbility->GetCameraSettleRemaining(ActionId) > 0.f)
		{
			bSettling = true;
			return;
		}
		TriggerMontage(Response);
	}

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return FString::Printf(TEXT("Доворот к цели перед выстрелом (%.2f/%.2f с)"), Elapsed, MaxWait);
	}
#endif

private:
	/**
	 * Единственная точка продолжения ветки. Перед ней способность узнаёт, что
	 * montage стартует ПРЯМО СЕЙЧАС, — от этого момента отсчитываются фазы,
	 * живущие внутри анимации (доворот во время подъёма из укрытия).
	 */
	void TriggerMontage(FLatentResponse& Response)
	{
		if (UTacticalAbility* OwnerAbility = Ability.Get())
		{
			if (!OwnerAbility->NotifyPresentationMontageStarting(ActionId))
			{
				// Решение протухло, пока ехал кадр (цель ушла за стену). Ветка НЕ
				// продолжается: иначе боец отыграл бы вскидывание и выстрел, а
				// commit отклонил бы его уже после анимации.
				Response.DoneIf(true);
				return;
			}
		}
		Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
	}

	/** Точка цели берётся из живого контекста: он же владеет замороженной целью. */
	FVector ResolveTargetLocation(const UTacticalAbility* OwnerAbility) const
	{
		const AActor* Target = OwnerAbility ? OwnerAbility->GetPresentationTarget(ActionId) : nullptr;
		return Target ? Target->GetActorLocation() : FVector::ZeroVector;
	}

	FName ExecutionFunction;
	int32 OutputLink = INDEX_NONE;
	FWeakObjectPtr CallbackTarget;
	TWeakObjectPtr<UTacticalAbility> Ability;
	FGuid ActionId;
	TWeakObjectPtr<AUnitBase> Shooter;
	float Elapsed = 0.f;
	float MaxWait = 0.f;
	bool bSkipWait = false;
	float PlannedDuration = 0.f;
	float SettleRemaining = 0.f;
	bool bSettling = false;
	FString ShooterName;
};

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

const AActor* UTacticalAbility::GetPresentationTarget(const FGuid& ActionId) const
{
	const FTacticalFireActionContext* Action = GetPresentationAction(ActionId);
	return Action ? Action->Target.Get() : nullptr;
}

FText UTacticalAbility::GetTooltipText() const
{
	// Имя обязано быть заполнено дизайнером; пустое — видимая ошибка данных, а не
	// повод молча показать пустую подсказку.
	const FText Name = DisplayName.IsEmpty()
		? FText::FromString(GetClass()->GetName())
		: DisplayName;

	TArray<FText> Lines;
	Lines.Add(Name);
	if (!Description.IsEmpty())
	{
		Lines.Add(Description);
	}

	// Стоимость и лимит — то, ради чего подсказку и открывают: «сколько осталось»
	// нигде больше на экране не видно.
	if (MaxUsesPerMission > 0)
	{
		Lines.Add(FText::Format(
			NSLOCTEXT("XRU1", "AbilityUses", "Осталось применений: {0} из {1}"),
			FText::AsNumber(UsesRemaining), FText::AsNumber(MaxUsesPerMission)));
	}
	Lines.Add(bConsumesAllRemainingAP
		? FText::Format(NSLOCTEXT("XRU1", "AbilityCostEnds", "Стоимость: {0} ОД, завершает ход бойца"),
			FText::AsNumber(ActionPointCost))
		: FText::Format(NSLOCTEXT("XRU1", "AbilityCost", "Стоимость: {0} ОД"),
			FText::AsNumber(ActionPointCost)));

	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

void UTacticalAbility::FaceShotTargetLatent(FGuid ActionId, FLatentActionInfo LatentInfo)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTacticsPresentation, Error,
			TEXT("[AimTurn] Нет мира у способности %s — фаза доворота пропущена, ветка презентации остановлена"),
			*GetNameSafe(this));
		return;
	}

	FLatentActionManager& LatentManager = World->GetLatentActionManager();
	if (LatentManager.FindExistingAction<FTacticalAimTurnLatentAction>(
		LatentInfo.CallbackTarget, LatentInfo.UUID) != nullptr)
	{
		// Повторный запуск того же узла, пока живо прежнее ожидание, дал бы две
		// параллельные ветки презентации на одну транзакцию.
		UE_LOG(LogTacticsPresentation, Warning,
			TEXT("[AimTurn] Повторный вызов узла доворота при живом ожидании (id=%s) — проигнорирован"),
			*ActionId.ToString(EGuidFormats::Digits));
		return;
	}

	const FTacticalFireActionContext* Action = GetPresentationAction(ActionId);
	AUnitBase* Shooter = Action ? Cast<AUnitBase>(Action->Shooter.Get()) : nullptr;
	const FString ShooterName = GetNameSafe(Shooter);

	// СТРЕЛЬБА ПОВЕРХ УКРЫТИЯ идёт в другом порядке: сначала боец встаёт (это
	// начало montage), и только потом доворачивается. Разворот сидя с
	// последующим вставанием читается как лишнее движение — фидбэк 2026-08-03.
	// Поэтому здесь ветка НЕ ждёт доворот: montage стартует сразу, а поворот
	// уходит в таймер, рассчитанный так, чтобы закончиться до самого выстрела.
	if (Action && Shooter && Action->FiringStance == EFiringStance::OverCover)
	{
		// ⚠️ Доворот планируется НЕ здесь, а в момент фактического старта montage
		// (NotifyPresentationMontageStarting). Пока он ставился отсюда, таймер
		// отсчитывался от вызова узла, а montage ещё ждал кадр — боец успевал
		// довернуться СИДЯ и только потом вставал («что-то делает, потом встаёт
		// и стреляет», запись PIE 2026-08-03).
		LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID,
			new FTacticalAimTurnLatentAction(LatentInfo, this, ActionId, Shooter,
				AimTurnMaxWait, /*bSkipWait=*/true, 0.f, /*SettleDelay=*/0.f, ShooterName));
		return;
	}

	// Отказ доворота ЗАВЕРШАЕТ ожидание с продолжением ветки: доворот —
	// косметика, и его невозможность не должна подвешивать выстрел.
	const float PlannedDuration = StartAimTurnTowardsTarget(ActionId, TEXT("перед montage"));
	const bool bSkipWait = PlannedDuration < 0.f || !Shooter;

	// Микропауза нужна там, где выстрел иначе склеится с движением: после
	// реального доворота и после StepOut (боец только что добежал и тормозит).
	// Выстрелу с места без доворота хватает `PreShotCameraSettleDelay`.
	const bool bAfterStepOut = Action && Action->FiringStance == EFiringStance::StepOut;
	const float SettleDelay = (!bSkipWait || bAfterStepOut) ? AimTurnSettleDelay : 0.f;

	LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID,
		new FTacticalAimTurnLatentAction(LatentInfo, this, ActionId, Shooter,
			AimTurnMaxWait, bSkipWait, FMath::Max(0.f, PlannedDuration), SettleDelay, ShooterName));

	if (bSkipWait && SettleDelay > 0.f)
	{
		UE_LOG(LogTacticsPresentation, Display,
			TEXT("[AimTurn] %s: доворота не было, но после StepOut держим паузу стабилизации %.2f с перед montage id=%s"),
			*ShooterName, SettleDelay, *ActionId.ToString(EGuidFormats::Digits));
	}
}

/**
 * Ожидание удержания кадра после выстрела. Отдельная фаза нужна ровно для
 * одного: StepOut обязан возвращаться в укрытие ПОСЛЕ того, как игрок дочитал
 * урон, а не поверх него. Само удержание принадлежит способности — здесь только
 * ожидание и отметка «этот hold уже отработан».
 */
class FTacticalShotHoldLatentAction : public FPendingLatentAction
{
public:
	FTacticalShotHoldLatentAction(const FLatentActionInfo& LatentInfo, UTacticalAbility* InAbility,
		const FGuid& InActionId, float InHoldDelay, const FString& InShooterName)
		: ExecutionFunction(LatentInfo.ExecutionFunction)
		, OutputLink(LatentInfo.Linkage)
		, CallbackTarget(LatentInfo.CallbackTarget)
		, Ability(InAbility)
		, ActionId(InActionId)
		, Remaining(FMath::Max(0.f, InHoldDelay))
		, HoldDelay(FMath::Max(0.f, InHoldDelay))
		, ShooterName(InShooterName)
	{
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		UTacticalAbility* OwnerAbility = Ability.Get();
		if (!OwnerAbility || !OwnerAbility->IsPresentationActionCurrent(ActionId))
		{
			// Транзакцию закрыли (abort/watchdog) — возвращать нечего и некому.
			UE_LOG(LogTacticsPresentation, Warning,
				TEXT("[ShotHold] %s: транзакция id=%s закрыта за %.2f с удержания — ветка возврата остановлена"),
				*ShooterName, *ActionId.ToString(EGuidFormats::Digits), HoldDelay - Remaining);
			Response.DoneIf(true);
			return;
		}

		Remaining -= Response.ElapsedTime();
		if (Remaining > 0.f)
		{
			return;
		}

		// Hold отработан здесь — терминал способности не должен держать кадр ещё раз.
		OwnerAbility->MarkPresentationHoldDone(ActionId);
		UE_LOG(LogTacticsPresentation, Display,
			TEXT("[ShotHold] %s: кадр удержан %.2f с → возврат в укрытие id=%s"),
			*ShooterName, HoldDelay, *ActionId.ToString(EGuidFormats::Digits));
		Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
	}

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return FString::Printf(TEXT("Удержание кадра после выстрела (%.2f с)"), Remaining);
	}
#endif

private:
	FName ExecutionFunction;
	int32 OutputLink = INDEX_NONE;
	FWeakObjectPtr CallbackTarget;
	TWeakObjectPtr<UTacticalAbility> Ability;
	FGuid ActionId;
	float Remaining = 0.f;
	float HoldDelay = 0.f;
	FString ShooterName;
};

void UTacticalAbility::WaitShotHoldLatent(FGuid ActionId, FLatentActionInfo LatentInfo)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FLatentActionManager& LatentManager = World->GetLatentActionManager();
	if (LatentManager.FindExistingAction<FTacticalShotHoldLatentAction>(
		LatentInfo.CallbackTarget, LatentInfo.UUID) != nullptr)
	{
		UE_LOG(LogTacticsPresentation, Warning,
			TEXT("[ShotHold] Повторный вызов узла удержания при живом ожидании (id=%s) — проигнорирован"),
			*ActionId.ToString(EGuidFormats::Digits));
		return;
	}

	const FTacticalFireActionContext* Action = GetPresentationAction(ActionId);
	const FString ShooterName = GetNameSafe(Action ? Action->Shooter.Get() : nullptr);
	const float HoldDelay = GetPresentationHoldDelay(ActionId);

	UE_LOG(LogTacticsPresentation, Display,
		TEXT("[ShotHold] %s: держим кадр %.2f с перед возвратом в укрытие id=%s"),
		*ShooterName, HoldDelay, *ActionId.ToString(EGuidFormats::Digits));

	LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID,
		new FTacticalShotHoldLatentAction(LatentInfo, this, ActionId, HoldDelay, ShooterName));
}

float UTacticalAbility::FindFireCommitTime(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return 0.f;
	}
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Event.Notify && Event.Notify->IsA<UAnimNotify_FireCommit>())
		{
			// ⚠️ Время notify живёт в пространстве montage, а планировщик доворота
			// и ожидание кадра считают РЕАЛЬНЫЕ секунды. Замедленный темп
			// (`RateScale` < 1 у стрельбы поверх укрытия — там растянут подъём)
			// иначе давал бы заниженное окно, и доворот вставал бы раньше срока.
			return Event.GetTriggerTime() / FMath::Max(0.01f, Montage->RateScale);
		}
	}
	return 0.f;
}

void UTacticalAbility::EnsureFacingAtCommit(const FGuid& ActionId)
{
	const FTacticalFireActionContext* Action = GetPresentationAction(ActionId);
	AUnitBase* Shooter = Action ? Cast<AUnitBase>(Action->Shooter.Get()) : nullptr;
	const AActor* Target = Action ? Action->Target.Get() : nullptr;
	if (!Shooter || !Target)
	{
		return;
	}

	const float FacingError = UTacticsCombatStatics::GetFacingErrorDegrees(
		Shooter, Target->GetActorLocation());
	if (FacingError <= FMath::Max(0.f, AimSnapMaxError))
	{
		return;
	}

	// Сюда попадают только сбои плавного доворота (его перебили, цель успела
	// уехать, montage оказался короче расчёта) — потому и Warning: в штатном
	// выстреле этой строки быть не должно.
	UE_LOG(LogTacticsPresentation, Warning,
		TEXT("[AimTurn] %s: на выстреле корпус отвёрнут на %.1f° (порог %.1f°) — мгновенный доворот-страховка, стойка %s id=%s"),
		*GetNameSafe(Shooter), FacingError, AimSnapMaxError,
		FiringStanceToString(Action->FiringStance), *ActionId.ToString(EGuidFormats::Digits));

	// MinAngleOverride заведомо больше любой дельты — доворот выполняется этим
	// же кадром И корректно гасит незавершённый плавный поворот (иначе Tick
	// продолжил бы крутить актора уже после выстрела).
	Shooter->FaceTowardsSmooth(Target->GetActorLocation(),
		/*bPlayTurnAnimation=*/false, /*TurnRateOverride=*/0.f, /*MinAngleOverride=*/181.f);
}

bool UTacticalAbility::NotifyPresentationMontageStarting(const FGuid& ActionId)
{
	const FTacticalFireActionContext* Action = GetPresentationAction(ActionId);
	AUnitBase* Shooter = Action ? Cast<AUnitBase>(Action->Shooter.Get()) : nullptr;
	if (!Action || !Shooter)
	{
		return false;
	}

	// ПОСЛЕДНЯЯ ТОЧКА, ГДЕ ОТКАЗ ЕЩЁ БЕСПЛАТЕН. Между активацией и стартом
	// анимации проходит наводка камеры (а у реакции — ещё и окно slow-mo, за
	// которое цель успевает уйти за стену). Раньше проверка стояла только на
	// самом `FireCommit`: боец вскидывался, стрелял — и лишь тогда решение
	// отклонялось («[ReactionAction] Reject invalid frozen solution», лог PIE
	// 2026-08-04). Отказ ДО montage выглядит как несостоявшийся выстрел, а не
	// как выстрел в никуда.
	//
	// ⚠️ КРОМЕ StepOut. Там боец УЖЕ вышел из укрытия, и возврат живёт в
	// BP-ветке за montage (`Return from Step Out` → `Set Actor Location` +
	// `Hug Cover`); `AbortPresentation` его не отыграет — транзакцию закроет, а
	// боец останется стоять в чистом поле. Для этой стойки отказ уже НЕ бесплатен,
	// поэтому решение проверяется по-старому — на самом `FireCommit`.
	if (Action->FiringStance != EFiringStance::StepOut
		&& !IsFrozenPresentationSolutionValid())
	{
		UE_LOG(LogTacticsPresentation, Warning,
			TEXT("[Presentation] %s: решение протухло до старта montage (цель ушла из линии огня) — анимация не играется, id=%s"),
			*GetNameSafe(Shooter), *ActionId.ToString(EGuidFormats::Digits));
		AbortPresentation(ActionId);
		return false;
	}

	if (Action->FiringStance == EFiringStance::OverCover)
	{
		// Отсчёт пошёл от реального старта анимации подъёма — теперь «встал,
		// довернулся, выстрелил» складывается в одно непрерывное движение.
		ScheduleAimTurnAfterRise(ActionId, *Action, Shooter, GetNameSafe(Shooter));
	}

	// НА НОГАХ ДО КОНЦА КАДРА — для ОБЕИХ стойек, которые стартуют из укрытия.
	// Стрелковый montage анимирован стоя; пока поза оставалась `Crouch`/`High`,
	// боец выпускал очередь сидя. У StepOut это било так же, как у OverCover:
	// он добегал до огневой точки, компонент укрытий снова находил там стену,
	// поза возвращалась в `Crouch` — и выстрел уходил из приседа (лог PIE
	// 2026-08-04, выстрелы 4/22/38: `стойка=StepOut`, `[FireCommit] … поза=2/3`).
	// Обратно боец садится вместе с уходом камеры — в ReleasePresentationStanding.
	if (Action->FiringStance == EFiringStance::OverCover
		|| Action->FiringStance == EFiringStance::StepOut)
	{
		Shooter->SetPresentationStanding(true);
		StandingUnit = Shooter;
	}
	return true;
}

void UTacticalAbility::ReleasePresentationStanding()
{
	if (AUnitBase* Unit = StandingUnit.Get())
	{
		Unit->SetPresentationStanding(false);
	}
	StandingUnit.Reset();
}

void UTacticalAbility::ScheduleAimTurnAfterRise(const FGuid& ActionId,
	const FTacticalFireActionContext& Action, AUnitBase* Shooter, const FString& ShooterName)
{
	UWorld* World = GetWorld();
	const float FacingError = UTacticsCombatStatics::GetFacingErrorDegrees(
		Shooter, Action.Target.IsValid() ? Action.Target->GetActorLocation() : FVector::ZeroVector);
	if (!World || !Action.Target.IsValid() || FacingError < FMath::Max(0.f, AimTurnMinAngle))
	{
		UE_LOG(LogTacticsPresentation, Display,
			TEXT("[AimTurn] %s: доворот поверх укрытия не нужен (отклонение %.1f°) — сразу montage id=%s"),
			*ShooterName, FacingError, *ActionId.ToString(EGuidFormats::Digits));
		return;
	}

	// Окно = момент выстрела внутри montage МИНУС микропауза: доворот обязан не
	// просто уложиться до выстрела, а закончиться заметно раньше него. Без этого
	// зазора поворот и выстрел склеивались в одно движение — «нет паузы между
	// доворотом и выстрелом» (фидбэк 2026-08-03). Зазор тот же, что у остальных
	// стоек (`AimTurnSettleDelay`), поэтому ритм выстрела одинаковый везде.
	const float CommitTime = FindFireCommitTime(Action.FireMontage.Get());
	const float Window = FMath::Max(0.f, CommitTime - FMath::Max(0.f, AimTurnSettleDelay));
	float Rate = FMath::Max(10.f, AimTurnRate);
	float Duration = FacingError / Rate;
	if (Duration > Window && Window > 0.f)
	{
		Rate = FMath::Min(FMath::Max(10.f, AimTurnRateMax), FacingError / Window);
		Duration = FacingError / Rate;
	}
	const float Delay = FMath::Clamp(Window - Duration, 0.f, FMath::Max(0.f, AimTurnRiseDelay));

	UE_LOG(LogTacticsPresentation, Display,
		TEXT("[AimTurn] %s: поверх укрытия — подъём, доворот через %.2f с (отклонение %.1f°, скорость %.0f °/с, длится %.2f с), выстрел на %.2f с, пауза перед выстрелом %.2f с id=%s"),
		*ShooterName, Delay, FacingError, Rate, Duration, CommitTime,
		FMath::Max(0.f, CommitTime - (Delay + Duration)),
		*ActionId.ToString(EGuidFormats::Digits));

	if (Delay <= 0.f)
	{
		StartAimTurnTowardsTarget(ActionId, TEXT("одновременно с подъёмом"), Rate);
		return;
	}
	World->GetTimerManager().SetTimer(AimTurnRiseTimer,
		FTimerDelegate::CreateUObject(this, &UTacticalAbility::StartDelayedAimTurn, ActionId, Rate),
		Delay, /*bLoop=*/false);
}

void UTacticalAbility::StartDelayedAimTurn(FGuid ActionId, float RateOverride)
{
	// Транзакцию могли закрыть, пока боец вставал — устаревший таймер молчит.
	if (IsPresentationActionCurrent(ActionId))
	{
		StartAimTurnTowardsTarget(ActionId, TEXT("после подъёма из укрытия"), RateOverride);
	}
}

float UTacticalAbility::StartAimTurnTowardsTarget(const FGuid& ActionId, const TCHAR* Reason,
	float RateOverride)
{
	const FTacticalFireActionContext* Action = GetPresentationAction(ActionId);
	AUnitBase* Shooter = Action ? Cast<AUnitBase>(Action->Shooter.Get()) : nullptr;
	const AActor* Target = Action ? Action->Target.Get() : nullptr;
	if (!Action || !Shooter || !Target)
	{
		UE_LOG(LogTacticsPresentation, Warning,
			TEXT("[AimTurn] Доворот (%s) пропущен id=%s: контекст=%s стрелок=%s цель=%s"),
			Reason, *ActionId.ToString(EGuidFormats::Digits),
			Action ? TEXT("есть") : TEXT("НЕТ"), *GetNameSafe(Shooter), *GetNameSafe(Target));
		return -1.f;
	}

	const float FacingError = UTacticsCombatStatics::GetFacingErrorDegrees(
		Shooter, Target->GetActorLocation());
	const float Rate = RateOverride > 0.f ? RateOverride : FMath::Max(10.f, AimTurnRate);
	if (FacingError < FMath::Max(0.f, AimTurnMinAngle))
	{
		UE_LOG(LogTacticsPresentation, Display,
			TEXT("[AimTurn] %s: доворот (%s) не нужен — уже смотрит на %s (отклонение %.1f° < %.1f°), стойка %s, id=%s"),
			*GetNameSafe(Shooter), Reason, *GetNameSafe(Target), FacingError, AimTurnMinAngle,
			FiringStanceToString(Action->FiringStance),
			*ActionId.ToString(EGuidFormats::Digits));
		return -1.f;
	}

	// Анимация доворота НЕ включается намеренно: клипы поворота в проекте только
	// приседные, их root motion в стейт-машине не применяется (RootMotionMode =
	// Montages Only), а длительность клипа (2.5 с) втрое больше самого поворота —
	// тело докручивалось бы уже после выстрела. Поворот здесь чисто механический.
	Shooter->FaceTowardsSmooth(Target->GetActorLocation(),
		/*bPlayTurnAnimation=*/false, Rate, /*MinAngleOverride=*/AimTurnMinAngle);

	const float PlannedDuration = FacingError / Rate;
	UE_LOG(LogTacticsPresentation, Display,
		TEXT("[AimTurn] %s → %s: старт доворота (%s), отклонение %.1f°, скорость %.0f °/с, план %.2f с (лимит %.2f), стойка %s, id=%s"),
		*GetNameSafe(Shooter), *GetNameSafe(Target), Reason, FacingError, Rate, PlannedDuration,
		AimTurnMaxWait, FiringStanceToString(Action->FiringStance),
		*ActionId.ToString(EGuidFormats::Digits));
	return PlannedDuration;
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
	// Отложенный доворот не должен пережить транзакцию, как и удержание позы.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AimTurnRiseTimer);
	}
	ReleasePresentationStanding();

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
