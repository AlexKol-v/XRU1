// Валидатор расстановки боевой миссии.
//
// Расстановку делает дизайнер глазами, но три вещи глазами не проверяются:
// длина маршрута ПО НАВМЕШУ (а не по прямой), число ходов на этот маршрут и
// дистанции между группами, при которых поды вскрываются каскадом. Команда
// `xru1.Mission.Validate` печатает ровно это — по фактическому состоянию мира.
//
// Правило то же, что у остальной диагностики (TacticsDebug): команда ничего не
// меняет в мире, только читает.

#include "CoreMinimal.h"

#include "AIBehaviorProfileDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "MissionObjectives.h"
#include "Engine/Level.h"
#include "Misc/PackageName.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "TacticalAIDirectorSubsystem.h"
#include "TacticalEncounter.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameMode.h"
#include "TacticsTypes.h"
#include "TurnManagerSubsystem.h"
#include "UnitAIController.h"
#include "UnitBase.h"
#include "XRU1Log.h"

namespace MissionValidator
{
	/**
	 * Команда юнита с фолбэком на дефолт класса.
	 *
	 * До BeginPlay `GetGenericTeamId()` возвращает NoTeam, поэтому в редакторе —
	 * там, где расстановку и проверяют, — стороны было не отличить. Значение
	 * применяется в BeginPlay из того же `DefaultTeamId`, так что фолбэк не
	 * выдумывает данные, а читает тот же источник раньше по времени.
	 */
	static uint8 ResolveTeamId(const AUnitBase* Unit)
	{
		const uint8 Runtime = Unit->GetGenericTeamId().GetId();
		return Runtime != FGenericTeamId::NoTeam.GetId() ? Runtime : Unit->GetDefaultTeamId();
	}

	/** Бюджет одного полного хода бегом: 2 AP × MoveRange. */
	static float GetTurnMoveBudget(const UWorld* World)
	{
		float MoveRange = 800.f;
		for (TActorIterator<AUnitBase> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (ResolveTeamId(*It) == TacticsTeamIds::Player)
			{
				MoveRange = FMath::Max(100.f, It->MoveRange);
				break;
			}
		}
		return MoveRange * 2.f;
	}

	/** Имя уровня, в котором лежит актор (для проверки «не в том sublevel»). */
	static FString GetLevelName(const AActor* Actor)
	{
		const ULevel* Level = Actor ? Actor->GetLevel() : nullptr;
		if (!Level)
		{
			return TEXT("?");
		}
		return Level->IsPersistentLevel()
			? FString(TEXT("persistent"))
			: FPackageName::GetShortName(Level->GetOutermost()->GetName());
	}

	/**
	 * Ссылки на точки, потерянные при сохранении. Обычно это значит, что точка
	 * лежит в другом уровне, чем сам актор: жёсткие ссылки между sublevel'ами
	 * Unreal не сохраняет и молча обнуляет.
	 */
	static int32 CountBrokenPoints(const TArray<TObjectPtr<AActor>>& Points)
	{
		int32 Broken = 0;
		for (const TObjectPtr<AActor>& Point : Points)
		{
			if (!Point)
			{
				++Broken;
			}
		}
		return Broken;
	}

	/** Длина пути по навмешу; отрицательное значение — пути нет. */
	static float GetNavPathLength(UWorld* World, const FVector& From, const FVector& To)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return -1.f;
		}
		const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, From, To);
		if (!Path || !Path->IsValid() || Path->IsPartial())
		{
			return -1.f;
		}
		return Path->GetPathLength();
	}

	static bool IsOnNavMesh(UWorld* World, const FVector& Location)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return true; // без навигации проверять нечего — это отдельная поломка
		}
		FNavLocation Projected;
		return Nav->ProjectPointToNavigation(Location, Projected, FVector(150.f, 150.f, 300.f));
	}

	static FString FormatDistance(float Centimeters, float TurnBudget)
	{
		if (Centimeters < 0.f)
		{
			return TEXT("ПУТИ НЕТ");
		}
		return FString::Printf(TEXT("%.0f см (%.1f хода)"), Centimeters, Centimeters / TurnBudget);
	}

	/** Центр стартовой позиции отряда. */
	static bool GetSquadCenter(UWorld* World, FVector& OutCenter, int32& OutCount)
	{
		FVector Sum = FVector::ZeroVector;
		OutCount = 0;
		for (TActorIterator<AUnitBase> It(World); It; ++It)
		{
			if (ResolveTeamId(*It) != TacticsTeamIds::Player)
			{
				continue;
			}
			Sum += It->GetActorLocation();
			++OutCount;
		}
		if (OutCount == 0)
		{
			return false;
		}
		OutCenter = Sum / static_cast<float>(OutCount);
		return true;
	}

	static void ValidateObjectives(UWorld* World, const FVector& SquadCenter, float TurnBudget)
	{
		ABombObjective* Bomb = nullptr;
		for (TActorIterator<ABombObjective> It(World); It; ++It)
		{
			Bomb = *It;
			break;
		}
		AEvacZone* Evac = nullptr;
		for (TActorIterator<AEvacZone> It(World); It; ++It)
		{
			Evac = *It;
			break;
		}

		if (!Bomb)
		{
			UE_LOG(LogXRU1Scenario, Warning, TEXT("  Заряда на карте нет — таймер миссии не включится"));
		}
		else
		{
			const float ToBomb = GetNavPathLength(World, SquadCenter, Bomb->GetActorLocation());
			UE_LOG(LogXRU1Scenario, Display, TEXT("  Заряд %s: действий %d, радиус работы %.0f, старт → заряд %s"),
				*Bomb->GetName(), Bomb->RequiredActions, Bomb->InteractRadius,
				*FormatDistance(ToBomb, TurnBudget));
			if (ToBomb < 0.f)
			{
				UE_LOG(LogXRU1Scenario, Error,
					TEXT("  ⚠ До заряда НЕТ ПУТИ по навмешу — проверь объёмы навигации в конфигурации миссии"));
			}

			// Двое должны закрывать заряд за один ход: значит вокруг него нужно
			// минимум две проходимые точки в радиусе работы.
			int32 WorkSpots = 0;
			const float R = FMath::Max(50.f, Bomb->InteractRadius - 40.f);
			for (int32 i = 0; i < 8; ++i)
			{
				const float Angle = i * (2.f * PI / 8.f);
				const FVector Probe = Bomb->GetActorLocation() +
					FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R, 0.f);
				if (IsOnNavMesh(World, Probe))
				{
					++WorkSpots;
				}
			}
			if (WorkSpots >= 2)
			{
				UE_LOG(LogXRU1Scenario, Display,
					TEXT("  Рабочих мест у заряда (проходимых точек в радиусе): %d из 8"), WorkSpots);
			}
			else
			{
				UE_LOG(LogXRU1Scenario, Warning,
					TEXT("  Рабочих мест у заряда всего %d из 8 — двое за один ход не справятся"),
					WorkSpots);
			}
		}

		if (!Evac)
		{
			UE_LOG(LogXRU1Scenario, Warning, TEXT("  Зоны эвакуации нет — победа миссии недостижима"));
		}
		else if (Bomb)
		{
			const float BombToEvac = GetNavPathLength(World, Bomb->GetActorLocation(), Evac->GetActorLocation());
			UE_LOG(LogXRU1Scenario, Display,
				TEXT("  Зона эвакуации %s: активна с начала=%d, заряд → эвакуация %s"),
				*Evac->GetName(), Evac->bActiveFromStart ? 1 : 0,
				*FormatDistance(BombToEvac, TurnBudget));
		}
	}

	static void ValidateEnemies(UWorld* World, const FVector& SquadCenter, float TurnBudget)
	{
		TArray<AUnitBase*> Enemies;
		for (TActorIterator<AUnitBase> It(World); It; ++It)
		{
			if (ResolveTeamId(*It) == TacticsTeamIds::Enemy)
			{
				Enemies.Add(*It);
			}
		}

		if (Enemies.Num() == 0)
		{
			UE_LOG(LogXRU1Scenario, Display,
				TEXT("--- Врагов, расставленных вручную, нет (состав создают энкаунтеры) ---"));
			return;
		}

		UE_LOG(LogXRU1Scenario, Display, TEXT("--- Враги на карте: %d ---"), Enemies.Num());

		TMap<FName, int32> PodSizes;
		for (const AUnitBase* Enemy : Enemies)
		{
			const FName Pod = Enemy->PodId.IsNone() ? FName(TEXT("(одиночка)")) : Enemy->PodId;
			PodSizes.FindOrAdd(Pod)++;

			const float ToSquad = FVector::Dist2D(Enemy->GetActorLocation(), SquadCenter);
			// «Через сколько ходов первый контакт»: путь минус радиус обзора.
			// Отрицательная длина означает, что пути нет вовсе, — это не «ноль
			// ходов», а отдельная поломка навигации.
			const float PathToEnemy = GetNavPathLength(World, SquadCenter, Enemy->GetActorLocation());
			const FString ContactText = PathToEnemy < 0.f
				? FString(TEXT("ПУТИ НЕТ"))
				: FString::Printf(TEXT("%.1f хода"),
					FMath::Max(0.f, PathToEnemy - UTacticsCombatStatics::SquadVisionRange) / TurnBudget);

			int32 BadPatrolPoints = 0;
			float MinLeg = TNumericLimits<float>::Max();
			float MaxLeg = 0.f;
			float ClosingLeg = 0.f; // отрезок «последняя точка → первая»
			const int32 PointCount = Enemy->PatrolPoints.Num();
			for (int32 i = 0; i < PointCount; ++i)
			{
				const AActor* Point = Enemy->PatrolPoints[i];
				if (!Point || !IsOnNavMesh(World, Point->GetActorLocation()))
				{
					++BadPatrolPoints;
					continue;
				}
				const AActor* Next = Enemy->PatrolPoints[(i + 1) % PointCount];
				if (Next && Next != Point)
				{
					const float Leg = FVector::Dist2D(Point->GetActorLocation(), Next->GetActorLocation());
					if (i == PointCount - 1)
					{
						ClosingLeg = Leg; // считаем отдельно: он есть только у кольца
					}
					else
					{
						MinLeg = FMath::Min(MinLeg, Leg);
						MaxLeg = FMath::Max(MaxLeg, Leg);
					}
				}
			}

			// Режим мирного поведения читается из пары «точки + радиус» — это
			// первое, что нужно видеть в отчёте: «почему он стоит/бегает».
			FString Mode;
			if (Enemy->PatrolPoints.Num() >= 2)
			{
				Mode = FString::Printf(TEXT("маршрут %d точек, %s"), Enemy->PatrolPoints.Num(),
					Enemy->PatrolRouteMode == EPatrolRouteMode::PingPong
						? TEXT("туда-обратно") : TEXT("кольцо"));
			}
			else if (Enemy->PatrolRoamRadius > 0.f)
			{
				Mode = FString::Printf(TEXT("обход зоны R=%.0f"), Enemy->PatrolRoamRadius);
			}
			else
			{
				Mode = Enemy->PatrolPoints.Num() == 1 ? TEXT("удержание точки") : TEXT("пост");
			}

			UE_LOG(LogXRU1Scenario, Display,
				TEXT("  %-26s под=%-14s %-20s до отряда %.0f см, контакт через %s"),
				*Enemy->GetActorNameOrLabel(),
				Enemy->PodId.IsNone() ? TEXT("одиночка") : *Enemy->PodId.ToString(),
				*Mode, ToSquad, *ContactText);

			if (ToSquad < UTacticsCombatStatics::SquadVisionRange)
			{
				UE_LOG(LogXRU1Scenario, Warning,
					TEXT("    ⚠ стоит ближе радиуса обзора (%.0f) — бой начнётся на первом же ходу"),
					UTacticsCombatStatics::SquadVisionRange);
			}
			if (BadPatrolPoints > 0)
			{
				UE_LOG(LogXRU1Scenario, Error,
					TEXT("    ⚠ точек патруля вне навмеша: %d — боец будет заканчивать ход без действий"),
					BadPatrolPoints);
			}
			if (Enemy->PatrolPoints.Num() >= 2 && MaxLeg > 1500.f)
			{
				UE_LOG(LogXRU1Scenario, Warning,
					TEXT("    ⚠ самый длинный отрезок маршрута %.0f см (>1500): перегон занимает больше хода"),
					MaxLeg);
			}
			if (Enemy->PatrolPoints.Num() >= 2 && MinLeg < 700.f)
			{
				UE_LOG(LogXRU1Scenario, Warning,
					TEXT("    ⚠ самый короткий отрезок маршрута %.0f см (<700): боец топчется на месте"),
					MinLeg);
			}
			// Незамкнутый маршрут в режиме кольца: боец, дойдя до последней
			// точки, побежит к первой через всю линию — вхолостую и мимо того,
			// что он должен охранять.
			if (PointCount >= 3 && Enemy->PatrolRouteMode == EPatrolRouteMode::Loop &&
				ClosingLeg > FMath::Max(1500.f, MaxLeg * 1.75f))
			{
				UE_LOG(LogXRU1Scenario, Warning,
					TEXT("    ⚠ маршрут не замкнут: возврат с последней точки на первую %.0f см "
						"при самом длинном отрезке %.0f — поставь режим «Туда-обратно»"),
					ClosingLeg, MaxLeg);
			}
			if (PointCount == 2 && Enemy->PatrolRouteMode == EPatrolRouteMode::Loop)
			{
				UE_LOG(LogXRU1Scenario, Warning,
					TEXT("    ⚠ маршрут из двух точек читается как маятник — нужно три и больше"));
			}
		}

		UE_LOG(LogXRU1Scenario, Display, TEXT("--- Поды ---"));
		for (const TPair<FName, int32>& Pod : PodSizes)
		{
			UE_LOG(LogXRU1Scenario, Display, TEXT("  %-16s бойцов %d%s"),
				*Pod.Key.ToString(), Pod.Value,
				Pod.Value > 3 ? TEXT("  ⚠ больше трёх: ход врага станет долгим") : TEXT(""));
		}

		// Каскад: смерть бойца поднимает поды в DeathAlertRadius, шум выстрела —
		// в 2000. Разные группы, стоящие ближе, вскрываются вместе.
		const UTacticalAIDirectorSubsystem* Director = World->GetSubsystem<UTacticalAIDirectorSubsystem>();
		const float DeathRadius = Director ? Director->DeathAlertRadius : 2500.f;
		for (int32 i = 0; i < Enemies.Num(); ++i)
		{
			for (int32 j = i + 1; j < Enemies.Num(); ++j)
			{
				const FName PodA = Enemies[i]->PodId;
				const FName PodB = Enemies[j]->PodId;
				if (PodA.IsNone() || PodB.IsNone() || PodA == PodB)
				{
					continue;
				}
				const float Dist = FVector::Dist2D(
					Enemies[i]->GetActorLocation(), Enemies[j]->GetActorLocation());
				if (Dist < DeathRadius)
				{
					UE_LOG(LogXRU1Scenario, Warning,
						TEXT("  ⚠ поды '%s' и '%s' в %.0f см (< %.0f): смерть в одном вскроет второй"),
						*PodA.ToString(), *PodB.ToString(), Dist, DeathRadius);
				}
			}
		}
	}

	/** Общие для энкаунтера и маяка проверки: уровень, ссылки, конфликт полей. */
	static void ValidateGroupCommon(const ATacticalSpawnGroupBase* Group,
		const TArray<TObjectPtr<AActor>>& SpawnPoints,
		const TArray<TObjectPtr<AActor>>& PatrolPoints, float RoamRadius)
	{
		const FString LevelName = GetLevelName(Group);

		const int32 BrokenSpawn = CountBrokenPoints(SpawnPoints);
		const int32 BrokenPatrol = CountBrokenPoints(PatrolPoints);
		if (BrokenSpawn > 0 || BrokenPatrol > 0)
		{
			UE_LOG(LogXRU1Scenario, Error,
				TEXT("    ⚠ потеряны ссылки на точки (спавн %d, патруль %d). Актор лежит в «%s»; "
					"жёсткие ссылки между разными уровнями Unreal не сохраняет — перенеси точки "
					"и группу в ОДИН уровень"),
				BrokenSpawn, BrokenPatrol, *LevelName);
		}

		// Группа в persistent живёт во ВСЕХ сценариях сразу: она появится и в
		// обучении, которое грузит свой sublevel на той же карте.
		if (LevelName == TEXT("persistent"))
		{
			UE_LOG(LogXRU1Scenario, Warning,
				TEXT("    ⚠ актор лежит в persistent-уровне: группа будет создаваться в ЛЮБОМ "
					"сценарии этой карты, включая обучение. Перенеси в sublevel миссии"));
		}

		// Радиус обхода работает только там, где нет маршрута: с двумя и более
		// точками боец идёт по ним, и число в поле вводит в заблуждение.
		if (RoamRadius > 0.f && PatrolPoints.Num() >= 2)
		{
			UE_LOG(LogXRU1Scenario, Warning,
				TEXT("    ⚠ задан RoamRadius=%.0f, но точек маршрута %d — радиус не применяется "
					"(он для режимов «пост» и «удержание точки»)"),
				RoamRadius, PatrolPoints.Num());
		}
	}

	static void ValidateSpawners(UWorld* World)
	{
		int32 Encounters = 0;
		for (TActorIterator<ATacticalEncounter> It(World); It; ++It)
		{
			++Encounters;
			if (Encounters == 1)
			{
				UE_LOG(LogXRU1Scenario, Display, TEXT("--- Энкаунтеры ---"));
			}
			UE_LOG(LogXRU1Scenario, Display,
				TEXT("  %-20s включён=%d  под=%-12s  бойцов E/M/H = %d/%d/%d  точек спавна=%d  патруль=%d  R обхода=%.0f  сторожей=%d"),
				*It->EncounterId.ToString(), It->bEnabled ? 1 : 0,
				It->PodId.IsNone() ? TEXT("одиночки") : *It->PodId.ToString(),
				It->GetCountForDifficulty(EDifficultyLevel::Easy),
				It->GetCountForDifficulty(EDifficultyLevel::Medium),
				It->GetCountForDifficulty(EDifficultyLevel::Hard),
				It->SpawnPoints.Num(), It->PatrolPoints.Num(), It->RoamRadius, It->SentryCount);
			if (!It->EnemyClass)
			{
				UE_LOG(LogXRU1Scenario, Error, TEXT("    ⚠ не задан EnemyClass — группа не появится"));
			}
			if (It->SpawnPoints.Num() == 0)
			{
				UE_LOG(LogXRU1Scenario, Warning,
					TEXT("    ⚠ нет точек спавна: бойцы встанут вокруг самого актора"));
			}
			ValidateGroupCommon(*It, It->SpawnPoints, It->PatrolPoints, It->RoamRadius);
		}

		int32 Beacons = 0;
		for (TActorIterator<ATacticalReinforcementBeacon> It(World); It; ++It)
		{
			++Beacons;
			if (Beacons == 1)
			{
				UE_LOG(LogXRU1Scenario, Display, TEXT("--- Маяки подкреплений ---"));
			}
			UE_LOG(LogXRU1Scenario, Display,
				TEXT("  %-20s триггер=%s порог=%d  волн максимум=%d (+%d за волну)  отсчёт=%d ход(ов) врага  раньше хода %d — нет"),
				*It->BeaconId.ToString(),
				It->TriggerMode == EReinforcementTrigger::Manual ? TEXT("вручную") : TEXT("мало врагов"),
				It->EnemyCountThreshold, It->MaxWaves, It->WaveSizeGrowth,
				It->CountdownEnemyTurns, It->EarliestTurn);
			if (It->SpawnPoints.Num() == 0)
			{
				UE_LOG(LogXRU1Scenario, Warning,
					TEXT("    ⚠ нет точек высадки: волна появится вокруг маяка"));
			}
			if (!It->EnemyClass)
			{
				UE_LOG(LogXRU1Scenario, Error, TEXT("    ⚠ не задан EnemyClass — волна не появится"));
			}
			ValidateGroupCommon(*It, It->SpawnPoints, It->PatrolPoints, It->RoamRadius);
		}

		if (Beacons == 0)
		{
			UE_LOG(LogXRU1Scenario, Display,
				TEXT("--- Маяков подкрепления нет: волн в этой миссии не будет ---"));
		}
	}
}

static FAutoConsoleCommandWithWorld GMissionValidateCommand(
	TEXT("xru1.Mission.Validate"),
	TEXT("Проверить расстановку миссии: дистанции в ходах по навмешу, поды, патрули, цели, энкаунтеры и маяки."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (!World)
		{
			return;
		}

		const float TurnBudget = MissionValidator::GetTurnMoveBudget(World);
		UE_LOG(LogXRU1Scenario, Display, TEXT("═══ Проверка расстановки миссии ═══"));
		UE_LOG(LogXRU1Scenario, Display, TEXT("  Бюджет хода бегом: %.0f см (2 AP)"), TurnBudget);

		if (const ATacticsGameMode* GameMode = World->GetAuthGameMode<ATacticsGameMode>())
		{
			const EDifficultyLevel Difficulty = GameMode->GetActiveDifficulty();
			const FTacticsDifficultyParams* Params = GameMode->DifficultyParams.Find(Difficulty);
			UE_LOG(LogXRU1Scenario, Display,
				TEXT("  Сложность: %d, лимит ходов: %d, одновременно атакующих: %d"),
				static_cast<int32>(Difficulty), Params ? Params->TurnLimit : -1,
				Params ? Params->MaxAttackersPerTurn : -1);
		}

		FVector SquadCenter = FVector::ZeroVector;
		int32 SquadCount = 0;
		const bool bHasSquad = MissionValidator::GetSquadCenter(World, SquadCenter, SquadCount);
		if (bHasSquad)
		{
			UE_LOG(LogXRU1Scenario, Display, TEXT("  Отряд: %d бойцов, центр (%.0f, %.0f)"),
				SquadCount, SquadCenter.X, SquadCenter.Y);
		}
		else
		{
			// Дистанции без отряда посчитать не от чего, но проверка групп,
			// точек и целей имеет смысл и так.
			UE_LOG(LogXRU1Scenario, Warning,
				TEXT("  Бойцов отряда на карте не найдено — дистанции и ритм ходов не считаются"));
		}

		if (bHasSquad)
		{
			MissionValidator::ValidateObjectives(World, SquadCenter, TurnBudget);
			MissionValidator::ValidateEnemies(World, SquadCenter, TurnBudget);
		}
		MissionValidator::ValidateSpawners(World);
		UE_LOG(LogXRU1Scenario, Display, TEXT("═══ Проверка завершена ═══"));
	}));
