#include "TurnManagerSubsystem.h"
#include "XRU1Log.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "TacticsCombatStatics.h"
#include "FogOfWarSubsystem.h" // состав сторон и фаза меняют видимость отряда
#include "TacticalAIDirectorSubsystem.h"
#include "TacticsAudioSubsystem.h"
#include "UnitVfxDataAsset.h" // прогрев эффектов боя до первого выстрела
#include "TacticalQuestEvents.h"
#include "UnitAIController.h"
#include "UnitBase.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UTurnManagerSubsystem::StartCombat(const TArray<AActor*>& PlayerUnits, const TArray<AActor*>& EnemyUnits)
{
	PlayerSide.Reset();
	for (AActor* A : PlayerUnits)
	{
		if (A) { PlayerSide.Add(A); }
	}

	EnemySide.Reset();
	for (AActor* A : EnemyUnits)
	{
		if (A) { EnemySide.Add(A); }
	}

	bInCombat = true;
	TurnNumber = 1;
	// Пороги таймера — состояние ОДНОГО боя: retry того же сценария обязан
	// проговорить реплики заново.
	bHalfTimeAnnounced = false;
	bFinalTurnsAnnounced = false;
	WarmUpCombatEffects();
	BeginPhase(ETurnPhase::Player);
}

void UTurnManagerSubsystem::WarmUpCombatEffects()
{
	// Каждый профиль греем один раз, даже если он общий у всего отряда.
	TSet<UUnitVfxDataAsset*> Profiles;
	auto CollectFrom = [&Profiles](const TArray<TObjectPtr<AActor>>& Side)
	{
		for (const TObjectPtr<AActor>& SideActor : Side)
		{
			if (const AUnitBase* Unit = Cast<AUnitBase>(SideActor.Get()))
			{
				if (UUnitVfxDataAsset* Profile = Unit->GetVfxProfile())
				{
					Profiles.Add(Profile);
				}
			}
		}
	};
	CollectFrom(PlayerSide);
	CollectFrom(EnemySide);

	for (UUnitVfxDataAsset* Profile : Profiles)
	{
		Profile->WarmUpEffects();
	}
}

bool UTurnManagerSubsystem::RegisterUnitInCombat(AActor* Unit)
{
	AUnitBase* TacticalUnit = Cast<AUnitBase>(Unit);
	if (!bInCombat || !TacticalUnit)
	{
		return false;
	}

	const uint8 TeamId = TacticalUnit->GetGenericTeamId().GetId();
	if (TeamId != TacticsTeamIds::Player && TeamId != TacticsTeamIds::Enemy)
	{
		UE_LOG(LogXRU1Turns, Warning, TEXT("[Turns] %s не введён в бой: TeamId=%u"),
			*GetNameSafe(Unit), TeamId);
		return false;
	}

	TArray<TObjectPtr<AActor>>& Side = (TeamId == TacticsTeamIds::Player) ? PlayerSide : EnemySide;
	if (Side.Contains(Unit))
	{
		return false;
	}

	// Размещённый враг мог остаться без контроллера, пока был выключен.
	if (!TacticalUnit->GetController())
	{
		TacticalUnit->SpawnDefaultController();
	}

	Side.Add(Unit);
	if (UActionPointsComponent* ActionPoints = Unit->FindComponentByClass<UActionPointsComponent>())
	{
		ActionPoints->ResetForNewTurn();
	}

	// Пересчёт укрытия на входе в бой: BeginPlay юнита из стримящегося sublevel
	// мог отработать раньше готовности коллизии соседей, а staged-боец включается
	// на позиции, где он ни разу не «прижимался». Без валидного ActiveCover не
	// строятся ни peek-точки у краёв, ни step-up над полуукрытием.
	if (UCoverDetectionComponent* Cover = TacticalUnit->GetCoverDetection())
	{
		Cover->EvaluateSurroundings();
	}
	// Новый участник боя — новый источник зрения или новая цель скрытия.
	if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		Fog->MarkVisibilityDirty(TacticalUnit);
	}
	OnUnitsChanged.Broadcast();
	return true;
}

bool UTurnManagerSubsystem::UnregisterUnitFromCombat(AActor* Unit)
{
	if (!Unit)
	{
		return false;
	}

	// Состав сторон меняется — туман обязан пересобрать видимость. Помечаем до
	// ветвлений: обе ветки ниже уходят в return, и дублировать вызов незачем.
	if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		Fog->MarkVisibilityDirty(Unit);
	}

	const int32 EnemyIndex = EnemySide.IndexOfByKey(Unit);
	if (EnemyIndex != INDEX_NONE)
	{
		EnemySide.RemoveAt(EnemyIndex);
		// Очередь вражеского хода индексирует ровно этот массив: без коррекции
		// удаление до текущего индекса пропустило бы следующего врага.
		if (EnemyIndex < EnemyTurnIndex)
		{
			--EnemyTurnIndex;
		}
		OnUnitsChanged.Broadcast();
		return true;
	}

	if (PlayerSide.Remove(Unit) > 0)
	{
		OnUnitsChanged.Broadcast();
		return true;
	}
	return false;
}

void UTurnManagerSubsystem::SetTurnLimit(int32 NewLimit)
{
	const int32 Clamped = FMath::Max(0, NewLimit);
	if (Clamped == TurnLimit)
	{
		return;
	}
	TurnLimit = Clamped;
	// Исходный лимит запоминаем один раз за бой: «половина времени» считается от
	// него, а не от текущего значения — иначе снятие заряда (лимит → 0) сделало
	// бы порог бессмысленным.
	if (InitialTurnLimit <= 0)
	{
		InitialTurnLimit = TurnLimit;
	}
	OnTurnLimitChanged.Broadcast();
}

void UTurnManagerSubsystem::BroadcastBombTimerMilestones()
{
	const int32 Remaining = GetTurnsRemaining();
	if (Remaining <= 0 || InitialTurnLimit <= 0)
	{
		return; // таймера нет либо заряд уже снят
	}

	// «Половина времени вышла»: первый ход игрока, на котором остаток опустился
	// до половины исходного лимита.
	if (!bHalfTimeAnnounced && Remaining <= FMath::CeilToInt(InitialTurnLimit / 2.f))
	{
		bHalfTimeAnnounced = true;
		UTacticalQuestEvents::BroadcastQuestEvent(
			this, TacticalQuestTags::Event_Tactical_Objective_Bomb_HalfTime, this);
	}

	if (!bFinalTurnsAnnounced && Remaining <= BombTickWarningTurns)
	{
		bFinalTurnsAnnounced = true;
		UTacticalQuestEvents::BroadcastQuestEvent(
			this, TacticalQuestTags::Event_Tactical_Objective_Bomb_FinalTurns, this);
	}
}

void UTurnManagerSubsystem::EndTurn()
{
	if (!bInCombat)
	{
		return;
	}

	StopEnemyTurnProcessing();

	switch (CurrentPhase)
	{
	case ETurnPhase::Player:
		// Таймер бомбы: игрок завершил свой N-й (лимитный) ход — взрыв, поражение.
		if (TurnLimit > 0 && TurnNumber >= TurnLimit)
		{
			OnTurnLimitExpired.Broadcast();
			EndCombat(false);
			return;
		}
		BeginPhase(ETurnPhase::Enemy);
		break;
	case ETurnPhase::Enemy:
		++TurnNumber;
		BeginPhase(ETurnPhase::Player);
		break;
	default:
		break;
	}
}

void UTurnManagerSubsystem::EndCombat(bool bPlayerWon)
{
	if (!bInCombat)
	{
		return;
	}

	StopEnemyTurnProcessing();
	bInCombat = false;
	CurrentPhase = ETurnPhase::None;
	OnCombatEnded.Broadcast(bPlayerWon);
}

void UTurnManagerSubsystem::CheckCombatOutcome()
{
	if (!bInCombat)
	{
		return;
	}

	auto AnyAlive = [](const TArray<TObjectPtr<AActor>>& Side)
	{
		for (const TObjectPtr<AActor>& Unit : Side)
		{
			if (Unit && UTacticsCombatStatics::IsUnitAlive(Unit))
			{
				return true;
			}
		}
		return false;
	};
	auto AnyEvacuated = [](const TArray<TObjectPtr<AActor>>& Side)
	{
		for (const TObjectPtr<AActor>& Unit : Side)
		{
			if (Unit && UTacticsCombatStatics::IsUnitEvacuated(Unit))
			{
				return true;
			}
		}
		return false;
	};

	const bool bEnemiesAlive = AnyAlive(EnemySide);
	// Эвакуированные живы: полная эвакуация отряда — не поражение (победу объявляет GameMode).
	const bool bPlayersAlive = AnyAlive(PlayerSide) || AnyEvacuated(PlayerSide);

	if (!bPlayersAlive)
	{
		EndCombat(false);
	}
	else if (!bEnemiesAlive && bAutoWinWhenEnemiesDead)
	{
		// Для миссий с целью (бомба/эвакуация) флаг выключен — зачистка не завершает бой.
		EndCombat(true);
	}
}

void UTurnManagerSubsystem::BeginPhase(ETurnPhase Phase)
{
	UE_LOG(LogXRU1Turns, Display, TEXT("[Turns] ═══ Фаза → %s (ход %d) ═══"),
		Phase == ETurnPhase::Player ? TEXT("ИГРОК") :
		Phase == ETurnPhase::Enemy ? TEXT("ВРАГ") : TEXT("None"), TurnNumber);
	CurrentPhase = Phase;
	ResetActionPointsForSide(Phase == ETurnPhase::Player ? PlayerSide : EnemySide);

	// Граница хода — обязательная точка сверки тумана: за чужой ход мир мог
	// измениться способами, которые не проходят через события юнита (телепорт
	// сценария, дверь, снятая голограмма).
	if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		Fog->MarkVisibilityDirty(this);
	}

	// Память контактов стареет ровно на границе хода: внутри хода достоверность
	// не должна «плыть» между решениями одного и того же бойца.
	if (UWorld* World = GetWorld())
	{
		if (UTacticalAIDirectorSubsystem* Director =
			World->GetSubsystem<UTacticalAIDirectorSubsystem>())
		{
			Director->AgeContacts(TurnNumber);
		}
	}
	OnTurnStarted.Broadcast(Phase);
	if (!bInCombat || CurrentPhase != Phase)
	{
		return; // listener мог синхронно завершить бой или сменить фазу
	}

	// Смена фазы — самый важный для игрока звуковой ориентир: без него непонятно,
	// закончился ход врага или бой просто «завис».
	if (const UWorld* World = GetWorld())
	{
		if (UTacticsAudioSubsystem* Audio = World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
		{
			Audio->PlayTurnStarted(Phase == ETurnPhase::Player);

			// Тик заряда — только в начале хода игрока и только в последние ходы
			// (GDD §13). Тикать каждый ход весь бой — шум, который перестают
			// слышать ровно к тому моменту, когда он что-то значит.
			const int32 Remaining = GetTurnsRemaining();
			if (Phase == ETurnPhase::Player && Remaining > 0 && Remaining <= BombTickWarningTurns)
			{
				Audio->PlayBombTick();
			}
		}
	}

	// Пороги таймера заряда — доменные факты, а не украшение звука: на них висят
	// реплики Купола (docs/02_LORE_SCRIPT.md §6). Считает их ЗДЕСЬ, потому что только TurnManager
	// знает и лимит (он зависит от сложности), и номер текущего хода. Оба
	// события one-shot на бой: повторная публикация на следующем ходу
	// превратила бы реплику в назойливый цикл.
	if (Phase == ETurnPhase::Player)
	{
		BroadcastBombTimerMilestones();
	}

	if (Phase == ETurnPhase::Player)
	{
		UTacticalQuestEvents::BroadcastQuestEvent(
			this, TacticalQuestTags::Event_Tactical_Turn_Player_Started, this);
	}
	else if (Phase == ETurnPhase::Enemy)
	{
		// Это подтверждённый конец player phase, а не raw Enter/request.
		UTacticalQuestEvents::BroadcastQuestEvent(
			this, TacticalQuestTags::Event_Tactical_Turn_Ended, this);
	}
	if (!bInCombat || CurrentPhase != Phase)
	{
		return; // message listener тоже может синхронно завершить/сменить фазу
	}

	// Ход врага исполняет сам менеджер: юниты действуют по одному.
	if (Phase == ETurnPhase::Enemy)
	{
		StartEnemyTurnProcessing();
	}
}

void UTurnManagerSubsystem::ResetActionPointsForSide(const TArray<TObjectPtr<AActor>>& Units)
{
	for (const TObjectPtr<AActor>& Unit : Units)
	{
		if (!Unit) { continue; }
		if (UActionPointsComponent* AP = Unit->FindComponentByClass<UActionPointsComponent>())
		{
			AP->ResetForNewTurn();
		}
	}
}

void UTurnManagerSubsystem::NotifyUnitAttacked(const AActor* Unit)
{
	if (Unit)
	{
		AttackedThisTurn.Add(TObjectKey<AActor>(Unit));
	}
}

void UTurnManagerSubsystem::NotifyUnitTargeted(const AActor* Target)
{
	if (Target)
	{
		++TargetedThisTurn.FindOrAdd(TObjectKey<AActor>(Target));
	}
}

int32 UTurnManagerSubsystem::GetTimesTargetedThisTurn(const AActor* Target) const
{
	const int32* Found = Target ? TargetedThisTurn.Find(TObjectKey<AActor>(Target)) : nullptr;
	return Found ? *Found : 0;
}

bool UTurnManagerSubsystem::IsAttackThrottled(const AActor* Unit) const
{
	if (MaxAttackersPerTurn < 0 || !Unit)
	{
		return false; // лимит выключен (Legend в терминах XCOM)
	}
	// Правило регулирует только вражеский AI: у игрока «сколько бойцов стреляет
	// за ход» — его собственное тактическое решение.
	if (!EnemySide.Contains(Unit))
	{
		return false;
	}
	// Уже стрелявшему добавка не запрещена: лимит считает ЮНИТОВ, вступивших в
	// бой, а не выстрелы. Иначе боец с 2 AP не смог бы отработать вторым очком.
	if (AttackedThisTurn.Contains(TObjectKey<AActor>(Unit)))
	{
		return false;
	}
	return AttackedThisTurn.Num() >= MaxAttackersPerTurn;
}

void UTurnManagerSubsystem::StartEnemyTurnProcessing()
{
	EnemyTurnIndex = 0;
	// Лимит атакующих и сведение огня считаются ЗА ХОД: новый ход — чистые счётчики.
	AttackedThisTurn.Reset();
	TargetedThisTurn.Reset();
	// Небольшая пауза перед первым действием: даём HUD показать смену фазы.
	GetWorld()->GetTimerManager().SetTimer(EnemyStepTimerHandle, this,
		&UTurnManagerSubsystem::ProcessNextEnemyUnit, EnemyStepInterval, false);
}

void UTurnManagerSubsystem::ProcessNextEnemyUnit()
{
	if (!bInCombat || CurrentPhase != ETurnPhase::Enemy)
	{
		return;
	}

	// Ищем следующего живого юнита с AI-контроллером; мёртвых и «безмозглых» пропускаем.
	while (EnemySide.IsValidIndex(EnemyTurnIndex))
	{
		AActor* Unit = EnemySide[EnemyTurnIndex];
		APawn* Pawn = Cast<APawn>(Unit);
		if (Pawn && !Pawn->GetController())
		{
			// Страховка для streamed/spawned-врагов со старым instance-значением
			// Auto Possess AI. Настроенный BP-класс контроллера при этом сохраняется.
			Pawn->SpawnDefaultController();
		}
		AUnitAIController* AI = Pawn ? Cast<AUnitAIController>(Pawn->GetController()) : nullptr;

		if (Unit && AI && UTacticsCombatStatics::IsUnitAlive(Unit))
		{
			// Ход бойца, которого игрок НЕ ВИДИТ, не показывают — его нечего
			// показывать. В XCOM 2 у AI-перемещений невидимые игроку куски пути
			// вырезаются целиком (`X2Action_Move`: «portions that are not visible
			// to the player are removed»), поэтому ход десятка скрытых врагов
			// проходит мгновенно. Здесь то же правило в нашей форме: ни полёта
			// камеры, ни пауз на чтение — только короткий тик, чтобы очередь
			// оставалась асинхронной.
			bLastEnemyWasHidden = !IsUnitVisibleToSquad(Unit);
			if (!bLastEnemyWasHidden)
			{
				OnEnemyUnitActivated.Broadcast(Unit);
			}

			// Пауза перед действиями — камера игрока долетает до юнита (XCOM-темп).
			GetWorld()->GetTimerManager().SetTimer(EnemyStepTimerHandle, this,
				&UTurnManagerSubsystem::ActivateCurrentEnemyUnit,
				bLastEnemyWasHidden ? HiddenEnemyStepInterval : EnemyActivationDelay, false);
			return;
		}

		if (Unit && UTacticsCombatStatics::IsUnitAlive(Unit))
		{
			UE_LOG(LogXRU1Turns, Error,
				TEXT("[TurnManager] Skipping living enemy %s: Pawn=%s Controller=%s (UnitAIController required)."),
				*GetNameSafe(Unit), *GetNameSafe(Pawn),
				*GetNameSafe(Pawn ? Pawn->GetController() : nullptr));
		}
		++EnemyTurnIndex;
	}

	// Все вражеские юниты отходили — ход возвращается игроку.
	EndTurn();
}

void UTurnManagerSubsystem::ActivateCurrentEnemyUnit()
{
	if (!bInCombat || CurrentPhase != ETurnPhase::Enemy || !EnemySide.IsValidIndex(EnemyTurnIndex))
	{
		return;
	}

	AActor* Unit = EnemySide[EnemyTurnIndex];
	APawn* Pawn = Cast<APawn>(Unit);
	AUnitAIController* AI = Pawn ? Cast<AUnitAIController>(Pawn->GetController()) : nullptr;
	if (Unit && AI && UTacticsCombatStatics::IsUnitAlive(Unit))
	{
		AI->ExecuteUnitTurn(FSimpleDelegate::CreateUObject(
			this, &UTurnManagerSubsystem::HandleEnemyUnitFinished));
	}
	else
	{
		// Юнит выбыл за время паузы (овервотч) — дальше по очереди.
		HandleEnemyUnitFinished();
	}
}

void UTurnManagerSubsystem::HandleEnemyUnitFinished()
{
	++EnemyTurnIndex;
	CheckCombatOutcome();
	if (!bInCombat)
	{
		return;
	}

	// Пауза «чтобы игрок успел прочитать ход» нужна только после хода, который
	// игрок видел. После скрытого читать нечего.
	const float NextDelay = bLastEnemyWasHidden ? HiddenEnemyStepInterval : EnemyStepInterval;
	GetWorld()->GetTimerManager().SetTimer(EnemyStepTimerHandle, this,
		&UTurnManagerSubsystem::ProcessNextEnemyUnit, NextDelay, false);
}

bool UTurnManagerSubsystem::IsUnitVisibleToSquad(const AActor* Unit) const
{
	if (!Unit)
	{
		return false;
	}
	// Единственный источник правды о видимости — подсистема тумана: HUD, камера
	// и правила уже читают её же, и темп хода не имеет права расходиться с
	// картинкой (иначе игрок увидит «прыжок» бойца, которого он всё-таки видел).
	if (const UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		return Fog->IsActorCurrentlyVisible(Unit);
	}
	return true; // без тумана считаем, что видно всё — прежнее поведение
}

void UTurnManagerSubsystem::StopEnemyTurnProcessing()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EnemyStepTimerHandle);
	}
	EnemyTurnIndex = 0;
}

TArray<AActor*> UTurnManagerSubsystem::GetActiveSideUnits() const
{
	const TArray<TObjectPtr<AActor>>& Side = (CurrentPhase == ETurnPhase::Enemy) ? EnemySide : PlayerSide;
	TArray<AActor*> Result;
	Result.Reserve(Side.Num());
	for (const TObjectPtr<AActor>& A : Side)
	{
		Result.Add(A.Get());
	}
	return Result;
}

bool UTurnManagerSubsystem::IsUnitOnActiveSide(const AActor* Unit) const
{
	if (!Unit || CurrentPhase == ETurnPhase::None)
	{
		return false;
	}
	const TArray<TObjectPtr<AActor>>& Side = (CurrentPhase == ETurnPhase::Enemy) ? EnemySide : PlayerSide;
	return Side.Contains(Unit);
}

TArray<AActor*> UTurnManagerSubsystem::GetSideUnits(const AActor* Unit) const
{
	TArray<AActor*> Result;
	if (!Unit)
	{
		return Result;
	}

	const TArray<TObjectPtr<AActor>>* Side = nullptr;
	if (PlayerSide.Contains(Unit))
	{
		Side = &PlayerSide;
	}
	else if (EnemySide.Contains(Unit))
	{
		Side = &EnemySide;
	}

	if (Side)
	{
		for (const TObjectPtr<AActor>& A : *Side)
		{
			if (A && UTacticsCombatStatics::IsUnitAlive(A))
			{
				Result.Add(A.Get());
			}
		}
	}
	return Result;
}

namespace
{
	// Сырая копия стороны (включая мёртвых) с отсевом null — общий код обоих геттеров.
	TArray<AActor*> CopySideUnits(const TArray<TObjectPtr<AActor>>& Side)
	{
		TArray<AActor*> Result;
		for (const TObjectPtr<AActor>& A : Side)
		{
			if (A) { Result.Add(A.Get()); }
		}
		return Result;
	}
}

TArray<AActor*> UTurnManagerSubsystem::GetPlayerSideUnits() const
{
	return CopySideUnits(PlayerSide);
}

TArray<AActor*> UTurnManagerSubsystem::GetEnemySideUnits() const
{
	return CopySideUnits(EnemySide);
}

int32 UTurnManagerSubsystem::GetAliveEnemyCount() const
{
	int32 Alive = 0;
	for (const TObjectPtr<AActor>& Unit : EnemySide)
	{
		if (Unit && UTacticsCombatStatics::IsUnitAlive(Unit))
		{
			++Alive;
		}
	}
	return Alive;
}

TArray<AActor*> UTurnManagerSubsystem::GetOpposingUnits(const AActor* Unit) const
{
	TArray<AActor*> Result;
	if (!Unit)
	{
		return Result;
	}

	const TArray<TObjectPtr<AActor>>* Opposing = nullptr;
	if (PlayerSide.Contains(Unit))
	{
		Opposing = &EnemySide;
	}
	else if (EnemySide.Contains(Unit))
	{
		Opposing = &PlayerSide;
	}

	if (Opposing)
	{
		for (const TObjectPtr<AActor>& A : *Opposing)
		{
			if (A && UTacticsCombatStatics::IsUnitAlive(A))
			{
				Result.Add(A.Get());
			}
		}
	}
	return Result;
}
