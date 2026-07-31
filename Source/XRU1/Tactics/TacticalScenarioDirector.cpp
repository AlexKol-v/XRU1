#include "TacticalScenarioDirector.h"
#include "XRU1Log.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "QuestDefinition.h"
#include "ScenarioActorRegistry.h"
#include "QuestSubsystem.h"
#include "TacticalPlayerController.h"
#include "TacticalQuestEvents.h"
#include "TacticalScenarioDataAsset.h"
#include "TacticsGameInstance.h"
#include "TacticsGameMode.h"
#include "TutorialActionGate.h"
#include "EngineUtils.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"

ATacticalScenarioDirector::ATacticalScenarioDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ATacticalScenarioDirector::BeginPlay()
{
	Super::BeginPlay();

	UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	ActiveScenario = GameInstance ? GameInstance->GetActiveScenario() : nullptr;

	// Прямой запуск общей карты (PIE без Hub/POI): берём preview-сценарий, чтобы
	// разработчик мог проверять шаги, не проходя кампанию. Настоящий bootstrap
	// всегда приоритетнее — ActiveScenario здесь уже был бы задан.
	if (!ActiveScenario && !PreviewScenario.IsNull() && GameInstance)
	{
		if (UTacticalScenarioDataAsset* Preview = PreviewScenario.LoadSynchronous();
			Preview && GameInstance->AdoptScenarioInPlace(Preview))
		{
			ActiveScenario = GameInstance->GetActiveScenario();
			UE_LOG(LogXRU1Scenario, Warning,
				TEXT("[Scenario] Прямой запуск карты: принят PreviewScenario %s (не путь кампании)"),
				*Preview->ScenarioId.ToString());
		}
	}

	ScenarioRunId = GameInstance ? GameInstance->GetActiveScenarioRunId() : 0;
	if (!ActiveScenario)
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Общая боевая карта открыта без ActiveScenario"));
		return;
	}
	if (!IsCurrentScenarioRun())
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Для одного run должен существовать ровно один Director"));
		return;
	}

	OnScenarioSelected(ActiveScenario);

	if (bAutoStreamScenarioSublevel)
	{
		BeginScenarioStreaming();
	}
}

void ATacticalScenarioDirector::BeginScenarioStreaming()
{
	UWorld* World = GetWorld();
	if (!World || !ActiveScenario || ActiveScenario->ScenarioSublevel.IsNull())
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] У сценария не задан ScenarioSublevel"));
		return;
	}

	const FString SublevelPackage = ActiveScenario->ScenarioSublevel.ToSoftObjectPath().GetLongPackageName();

	// В PIE пакет streaming level переименован в UEDPIE_<n>_<Имя>, поэтому
	// сравнивать длинные имена нельзя: сценарий ссылается на исходный ассет.
	// Сводим обе стороны к короткому имени без PIE-префикса.
	auto NormalizeLevelName = [](const FString& PackageName)
	{
		return UWorld::RemovePIEPrefix(FPackageName::GetShortName(PackageName));
	};
	const FString WantedName = NormalizeLevelName(SublevelPackage);

	ULevelStreaming* Target = nullptr;
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (Streaming &&
			NormalizeLevelName(Streaming->GetWorldAssetPackageName()) == WantedName)
		{
			Target = Streaming;
			break;
		}
	}
	if (!Target)
	{
		UE_LOG(LogXRU1Scenario, Error,
			TEXT("[Scenario] Sublevel %s не добавлен в persistent-карту через Window → Levels"),
			*SublevelPackage);
		return;
	}

	ScenarioStreamingLevel = Target;

	// Уровень мог остаться видимым после работы в редакторе — тогда OnLevelShown
	// уже не придёт, и ждать его означало бы зависнуть до конца сессии.
	if (Target->IsLevelVisible())
	{
		StreamingTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
			this, &ATacticalScenarioDirector::StartScenarioAfterStream);
		return;
	}

	Target->OnLevelShown.AddDynamic(this, &ATacticalScenarioDirector::HandleScenarioLevelShown);
	Target->SetShouldBeLoaded(true);
	Target->SetShouldBeVisible(true);
}

void ATacticalScenarioDirector::HandleScenarioLevelShown()
{
	if (ULevelStreaming* Streaming = ScenarioStreamingLevel.Get())
	{
		Streaming->OnLevelShown.RemoveDynamic(this, &ATacticalScenarioDirector::HandleScenarioLevelShown);
	}

	// Актор уровня существует, но его BeginPlay в этом кадре ещё мог не пройти:
	// реестр AnchorId и стороны боя собираем ровно на следующем tick.
	StreamingTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
		this, &ATacticalScenarioDirector::StartScenarioAfterStream);
}

void ATacticalScenarioDirector::StartScenarioAfterStream()
{
	if (bScenarioStartRequested || !IsCurrentScenarioRun())
	{
		return;
	}
	bScenarioStartRequested = true;

	if (!StartConfiguredQuest())
	{
		UE_LOG(LogXRU1Scenario, Error,
			TEXT("[Scenario] StartConfiguredQuest отклонил запуск сценария %s: "
				"проверь QuestId/QuestLogic в Quest Definition и состояние quest instance"),
			*ActiveScenario->ScenarioId.ToString());
	}
}

bool ATacticalScenarioDirector::IsCurrentScenarioRun() const
{
	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	if (!GameInstance || !ActiveScenario ||
		GameInstance->GetActiveScenario() != ActiveScenario ||
		ScenarioRunId <= 0 || GameInstance->GetActiveScenarioRunId() != ScenarioRunId)
	{
		return false;
	}

	int32 DirectorCount = 0;
	for (TActorIterator<ATacticalScenarioDirector> It(GetWorld()); It; ++It)
	{
		++DirectorCount;
	}
	return DirectorCount == 1;
}

bool ATacticalScenarioDirector::StartConfiguredQuest(AActor* QuestOwner)
{
	if (!IsCurrentScenarioRun() || ActiveScenario->QuestDefinition.IsNull())
	{
		return false;
	}

	UQuestDefinition* Definition = ActiveScenario->QuestDefinition.LoadSynchronous();
	UGameInstance* GameInstance = GetGameInstance();
	UQuestSubsystem* Quests = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
	if (!Definition || !Definition->QuestId.IsValid() || !Quests)
	{
		return false;
	}

	if (!QuestOwner)
	{
		const APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		QuestOwner = PlayerController ? PlayerController->GetPawn() : nullptr;
	}

	PlaceCameraAtScenarioAnchor();

	ActiveQuestId = Definition->QuestId;

	// Quest живёт в реестре AssetManager, а не по ссылке из Data Asset: если
	// DA_Quest_* лежит вне просканированных папок (DefaultGame.ini,
	// PrimaryAssetTypesToScan «Quest»), MakeQuestAvailable молча ничего не
	// делает, и сценарий падает с невнятным «нельзя запустить из состояния 0».
	if (!Quests->GetQuestDefinition(ActiveQuestId))
	{
		UE_LOG(LogXRU1Scenario, Error,
			TEXT("[Scenario] Quest %s отсутствует в реестре AssetManager. Ассет %s лежит вне ")
			TEXT("папок скана PrimaryAssetTypesToScan (Config/DefaultGame.ini) — перенесите его ")
			TEXT("в просканированную папку или добавьте новую в Directories и перезапустите редактор"),
			*ActiveQuestId.ToString(), *GetNameSafe(Definition));
		return false;
	}

	if (Quests->GetQuestState(ActiveQuestId) == EQuestState::Inactive)
	{
		Quests->MakeQuestAvailable(ActiveQuestId, QuestOwner);
	}

	const EQuestState State = Quests->GetQuestState(ActiveQuestId);
	if (State == EQuestState::Active)
	{
		Quests->SetTrackedQuest(ActiveQuestId);
		ATacticsGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ATacticsGameMode>() : nullptr;
		if (!GameMode || !GameMode->StartScenarioCombat())
		{
			return false;
		}
		if (!bReadyEventBroadcast && !bReadyEventPending)
		{
			bReadyEventPending = true;
			ReadyEventTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
				this, &ATacticalScenarioDirector::BroadcastReadyEvent);
		}
		return true;
	}
	if (State != EQuestState::Available)
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Quest %s нельзя запустить из состояния %d"),
			*ActiveQuestId.ToString(), static_cast<int32>(State));
		return false;
	}

	const bool bStarted = Quests->StartQuestById(ActiveQuestId) != nullptr &&
		Quests->GetQuestState(ActiveQuestId) == EQuestState::Active;
	if (bStarted)
	{
		Quests->SetTrackedQuest(ActiveQuestId);
		ATacticsGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ATacticsGameMode>() : nullptr;
		if (!GameMode || !GameMode->StartScenarioCombat())
		{
			UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Quest запущен, но GameMode не смог стартовать бой"));
			return false;
		}

		// PlayerStarted и Ready не должны попасть в StateTree одной пачкой: иначе
		// последовательные objectives могут потерять второй sibling event.
		bReadyEventPending = true;
		ReadyEventTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
			this, &ATacticalScenarioDirector::BroadcastReadyEvent);
	}
	return bStarted;
}

void ATacticalScenarioDirector::PlaceCameraAtScenarioAnchor()
{
	if (bInitialCameraPlaced || !ActiveScenario ||
		ActiveScenario->InitialCameraAnchorId.IsNone())
	{
		return;
	}

	AActor* CameraAnchor = UTacticalScenarioSubsystem::FindScenarioActorInWorld(
		this, ActiveScenario->InitialCameraAnchorId);
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* CameraPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!CameraAnchor || !CameraPawn)
	{
		UE_LOG(LogXRU1Scenario, Warning,
			TEXT("[Scenario] Камера не поставлена на якорь %s: %s"),
			*ActiveScenario->InitialCameraAnchorId.ToString(),
			!CameraAnchor ? TEXT("якорь не найден в registry") : TEXT("нет camera pawn"));
		return;
	}

	// Высоту оставляем родную: camera pawn сам держит свой ригель по земле,
	// якорь задаёт только точку интереса сценария в плане.
	FVector NewLocation = CameraAnchor->GetActorLocation();
	NewLocation.Z = CameraPawn->GetActorLocation().Z;
	CameraPawn->SetActorLocation(NewLocation);
	bInitialCameraPlaced = true;

	// Автофокус старта боя («центр отряда») не должен перебить сценарный ракурс.
	if (ATacticalPlayerController* TacticalController =
			Cast<ATacticalPlayerController>(PlayerController))
	{
		TacticalController->NotifyScenarioCameraPlaced();
	}
}

void ATacticalScenarioDirector::BroadcastReadyEvent()
{
	bReadyEventPending = false;
	if (!IsCurrentScenarioRun() || bReadyEventBroadcast)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UQuestSubsystem* Quests = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
	if (!Quests || Quests->GetQuestState(ActiveQuestId) != EQuestState::Active)
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Quest %s завершился до Scenario.Ready; input не открыт"),
			*ActiveQuestId.ToString());
		return;
	}

	bReadyEventBroadcast = UTacticalQuestEvents::BroadcastQuestEvent(
		this, TacticalQuestTags::Event_Tactical_Scenario_Ready, this);
	if (bReadyEventBroadcast)
	{
		// SendStateTreeEvent только ставит Ready в очередь. Открывать ввод здесь
		// нельзя: первый action мог бы попасть в ту же пачку до перехода дерева.
		ReadyEventTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
			this, &ATacticalScenarioDirector::OpenScenarioReadyGate);
	}
	else
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Не удалось опубликовать Scenario.Ready"));
	}
}

void ATacticalScenarioDirector::OpenScenarioReadyGate()
{
	if (!IsCurrentScenarioRun() || !bReadyEventBroadcast || bReadyGateOpened)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UQuestSubsystem* Quests = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
	if (!Quests || Quests->GetQuestState(ActiveQuestId) != EQuestState::Active)
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Quest %s завершился до открытия Action Gate"),
			*ActiveQuestId.ToString());
		return;
	}

	bReadyGateOpened = true;
	OnScenarioReady();
}

bool ATacticalScenarioDirector::FinalizeConfiguredScenario(bool bSuccess)
{
	if (!IsCurrentScenarioRun())
	{
		return false;
	}
	if (bScenarioFinalized)
	{
		return true;
	}
	if (bFinalizationPending)
	{
		return bPendingFinalizationSuccess == bSuccess &&
			PendingFinalizationRunId == ScenarioRunId;
	}
	if (!ActiveQuestId.IsValid())
	{
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UQuestSubsystem* Quests = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
	if (!Quests)
	{
		return false;
	}
	const EQuestState ExpectedState = bSuccess ? EQuestState::Completed : EQuestState::Failed;
	const EQuestState StateBeforeResult = Quests->GetQuestState(ActiveQuestId);
	if (StateBeforeResult != ExpectedState && StateBeforeResult != EQuestState::Active)
	{
		return false;
	}

	// Evac.Unit/Evac.Squad и другие terminal objective events были отправлены в
	// текущем frame. Сначала даём StateTree tick обработать их; Result публикуем
	// уже на следующем tick отдельной пачкой.
	bFinalizationPending = true;
	bPendingFinalizationSuccess = bSuccess;
	PendingFinalizationRunId = ScenarioRunId;
	FinalizationStage = 1;
	FinalizationTimerHandle = GetWorldTimerManager().SetTimerForNextTick(
		this, &ATacticalScenarioDirector::CompletePendingFinalization);
	return true;
}

void ATacticalScenarioDirector::CompletePendingFinalization()
{
	if (!bFinalizationPending || PendingFinalizationRunId != ScenarioRunId ||
		!IsCurrentScenarioRun())
	{
		bFinalizationPending = false;
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UQuestSubsystem* Quests = GameInstance ? GameInstance->GetSubsystem<UQuestSubsystem>() : nullptr;
	if (!Quests)
	{
		bFinalizationPending = false;
		return;
	}

	const EQuestState ExpectedState = bPendingFinalizationSuccess
		? EQuestState::Completed : EQuestState::Failed;
	const EQuestState CurrentState = Quests->GetQuestState(ActiveQuestId);

	if (FinalizationStage == 1)
	{
		const FGameplayTag ResultChannel = bPendingFinalizationSuccess
			? TacticalQuestTags::Event_Tactical_Scenario_Succeeded
			: TacticalQuestTags::Event_Tactical_Scenario_Failed;
		if (!UTacticalQuestEvents::BroadcastQuestEvent(this, ResultChannel, this))
		{
			bFinalizationPending = false;
			FinalizationStage = 0;
			return;
		}
		if (CurrentState == ExpectedState)
		{
			bScenarioFinalized = true;
			bFinalizationPending = false;
			FinalizationStage = 0;
			return;
		}
		if (CurrentState != EQuestState::Active)
		{
			bFinalizationPending = false;
			FinalizationStage = 0;
			UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Quest %s покинул Active с неожиданным state %d"),
				*ActiveQuestId.ToString(), static_cast<int32>(CurrentState));
			return;
		}

		FinalizationStage = 2;
		GetWorldTimerManager().SetTimer(FinalizationTimerHandle, this,
			&ATacticalScenarioDirector::CompletePendingFinalization,
			FMath::Max(0.05f, QuestFinalizationGracePeriod), false);
		return;
	}
	if (CurrentState != ExpectedState && CurrentState != EQuestState::Active)
	{
		bFinalizationPending = false;
		FinalizationStage = 0;
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Quest %s покинул Active с неожиданным state %d"),
			*ActiveQuestId.ToString(), static_cast<int32>(CurrentState));
		return;
	}

	if (Quests->GetQuestState(ActiveQuestId) == EQuestState::Active)
	{
		if (bPendingFinalizationSuccess)
		{
			Quests->CompleteQuest(ActiveQuestId);
		}
		else
		{
			Quests->FailQuest(ActiveQuestId);
		}
	}

	bScenarioFinalized = Quests->GetQuestState(ActiveQuestId) == ExpectedState;
	bFinalizationPending = false;
	FinalizationStage = 0;
	if (!bScenarioFinalized)
	{
		UE_LOG(LogXRU1Scenario, Error, TEXT("[Scenario] Quest %s не перешёл в ожидаемый terminal state"),
			*ActiveQuestId.ToString());
	}
}

void ATacticalScenarioDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(FinalizationTimerHandle);
	GetWorldTimerManager().ClearTimer(ReadyEventTimerHandle);
	GetWorldTimerManager().ClearTimer(StreamingTimerHandle);
	if (ULevelStreaming* Streaming = ScenarioStreamingLevel.Get())
	{
		Streaming->OnLevelShown.RemoveDynamic(this, &ATacticalScenarioDirector::HandleScenarioLevelShown);
	}
	ScenarioStreamingLevel.Reset();
	bFinalizationPending = false;
	bReadyEventPending = false;

	// Политика шага живёт в WorldSubsystem, но снять её надо явно: выход в Hub
	// посреди закрытого gate не должен оставить ввод заблокированным.
	if (UWorld* World = GetWorld())
	{
		if (UTutorialActionGateSubsystem* Gate = World->GetSubsystem<UTutorialActionGateSubsystem>())
		{
			Gate->ClearAllPolicies();
		}
	}

	// Runner принадлежит World, а instance — GameInstance. При abort/reload нельзя
	// оставлять Active-instance без runner: возвращаем его в Available.
	if (ActiveQuestId.IsValid())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UQuestSubsystem* Quests = GameInstance->GetSubsystem<UQuestSubsystem>();
				Quests)
			{
				if (Quests->GetQuestState(ActiveQuestId) == EQuestState::Active)
				{
					Quests->AbandonQuest(ActiveQuestId);
				}
				if (Quests->GetTrackedQuest() == ActiveQuestId)
				{
					Quests->SetTrackedQuest(FGameplayTag());
				}
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}
