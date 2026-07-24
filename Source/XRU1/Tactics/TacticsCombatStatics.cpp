#include "TacticsCombatStatics.h"
#include "TacticsGameplayTags.h"
#include "CoverDetectionComponent.h"
#include "CoverTuningDataAsset.h"
#include "TacticsGameInstance.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "UnitAIController.h"
#include "TacticalPlayerController.h"
#include "TDCombatant.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GenericTeamAgentInterface.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

/**
 * Диагностика линии огня (Ф4): при `xru1.LOS.Debug 1` логировать по запасному
 * пути число огневых позиций и итог видимости, а также рисовать позиции
 * DrawDebugSphere; GetFiringStance печатает выбранную стойку. Образец —
 * `xru1.AI.LogCombat` в UnitAIController.cpp. Без этого критерии приёмки про
 * стойку (Ф4 №4) и взаимность (Ф5 №6) не наблюдаемы в PIE.
 */
static TAutoConsoleVariable<int32> CVarLOSDebug(
	TEXT("xru1.LOS.Debug"),
	0,
	TEXT("1 — логировать/рисовать огневые позиции и стойку при расчёте линии огня."),
	ECVF_Default);

const UCoverTuningDataAsset* UTacticsCombatStatics::GetCoverTuning(const UWorld* World)
{
	// Глобальный тюнинг с GameInstance (обычно BP-наследник), иначе CDO. CDO
	// несёт дефолты = прежние числа кода, поэтому фолбэк не меняет поведение.
	if (World)
	{
		if (const UTacticsGameInstance* GI = World->GetGameInstance<UTacticsGameInstance>())
		{
			if (const UCoverTuningDataAsset* Tuning = GI->CoverTuning)
			{
				return Tuning;
			}
		}
	}
	return GetDefault<UCoverTuningDataAsset>();
}

bool UTacticsCombatStatics::IsUnitAlive(const AActor* Unit)
{
	const ATDCombatant* Combatant = Cast<ATDCombatant>(Unit);
	if (!Combatant || Combatant->GetHealth() <= 0.f)
	{
		return false;
	}
	// Эвакуированный жив, но на поле не участвует — за «живого бойца на карте» не считаем.
	if (const AUnitBase* UnitBase = Cast<AUnitBase>(Unit))
	{
		return !UnitBase->IsEvacuated();
	}
	return true;
}

bool UTacticsCombatStatics::IsUnitEvacuated(const AActor* Unit)
{
	const AUnitBase* UnitBase = Cast<AUnitBase>(Unit);
	return UnitBase && UnitBase->IsEvacuated();
}

bool UTacticsCombatStatics::IsUnitDowned(const AActor* Unit)
{
	const AUnitBase* UnitBase = Cast<AUnitBase>(Unit);
	return UnitBase && UnitBase->IsDowned();
}

bool UTacticsCombatStatics::AreHostile(const AActor* A, const AActor* B)
{
	if (!A || !B)
	{
		return false;
	}
	return FGenericTeamId::GetAttitude(A, B) == ETeamAttitude::Hostile;
}

float UTacticsCombatStatics::ComputeHitChance(const AActor* Shooter, const AActor* Target, float BaseHitChance)
{
	float DefenseBonus = 0.f;
	if (Target)
	{
		if (const UCoverDetectionComponent* Cover = Target->FindComponentByClass<UCoverDetectionComponent>())
		{
			DefenseBonus = Cover->GetDefenseBonusAgainst(Shooter);
		}

		// Глухая оборона удваивает бонус укрытия цели.
		if (DefenseBonus > 0.f)
		{
			if (const UAbilitySystemComponent* TargetASC =
				UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
			{
				if (TargetASC->HasMatchingGameplayTag(TacticsGameplayTags::State_HunkeredDown))
				{
					DefenseBonus *= GetCoverTuning(Target->GetWorld())->HunkerDownMultiplier;
				}
			}
		}
	}
	return FMath::Clamp(BaseHitChance - DefenseBonus, 5.f, 95.f);
}

bool UTacticsCombatStatics::ResolveShot(AActor* Shooter, AActor* Target, float BaseHitChance, float Damage,
	TSubclassOf<UGameplayEffect> DamageEffectClass)
{
	if (!Shooter || !Target || !DamageEffectClass || !IsUnitAlive(Target))
	{
		return false;
	}

	// Стрелок разворачивается ЛИЦОМ к цели (XCOM): без этого выстрел «в спину» и
	// не читается, в кого он. Общий хелпер — тот же, что при взятии цели на
	// прицел. Работает для всех путей выстрела: игрок, AI, Overwatch, туториал.
	FaceActorTowards(Shooter, Target->GetActorLocation());

	// Камера показывает выстрел кадром «из-за плеча» — и выстрел игрока, и
	// выстрел врага (игрок должен видеть, в кого стреляют по его отряду).
	if (UWorld* ShotWorld = Shooter->GetWorld())
	{
		if (ATacticalPlayerController* PC = Cast<ATacticalPlayerController>(ShotWorld->GetFirstPlayerController()))
		{
			PC->NotifyShotFired(Shooter, Target);
		}
	}

	// Скриптовые выстрелы туториала (100/0) минуют кламп [5..95].
	float HitChance;
	if (BaseHitChance >= 100.f)
	{
		HitChance = 100.f;
	}
	else if (BaseHitChance <= 0.f)
	{
		HitChance = 0.f;
	}
	else
	{
		HitChance = ComputeHitChance(Shooter, Target, BaseHitChance);
	}
	// Строгий бросок: 100% попадает всегда (Roll может выпасть ровно 100),
	// 0% мажет всегда (Roll может выпасть ровно 0) — скриптовые выстрелы честны.
	const bool bHit = HitChance >= 100.f || FMath::FRandRange(0.f, 100.f) < HitChance;

	if (bHit)
	{
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (TargetASC)
		{
			// Спек делаем от ASC стрелка (если есть) — тогда в контексте виден Instigator.
			UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Shooter);
			UAbilitySystemComponent* SpecOwner = SourceASC ? SourceASC : TargetASC;

			FGameplayEffectContextHandle Context = SpecOwner->MakeEffectContext();
			Context.AddInstigator(Shooter, Shooter);

			const FGameplayEffectSpecHandle Spec = SpecOwner->MakeOutgoingSpec(DamageEffectClass, 1.f, Context);
			if (Spec.IsValid())
			{
				// Разброс ±10% (GDD §5.4), туториальные форс-выстрелы тоже слегка варьируются.
				const float FinalDamage = FMath::Abs(Damage) * FMath::FRandRange(0.9f, 1.1f);
				Spec.Data->SetSetByCallerMagnitude(TacticsGameplayTags::Data_Damage, -FinalDamage);
				TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
			}
		}
	}

	// Выстрел слышно: враги стрелка поблизости поднимают тревогу (XCOM yellow alert).
	NotifyCombatNoise(Shooter, Shooter->GetActorLocation());

	// Выстрел мог убить последнего юнита стороны — проверяем исход боя.
	if (UWorld* World = Shooter->GetWorld())
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->CheckCombatOutcome();
		}
	}

	return bHit;
}

void UTacticsCombatStatics::FaceActorTowards(AActor* Actor, const FVector& TargetLocation)
{
	if (!Actor)
	{
		return;
	}
	FVector ToTarget = TargetLocation - Actor->GetActorLocation();
	ToTarget.Z = 0.f;
	if (ToTarget.Normalize())
	{
		const FRotator Current = Actor->GetActorRotation();
		Actor->SetActorRotation(FRotator(Current.Pitch, ToTarget.Rotation().Yaw, Current.Roll));
	}
}

void UTacticsCombatStatics::GetFiringPositions(const UWorld* World, const AActor* Unit,
	const FVector& EyeLocation, const FVector& OtherLocation,
	TArray<FVector, TInlineAllocator<4>>& OutEyePositions)
{
	OutEyePositions.Reset();
	OutEyePositions.Add(EyeLocation); // ЦЕНТР — ВСЕГДА и первым (порядок значим, см. .h)
	if (!World)
	{
		return;
	}

	// Тюнинг: у юнита свой (TuningOverride) → глобальный → CDO.
	const UCoverTuningDataAsset* Tuning = nullptr;
	if (const UCoverDetectionComponent* Cover =
		Unit ? Unit->FindComponentByClass<UCoverDetectionComponent>() : nullptr)
	{
		Tuning = Cover->GetTuning();
	}
	if (!Tuning)
	{
		Tuning = GetCoverTuning(World);
	}

	// Направление на «другого» и боковая ось выглядывания.
	FVector Dir = OtherLocation - EyeLocation;
	Dir.Z = 0.;
	const FVector DirToOther = Dir.GetSafeNormal();
	// Без капсулы (Unit==nullptr) или без направления выглядывание не построить —
	// остаётся только центр (прямая видимость).
	if (!Unit || DirToOther.IsNearlyZero())
	{
		return;
	}
	FVector Side = FVector::CrossProduct(DirToOther, FVector::UpVector);
	if (!Side.Normalize())
	{
		Side = FVector::RightVector;
	}

	float CapsuleHalfHeight = 88.f;
	float CapsuleRadius = 34.f;
	if (const ACharacter* Character = Cast<ACharacter>(Unit))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			CapsuleRadius = Capsule->GetScaledCapsuleRadius();
		}
	}

	// Точка ПОЛА (§II.3): EyeLocation = пол + пол-капсулы + EyeHeightOffset.
	const FVector FootBase = EyeLocation - FVector(0.f, 0.f, Tuning->EyeHeightOffset + CapsuleHalfHeight);

	// ⚠️ ГЛАВНОЕ ПРАВИЛО (по замечанию игрока, XCOM): выглядывать можно только
	// из УКРЫТИЯ. Юнит в открытом поле видит/виден ТОЛЬКО по прямой (центр).
	// Поэтому и step-out, и края укрытия добавляются лишь если юнит реально в
	// укрытии в сторону «другого». Раньше step-out ±LosPeekOffset добавлялся
	// всегда — отсюда «стреляет со стороны, стоя без укрытия».
	if (UCoverDetectionComponent::TraceCoverAtLocation(World, FootBase, DirToOther,
		Tuning->CoverTraceDistance, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
		Tuning->CoverTraceChannel, Unit) == ECoverType::None)
	{
		return; // не в укрытии — только прямая видимость из центра
	}

	// Свип мира: юниты выстрел не блокируют — фильтр по типам объектов, как в LOS.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FiringPeek), /*bTraceComplex=*/false);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Tuning->LosSphereRadius);
	auto SweepClear = [&](const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		return !World->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, ObjectParams, Sphere, Params);
	};

	// В укрытии: небольшой step-out ±LosPeekOffset (привстать/качнуться у края).
	for (const float PeekSign : {1.f, -1.f})
	{
		const FVector Peek = EyeLocation + Side * (Tuning->LosPeekOffset * PeekSign);
		if (SweepClear(EyeLocation, Peek))
		{
			OutEyePositions.Add(Peek);
		}
	}

	// Занятость — один снимок, чтобы peek не встал в другого юнита.
	TArray<FVector> Obstacles;
	GetUnitObstacles(const_cast<UWorld*>(World), Unit, Obstacles);
	const double ClearanceSq = FMath::Square(static_cast<double>(GetUnitClearance(Unit)));
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(const_cast<UWorld*>(World));

	// По каждой стороне шагаем вдоль укрытия до края, там пробуем выглянуть.
	for (const float SideSign : {1.f, -1.f})
	{
		for (float Offset = Tuning->PeekEdgeStep; Offset <= Tuning->PeekEdgeMaxDistance;
			Offset += Tuning->PeekEdgeStep)
		{
			const FVector P = FootBase + Side * (SideSign * Offset);
			if (UCoverDetectionComponent::TraceCoverAtLocation(World, P, DirToOther,
				Tuning->CoverTraceDistance, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
				Tuning->CoverTraceChannel, Unit) != ECoverType::None)
			{
				continue; // ещё за укрытием — шагаем к краю
			}

			// Укрытие кончилось — это точка сразу за краем. Одна попытка на сторону:
			// дальше только открытое пространство, нового края там нет.
			const FVector PeekFoot = P + Side * (SideSign * (CapsuleRadius + Tuning->PeekOutwardOffset));
			FNavLocation Projected;
			if (NavSys && NavSys->ProjectPointToNavigation(PeekFoot, Projected, FVector(60.f, 60.f, 200.f)))
			{
				bool bOccupied = false;
				for (const FVector& Obstacle : Obstacles)
				{
					if (FVector::DistSquared2D(Obstacle, Projected.Location) < ClearanceSq)
					{
						bOccupied = true;
						break;
					}
				}
				if (!bOccupied)
				{
					const FVector PeekEye =
						Projected.Location + FVector(0.f, 0.f, CapsuleHalfHeight + Tuning->EyeHeightOffset);
					if (SweepClear(EyeLocation, PeekEye))
					{
						OutEyePositions.Add(PeekEye);
					}
				}
			}
			break; // край на этой стороне найден
		}
	}
}

bool UTacticsCombatStatics::IsLOSDebugEnabled()
{
	return CVarLOSDebug.GetValueOnGameThread() != 0;
}

bool UTacticsCombatStatics::HasLineOfSight(const AActor* Viewer, const AActor* Target)
{
	if (!Viewer || !Target)
	{
		return false;
	}
	const UWorld* World = Viewer->GetWorld();
	if (!World)
	{
		return false;
	}
	return HasLineOfSightFromLocation(World,
		Viewer->GetActorLocation() + FVector(0.f, 0.f, GetCoverTuning(World)->EyeHeightOffset), Target, Viewer);
}

bool UTacticsCombatStatics::HasLineOfSightFromLocation(const UWorld* World, const FVector& EyeLocation,
	const AActor* Target, const AActor* Shooter)
{
	if (!World || !Target)
	{
		return false;
	}

	const UCoverTuningDataAsset* Tuning = GetCoverTuning(World);

	// Только геометрия мира: юниты выстрел не блокируют (XCOM — сквозь своих
	// стрелять можно), поэтому фильтруем по типу объекта, а не по каналу.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic); // двигаемые пропсы-укрытия
	FCollisionQueryParams Params(SCENE_QUERY_STAT(UnitLOS), /*bTraceComplex=*/false);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Tuning->LosSphereRadius);

	auto SphereClear = [&](const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		return !World->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, ObjectParams, Sphere, Params);
	};

	// Точки цели: глаза и корпус — цель за низкой стеной видна по глазам,
	// цель на уступе — по корпусу.
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector TargetPoints[2] = {
		TargetLocation + FVector(0.f, 0.f, Tuning->EyeHeightOffset),
		TargetLocation - FVector(0.f, 0.f, 20.f)
	};

	// БЫСТРЫЙ ПУТЬ — ПРЯМАЯ видимость из ЦЕНТРА глаз к точкам цели (глаза/корпус).
	// Никаких step-out: выглядывание вбок — это механика УКРЫТИЯ и живёт только в
	// запасном пути через GetFiringPositions (гейтед фактом укрытия). Так юнит в
	// открытом поле видит/виден строго по прямой (по замечанию игрока), а не
	// «из-за угла, стоя без укрытия».
	for (const FVector& Point : TargetPoints)
	{
		if (SphereClear(EyeLocation, Point))
		{
			return true;
		}
	}

	// ЗАПАСНОЙ ПУТЬ (только если быстрый не прошёл и есть стрелок): огневые
	// позиции у краёв укрытия стрелка. Надмножество быстрого пути, поэтому
	// запуск после неудачи безопасен и стоит лишь когда прямой видимости нет.
	if (!Shooter)
	{
		return false;
	}

	TArray<FVector, TInlineAllocator<4>> PositionsA;
	GetFiringPositions(World, Shooter, EyeLocation, TargetLocation, PositionsA);

	// Ф5 — симметрия: цель тоже высовывается из-за своего края. ТА ЖЕ функция с
	// переставленными аргументами (§III.2), отдельной логики для цели нет.
	// Укрытие цели при этом считается от её НАСТОЯЩЕЙ позиции (в ComputeHitChance),
	// а не отсюда — peek нужен только для луча (§II.6 п.5).
	TArray<FVector, TInlineAllocator<4>> PositionsB;
	GetFiringPositions(World, Target, TargetPoints[0], EyeLocation, PositionsB);
	// Точка КОРПУСА цели (нужна для целей на уступе, легко теряется при переборе пар).
	PositionsB.Add(TargetPoints[1]);

	bool bVisible = false;
	int32 WinAIdx = -1;
	int32 WinBIdx = -1;
	for (int32 ai = 0; ai < PositionsA.Num() && !bVisible; ++ai)
	{
		for (int32 bi = 0; bi < PositionsB.Num(); ++bi)
		{
			if (SphereClear(PositionsA[ai], PositionsB[bi]))
			{
				bVisible = true;
				WinAIdx = ai;
				WinBIdx = bi;
				break;
			}
		}
	}

#if ENABLE_DRAW_DEBUG
	if (CVarLOSDebug.GetValueOnAnyThread() > 0)
	{
		// Визуальный разбор LOS: видно ГДЕ огневые позиции и КАКОЙ луч дал
		// видимость. Стрелок: центр (0) — белый, step-out/края — голубые. Цель:
		// центр (0) — жёлтый, корпус (последний) — оранжевый, края — маджента.
		// Победивший луч — толстая зелёная линия. Индексы winA/winB — в логе.
		UWorld* DbgWorld = const_cast<UWorld*>(World);
		// Живёт чуть дольше троттлинга непрерывного дебага в PlayerTick
		// (LOSDebugInterval = 0.25с) — без нахлёста был бы мигающий разрыв
		// между перерисовками (0.12с видно / 0.13с пусто).
		const float DbgDur = 0.35f;
		for (int32 i = 0; i < PositionsA.Num(); ++i)
		{
			DrawDebugSphere(DbgWorld, PositionsA[i], 14.f, 6,
				i == 0 ? FColor::White : FColor::Cyan, false, DbgDur);
		}
		for (int32 i = 0; i < PositionsB.Num(); ++i)
		{
			const FColor PointColor = (i == 0) ? FColor::Yellow
				: (i == PositionsB.Num() - 1) ? FColor::Orange : FColor::Magenta;
			DrawDebugSphere(DbgWorld, PositionsB[i], 12.f, 6, PointColor, false, DbgDur);
		}
		if (bVisible)
		{
			DrawDebugLine(DbgWorld, PositionsA[WinAIdx], PositionsB[WinBIdx],
				FColor::Green, false, DbgDur, 0, 3.f);
		}
		UE_LOG(LogTemp, Log,
			TEXT("[LOS] %s -> %s: shooterPos=%d targetPos=%d visible=%d winA=%d winB=%d"),
			*GetNameSafe(Shooter), *GetNameSafe(Target), PositionsA.Num(), PositionsB.Num(),
			bVisible ? 1 : 0, WinAIdx, WinBIdx);
	}
#endif

	return bVisible;
}

EFiringStance UTacticsCombatStatics::GetFiringStance(const AActor* Shooter, const AActor* Target,
	FVector& OutFiringEyeLocation)
{
	OutFiringEyeLocation = Shooter ? Shooter->GetActorLocation() : FVector::ZeroVector;
	const UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (!Shooter || !Target || !World)
	{
		return EFiringStance::Open;
	}

	const UCoverTuningDataAsset* Tuning = GetCoverTuning(World);
	const FVector EyeLocation = Shooter->GetActorLocation() + FVector(0.f, 0.f, Tuning->EyeHeightOffset);
	OutFiringEyeLocation = EyeLocation; // фолбэк — центр глаз, если LOS нет вообще

	const FVector TargetLocation = Target->GetActorLocation();
	const FVector TargetPoints[2] = {
		TargetLocation + FVector(0.f, 0.f, Tuning->EyeHeightOffset),
		TargetLocation - FVector(0.f, 0.f, 20.f)
	};

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FiringStance), /*bTraceComplex=*/false);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Tuning->LosSphereRadius);
	auto SphereClear = [&](const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		return !World->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, ObjectParams, Sphere, Params);
	};

	TArray<FVector, TInlineAllocator<4>> Positions;
	GetFiringPositions(World, Shooter, EyeLocation, TargetLocation, Positions);

	// Порядок в Positions: центр → быстрый step-out → края. Первая позиция с
	// линией огня и определяет стойку (§III.3): центр → OverCover/Open, иначе StepOut.
	EFiringStance Stance = EFiringStance::Open;
	for (int32 i = 0; i < Positions.Num(); ++i)
	{
		bool bClear = false;
		for (const FVector& Point : TargetPoints)
		{
			if (SphereClear(Positions[i], Point))
			{
				bClear = true;
				break;
			}
		}
		if (!bClear)
		{
			continue;
		}

		OutFiringEyeLocation = Positions[i];
		if (i == 0)
		{
			ECoverType Cover = ECoverType::None;
			if (const UCoverDetectionComponent* CoverComp = Shooter->FindComponentByClass<UCoverDetectionComponent>())
			{
				Cover = CoverComp->GetCoverAgainst(Target);
			}
			Stance = (Cover == ECoverType::None) ? EFiringStance::Open : EFiringStance::OverCover;
		}
		else
		{
			Stance = EFiringStance::StepOut;
		}
		break;
	}

#if ENABLE_DRAW_DEBUG
	if (CVarLOSDebug.GetValueOnAnyThread() > 0)
	{
		static const TCHAR* StanceNames[] = { TEXT("Open"), TEXT("OverCover"), TEXT("StepOut") };
		UE_LOG(LogTemp, Log, TEXT("[LOS] Stance %s -> %s: positions=%d stance=%s"),
			*GetNameSafe(Shooter), *GetNameSafe(Target), Positions.Num(),
			StanceNames[static_cast<uint8>(Stance)]);
	}
#endif
	return Stance;
}

float UTacticsCombatStatics::GetAimDistanceModifier(const AUnitBase* Shooter, float Distance)
{
	if (!Shooter)
	{
		return 0.f;
	}

	// Дизайнерский профиль оружия (кривая в BP-классе юнита) — приоритетнее.
	if (Shooter->AimByDistanceCurve)
	{
		return Shooter->AimByDistanceCurve->GetFloatValue(Distance);
	}

	// Встроенный профиль «винтовки» (XCOM 2-подобный): бонус в упор, ноль на
	// средней дистанции, мягкий штраф на дальней. Числа согласованы с GDD §5.4.
	if (Distance <= 300.f)
	{
		return 10.f;
	}
	if (Distance <= 1200.f)
	{
		return FMath::GetMappedRangeValueClamped(FVector2D(300., 1200.), FVector2D(10., 0.), Distance);
	}
	return FMath::GetMappedRangeValueClamped(FVector2D(1200., 2500.), FVector2D(0., -15.), Distance);
}

bool UTacticsCombatStatics::SquadHasLineOfSight(const AActor* Unit, const AActor* Target)
{
	if (!Unit || !Target)
	{
		return false;
	}
	const UWorld* World = Unit->GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return false;
	}

	// Союзники юнита = противники его противников; проще: все юниты его стороны.
	for (AActor* Ally : TurnManager->GetSideUnits(Unit))
	{
		if (Ally && Ally != Unit && IsUnitAlive(Ally) &&
			FVector::Dist(Ally->GetActorLocation(), Target->GetActorLocation()) <= SquadVisionRange &&
			HasLineOfSight(Ally, Target))
		{
			return true;
		}
	}
	return false;
}

void UTacticsCombatStatics::NotifyCombatNoise(AActor* Instigator, const FVector& Location, float Radius)
{
	if (!Instigator)
	{
		return;
	}
	const UWorld* World = Instigator->GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return;
	}

	for (AActor* Enemy : TurnManager->GetOpposingUnits(Instigator))
	{
		if (!Enemy || FVector::Dist(Enemy->GetActorLocation(), Location) > Radius)
		{
			continue;
		}
		if (const APawn* Pawn = Cast<APawn>(Enemy))
		{
			if (AUnitAIController* AI = Cast<AUnitAIController>(Pawn->GetController()))
			{
				AI->NotifyNoiseHeard(Location);
			}
		}
	}
}

bool UTacticsCombatStatics::GetPointAlongPathBudget(UObject* WorldContextObject, const AActor* Mover,
	const FVector& Start, const FVector& Goal, float PathBudget, FVector& OutPoint)
{
	OutPoint = Start;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSys)
	{
		return false;
	}

	UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(World, Start, Goal);
	if (!Path || !Path->IsValid() || Path->PathPoints.Num() < 2)
	{
		return false;
	}

	// Бюджет урезаем ещё и клиренсом: если впереди стоящий юнит, идём до него,
	// а не сквозь. Именно УСЕЧЕНИЕ, а не отказ — иначе AI, упёршийся в союзника,
	// впустую пропускал бы ход вместо «подойти насколько можно».
	TArray<FVector> Obstacles;
	GetUnitObstacles(World, Mover, Obstacles);
	const double ClearanceLimit = FindPathClearanceLimit(Path->PathPoints, Obstacles, GetUnitClearance(Mover));
	double Remaining = PathBudget;
	if (ClearanceLimit >= 0.)
	{
		Remaining = FMath::Min(Remaining, ClearanceLimit);
	}
	if (Remaining <= 0.)
	{
		return false; // упёрлись сразу на старте — двигаться некуда
	}

	// Идём по сегментам пути, пока не исчерпаем бюджет длины.
	OutPoint = Path->PathPoints[0];
	for (int32 i = 1; i < Path->PathPoints.Num(); ++i)
	{
		const FVector& A = Path->PathPoints[i - 1];
		const FVector& B = Path->PathPoints[i];
		const double SegmentLength = FVector::Dist(A, B);

		if (SegmentLength >= Remaining)
		{
			OutPoint = A + (B - A).GetSafeNormal() * Remaining;
			return true;
		}
		Remaining -= SegmentLength;
		OutPoint = B;
	}
	// Путь короче бюджета — дошли до самой цели.
	return true;
}

int32 UTacticsCombatStatics::GetMoveCostForDistance(const AUnitBase* Unit, float PathLength, int32 AvailableActionPoints)
{
	if (!Unit || PathLength < 0.f || AvailableActionPoints <= 0)
	{
		return 0;
	}
	if (PathLength <= Unit->MoveRange)
	{
		return 1;
	}
	if (PathLength <= Unit->MoveRange * 2.f && AvailableActionPoints >= 2)
	{
		return 2;
	}
	return 0; // вне оплачиваемой зоны
}

float UTacticsCombatStatics::GetUnitClearance(const AActor* Mover)
{
	// Занятая клетка стоящего + СОБСТВЕННЫЙ радиус бегущего. Второе слагаемое —
	// то самое «раздувание препятствия на радиус агента», без которого маршрут
	// прокладывался между двумя бойцами в щель, куда третий физически не лезет.
	// Радиус берём с капсулы (BP может её менять), фолбэк — дефолт ACharacter.
	float MoverRadius = 34.f;
	if (const ACharacter* Character = Cast<ACharacter>(Mover))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			MoverRadius = Capsule->GetScaledCapsuleRadius();
		}
	}
	return UnitObstacleRadius + MoverRadius;
}

double UTacticsCombatStatics::FindPathClearanceLimit(const TArray<FVector>& PathPoints,
	const TArray<FVector>& Obstacles, float Clearance)
{
	if (PathPoints.Num() < 2 || Obstacles.Num() == 0)
	{
		return -1.;
	}

	// Идём по сегментам, копим пройденную длину; на каждом ищем первую точку,
	// где просвет до любого диска меньше Clearance. Шаг сэмплирования — доля
	// клиренса: диск нельзя «перепрыгнуть» между двумя пробами.
	const double Step = FMath::Max(10., static_cast<double>(Clearance) * 0.5);
	double Travelled = 0.;
	for (int32 i = 0; i + 1 < PathPoints.Num(); ++i)
	{
		FVector A = PathPoints[i];
		FVector B = PathPoints[i + 1];
		A.Z = 0.;
		B.Z = 0.;
		const double SegmentLength = FVector::Dist(A, B);
		if (SegmentLength <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const int32 NumSteps = FMath::Max(1, FMath::CeilToInt(SegmentLength / Step));
		for (int32 S = 0; S <= NumSteps; ++S)
		{
			const double T = static_cast<double>(S) / NumSteps;
			const FVector Point = FMath::Lerp(A, B, T);
			for (const FVector& Obstacle : Obstacles)
			{
				if (FVector::DistSquared2D(Obstacle, Point) < FMath::Square(static_cast<double>(Clearance)))
				{
					return Travelled + SegmentLength * T;
				}
			}
		}
		Travelled += SegmentLength;
	}
	return -1.; // путь чист целиком
}

// --- Занятость (диски юнитов вместо мутаций навмеша) ---------------------------

bool UTacticsCombatStatics::IsUnitInTransit(const AActor* Unit)
{
	const APawn* Pawn = Cast<APawn>(Unit);
	const AUnitAIController* UnitAI = Pawn ? Cast<AUnitAIController>(Pawn->GetController()) : nullptr;
	return UnitAI && UnitAI->IsMoving();
}

void UTacticsCombatStatics::GetUnitObstacles(UWorld* World, const AActor* Ignored, TArray<FVector>& OutPositions)
{
	OutPositions.Reset();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AUnitBase> It(World); It; ++It)
	{
		AUnitBase* Unit = *It;
		if (Unit == Ignored || !IsUnitAlive(Unit))
		{
			continue;
		}
		// Бегущий юнит — переходное состояние: диск не ставим, по финишу
		// AIController уведомит контроллер игрока и зона перестроится.
		//
		// Судим по статусу path following, а НЕ по velocity: после финиша боец
		// ещё ~0.3 с гасит скорость торможением и по velocity считался бы
		// «бегущим» — поэтому зона следующего бойца, построенная в тот же кадр,
		// показывала его клетку свободной, и лечилось это только переключением
		// выбора туда-обратно.
		if (IsUnitInTransit(Unit))
		{
			continue;
		}
		OutPositions.Add(Unit->GetActorLocation());
	}
}

bool UTacticsCombatStatics::AdjustGoalOutOfUnits(UWorld* World, const AActor* Mover, FVector& InOutGoal)
{
	TArray<FVector> Obstacles;
	GetUnitObstacles(World, Mover, Obstacles);
	return AdjustGoalOutOfUnits(Obstacles, Mover, InOutGoal);
}

bool UTacticsCombatStatics::AdjustGoalOutOfUnits(const TArray<FVector>& Obstacles, const AActor* Mover,
	FVector& InOutGoal)
{
	// Радиус тот же, что у поля достижимости: вытолкнуть надо на расстояние, где
	// боец действительно помещается, иначе поле отклонит собственную же цель.
	const double Clearance = GetUnitClearance(Mover);

	// Пара итераций: выталкивание из одного диска может вдавить в соседний.
	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		bool bMoved = false;
		for (const FVector& Position : Obstacles)
		{
			const double Dist = FVector::Dist2D(Position, InOutGoal);
			if (Dist >= Clearance)
			{
				continue;
			}
			// На край диска с зазором; клик точно в центр — в сторону ходящего.
			FVector Away = InOutGoal - Position;
			Away.Z = 0.;
			if (!Away.Normalize())
			{
				Away = Mover ? (Mover->GetActorLocation() - Position).GetSafeNormal2D() : FVector::ForwardVector;
			}
			InOutGoal = Position + Away * (Clearance + 15.);
			bMoved = true;
		}
		if (!bMoved)
		{
			return true; // цель вне всех дисков
		}
	}

	// Не разрулилось (плотная толпа) — проверяем финально.
	for (const FVector& Position : Obstacles)
	{
		if (FVector::Dist2D(Position, InOutGoal) < Clearance)
		{
			return false;
		}
	}
	return true;
}
