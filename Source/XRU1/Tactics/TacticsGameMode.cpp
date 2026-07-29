#include "TacticsGameMode.h"
#include "UnitBase.h"
#include "MissionObjectives.h"
#include "TacticalPlayerController.h"
#include "TacticalQuestEvents.h"
#include "TacticalScenarioDataAsset.h"
#include "TacticalScenarioDirector.h"
#include "TurnManagerSubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "TacticsCombatStatics.h"
#include "UnitAIController.h"
#include "TDAttributeSet.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "MissionResultWidget.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

ATacticsGameMode::ATacticsGameMode()
{
	// Дефолтные пресеты сложности по GDD §10 (правятся в BP).
	// Последнее поле — лимит одновременно атакующих врагов (A8), verbatim XCOM 2
	// `MaxEngagedEnemies`: Rookie 4 / Veteran 6 / Legend −1 (без лимита).
	DifficultyParams.Add(EDifficultyLevel::Easy,   {80.f,  55.f, 12,  4});
	DifficultyParams.Add(EDifficultyLevel::Medium, {100.f, 65.f, 10,  6});
	DifficultyParams.Add(EDifficultyLevel::Hard,   {120.f, 70.f,  8, -1});
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
		UE_LOG(LogTemp, Log, TEXT("[GameMode] Ожидаю готовности scenario sublevel"));
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(StartCombatTimerHandle, this,
		&ATacticsGameMode::StartMissionCombat, FMath::Max(0.05f, CombatStartDelay), false);
}

bool ATacticsGameMode::StartScenarioCombat()
{
	if (bCombatStarted)
	{
		return true;
	}

	GetWorldTimerManager().ClearTimer(StartCombatTimerHandle);
	StartMissionCombat();
	return bCombatStarted;
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
	return EDifficultyLevel::Medium; // прямой запуск карты в PIE без кампании
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

void ATacticsGameMode::StartMissionCombat()
{
	if (bCombatStarted)
	{
		return;
	}

	UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>();
	if (!TurnManager)
	{
		return;
	}

	// Эти actors могут жить в streamed sublevel, поэтому подписываемся только
	// на подтверждённой границе готовности сценария, а не в BeginPlay persistent.
	for (TActorIterator<ABombObjective> It(GetWorld()); It; ++It)
	{
		It->OnDisarmed.AddUniqueDynamic(this, &ATacticsGameMode::HandleBombDisarmed);
	}
	for (TActorIterator<AEvacZone> It(GetWorld()); It; ++It)
	{
		It->OnUnitEvacuated.AddUniqueDynamic(this, &ATacticsGameMode::HandleUnitEvacuated);
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
		UE_LOG(LogTemp, Warning, TEXT("[GameMode] У %s нет FTacticsDifficultyParams для сложности %d — ")
			TEXT("враги останутся с дефолтными статами BP, а таймер бомбы (если она есть на карте) не включится"),
			*GetNameSafe(this), static_cast<int32>(Difficulty));
	}

	// Сбор сторон по каноническим TacticsTeamIds.
	PlayerUnits.Reset();
	TArray<AActor*> Players;
	TArray<AActor*> Enemies;
	for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
	{
		AUnitBase* Unit = *It;
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
				UE_LOG(LogTemp, Error,
					TEXT("[CombatStart] Enemy %s has no UnitAIController (Controller=%s). Its turn will be skipped."),
					*GetNameSafe(Unit), *GetNameSafe(EnemyPawn ? EnemyPawn->GetController() : nullptr));
			}

			if (AppliedParams)
			{
				ApplyDifficultyToEnemy(Unit, *AppliedParams);
			}
			Enemies.Add(Unit);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CombatStart] Tactical unit %s ignored: TeamId=%u (expected Player=%u or Enemy=%u)."),
				*GetNameSafe(Unit), TeamId, TacticsTeamIds::Player, TacticsTeamIds::Enemy);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[CombatStart] Registered players=%d enemies=%d"),
		Players.Num(), Enemies.Num());

	// Таймер бомбы включаем только там, где есть заряд.
	bool bHasBomb = false;
	for (TActorIterator<ABombObjective> It(GetWorld()); It; ++It)
	{
		bHasBomb = true;
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
	TurnManager->bAutoWinWhenEnemiesDead = bWinWhenAllEnemiesDead && !bHasBomb;

	bCombatStarted = true;
	TurnManager->StartCombat(Players, Enemies);

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
	bool bAnyEvacuated = false;
	for (const TObjectPtr<AUnitBase>& Unit : PlayerUnits)
	{
		if (!Unit)
		{
			continue;
		}
		if (Unit->IsEvacuated())
		{
			bAnyEvacuated = true;
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
			UE_LOG(LogTemp, Error, TEXT("[GameMode] Для run %d найдено ScenarioDirector: %d; "
				"save/result остановлены"), GameInstance->GetActiveScenarioRunId(), DirectorCount);
			return;
		}
		if (!CurrentDirector->FinalizeConfiguredScenario(bPlayerWon))
		{
			UE_LOG(LogTemp, Error, TEXT("[GameMode] ScenarioDirector отклонил terminal request; "
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
		UE_LOG(LogTemp, Error, TEXT("[GameMode] ScenarioDirector потерян до terminal confirmation"));
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
		UE_LOG(LogTemp, Error, TEXT("[GameMode] Quest не подтвердил terminal state за 3 секунды; "
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
