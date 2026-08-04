#include "TacticalEncounter.h"

#include "AIController.h"
#include "CollisionQueryParams.h"
#include "Components/BillboardComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "TacticalAIDirectorSubsystem.h"
#include "TacticalQuestEvents.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameMode.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "XRU1Log.h"

// ---------------------------------------------------------------------------
// ATacticalSpawnGroupBase
// ---------------------------------------------------------------------------

ATacticalSpawnGroupBase::ATacticalSpawnGroupBase()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	EditorIcon = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon)
	{
		EditorIcon->SetupAttachment(Root);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif
}

TArray<AUnitBase*> ATacticalSpawnGroupBase::GetSpawnedUnits() const
{
	TArray<AUnitBase*> Result;
	Result.Reserve(SpawnedUnits.Num());
	for (const TObjectPtr<AUnitBase>& Unit : SpawnedUnits)
	{
		if (Unit)
		{
			Result.Add(Unit);
		}
	}
	return Result;
}

int32 ATacticalSpawnGroupBase::GetCountForDifficulty(EDifficultyLevel Difficulty) const
{
	const int32* Found = CountByDifficulty.Find(Difficulty);
	return Found ? FMath::Max(0, *Found) : 0;
}

bool ATacticalSpawnGroupBase::IsTooCloseToSquad(const FVector& Location) const
{
	if (MinDistanceToSquad <= 0.f)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return false;
	}

	for (AActor* Actor : TurnManager->GetPlayerSideUnits())
	{
		if (!UTacticsCombatStatics::IsUnitAlive(Actor))
		{
			continue;
		}
		if (FVector::Dist2D(Actor->GetActorLocation(), Location) < MinDistanceToSquad)
		{
			return true;
		}
	}
	return false;
}

bool ATacticalSpawnGroupBase::ResolveSpawnLocation(int32 Index, FVector& OutLocation,
	FRotator& OutRotation) const
{
	// Неконстантный World: навигационная система выдаётся только по нему.
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Явная точка имеет приоритет: дизайнер поставил её глазами, и подменять её
	// «умным» поиском нельзя — это ровно то, за что ругают процедурный спавн.
	// Точек не хватило на всех — недостающие ищутся рядом с последней заданной
	// (аналог `IdealPodMemberSpawnDistance` у XCOM), а без точек вовсе — вокруг
	// самого актора группы.
	const AActor* OwnPoint = SpawnPoints.IsValidIndex(Index) ? SpawnPoints[Index].Get() : nullptr;
	const AActor* FallbackPoint = nullptr;
	for (int32 i = SpawnPoints.Num() - 1; i >= 0 && !FallbackPoint; --i)
	{
		FallbackPoint = SpawnPoints[i].Get();
	}

	const AActor* Anchor = OwnPoint ? OwnPoint : FallbackPoint;
	const FVector Base = Anchor ? Anchor->GetActorLocation() : GetActorLocation();
	OutRotation = Anchor ? Anchor->GetActorRotation() : GetActorRotation();

	if (OwnPoint && IsSpawnLocationAllowed(Base))
	{
		OutLocation = Base;
		return true;
	}

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav)
	{
		OutLocation = Base;
		return IsSpawnLocationAllowed(Base);
	}

	// Шаг 1. Случайная достижимая точка в радиусе. Самый качественный результат:
	// он гарантирует связность с остальной картой.
	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		FNavLocation Candidate;
		if (!Nav->GetRandomReachablePointInRadius(Base, SpawnScatterRadius, Candidate))
		{
			continue;
		}
		if (!IsSpawnLocationAllowed(Candidate.Location))
		{
			continue;
		}
		OutLocation = Candidate.Location;
		return true;
	}

	// Шаг 2. Детерминированное кольцо вокруг базы с проекцией на навмеш.
	//
	// `GetRandomReachablePointInRadius` требует построенного навмеша ВОКРУГ
	// точки и связного пути до неё. При RuntimeGeneration=Dynamic на момент
	// старта боя дальние участки могут быть ещё не сгенерированы, и запрос
	// возвращает false даже там, где место очевидно есть. Из-за этого бойцы
	// группы молча пропадали (прогон 2026-08-03: «создано 1 из 3»).
	const float Radii[] = { SpawnScatterRadius, SpawnScatterRadius * 0.5f,
		FMath::Max(SpawnScatterRadius, 400.f) };
	for (float Radius : Radii)
	{
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const float Angle = Step * (2.f * PI / 8.f) + Index * 0.4f;
			const FVector Candidate = Base +
				FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			FNavLocation Projected;
			if (Nav->ProjectPointToNavigation(Candidate, Projected, FVector(400.f, 400.f, 500.f)) &&
				IsSpawnLocationAllowed(Projected.Location))
			{
				OutLocation = Projected.Location;
				return true;
			}
		}
	}

	// Шаг 3. Сама база, спроецированная на навмеш.
	FNavLocation Projected;
	if (Nav->ProjectPointToNavigation(Base, Projected, FVector(500.f, 500.f, 500.f)) &&
		IsSpawnLocationAllowed(Projected.Location))
	{
		OutLocation = Projected.Location;
		return true;
	}

	// Шаг 4. Навигация не подтвердила ни одной точки — ставим бойца у базы со
	// смещением по индексу. Появиться на неподтверждённой точке лучше, чем не
	// появиться вовсе: пропавший боец ломает состав миссии молча, а этот случай
	// виден в логе и исправляется переносом точки.
	const float FallbackAngle = Index * (2.f * PI / 6.f);
	const float FallbackRadius = Index == 0 ? 0.f : FMath::Max(120.f, SpawnScatterRadius * 0.5f);
	const FVector Fallback = Base +
		FVector(FMath::Cos(FallbackAngle) * FallbackRadius, FMath::Sin(FallbackAngle) * FallbackRadius, 0.f);
	if (!IsSpawnLocationAllowed(Fallback))
	{
		return false; // правило дистанции до отряда нарушать нельзя даже в фолбэке
	}

	UE_LOG(LogXRU1Scenario, Warning,
		TEXT("[Encounter] %s: боец %d поставлен без подтверждения навмешем (%.0f, %.0f). "
			"Проверь, что точка спавна стоит на проходимом полу"),
		*GetNameSafe(this), Index + 1, Fallback.X, Fallback.Y);
	OutLocation = Fallback;
	return true;
}

bool ATacticalSpawnGroupBase::IsSpawnLocationAllowed(const FVector& Location) const
{
	return !IsTooCloseToSquad(Location);
}

void ATacticalSpawnGroupBase::ConfigureSpawnedUnit(AUnitBase* Unit, int32 IndexInGroup,
	int32 TotalCount) const
{
	if (!Unit)
	{
		return;
	}

	Unit->PodId = PodId;

	// Сторожа — первые бойцы группы: их точки стоят у охраняемого объекта,
	// поэтому первая позиция в SpawnPoints и есть позиция сторожа. Сторож
	// охраняет объект, а не гуляет вокруг, поэтому радиус обхода ему не идёт.
	const bool bIsSentry = IndexInGroup < FMath::Clamp(SentryCount, 0, TotalCount);
	Unit->PatrolRoamRadius = bIsSentry ? 0.f : RoamRadius;

	if (bIsSentry || PatrolPoints.Num() == 0)
	{
		// Без маршрута, но с радиусом боец обходит зону вокруг своего места
		// появления — это и есть «удержание участка».
		Unit->PatrolPoints.Reset();
		return;
	}

	// Порядок точек у всех бойцов ОДИН И ТОТ ЖЕ — это геометрия маршрута,
	// которую нарисовал дизайнер. Растягивает группу по маршруту стартовый
	// индекс, а не перестановка массива: сдвиг массива увёл бы `PingPong`
	// разворачиваться в чужих местах.
	Unit->PatrolPoints = PatrolPoints;
	Unit->PatrolRouteMode = PatrolRouteMode;
	Unit->bPatrolStartFromNearest = bStartFromNearestPoint;
	Unit->PatrolStartIndex = (bStaggerPatrolStart && PatrolPoints.Num() > 1)
		? IndexInGroup % PatrolPoints.Num() : 0;
}

int32 ATacticalSpawnGroupBase::SpawnUnits(int32 Count, bool bRegisterInCombat,
	TArray<AUnitBase*>& OutUnits)
{
	UWorld* World = GetWorld();
	if (!World || Count <= 0)
	{
		return 0;
	}
	if (!EnemyClass)
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Encounter] %s: не задан EnemyClass — группа не создана"),
			*GetNameSafe(this));
		return 0;
	}

	ATacticsGameMode* GameMode = World->GetAuthGameMode<ATacticsGameMode>();
	UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>();

	int32 Created = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FVector Location;
		FRotator Rotation;
		if (!ResolveSpawnLocation(Index, Location, Rotation))
		{
			UE_LOG(LogXRU1Scenario, Error,
				TEXT("[Encounter] %s: боец %d/%d не создан — все кандидаты нарушают "
					"MinDistanceToSquad=%.0f. Отодвинь точку спавна от отряда"),
				*GetNameSafe(this), Index + 1, Count, MinDistanceToSquad);
			continue;
		}

		// Deferred: PodId и маршрут обязаны быть выставлены ДО OnPossess —
		// именно там боец попадает в свой под. Обычный SpawnActor успевал
		// зарегистрировать его одиночкой раньше, чем мы назначим группу.
		FTransform SpawnTransform(Rotation, Location);
		AUnitBase* Unit = World->SpawnActorDeferred<AUnitBase>(
			EnemyClass, SpawnTransform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Unit)
		{
			continue;
		}

		ConfigureSpawnedUnit(Unit, Index, Count);
		Unit->FinishSpawning(SpawnTransform);

		if (!Unit->GetController())
		{
			Unit->SpawnDefaultController();
		}

		// Сложность и профиль поведения — те же, что у размещённых руками
		// врагов: иначе заспавненный боец играл бы по чужим правилам.
		if (GameMode)
		{
			GameMode->ApplySpawnedEnemyDefaults(Unit);
		}

		if (bRegisterInCombat && TurnManager)
		{
			TurnManager->RegisterUnitInCombat(Unit);
		}

		SpawnedUnits.Add(Unit);
		OutUnits.Add(Unit);
		++Created;
	}

	if (Created > 0)
	{
		OnUnitsSpawned(OutUnits);
	}
	return Created;
}

// ---------------------------------------------------------------------------
// ATacticalEncounter
// ---------------------------------------------------------------------------

ATacticalEncounter::ATacticalEncounter()
{
	// Стартовая группа стоит на карте с начала боя — дистанцию до отряда
	// проверять не нужно, её задаёт сама расстановка.
	MinDistanceToSquad = 0.f;
}

int32 ATacticalEncounter::SpawnForDifficulty(EDifficultyLevel Difficulty)
{
	if (!bEnabled || bSpawned)
	{
		return 0;
	}
	bSpawned = true;

	const int32 Count = GetCountForDifficulty(Difficulty);
	if (Count <= 0)
	{
		UE_LOG(LogXRU1Scenario, Log,
			TEXT("[Encounter] %s (%s): на этой сложности бойцов не положено"),
			*GetNameSafe(this), *EncounterId.ToString());
		return 0;
	}

	TArray<AUnitBase*> Units;
	const int32 Created = SpawnUnits(Count, /*bRegisterInCombat=*/false, Units);
	UE_LOG(LogXRU1Scenario, Log,
		TEXT("[Encounter] %s (%s): создано %d из %d, под='%s', точек патруля=%d, сторожей=%d"),
		*GetNameSafe(this), *EncounterId.ToString(), Created, Count,
		PodId.IsNone() ? TEXT("одиночки") : *PodId.ToString(),
		PatrolPoints.Num(), FMath::Clamp(SentryCount, 0, Count));
	return Created;
}

// ---------------------------------------------------------------------------
// ATacticalReinforcementBeacon
// ---------------------------------------------------------------------------

ATacticalReinforcementBeacon::ATacticalReinforcementBeacon()
{
	// Прибывшая волна не имеет права высадиться игроку в упор: это правило
	// XCOM (`XComExclusionDistance`), без него подкрепление читается как
	// нечестный спавн за спиной. 1600 = ход бегом (2 AP × MoveRange).
	MinDistanceToSquad = 1600.f;
}

void ATacticalReinforcementBeacon::BeginPlay()
{
	Super::BeginPlay();

	if (UTurnManagerSubsystem* TurnManager = GetWorld()
		? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr)
	{
		TurnManager->OnTurnStarted.AddUniqueDynamic(this,
			&ATacticalReinforcementBeacon::HandleTurnStarted);
	}
}

void ATacticalReinforcementBeacon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTurnManagerSubsystem* TurnManager = GetWorld()
		? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr)
	{
		TurnManager->OnTurnStarted.RemoveDynamic(this,
			&ATacticalReinforcementBeacon::HandleTurnStarted);
	}
	Super::EndPlay(EndPlayReason);
}

bool ATacticalReinforcementBeacon::IsSpawnLocationAllowed(const FVector& Location) const
{
	if (!Super::IsSpawnLocationAllowed(Location))
	{
		return false;
	}
	if (!bAvoidSquadVision)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!World || !TurnManager)
	{
		return true;
	}

	// «Видит ли отряд эту точку» — та же формула, что у правил видимости:
	// дистанция обзора отряда + прямая линия. Появление противника прямо на
	// глазах читается как нечестный спавн, а не как давление.
	const FVector Target = Location + FVector(0.f, 0.f, 90.f);
	for (AActor* Actor : TurnManager->GetPlayerSideUnits())
	{
		if (!UTacticsCombatStatics::IsUnitAlive(Actor))
		{
			continue;
		}
		if (FVector::Dist2D(Actor->GetActorLocation(), Location) > UTacticsCombatStatics::SquadVisionRange)
		{
			continue;
		}

		const FVector Eye = Actor->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ReinforcementVisibility), /*bTraceComplex=*/false);
		Params.AddIgnoredActor(Actor);
		const bool bBlocked = World->LineTraceSingleByChannel(
			Hit, Eye, Target, ECC_Visibility, Params);
		if (!bBlocked)
		{
			return false; // отряд смотрит прямо на точку высадки
		}
	}
	return true;
}

ATacticalReinforcementBeacon* ATacticalReinforcementBeacon::FindBeacon(
	const UObject* WorldContextObject, FName BeaconId)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATacticalReinforcementBeacon> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (BeaconId.IsNone() || It->BeaconId == BeaconId)
		{
			return *It;
		}
	}
	return nullptr;
}

bool ATacticalReinforcementBeacon::ShouldAutoRequest() const
{
	if (TriggerMode != EReinforcementTrigger::WhenEnemiesLow)
	{
		return false;
	}

	const UTurnManagerSubsystem* TurnManager = GetWorld()
		? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager || !TurnManager->IsInCombat())
	{
		return false;
	}
	if (TurnManager->GetTurnNumber() < EarliestTurn)
	{
		return false;
	}
	// Кулдаун: зачистка второй волны не должна мгновенно вызывать третью.
	if (WavesSpawned > 0 && TurnManager->GetTurnNumber() - LastWaveTurn < CooldownTurns)
	{
		return false;
	}
	return TurnManager->GetAliveEnemyCount() <= EnemyCountThreshold;
}

bool ATacticalReinforcementBeacon::RequestWave()
{
	if (bWavePending)
	{
		return false;
	}
	if (WavesSpawned >= MaxWaves)
	{
		return false;
	}

	const UTurnManagerSubsystem* TurnManager = GetWorld()
		? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager || !TurnManager->IsInCombat())
	{
		return false;
	}

	bWavePending = true;
	CountdownLeft = FMath::Max(0, CountdownEnemyTurns);

	UE_LOG(LogXRU1Scenario, Log,
		TEXT("[Reinforcements] %s: волна %d запрошена, высадка через %d ход(ов) врага"),
		*BeaconId.ToString(), WavesSpawned + 1, CountdownLeft);

	// Маяк виден игроку СРАЗУ: в XCOM 2 flare появляется в момент запроса, а не
	// вместе с юнитами — именно это даёт ход на реакцию.
	OnBeaconSignal();
	UTacticalQuestEvents::BroadcastQuestEvent(
		this, TacticalQuestTags::Event_Tactical_Reinforcements_Signaled, this);

	if (CountdownLeft == 0)
	{
		SpawnWave();
	}
	return true;
}

void ATacticalReinforcementBeacon::HandleTurnStarted(ETurnPhase Phase)
{
	if (Phase != ETurnPhase::Enemy)
	{
		return;
	}

	// Отсчёт идёт в ходах ВРАГА — verbatim XCOM 2 (`OnTurnBegun` слушает
	// PlayerTurnBegun вражеской стороны).
	if (bWavePending)
	{
		if (--CountdownLeft <= 0)
		{
			SpawnWave();
		}
		return;
	}

	if (ShouldAutoRequest())
	{
		RequestWave();
	}
}

void ATacticalReinforcementBeacon::SpawnWave()
{
	bWavePending = false;

	UWorld* World = GetWorld();
	UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager || !TurnManager->IsInCombat())
	{
		return; // бой закончился, пока волна была в пути
	}

	ATacticsGameMode* GameMode = World->GetAuthGameMode<ATacticsGameMode>();
	const EDifficultyLevel Difficulty = GameMode
		? GameMode->GetActiveDifficulty() : EDifficultyLevel::Medium;

	// Каждая следующая волна сильнее предыдущей — правило Long War 2.
	const int32 BaseCount = GetCountForDifficulty(Difficulty);
	const int32 Count = FMath::Max(0, BaseCount + WavesSpawned * FMath::Max(0, WaveSizeGrowth));
	if (Count <= 0)
	{
		return;
	}

	TArray<AUnitBase*> Units;
	const int32 Created = SpawnUnits(Count, /*bRegisterInCombat=*/true, Units);
	if (Created <= 0)
	{
		UE_LOG(LogXRU1Scenario, Warning,
			TEXT("[Reinforcements] %s: не удалось высадить ни одного бойца "
				"(нет валидных точек: навмеш, дистанция до отряда или обзор отряда)"),
			*BeaconId.ToString());
		return;
	}

	++WavesSpawned;
	LastWaveTurn = TurnManager->GetTurnNumber();

	// Прибывшие знают, где отряд: в XCOM 2 группа получает mapwide-alert на
	// позицию XCOM. Без этого волна высаживается и уходит патрулировать — то
	// есть ведёт себя как декорация.
	if (bAlertedOnArrival)
	{
		if (UTacticalAIDirectorSubsystem* Director = World->GetSubsystem<UTacticalAIDirectorSubsystem>())
		{
			AActor* Nearest = nullptr;
			float BestDistSq = TNumericLimits<float>::Max();
			for (AActor* Actor : TurnManager->GetPlayerSideUnits())
			{
				if (!UTacticsCombatStatics::IsUnitAlive(Actor))
				{
					continue;
				}
				const float DistSq = FVector::DistSquared2D(Actor->GetActorLocation(), GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Nearest = Actor;
				}
			}
			if (Nearest)
			{
				for (AUnitBase* Unit : Units)
				{
					Director->NotifyEnemySpotted(Unit, Nearest);
				}
			}
		}
	}

	UE_LOG(LogXRU1Scenario, Log,
		TEXT("[Reinforcements] %s: высадилась волна %d — бойцов %d, под='%s', тревога=%d"),
		*BeaconId.ToString(), WavesSpawned, Created,
		PodId.IsNone() ? TEXT("одиночки") : *PodId.ToString(), bAlertedOnArrival ? 1 : 0);

	OnWaveArrived(Units);
	UTacticalQuestEvents::BroadcastQuestEvent(
		this, TacticalQuestTags::Event_Tactical_Reinforcements_Arrived, this);
}
