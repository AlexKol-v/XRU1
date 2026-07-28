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
#include "GameFramework/CharacterMovementComponent.h"
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

const FCollisionObjectQueryParams& UTacticsCombatStatics::GetShotGeometryObjects()
{
	// Собирается один раз: набор неизменен, а спрашивают его в горячих циклах
	// (перебор огневых позиций × точек цели, кольцо позиций AI).
	static const FCollisionObjectQueryParams Params = []
	{
		FCollisionObjectQueryParams Result;
		Result.AddObjectTypesToQuery(ECC_WorldStatic);
		Result.AddObjectTypesToQuery(ECC_WorldDynamic); // двигаемые пропсы-укрытия
		return Result;
	}();
	return Params;
}

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

	const bool bHit = ResolveShotMechanics(Shooter, Target, HitChance, Damage, DamageEffectClass,
		Shooter->GetActorLocation());
	// Legacy/скриптовый путь не имеет длительного presentation lifecycle, поэтому
	// исход боя можно проверить сразу после механики.
	if (UWorld* World = Shooter->GetWorld())
	{
		if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->CheckCombatOutcome();
		}
	}
	return bHit;
}

bool UTacticsCombatStatics::ResolveShotMechanics(AActor* Shooter, AActor* Target, float ResolvedHitChance,
	float Damage, TSubclassOf<UGameplayEffect> DamageEffectClass, const FVector& ShotOrigin)
{
	if (!Shooter || !Target || !DamageEffectClass || !IsUnitAlive(Target))
	{
		return false;
	}

	const float HitChance = FMath::Clamp(ResolvedHitChance, 0.f, 100.f);
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
	NotifyCombatNoise(Shooter, ShotOrigin);

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
	const UCoverDetectionComponent* Cover =
		Unit ? Unit->FindComponentByClass<UCoverDetectionComponent>() : nullptr;
	const UCoverTuningDataAsset* Tuning = nullptr;
	if (Cover)
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

	// ФАКТИЧЕСКАЯ огневая позиция текущего юнита строится только в frame
	// зафиксированной ActiveCover. Прежний код брал tangent от цели, подтверждал
	// peek одной стеной, а край искал по другой — отсюда выбор противоположного
	// угла и попытка возврата сквозь геометрию.
	const FVector CurrentEye = Unit->GetActorLocation()
		+ FVector(0.f, 0.f, Tuning->EyeHeightOffset);
	const bool bAtCurrentUnitPosition = EyeLocation.Equals(CurrentEye, 2.f);
	if (bAtCurrentUnitPosition)
	{
		if (!Cover || Cover->ActiveCoverWallId == 0 || Cover->ActiveCoverNormal.IsNearlyZero())
		{
			return; // открытое поле: только центр, никаких виртуальных ±lean-root
		}

		const FVector ToWall = -Cover->ActiveCoverNormal.GetSafeNormal2D();
		FVector Tangent = FVector::CrossProduct(ToWall, FVector::UpVector).GetSafeNormal2D();
		if (ToWall.IsNearlyZero() || Tangent.IsNearlyZero())
		{
			return;
		}

		const FVector AnchorRoot = Cover->ActiveCoverAnchor;
		const FVector AnchorFoot = AnchorRoot - FVector(0.f, 0.f, CapsuleHalfHeight);
		const float AlongWall = FVector::DotProduct(
			OtherLocation - AnchorRoot, Tangent);
		const float FirstSign = AlongWall < -2.f ? -1.f : 1.f;
		const float SideSigns[2] = {FirstSign, -FirstSign};

		TArray<FVector> Obstacles;
		GetUnitObstacles(const_cast<UWorld*>(World), Unit, Obstacles);
		const double ClearanceSq = FMath::Square(static_cast<double>(GetUnitClearance(Unit)));
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(
			const_cast<UWorld*>(World));
		const UCapsuleComponent* Capsule = Cast<ACharacter>(Unit)
			? Cast<ACharacter>(Unit)->GetCapsuleComponent()
			: nullptr;
		if (!NavSys || !Capsule)
		{
			return;
		}

		const FCollisionObjectQueryParams& ShotObjects = GetShotGeometryObjects();
		FCollisionQueryParams ShotParams(SCENE_QUERY_STAT(ActiveCoverFirePeek), false);
		const FCollisionShape LosSphere = FCollisionShape::MakeSphere(Tuning->LosSphereRadius);
		FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(ActiveCoverFireCapsule), false, Unit);
		const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(
			CapsuleRadius, CapsuleHalfHeight);

		for (const float SideSign : SideSigns)
		{
			for (float Offset = FMath::Max(1.f, Tuning->PeekEdgeStep);
				Offset <= Tuning->PeekEdgeMaxDistance; Offset += FMath::Max(1.f, Tuning->PeekEdgeStep))
			{
				const FVector Probe = AnchorFoot + Tangent * (SideSign * Offset);
				FHitResult CoverHit;
				const ECoverType ProbeCover = UCoverDetectionComponent::TraceCoverAtLocation(
					World, Probe, ToWall, Tuning->CoverTraceDistance,
					Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
					Unit, 0.f, &CoverHit);
				if (ProbeCover != ECoverType::None && Cover->MatchesActiveCoverHit(CoverHit))
				{
					continue; // всё ещё вдоль ТОЙ ЖЕ активной стены
				}

				// Активная стена закончилась (либо началась соседняя). Точка root
				// находится уже за её краем и проходит nav/occupancy/capsule validation.
				const FVector PeekFoot = AnchorFoot + Tangent * SideSign
					* (Offset + CapsuleRadius + Tuning->PeekOutwardOffset);
				FNavLocation Projected;
				if (!NavSys->ProjectPointToNavigation(PeekFoot, Projected,
					FVector(60.f, 60.f, 200.f)))
				{
					break;
				}

				bool bOccupied = false;
				for (const FVector& Obstacle : Obstacles)
				{
					if (FVector::DistSquared2D(Obstacle, Projected.Location) < ClearanceSq)
					{
						bOccupied = true;
						break;
					}
				}
				const FVector ProjectedRoot = Projected.Location
					+ FVector(0.f, 0.f, CapsuleHalfHeight);
				FHitResult CapsuleHit;
				const bool bCapsuleBlocked = World->SweepSingleByChannel(
					CapsuleHit, Unit->GetActorLocation(), ProjectedRoot, FQuat::Identity,
					Capsule->GetCollisionObjectType(), CapsuleShape, CapsuleParams);
				const FVector PeekEye = ProjectedRoot
					+ FVector(0.f, 0.f, Tuning->EyeHeightOffset);
				FHitResult EyePathHit;
				const bool bEyePathBlocked = World->SweepSingleByObjectType(
					EyePathHit, EyeLocation, PeekEye, FQuat::Identity,
					ShotObjects, LosSphere, ShotParams);
				if (!bOccupied && !bCapsuleBlocked && !bEyePathBlocked)
				{
					OutEyePositions.Add(PeekEye);
				}
				break; // по этой стороне активный край уже найден
			}
		}
		return;
	}

	// ⚠️ ГЛАВНОЕ ПРАВИЛО (по замечанию игрока, XCOM): выглядывать можно только
	// из УКРЫТИЯ. Юнит в открытом поле видит/виден ТОЛЬКО по прямой (центр).
	//
	// ⚠️ НО укрытие НЕ обязано стоять строго в сторону цели. Первая редакция
	// проверяла только направление на цель — и юнит, прижавшийся к пиллару
	// БОКОМ (цель за углом), не получал ни одной огневой позиции кроме центра.
	// В логе это видно прямо: `shooterPos=1` в 76% запросов, и все они
	// `visible=0`. Именно из-за этого «стоит за препятствием, а пика нет».
	//
	// Поэтому проверяем укрытие в НЕСКОЛЬКИХ направлениях: на цель, вбок (обе
	// стороны) и назад. Это ровно тот набор, из-за которого выглядывание имеет
	// смысл; чистое поле по-прежнему не даёт ни одной боковой позиции, потому
	// что там укрытия нет ни в одном из них.
	auto HasCoverToward = [&](const FVector& Direction)
	{
		return UCoverDetectionComponent::TraceCoverAtLocation(World, FootBase, Direction,
			Tuning->CoverTraceDistance, Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
			Unit) != ECoverType::None;
	};

	if (!HasCoverToward(DirToOther) && !HasCoverToward(Side) &&
		!HasCoverToward(-Side) && !HasCoverToward(-DirToOther))
	{
		return; // укрытия нет ни с одной стороны — только прямая видимость из центра
	}

	// Свип мира: юниты выстрел не блокируют — единая геометрия выстрела.
	const FCollisionObjectQueryParams& ObjectParams = GetShotGeometryObjects();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FiringPeek), /*bTraceComplex=*/false);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Tuning->LosSphereRadius);
	auto SweepClear = [&](const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		return !World->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, ObjectParams, Sphere, Params);
	};

	// Provisional preview из гипотетической клетки: небольшой lean допустим
	// только для оценки LOS и НИКОГДА не становится root фактического действия.
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
				Unit) != ECoverType::None)
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
	const FCollisionObjectQueryParams& ObjectParams = GetShotGeometryObjects();
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

bool UTacticsCombatStatics::HasLineOfSightFromFrozenOrigin(const UWorld* World,
	const FVector& FiringEyeLocation, const AActor* Target)
{
	if (!World || !Target)
	{
		return false;
	}

	const UCoverTuningDataAsset* Tuning = GetCoverTuning(World);
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector TargetEye = TargetLocation + FVector(0.f, 0.f, Tuning->EyeHeightOffset);

	// Источник намеренно один: stale callback или StepOut не могут незаметно
	// выбрать новую позицию стрелка после подтверждения действия.
	TArray<FVector, TInlineAllocator<4>> TargetPoints;
	GetFiringPositions(World, Target, TargetEye, FiringEyeLocation, TargetPoints);
	TargetPoints.Add(TargetLocation - FVector(0.f, 0.f, 20.f));

	const FCollisionObjectQueryParams& ObjectParams = GetShotGeometryObjects();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FrozenFireCommitLOS), /*bTraceComplex=*/false);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Tuning->LosSphereRadius);
	for (const FVector& Point : TargetPoints)
	{
		FHitResult Hit;
		if (!World->SweepSingleByObjectType(Hit, FiringEyeLocation, Point, FQuat::Identity,
			ObjectParams, Sphere, Params))
		{
			return true;
		}
	}
	return false;
}

void UTacticsCombatStatics::GetViableFiringPositions(const AActor* Shooter, const AActor* Target,
	TArray<FVector, TInlineAllocator<4>>& OutPositions)
{
	OutPositions.Reset();
	const UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (!World || !Target)
	{
		return;
	}

	const UCoverTuningDataAsset* Tuning = GetCoverTuning(World);
	const FVector EyeLocation = Shooter->GetActorLocation() + FVector(0.f, 0.f, Tuning->EyeHeightOffset);
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector TargetPoints[2] = {
		TargetLocation + FVector(0.f, 0.f, Tuning->EyeHeightOffset),
		TargetLocation - FVector(0.f, 0.f, 20.f)
	};

	const FCollisionObjectQueryParams& ObjectParams = GetShotGeometryObjects();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ViableFiringPos), /*bTraceComplex=*/false);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Tuning->LosSphereRadius);

	TArray<FVector, TInlineAllocator<4>> Candidates;
	GetFiringPositions(World, Shooter, EyeLocation, TargetLocation, Candidates);
	for (const FVector& Candidate : Candidates)
	{
		for (const FVector& Point : TargetPoints)
		{
			FHitResult Hit;
			if (!World->SweepSingleByObjectType(Hit, Candidate, Point, FQuat::Identity,
				ObjectParams, Sphere, Params))
			{
				OutPositions.Add(Candidate);
				break;
			}
		}
	}
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

	const FCollisionObjectQueryParams& ObjectParams = GetShotGeometryObjects();
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

		if (i == 0)
		{
			// ⚠️ ЗДЕСЬ НЕЛЬЗЯ звать GetCoverAgainst — это бесконечная рекурсия:
			// GetCoverAgainst сам спрашивает GetFiringStance, чтобы узнать,
			// ОТКУДА реально прилетит выстрел. Разрыв цикла осмысленный, а не
			// технический: стойка — вопрос о СОБСТВЕННОЙ позе стрелка («стою ли я
			// за своей стеной в сторону цели»), а GetCoverAgainst отвечает на
			// другой вопрос — про фланг, то есть дойдёт ли конкретный выстрел.
			float ShooterHalfHeight = 88.f;
			if (const ACharacter* ShooterCharacter = Cast<ACharacter>(Shooter))
			{
				if (const UCapsuleComponent* Capsule = ShooterCharacter->GetCapsuleComponent())
				{
					ShooterHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
				}
			}
			const FVector ShooterFloor =
				Shooter->GetActorLocation() - FVector(0.f, 0.f, ShooterHalfHeight);
			// Стена дальше цели — не «моё укрытие в сторону цели» (тот же кламп
			// «между», что и у GetCoverAgainst).
			FHitResult CoverHit;
			const ECoverType TracedCover = UCoverDetectionComponent::TraceCoverAtLocation(World,
				ShooterFloor, (TargetLocation - Shooter->GetActorLocation()).GetSafeNormal2D(),
				UCoverDetectionComponent::GetCoverTraceLength(Tuning, ShooterFloor, TargetLocation),
				Tuning->HalfCoverHeight, Tuning->FullCoverHeight,
				Shooter, Tuning->LosSphereRadius, &CoverHit);
			const UCoverDetectionComponent* ActiveCover =
				Shooter->FindComponentByClass<UCoverDetectionComponent>();
			const ECoverType Cover = ActiveCover && ActiveCover->MatchesActiveCoverHit(CoverHit)
				? TracedCover
				: ECoverType::None;
			if (Cover == ECoverType::Full)
			{
				// Высокую стену нельзя «перестрелять сверху» crouched-монтажом.
				// Центральный луч здесь только визуальная аномалия геометрии: ищем
				// следующий реально доступный target-aware край.
				continue;
			}
			OutFiringEyeLocation = Positions[i];
			Stance = (Cover == ECoverType::Half) ? EFiringStance::OverCover : EFiringStance::Open;
		}
		else
		{
			OutFiringEyeLocation = Positions[i];
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

bool UTacticsCombatStatics::IsTargetFlankedBy(const AActor* Target, const AActor* Shooter)
{
	if (!Target || !Shooter)
	{
		return false;
	}
	const UCoverDetectionComponent* Cover = Target->FindComponentByClass<UCoverDetectionComponent>();
	if (!Cover || Cover->BestCoverAround == ECoverType::None)
	{
		return false; // нечем прикрыться — «открыт», а не «фланкирован»
	}
	// ТОЧНЫЙ путь: GetCoverAgainst считает укрытие против ФАКТИЧЕСКОЙ огневой
	// позиции стрелка (выглядывание). Это то, что видит игрок, поэтому щит и
	// шанс попадания обязаны идти именно отсюда.
	return Cover->GetCoverAgainst(Shooter) == ECoverType::None;
}

bool UTacticsCombatStatics::IsTargetFlankedByLocation(const AActor* Target, const FVector& ShooterLocation)
{
	if (!Target)
	{
		return false;
	}

	const UCoverDetectionComponent* Cover = Target->FindComponentByClass<UCoverDetectionComponent>();
	if (!Cover)
	{
		return false; // нечем прикрыться — «открыт», а не «фланкирован»
	}

	// BestCoverAround — кэш ЛОКАЛЬНОГО укрытия цели (перестраивается после
	// каждого её перемещения): «стоит ли она вообще за чем-то».
	if (Cover->BestCoverAround == ECoverType::None)
	{
		return false;
	}

	// Против стрелка считаем ЗАНОВО (стороны + дуга): зависит от того, откуда
	// стреляют, и кэшировать это нельзя — сломает разрушаемость (§V.2 п.4).
	float TargetHalfHeight = 88.f;
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(Target))
	{
		if (const UCapsuleComponent* Capsule = TargetCharacter->GetCapsuleComponent())
		{
			TargetHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	const FVector TargetFloor = Target->GetActorLocation() - FVector(0.f, 0.f, TargetHalfHeight);

	return Cover->EvaluateCoverAtLocation(TargetFloor, ShooterLocation) == ECoverType::None;
}

ECoverShield UTacticsCombatStatics::GetCoverShieldAgainst(const AActor* Target, const AActor* Shooter,
	ECoverType& OutShieldCover)
{
	OutShieldCover = ECoverType::None;
	if (!Target || !Shooter)
	{
		return ECoverShield::None;
	}

	const UCoverDetectionComponent* Cover = Target->FindComponentByClass<UCoverDetectionComponent>();
	if (!Cover)
	{
		return ECoverShield::None;
	}

	// 1) Укрытие работает против этого стрелка — синий щит.
	const ECoverType Against = Cover->GetCoverAgainst(Shooter);
	if (Against != ECoverType::None)
	{
		OutShieldCover = Against;
		return ECoverShield::Covered;
	}

	// 2) Укрытие есть «вообще», но не против нас — стрелок во фланге, жёлтый щит.
	// Щит рисуем по локальному укрытию цели: игрок должен видеть, ЧТО именно он
	// обошёл (низкое или высокое).
	if (Cover->BestCoverAround != ECoverType::None)
	{
		OutShieldCover = Cover->BestCoverAround;
		return ECoverShield::Flanked;
	}

	// 3) Чистое поле — щита нет вовсе.
	return ECoverShield::None;
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

	// ⚠️ ПУТЬ СКВОЗЬ СОЮЗНИКА — НЕ ПРЕПЯТСТВИЕ (правило XCOM и корень «ботов
	// гуськом», 2026-07-25).
	//
	// Раньше здесь бюджет дополнительно урезался `FindPathClearanceLimit`:
	// длиной до первого места, где полилиния навмеша подходит к стоящему юниту
	// ближе `GetUnitClearance` (≈94 см). Последствие проявлялось не в самом
	// движении, а в `FindCoverPoint`: он считает точку достижимой, только если
	// урезанный путь пришёл в неё (расхождение ≤ 75 см). То есть ЛЮБАЯ позиция
	// за спиной союзника отбраковывалась как недостижимая — и бойцы могли
	// выбирать точки только «до» товарищей, выстраиваясь в колонну.
	//
	// Заголовок `FindPathClearanceLimit` прямо предупреждал: «путь задевает
	// юнита» != «дойти нельзя», навмеш строит прямую и в чистом поле пройдёт
	// сквозь одиночного бойца, которого Detour Crowd обходит на бегу. Функция
	// использовалась ровно вопреки собственному контракту, поэтому удалена.
	//
	// Занятость по-прежнему соблюдается, но там, где ей место — на КОНЦЕ пути:
	// кандидаты в `FindCoverPoint` проверяются на диски занятости, а
	// `MoveWithBudget` дополнительно зовёт `AdjustGoalOutOfUnits`. Встать в
	// союзника нельзя; пробежать мимо — можно, этим и занят Detour Crowd.
	double Remaining = PathBudget;
	if (Remaining <= 0.)
	{
		return false;
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

// ⚠️ `FindPathClearanceLimit` УДАЛЕНА (2026-07-25). Она отвечала на вопрос
// «докуда мы точно дойдём по прямой, не задев стоящего юнита», а вызывалась как
// ответ на «достижима ли точка» — ровно вопреки предупреждению в собственном
// заголовке. Из-за этого `FindCoverPoint` отбраковывал любую позицию за спиной
// союзника, и бойцы строились в колонну. Единственный вызов убран, функция
// вместе с ним: держать метрику, которую снова захочется применить не по
// назначению, хуже, чем не иметь её вовсе.

// --- Занятость (диски юнитов вместо мутаций навмеша) ---------------------------

bool UTacticsCombatStatics::IsUnitInTransit(const AActor* Unit)
{
	const APawn* Pawn = Cast<APawn>(Unit);
	const AUnitAIController* UnitAI = Pawn ? Cast<AUnitAIController>(Pawn->GetController()) : nullptr;
	if (UnitAI && UnitAI->IsMoving())
	{
		return true;
	}

	const ACharacter* Character = Cast<ACharacter>(Unit);
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return Movement && Movement->Velocity.SizeSquared2D() > FMath::Square(5.f);
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
