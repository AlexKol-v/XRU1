#include "TacticsGameMode.h"
#include "UnitBase.h"
#include "MissionObjectives.h"
#include "TurnManagerSubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "TacticsCombatStatics.h"
#include "TDAttributeSet.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "MissionResultWidget.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATacticsGameMode::ATacticsGameMode()
{
	// Дефолтные пресеты сложности по GDD §10 (правятся в BP).
	DifficultyParams.Add(EDifficultyLevel::Easy,   {80.f, 55.f, 12});
	DifficultyParams.Add(EDifficultyLevel::Medium, {100.f, 65.f, 10});
	DifficultyParams.Add(EDifficultyLevel::Hard,   {120.f, 70.f, 8});
}

void ATacticsGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Цели миссии: бомбы и зоны эвакуации подписываем до старта боя.
	for (TActorIterator<ABombObjective> It(GetWorld()); It; ++It)
	{
		It->OnDisarmed.AddDynamic(this, &ATacticsGameMode::HandleBombDisarmed);
	}
	for (TActorIterator<AEvacZone> It(GetWorld()); It; ++It)
	{
		It->OnUnitEvacuated.AddDynamic(this, &ATacticsGameMode::HandleUnitEvacuated);
	}

	if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
	{
		TurnManager->OnCombatEnded.AddDynamic(this, &ATacticsGameMode::HandleCombatEnded);
		TurnManager->OnTurnLimitExpired.AddDynamic(this, &ATacticsGameMode::HandleTurnLimitExpired);
	}

	// Боевой HUD — на слой Game корневого лейаута (лейаут создаёт контроллер).
	// Отложенный старт: даём юнитам/навмешу закончить BeginPlay.
	GetWorld()->GetTimerManager().SetTimer(StartCombatTimerHandle, this,
		&ATacticsGameMode::StartMissionCombat, FMath::Max(0.05f, CombatStartDelay), false);
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
	UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>();
	if (!TurnManager)
	{
		return;
	}

	const EDifficultyLevel Difficulty = ResolveDifficulty();
	const FTacticsDifficultyParams* Params = DifficultyParams.Find(Difficulty);

	// Сбор сторон по TeamId: 1 — отряд игрока, 2 — враги.
	PlayerUnits.Reset();
	TArray<AActor*> Players;
	TArray<AActor*> Enemies;
	for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
	{
		AUnitBase* Unit = *It;
		const uint8 TeamId = Unit->GetGenericTeamId().GetId();
		if (TeamId == 1)
		{
			Players.Add(Unit);
			PlayerUnits.Add(Unit);
		}
		else if (TeamId == 2)
		{
			if (Params)
			{
				ApplyDifficultyToEnemy(Unit, *Params);
			}
			Enemies.Add(Unit);
		}
	}

	// Таймер бомбы включаем только там, где есть заряд.
	bool bHasBomb = false;
	for (TActorIterator<ABombObjective> It(GetWorld()); It; ++It)
	{
		bHasBomb = true;
		break;
	}
	TurnManager->SetTurnLimit(bHasBomb && Params ? Params->TurnLimit : 0);
	TurnManager->bAutoWinWhenEnemiesDead = bWinWhenAllEnemiesDead && !bHasBomb;

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
	if (AreAllLivingPlayersEvacuated())
	{
		if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->EndCombat(true);
		}
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
