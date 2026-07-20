#include "TacticsCombatStatics.h"
#include "TacticsGameplayTags.h"
#include "CoverDetectionComponent.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "UnitAIController.h"
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
					DefenseBonus *= 2.f;
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

bool UTacticsCombatStatics::HasLineOfSight(const AActor* Viewer, const AActor* Target)
{
	if (!Viewer || !Target)
	{
		return false;
	}
	UWorld* World = Viewer->GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(UnitLOS), true, Viewer);
	Params.AddIgnoredActor(Target);

	// Смотрим «с уровня глаз» — над полными укрытиями не видно, над низкими — да.
	const FVector EyeOffset(0.f, 0.f, 60.f);
	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit,
		Viewer->GetActorLocation() + EyeOffset,
		Target->GetActorLocation() + EyeOffset,
		ECC_Visibility, Params);
	return !bBlocked;
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
