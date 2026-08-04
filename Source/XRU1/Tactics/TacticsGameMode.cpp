#include "TacticsGameMode.h"
#include "XRU1Log.h"
#include "UnitBase.h"
#include "FogGridSubsystem.h"   // визуальный слой тумана: запекание сетки на старте боя
#include "FogOfWarSubsystem.h" // новая fog-сессия на каждый запуск сценария
#include "MissionObjectives.h"
#include "ObjectivePointerSubsystem.h" // указатель «куда идти» на цели миссии
#include "ScenarioActorRegistry.h"
#include "TacticalPlayerController.h"
#include "TacticalQuestEvents.h"
#include "TacticalScenarioDataAsset.h"
#include "TacticalScenarioDirector.h"
#include "TacticsAudioSubsystem.h"
#include "TurnManagerSubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "TacticsCombatStatics.h"
#include "AIBehaviorProfileDataAsset.h"
#include "MissionVoiceDirector.h" // реплики боя ведёт директор, а не StateTree
#include "TacticalEncounter.h" // стартовые группы врагов, описанные данными
#include "UnitAIController.h"
#include "TDAttributeSet.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "MissionResultWidget.h"
#include "AbilitySystemComponent.h"
#include "NavigationSystem.h" // старт боя ждёт готовности навмеша (NavigationReadyTimeout)
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

ATacticsGameMode::ATacticsGameMode()
{
	// Дефолтные пресеты сложности по GDD §10 (правятся в BP).
	// Последнее поле — лимит одновременно атакующих врагов (A8), verbatim XCOM 2
	// `MaxEngagedEnemies`: Rookie 4 / Veteran 6 / Legend −1 (без лимита).
	//
	// Лимит ходов пересчитан 2026-08-03 под реальный масштаб двора: дорога до
	// заряда занимает порядка десяти ходов, поэтому прежние 12/10/8 делали
	// миссию непроходимой ещё до первого выстрела. Числа держат ту же разницу
	// между уровнями (запас ~25 % / ~12 % / 0 к «чистому» проходу).
	DifficultyParams.Add(EDifficultyLevel::Easy,   {80.f,  55.f, 31,  4});
	DifficultyParams.Add(EDifficultyLevel::Medium, {100.f, 65.f, 28,  6});
	DifficultyParams.Add(EDifficultyLevel::Hard,   {120.f, 70.f, 25, -1});
}

void ATacticsGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
	{
		TurnManager->OnCombatEnded.AddUniqueDynamic(this, &ATacticsGameMode::HandleCombatEnded);
		TurnManager->OnTurnLimitExpired.AddUniqueDynamic(this, &ATacticsGameMode::HandleTurnLimitExpired);
	}

	// Scenario-run стартует только из Director после OnLevelShown: в BeginPlay
	// scenario actors ещё могут отсутствовать. Прямой PIE старых карт сохраняет
	// прежний delayed-start без обязательного Scenario Data Asset.
	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	if (GameInstance && GameInstance->GetActiveScenario())
	{
		UE_LOG(LogXRU1Scenario, Log, TEXT("[GameMode] Ожидаю готовности scenario sublevel"));
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(StartCombatTimerHandle, this,
		&ATacticsGameMode::StartMissionCombat, FMath::Max(0.05f, CombatStartDelay), false);
}

bool ATacticsGameMode::IsNavigationReadyForCombat()
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSys)
	{
		// Навигации на карте нет вовсе — ждать нечего. Отсутствие навмеша это
		// отдельный дефект уровня, и превращать его в вечную загрузку нельзя.
		return true;
	}
	if (NavSys->IsNavigationBuildInProgress())
	{
		return false;
	}
	// Тайлы достроены, но инстанс NavData мог ещё не зарегистрироваться — именно
	// на этом спотыкается и Detour Crowd («Unable to find RecastNavMesh instance
	// while trying to create UCrowdManager instance»).
	return NavSys->GetDefaultNavDataInstance() != nullptr;
}

bool ATacticsGameMode::IsCombatStartPending() const
{
	return !bCombatStarted && GetWorldTimerManager().IsTimerActive(StartCombatTimerHandle);
}

bool ATacticsGameMode::StartScenarioCombat()
{
	if (bCombatStarted)
	{
		return true;
	}

	GetWorldTimerManager().ClearTimer(StartCombatTimerHandle);
	StartMissionCombat();

	// ⚠️ ОЖИДАНИЕ НАВИГАЦИИ — НЕ ОТКАЗ, и вернуть здесь false нельзя.
	//
	// `ATacticalScenarioDirector` трактует false как «GameMode не смог стартовать
	// бой», пишет Error и отменяет запуск сценария целиком. То есть попытка
	// честно сказать «ещё не готов» убила бы миссию вместо того, чтобы её
	// подождать. Отложенный старт — это принятое обязательство: таймер взведён,
	// бой начнётся сам.
	return bCombatStarted || IsCombatStartPending();
}

EDifficultyLevel ATacticsGameMode::ResolveDifficulty() const
{
	if (const UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>())
	{
		if (GI->CurrentSave)
		{
			return GI->CurrentSave->Difficulty;
		}
	}
	// Прямой запуск карты без кампании: сложность берётся из настройки боевого
	// стенда, чтобы PIE воспроизводил тот режим, на котором миссия собрана.
	return DifficultyWithoutCampaign;
}

void ATacticsGameMode::ApplyDifficultyToEnemy(AUnitBase* Enemy, const FTacticsDifficultyParams& Params)
{
	if (!Enemy)
	{
		return;
	}
	if (Params.EnemyAim > 0.f)
	{
		Enemy->BaseAim = Params.EnemyAim;
	}
	if (Params.EnemyHealth > 0.f)
	{
		if (UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent())
		{
			ASC->ApplyModToAttribute(UTDAttributeSet::GetMaxHealthAttribute(),
				EGameplayModOp::Override, Params.EnemyHealth);
			ASC->ApplyModToAttribute(UTDAttributeSet::GetHealthAttribute(),
				EGameplayModOp::Override, Params.EnemyHealth);
		}
	}
}

void ATacticsGameMode::ApplyBehaviorProfileToEnemy(AUnitBase* Enemy, EDifficultyLevel Difficulty) const
{
	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	if (!Enemy || !GameInstance)
	{
		return;
	}
	const TObjectPtr<UAIBehaviorProfileDataAsset>* Profile =
		GameInstance->AIProfilesByDifficulty.Find(Difficulty);
	if (!Profile)
	{
		return; // профили сложности ещё не заведены — остаётся baseline BP-контроллера
	}

	APawn* Pawn = Cast<APawn>(Enemy);
	if (AUnitAIController* AI = Pawn ? Cast<AUnitAIController>(Pawn->GetController()) : nullptr)
	{
		AI->SetBehaviorProfile(*Profile);
	}
}

void ATacticsGameMode::ApplySpawnedEnemyDefaults(AUnitBase* Enemy)
{
	if (!Enemy)
	{
		return;
	}
	const EDifficultyLevel Difficulty = ResolveDifficulty();
	if (const FTacticsDifficultyParams* Params = DifficultyParams.Find(Difficulty))
	{
		ApplyDifficultyToEnemy(Enemy, *Params);
	}
	ApplyBehaviorProfileToEnemy(Enemy, Difficulty);
}

int32 ATacticsGameMode::SpawnConfiguredEncounters(EDifficultyLevel Difficulty)
{
	int32 Total = 0;
	int32 Groups = 0;
	for (TActorIterator<ATacticalEncounter> It(GetWorld()); It; ++It)
	{
		// Группа выключенного сценария не должна ожить вместе с чужим запуском:
		// энкаунтеры лежат в scenario sublevel, но общий persistent виден всегда.
		if (!UTacticalScenarioSubsystem::IsActorScenarioActive(*It))
		{
			continue;
		}
		const int32 Created = It->SpawnForDifficulty(Difficulty);
		if (Created > 0)
		{
			++Groups;
			Total += Created;
		}
	}
	if (Groups > 0)
	{
		UE_LOG(LogXRU1Scenario, Log,
			TEXT("[CombatStart] Энкаунтеры: групп %d, бойцов %d (сложность %d)"),
			Groups, Total, static_cast<int32>(Difficulty));
	}
	return Total;
}

void ATacticsGameMode::StartMissionCombat()
{
	if (bCombatStarted)
	{
		return;
	}

	UWorld* World = GetWorld();
	UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return;
	}

	// НАВИГАЦИЯ ДОЛЖНА БЫТЬ ГОТОВА ДО РАССТАНОВКИ ГРУПП (см. NavigationReadyTimeout).
	// Проверка стоит здесь, а не в энкаунтере: ждать должен ОДИН распорядитель
	// старта, иначе каждая группа заводила бы свой таймер и порядок расстановки
	// поехал бы от прогона к прогону.
	if (NavigationReadyTimeout > 0.f)
	{
		if (CombatStartRequestedTime < 0.f)
		{
			CombatStartRequestedTime = World->GetTimeSeconds();
		}
		const float Waited = World->GetTimeSeconds() - CombatStartRequestedTime;

		if (!IsNavigationReadyForCombat())
		{
			if (Waited < NavigationReadyTimeout)
			{
				if (Waited <= 0.f)
				{
					UE_LOG(LogXRU1Scenario, Log,
						TEXT("[GameMode] Ожидаю готовности навигации (не более %.0f с)"),
						NavigationReadyTimeout);
				}
				World->GetTimerManager().SetTimer(StartCombatTimerHandle, this,
					&ATacticsGameMode::StartMissionCombat, 0.1f, false);
				return;
			}

			// Таймаут. Стартуем как есть — иначе миссия не начнётся вообще, — но
			// говорим прямо, чем это грозит: группы встанут фолбэком со смещением
			// по индексу вместо заданных точек.
			UE_LOG(LogXRU1Scenario, Warning,
				TEXT("[GameMode] Навигация не готова за %.1f с — стартую бой как есть. "
					 "Ожидаемо: [Encounter] ... без подтверждения навмешем и группы "
					 "не на своих точках. Проверь навмеш карты и bForceRebuildOnLoad"),
				Waited);
		}
		else if (Waited > 0.05f)
		{
			UE_LOG(LogXRU1Scenario, Log,
				TEXT("[GameMode] Навигация готова через %.2f с — стартую бой"), Waited);
		}
	}

	// Эти actors могут жить в streamed sublevel, поэтому подписываемся только
	// на подтверждённой границе готовности сценария, а не в BeginPlay persistent.
	// Указатели на цели миссии живут ровно один запуск: retry и переход
	// Tutorial → Mission01 не должны наследовать стрелку на чужую бомбу.
	UObjectivePointerSubsystem* Pointers = GetWorld()->GetSubsystem<UObjectivePointerSubsystem>();
	if (Pointers)
	{
		Pointers->ClearAllObjectives();
	}

	for (TActorIterator<ABombObjective> It(GetWorld()); It; ++It)
	{
		It->OnDisarmed.AddUniqueDynamic(this, &ATacticsGameMode::HandleBombDisarmed);
		// Куда идти — задача UI, а не тумана: цель не прячется никогда (модель
		// XCOM 2, docs/10_FOG_OF_WAR.md §2.6). Счётчик у стрелки показывает
		// остаток ходов — аналог `CounterValue` у `XComGameState_IndicatorArrow`.
		if (Pointers)
		{
			Pointers->RegisterObjective(TEXT("Objective.Bomb"), *It,
				NSLOCTEXT("XRU1.Objective", "PointerBomb", "ЗАРЯД"),
				EObjectivePointerRule::WhileBombArmed, EObjectivePointerTone::Urgent,
				/*bShowTurnsRemaining=*/true);
		}
	}
	for (TActorIterator<AEvacZone> It(GetWorld()); It; ++It)
	{
		It->OnUnitEvacuated.AddUniqueDynamic(this, &ATacticsGameMode::HandleUnitEvacuated);
		// Правило показа — «зона включена», поэтому регистрировать можно сразу:
		// пока туториал её не активировал, указатель молчит сам.
		if (Pointers)
		{
			Pointers->RegisterObjective(TEXT("Objective.Evac"), *It,
				NSLOCTEXT("XRU1.Objective", "PointerEvac", "ЭВАКУАЦИЯ"),
				EObjectivePointerRule::WhileEvacActive, EObjectivePointerTone::Normal);
		}
	}

	const EDifficultyLevel Difficulty = ResolveDifficulty();
	const FTacticsDifficultyParams* Params = DifficultyParams.Find(Difficulty);
	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	const UTacticalScenarioDataAsset* Scenario = GameInstance
		? GameInstance->GetActiveScenario() : nullptr;
	const bool bApplyMissionDifficulty = !Scenario ||
		Scenario->Kind == ETacticalScenarioKind::Mission;
	const FTacticsDifficultyParams* AppliedParams = bApplyMissionDifficulty ? Params : nullptr;
	if (Scenario)
	{
		MissionId = Scenario->ScenarioId;
	}
	if (!Params)
	{
		UE_LOG(LogXRU1Scenario, Warning, TEXT("[GameMode] У %s нет FTacticsDifficultyParams для сложности %d — ")
			TEXT("враги останутся с дефолтными статами BP, а таймер бомбы (если она есть на карте) не включится"),
			*GetNameSafe(this), static_cast<int32>(Difficulty));
	}

	// Группы-энкаунтеры создаются ДО сбора сторон: их бойцы должны попасть в бой
	// обычным путём, наравне с расставленными руками. Отсев по сложности — это
	// «не создать лишнего», а не «удалить актора посреди первого хода»
	// (правило 11 §6.1).
	SpawnConfiguredEncounters(Difficulty);

	// Сбор сторон по каноническим TacticsTeamIds.
	PlayerUnits.Reset();
	TArray<AActor*> Players;
	TArray<AActor*> Enemies;
	for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
	{
		AUnitBase* Unit = *It;
		// Staged-голограмма шага C/D физически стоит на карте с начала боя, но в
		// стороны попадает только когда её включит StateTree: иначе она ходила бы
		// и считалась живым врагом всё обучение.
		if (!UTacticalScenarioSubsystem::IsActorScenarioActive(Unit))
		{
			continue;
		}
		const uint8 TeamId = Unit->GetGenericTeamId().GetId();
		if (TeamId == TacticsTeamIds::Player)
		{
			Players.Add(Unit);
			PlayerUnits.Add(Unit);
		}
		else if (TeamId == TacticsTeamIds::Enemy)
		{
			// Размещённый экземпляр врага может сохранить старое значение Auto Possess AI
			// после смены класса контроллера в BP. Не кладём такого юнита в очередь молча:
			// SpawnDefaultController использует настроенный на юните класс, в том числе
			// BP_AIController_Marauder.
			APawn* EnemyPawn = Cast<APawn>(Unit);
			if (EnemyPawn && !EnemyPawn->GetController())
			{
				EnemyPawn->SpawnDefaultController();
			}

			const AUnitAIController* EnemyAI = EnemyPawn
				? Cast<AUnitAIController>(EnemyPawn->GetController()) : nullptr;
			if (!EnemyAI)
			{
				UE_LOG(LogXRU1Scenario, Error,
					TEXT("[CombatStart] Enemy %s has no UnitAIController (Controller=%s). Its turn will be skipped."),
					*GetNameSafe(Unit), *GetNameSafe(EnemyPawn ? EnemyPawn->GetController() : nullptr));
			}

			if (AppliedParams)
			{
				ApplyDifficultyToEnemy(Unit, *AppliedParams);
			}

			// Стиль поведения — вторая половина сложности. Профиль назначается
			// ЗДЕСЬ, а не в BP каждого врага: иначе один забытый экземпляр играет
			// по чужим правилам, и разница уровней перестаёт быть honest.
			// Та же функция зовётся для врагов, созданных энкаунтером и
			// подкреплением, — правило ровно одно на всех.
			ApplyBehaviorProfileToEnemy(Unit, Difficulty);
			Enemies.Add(Unit);
		}
		else
		{
			UE_LOG(LogXRU1Scenario, Warning,
				TEXT("[CombatStart] Tactical unit %s ignored: TeamId=%u (expected Player=%u or Enemy=%u)."),
				*GetNameSafe(Unit), TeamId, TacticsTeamIds::Player, TacticsTeamIds::Enemy);
		}
	}
	UE_LOG(LogXRU1Scenario, Log, TEXT("[CombatStart] Registered players=%d enemies=%d"),
		Players.Num(), Enemies.Num());

	// Таймер бомбы включаем только там, где есть заряд.
	bool bHasBomb = false;
	for (TActorIterator<ABombObjective> It(GetWorld()); It; ++It)
	{
		bHasBomb = true;
		break;
	}
	bool bHasEvacZone = false;
	for (TActorIterator<AEvacZone> It(GetWorld()); It; ++It)
	{
		bHasEvacZone = true;
		break;
	}
	int32 ResolvedTurnLimit = bHasBomb && AppliedParams ? AppliedParams->TurnLimit : 0;
	if (Scenario && Scenario->TurnLimit >= 0)
	{
		ResolvedTurnLimit = Scenario->TurnLimit;
	}
	TurnManager->SetTurnLimit(ResolvedTurnLimit);
	// A8: лимит одновременно атакующих врагов — из того же пресета сложности.
	TurnManager->SetMaxAttackersPerTurn(AppliedParams ? AppliedParams->MaxAttackersPerTurn : -1);
	// Победа зачисткой запрещена везде, где есть авторская цель. В туториале
	// бомбы нет, но после D2 все голограммы мертвы, и без учёта evac-зоны бой
	// закончился бы победой ДО шага D3 «эвакуировать отряд».
	TurnManager->bAutoWinWhenEnemiesDead = bWinWhenAllEnemiesDead && !bHasBomb && !bHasEvacZone;

	// Туман получает новую сессию ДО первого хода: `Showreel_Scene` общая для
	// Tutorial и Mission01, поэтому идентификатор сессии — пара
	// `ScenarioId + RunId`, а не имя загруженной карты. Первый полный пересчёт
	// проходит здесь же, чтобы бой не начинался с кадра, где видна вся
	// расстановка врагов.
	const FName FogScenarioId = Scenario ? Scenario->ScenarioId : NAME_None;
	const int32 FogRunId = GameInstance ? GameInstance->GetActiveScenarioRunId() : 0;
	if (UFogOfWarSubsystem* Fog = GetWorld()->GetSubsystem<UFogOfWarSubsystem>())
	{
		Fog->ResetForScenario(FogScenarioId, FogRunId);
	}

	// Визуальный слой сбрасывается тем же событием и той же парой ключей: сетка —
	// отражение правил, и пережить `ScenarioRunId` её `Explored` не имеет права.
	// Запекание блокеров идёт здесь, потому что scenario sublevel (а с ним и
	// объёмы навигации, задающие границы) к этому моменту уже загружен.
	if (UFogGridSubsystem* FogGrid = GetWorld()->GetSubsystem<UFogGridSubsystem>())
	{
		FogGrid->ResetForScenario(FogScenarioId, FogRunId,
			Scenario ? Scenario->bStartFullyExplored : false);
	}

	// Реплики боя ведёт отдельный директор по таблице сценария: StateTree
	// отвечает за ЦЕЛИ, а реакции на события — за ним (16 §6.2).
	if (UMissionVoiceDirectorSubsystem* Voice =
		GetWorld()->GetSubsystem<UMissionVoiceDirectorSubsystem>())
	{
		Voice->StartMission(Scenario ? Scenario->VoiceLines.LoadSynchronous() : nullptr);
	}

	bCombatStarted = true;
	TurnManager->StartCombat(Players, Enemies);

	// Боевая музыка стартует с подтверждённым боем, а не с загрузкой уровня:
	// в туториале между стримом сублевела и первым ходом проходит заметная пауза.
	if (UTacticsAudioSubsystem* Audio = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
	{
		Audio->PlayCombatMusic();
	}

	// HUD пушим после старта (лейаут игрока уже создан его контроллером).
	if (TacticalHUDClass)
	{
		if (UGameUIManagerSubsystem* UIManager = GetGameInstance()->GetSubsystem<UGameUIManagerSubsystem>())
		{
			if (UPrimaryGameLayout* RootLayout = UIManager->GetRootLayout())
			{
				RootLayout->PushWidgetToLayer(EUILayer::Game, TacticalHUDClass);
			}
		}
	}
}

void ATacticsGameMode::ActivateEvacuation()
{
	for (TActorIterator<AEvacZone> It(GetWorld()); It; ++It)
	{
		It->ActivateZone();
	}

	// Зона открылась прямо сейчас: у бойца, уже стоящего в ней, кнопка F должна
	// ожить немедленно, а не после следующего события боя.
	if (ATacticalPlayerController* PlayerController =
		Cast<ATacticalPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PlayerController->NotifyAvailableActionsChanged();
	}
}

void ATacticsGameMode::HandleBombDisarmed()
{
	// Заряд снят: таймер больше не тикает, отряд уходит на эвакуацию.
	if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
	{
		TurnManager->SetTurnLimit(0);
	}
	ActivateEvacuation();
}

void ATacticsGameMode::HandleUnitEvacuated(AUnitBase* /*Unit*/)
{
	if (AreAllLivingPlayersEvacuated() && !bSquadEvacuationPending)
	{
		bSquadEvacuationPending = true;
		SquadEvacuationTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
			this, &ATacticsGameMode::CompleteSquadEvacuation);
	}
}

void ATacticsGameMode::CompleteSquadEvacuation()
{
	bSquadEvacuationPending = false;
	if (!AreAllLivingPlayersEvacuated())
	{
		return;
	}

	UTacticalQuestEvents::BroadcastQuestEvent(
		this, TacticalQuestTags::Event_Tactical_Objective_Evac_Squad, this);
	if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
	{
		TurnManager->EndCombat(true);
	}
}

bool ATacticsGameMode::AreAllLivingPlayersEvacuated() const
{
	// Источник состава — АКТУАЛЬНАЯ сторона игрока TurnManager, а не стартовый
	// PlayerUnits: staged-бойцы туториола (Танк/Оса/Кадет) регистрируются в бою
	// уже ПОСЛЕ старта сценария и в стартовый список не попадают — правило по
	// нему никогда не видело ни одного эвакуированного (прогон 2026-08-01).
	const UTurnManagerSubsystem* TurnManager = GetWorld()
		? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return false;
	}

	bool bAnyEvacuated = false;
	for (AActor* Actor : TurnManager->GetPlayerSideUnits())
	{
		const AUnitBase* Unit = Cast<AUnitBase>(Actor);
		if (!Unit)
		{
			continue;
		}
		// Эвакуация проверяется ПЕРВОЙ: ушедший боец скрыт, и любые последующие
		// фильтры «активности» не должны отнимать у него зачёт.
		if (Unit->IsEvacuated())
		{
			bAnyEvacuated = true;
			continue;
		}
		// Боец, снятый со сцены сценарием, в эвакуации не участвует (страховка:
		// деактивация и так дерегистрирует из стороны).
		if (!UTacticalScenarioSubsystem::IsActorScenarioActive(Unit))
		{
			continue;
		}
		// Живой (не Downed) юнит ещё на карте — эвакуация не закончена.
		if (UTacticsCombatStatics::IsUnitAlive(Unit))
		{
			return false;
		}
	}
	// Downed/мёртвые остаются на карте потерями, но победу не блокируют (GDD §5.7).
	return bAnyEvacuated;
}

void ATacticsGameMode::HandleTurnLimitExpired()
{
	bDefeatByTimeout = true;
}

void ATacticsGameMode::HandleCombatEnded(bool bPlayerWon)
{
	// Стингер — на границе исхода боя, до всей result-машинерии: игрок слышит
	// точку раньше, чем откроется экран, и это правильный порядок.
	if (UTacticsAudioSubsystem* Audio = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
	{
		Audio->PlayOutcomeStinger(bPlayerWon);
	}

	// Scenario run обязан иметь ровно один Director текущего поколения. Legacy
	// direct PIE без ActiveScenario сохраняет прежний синхронный result path.
	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	if (GameInstance && GameInstance->GetActiveScenario())
	{
		ATacticalScenarioDirector* CurrentDirector = nullptr;
		int32 DirectorCount = 0;
		for (TActorIterator<ATacticalScenarioDirector> It(GetWorld()); It; ++It)
		{
			if (It->IsCurrentScenarioRun())
			{
				CurrentDirector = *It;
				++DirectorCount;
			}
		}

		if (DirectorCount != 1 || !CurrentDirector)
		{
			UE_LOG(LogXRU1Scenario, Error, TEXT("[GameMode] Для run %d найдено ScenarioDirector: %d; "
				"save/result остановлены"), GameInstance->GetActiveScenarioRunId(), DirectorCount);
			return;
		}
		if (!CurrentDirector->FinalizeConfiguredScenario(bPlayerWon))
		{
			UE_LOG(LogXRU1Scenario, Error, TEXT("[GameMode] ScenarioDirector отклонил terminal request; "
				"save/result остановлены"));
			return;
		}

		bCombatResultPending = true;
		bPendingPlayerWon = bPlayerWon;
		PendingScenarioDirector = CurrentDirector;
		ScenarioFinalizationDeadline = GetWorld()->GetTimeSeconds() + 3.0;
		ScenarioFinalizationTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
			this, &ATacticsGameMode::PollScenarioFinalization);
		return;
	}

	CompleteCombatResult(bPlayerWon);
}

void ATacticsGameMode::PollScenarioFinalization()
{
	if (!bCombatResultPending)
	{
		return;
	}

	ATacticalScenarioDirector* Director = PendingScenarioDirector.Get();
	if (!Director || !Director->IsCurrentScenarioRun())
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[GameMode] ScenarioDirector потерян до terminal confirmation"));
		bCombatResultPending = false;
		return;
	}

	if (Director->IsScenarioFinalized())
	{
		const bool bPlayerWon = bPendingPlayerWon;
		bCombatResultPending = false;
		PendingScenarioDirector.Reset();
		CompleteCombatResult(bPlayerWon);
		return;
	}

	if (GetWorld()->GetTimeSeconds() >= ScenarioFinalizationDeadline)
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[GameMode] Quest не подтвердил terminal state за 3 секунды; "
			"save/result остановлены"));
		bCombatResultPending = false;
		return;
	}

	ScenarioFinalizationTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
		this, &ATacticsGameMode::PollScenarioFinalization);
}

void ATacticsGameMode::CompleteCombatResult(bool bPlayerWon)
{

	// Прогресс кампании: победа записывает миссию в сейв.
	if (bPlayerWon && MissionId != NAME_None)
	{
		if (UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>())
		{
			if (GI->CurrentSave)
			{
				GI->CurrentSave->CompletedMissions.AddUnique(MissionId);
				GI->SaveCampaign();
			}
		}
	}

	// Экран результата.
	if (MissionResultWidgetClass)
	{
		if (UGameUIManagerSubsystem* UIManager = GetGameInstance()->GetSubsystem<UGameUIManagerSubsystem>())
		{
			if (UPrimaryGameLayout* RootLayout = UIManager->GetRootLayout())
			{
				if (UMissionResultWidget* Result = Cast<UMissionResultWidget>(
					RootLayout->PushWidgetToLayer(EUILayer::Menu, MissionResultWidgetClass)))
				{
					Result->SetupResult(bPlayerWon, bDefeatByTimeout);
				}
			}
		}
	}
}
