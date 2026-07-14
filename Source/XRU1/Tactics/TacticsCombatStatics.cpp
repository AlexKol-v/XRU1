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
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

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
	const bool bHit = FMath::FRandRange(0.f, 100.f) <= HitChance;

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
			FVector::Dist(Ally->GetActorLocation(), Target->GetActorLocation()) <= 2500.f &&
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

bool UTacticsCombatStatics::GetPointAlongPathBudget(UObject* WorldContextObject, const FVector& Start,
	const FVector& Goal, float PathBudget, FVector& OutPoint)
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

	// Идём по сегментам пути, пока не исчерпаем бюджет длины.
	float Remaining = PathBudget;
	OutPoint = Path->PathPoints[0];
	for (int32 i = 1; i < Path->PathPoints.Num(); ++i)
	{
		const FVector& A = Path->PathPoints[i - 1];
		const FVector& B = Path->PathPoints[i];
		const float SegmentLength = FVector::Dist(A, B);

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

float UTacticsCombatStatics::GetNavPathLength(UObject* WorldContextObject, const FVector& Start, const FVector& Goal)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSys)
	{
		return -1.f;
	}

	UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(World, Start, Goal);
	if (!Path || !Path->IsValid() || Path->IsPartial())
	{
		return -1.f;
	}
	return Path->GetPathLength();
}
