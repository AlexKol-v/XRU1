#include "TacticalPlayerController.h"
#include "UnitBase.h"
#include "UnitAIController.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "CoverTuningDataAsset.h"
#include "Components/CapsuleComponent.h" // полувысота капсулы для превью точки
#include "TacticalAbility.h"
#include "TacticalCameraPawn.h"
#include "GA_Attack.h"
#include "GA_Overwatch.h"
#include "MoveRangeVisualizer.h"
#include "MissionObjectives.h"
#include "ScenarioActorRegistry.h"
#include "TacticsAudioSubsystem.h"
#include "TacticsGameInstance.h"
#include "XRU1Log.h"
#include "TacticalQuestEvents.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameplayTags.h"
#include "TurnManagerSubsystem.h"
#include "TutorialActionGate.h"
#include "TutorialHintOverlay.h"
#include "TutorialPresentation.h"
#include "QuestDefinition.h"
#include "QuestInstance.h"
#include "QuestSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "FogOfWarSubsystem.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "MenuWidgets.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/DecalComponent.h"
#include "TacticalHUDStyleData.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "CoreGlobals.h" // GFrameCounter — штамп кадра для отложенного автоперехода

namespace
{
	/**
	 * Приказ игрока в терминах обучения. Отдельный enum нужен потому, что gate
	 * ограничивает НАМЕРЕНИЕ шага, а ETacticalPlayerCommand описывает конкуренцию
	 * за одну тактическую активацию — это разные оси.
	 */
	ETutorialAction TacticalCommandToTutorialAction(ETacticalPlayerCommand Command)
	{
		switch (Command)
		{
		case ETacticalPlayerCommand::Move:         return ETutorialAction::Move;
		case ETacticalPlayerCommand::Attack:       return ETutorialAction::Attack;
		case ETacticalPlayerCommand::Overwatch:    return ETutorialAction::Overwatch;
		case ETacticalPlayerCommand::HunkerDown:   return ETutorialAction::Hunker;
		case ETacticalPlayerCommand::ClassAbility: return ETutorialAction::ClassAbility;
		case ETacticalPlayerCommand::Interact:     return ETutorialAction::Interact;
		case ETacticalPlayerCommand::SkipUnitTurn: return ETutorialAction::EndTurn;
		default:                                   return ETutorialAction::Move;
		}
	}
}

ATacticalPlayerController::ATacticalPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void ATacticalPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}

	// Корневой UI-слой (стеки CommonUI) — как у контроллеров меню/хаба.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UGameUIManagerSubsystem* UIManager = GameInstance->GetSubsystem<UGameUIManagerSubsystem>())
		{
			UIManager->CreateLayout(this, RootLayoutClass);
		}
	}

	// Политика шага обучения может прийти уже после автовыбора бойца в начале
	// фазы — тогда выбор нужно снять, иначе шаг «выбери такого-то» окажется
	// закрыт автоматикой и не сможет опубликовать подтверждённое событие.
	if (UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this))
	{
		Gate->OnPolicyChanged.AddUniqueDynamic(
			this, &ATacticalPlayerController::HandleTutorialPolicyChanged);
		Gate->OnDestinationsChanged.AddUniqueDynamic(
			this, &ATacticalPlayerController::HandleTutorialDestinationsChanged);
	}

	// Трекер целей обучения — чистый Slate поверх viewport, WBP не требуется.
	if (GEngine && GEngine->GameViewport)
	{
		TutorialHintOverlay = SNew(STutorialHintOverlay)
			.Owner(TWeakObjectPtr<ATacticalPlayerController>(this));
		GEngine->GameViewport->AddViewportWidgetContent(
			TutorialHintOverlay.ToSharedRef(), /*ZOrder=*/8);
	}

	// Визуализатор зоны хода.
	if (MoveRangeVisualizerClass)
	{
		MoveRangeVisualizer = GetWorld()->SpawnActor<AMoveRangeVisualizer>(MoveRangeVisualizerClass);
		if (MoveRangeVisualizer)
		{
			MoveRangeVisualizer->Hide();
		}
	}
	else
	{
		UE_LOG(LogXRU1Combat, Warning, TEXT("[MoveRange] MoveRangeVisualizerClass не назначен в BP-контроллере — зона хода и превью пути не будут видны"));
	}

	// Блокировка ввода в фазу врага + сброс выбора при смене фазы.
	if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
	{
		TurnManager->OnTurnStarted.AddDynamic(this, &ATacticalPlayerController::HandleTurnStarted);
		TurnManager->OnEnemyUnitActivated.AddDynamic(this, &ATacticalPlayerController::HandleEnemyUnitActivated);
	}

	// Ввод: и мир (клики), и UI (HUD).
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}

void ATacticalPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TutorialHintOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(TutorialHintOverlay.ToSharedRef());
	}
	TutorialHintOverlay.Reset();
	for (const TWeakObjectPtr<UDecalComponent>& Marker : TutorialDestinationMarkers)
	{
		if (Marker.IsValid())
		{
			Marker->DestroyComponent();
		}
	}
	TutorialDestinationMarkers.Reset();
	// Кольца «кого можно поднять» живут по тем же правилам, что и маркеры точек:
	// декали спавнятся в персистентный мир и переживают выгрузку сценарного
	// сублевела, если их не убрать явно.
	for (const TWeakObjectPtr<UDecalComponent>& Ring : ReviveRingDecals)
	{
		if (Ring.IsValid())
		{
			Ring->DestroyComponent();
		}
	}
	ReviveRingDecals.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATacticalPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Контекст тактического ввода.
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (TacticalInputContext)
			{
				InputSubsystem->AddMappingContext(TacticalInputContext, /*Priority=*/1);
			}
		}
	}

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		return;
	}

	auto BindPressed = [Input, this](const TObjectPtr<UInputAction>& Action, auto Func)
	{
		if (Action)
		{
			Input->BindAction(Action, ETriggerEvent::Started, this, Func);
		}
	};

	BindPressed(SelectAction, &ATacticalPlayerController::HandleSelectPressed);
	BindPressed(CommandAction, &ATacticalPlayerController::HandleCommandPressed);
	BindPressed(EndTurnAction, &ATacticalPlayerController::RequestEndTurn);
	BindPressed(AttackAction, &ATacticalPlayerController::RequestAttack);
	BindPressed(OverwatchAction, &ATacticalPlayerController::RequestOverwatch);
	BindPressed(HunkerAction, &ATacticalPlayerController::RequestHunkerDown);
	BindPressed(ClassAbilityAction, &ATacticalPlayerController::RequestClassAbility);
	BindPressed(InteractAction, &ATacticalPlayerController::RequestInteract);
	BindPressed(SkipTurnAction, &ATacticalPlayerController::RequestSkipUnitTurn);
	BindPressed(NextUnitAction, &ATacticalPlayerController::SelectNextUnit);
	BindPressed(PauseAction, &ATacticalPlayerController::RequestPause);

	if (CameraPanAction)
	{
		Input->BindAction(CameraPanAction, ETriggerEvent::Triggered, this,
			&ATacticalPlayerController::HandleCameraPan);
	}
	if (CameraRotateAction)
	{
		Input->BindAction(CameraRotateAction, ETriggerEvent::Started, this,
			&ATacticalPlayerController::HandleCameraRotate);
	}
	if (CameraZoomAction)
	{
		Input->BindAction(CameraZoomAction, ETriggerEvent::Triggered, this,
			&ATacticalPlayerController::HandleCameraZoom);
	}

	// Слоты 1–4: BindAction с payload-индексом.
	for (int32 i = 0; i < SelectSlotActions.Num(); ++i)
	{
		if (SelectSlotActions[i])
		{
			Input->BindAction(SelectSlotActions[i], ETriggerEvent::Started, this,
				&ATacticalPlayerController::SelectUnitBySlot, i);
		}
	}
}

void ATacticalPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Юнит закончил перемещение — зона хода перестраивается от новой позиции,
	// камера прекращает сопровождение бегущего бойца.
	const bool bMovingNow = IsSelectedUnitMoving();
	if (bSelectedUnitWasMoving && !bMovingNow)
	{
		RefreshMoveRange();
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->ClearFollowTarget();
		}

		// Отложенный XCOM-автопереход: бегун потратил последние AP — теперь,
		// когда он остановился, выбор уходит следующему бойцу с AP, а если
		// такого нет — ход автоматически завершается (переходит врагу).
		// Во время кадра выстрела (Overwatch мог стрелять по бегущему) — ждём.
		if (bAutoSelectUnits && IsPlayerPhase() && SelectedUnit &&
			SelectedUnit->GetActionPoints() && !SelectedUnit->GetActionPoints()->HasActionsLeft())
		{
			if (bReactionPlaying || IsCameraFramingShot() || IsUnitActionInProgress(SelectedUnit))
			{
				bPendingAutoAdvance = true;
				PendingAutoAdvanceFrame = GFrameCounter;
			}
			else
			{
				SelectNextUnit();
				TryAutoEndTurn();
			}
		}
	}
	bSelectedUnitWasMoving = bMovingNow;

	// Отложенный автопереход дозрел: кадр выстрела кончился, никто не бежит.
	// Штамп кадра (см. PendingAutoAdvanceFrame): не разрешаем в тот же кадр, где
	// pending взведён — иначе перехватили бы ход до старта кадра выстрела.
	if (bPendingAutoAdvance && GFrameCounter != PendingAutoAdvanceFrame && !bReactionPlaying &&
		IsPlayerPhase() && !bMovingNow && !IsCameraFramingShot() &&
		!IsUnitActionInProgress(SelectedUnit))
	{
		UE_LOG(LogXRU1Combat, Log, TEXT("[AutoAdv] pending дозрел (кадр выстрела кончился) → переход"));
		bPendingAutoAdvance = false;
		if (bAutoSelectUnits && SelectedUnit && SelectedUnit->GetActionPoints() &&
			!SelectedUnit->GetActionPoints()->HasActionsLeft())
		{
			SelectNextUnit();
		}
		TryAutoEndTurn();
	}

	UpdateEdgeScroll();
	UpdateHoverHighlight();
	UpdatePathPreviewUnderCursor();
	UpdateSquadOverheadVisibility();

	// Действующий враг вышел из-за угла и стал виден отряду — подхватываем его
	// камерой прямо на ходу. Троттлинг тот же, что у дебага LOS: сам предикат
	// делает сферо-свипы по всем бойцам, каждый кадр это лишнее.
	if (PendingEnemyCameraUnit.IsValid() && IsEnemyPhaseNow())
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastEnemyVisibilityCheckTime >= LOSDebugInterval)
		{
			LastEnemyVisibilityCheckTime = Now;
			AActor* const Enemy = PendingEnemyCameraUnit.Get();
			if (UTacticsCombatStatics::IsUnitAlive(Enemy) && IsVisibleToSquad(Enemy))
			{
				PendingEnemyCameraUnit = nullptr;
				if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
				{
					Camera->SetFollowTarget(Enemy);
				}
			}
		}
	}
	else if (!IsEnemyPhaseNow())
	{
		PendingEnemyCameraUnit = nullptr; // ход вернулся игроку — заявка протухла
	}

	// Дебаг LOS (xru1.LOS.Debug 1): без этого DrawDebug* внутри HasLineOfSight
	// рисуется только в момент РЕАЛЬНОГО запроса (AI-ход, обновление HUD при
	// смене выбора/наведения) и тут же гаснет — на глаз не разобрать геометрию.
	// Здесь ПЕРИОДИЧЕСКИ (раз в LOSDebugInterval, не каждый кадр — иначе лог
	// захлёстывает) гоним LOS выбранного юнита против живых врагов ТОЛЬКО ради
	// побочного эффекта отрисовки; IsLOSDebugEnabled() — мгновенный ранний выход,
	// когда CVar выключен (нулевая цена в обычной игре).
	if (UTacticsCombatStatics::IsLOSDebugEnabled() && SelectedUnit &&
		UTacticsCombatStatics::IsUnitAlive(SelectedUnit))
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastLOSDebugTime >= LOSDebugInterval)
		{
			LastLOSDebugTime = Now;
			if (const UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
			{
				DrawCoverSidesDebug(SelectedUnit);
				for (AActor* Enemy : TurnManager->GetOpposingUnits(SelectedUnit))
				{
					if (Enemy && UTacticsCombatStatics::IsUnitAlive(Enemy))
					{
						UTacticsCombatStatics::HasLineOfSight(SelectedUnit, Enemy);
						DrawCoverSidesDebug(Enemy); // по врагам видно, откуда их прикрывает
					}
				}
			}
		}
	}
}

void ATacticalPlayerController::DrawCoverSidesDebug(const AActor* Unit) const
{
	const UCoverDetectionComponent* Cover =
		Unit ? Unit->FindComponentByClass<UCoverDetectionComponent>() : nullptr;
	if (!Cover || Cover->CoverSides.Num() == 0)
	{
		return;
	}

	const FVector Origin = Unit->GetActorLocation();

	// ⚠️ Дугу защиты здесь рисовать НЕЛЬЗЯ, хотя раньше рисовали: правило больше
	// не угловое. Укрытие решается физикой выстрела (луч от цели к огневой
	// позиции стрелка), и нарисованный сектор врал бы игроку.
	//
	// Стрелки — это ВИЗУАЛЬНЫЙ слой: «к каким стенам боец прижат». Полезно для
	// анимации и для понимания расстановки, но не для «фланг или нет».
	for (const FCoverSide& Side : Cover->CoverSides)
	{
		// Half — голубым, Full — синим; та же семантика, что у щита в HUD.
		const FColor Color = (Side.Type == ECoverType::Full) ? FColor::Blue : FColor::Cyan;
		DrawDebugDirectionalArrow(GetWorld(), Origin, Origin + Side.Direction * Side.Distance,
			40.f, Color, false, LOSDebugInterval * 1.1f, 0, 4.f);
	}

	// А вот что РЕАЛЬНО решает фланг: работает ли укрытие против каждого врага.
	// Зелёная линия к врагу — он нас не пробивает (укрытие держит), красная —
	// держит фланг и стреляет без штрафа.
	if (const UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
	{
		for (AActor* Enemy : TurnManager->GetOpposingUnits(Unit))
		{
			if (!Enemy || !UTacticsCombatStatics::IsUnitAlive(Enemy))
			{
				continue;
			}
			const bool bCovered = Cover->GetCoverAgainst(Enemy) != ECoverType::None;
			DrawDebugLine(GetWorld(), Origin, Enemy->GetActorLocation(),
				bCovered ? FColor::Green : FColor::Red, false, LOSDebugInterval * 1.1f, 0, 2.f);
		}
	}
}

void ATacticalPlayerController::UpdateEdgeScroll()
{
	if (!bEdgeScrollEnabled || IsTargetingAttack())
	{
		return;
	}
	// Курсор мог случайно остаться у края в момент кинематографичного выстрела.
	// Пассивный edge scroll не считается осознанным manual override кадра.
	if (const ATacticalCameraPawn* Camera =
		Cast<ATacticalCameraPawn>(GetPawn());
		Camera && Camera->IsFramingShot())
	{
		return;
	}

	// В ход врага камеру ведёт авто-слежение (XCOM). Edge scroll здесь выключен:
	// иначе курсор, замерший у края экрана, каждый кадр рвал бы follow и камера
	// не переходила бы к следующему действующему врагу.
	if (IsEnemyPhaseNow())
	{
		return;
	}

	float MouseX, MouseY;
	int32 ViewX, ViewY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return; // курсор вне окна — не скроллим
	}
	GetViewportSize(ViewX, ViewY);
	if (ViewX <= 0 || ViewY <= 0)
	{
		return;
	}

	// Единичный вектор панорамы: у левого края — влево, у верхнего — вперёд и т.д.
	FVector2D Pan = FVector2D::ZeroVector;
	if (MouseX <= EdgeScrollMarginPx)                 { Pan.X = -1.f; }
	else if (MouseX >= ViewX - EdgeScrollMarginPx)    { Pan.X = 1.f; }
	if (MouseY <= EdgeScrollMarginPx)                 { Pan.Y = 1.f; }
	else if (MouseY >= ViewY - EdgeScrollMarginPx)    { Pan.Y = -1.f; }

	if (!Pan.IsNearlyZero())
	{
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->AddPanInput(Pan); // ручной ввод: заодно разрывает follow (XCOM)
		}
	}
}

void ATacticalPlayerController::UpdateHoverHighlight()
{
	// Кого подсвечивать: живой неэвакуированный юнит под курсором (свой — контекст
	// выбора, враг — контекст атаки; цвет обводки различает stencil-значение).
	AUnitBase* NewHovered = nullptr;
	FHitResult Hit;
	if (TraceUnderCursor(Hit))
	{
		AUnitBase* Unit = Cast<AUnitBase>(Hit.GetActor());
		if (Unit && !Unit->IsDead() && !Unit->IsEvacuated())
		{
			// В фазу врага свои юниты не интерактивны — наведением их не подсвечиваем
			// (как в XCOM). Врагов подсвечиваем всегда, чтобы читать их ход.
			const bool bAlly =
				Unit->GetGenericTeamId().GetId() == TacticsTeamIds::Player;
			if (!(bAlly && IsEnemyPhaseNow()))
			{
				NewHovered = Unit;
			}
		}
	}

	if (HoveredUnit.Get() == NewHovered)
	{
		return;
	}

	if (AUnitBase* OldHovered = HoveredUnit.Get())
	{
		// Взятая на прицел цель остаётся подсвеченной, даже когда курсор ушёл
		// (XCOM): её обводку снимет только смена цели / выход из прицеливания.
		if (OldHovered != CurrentAttackTarget.Get())
		{
			OldHovered->SetHoverHighlight(false);
		}
	}
	if (NewHovered)
	{
		NewHovered->SetHoverHighlight(true);
	}
	HoveredUnit = NewHovered;
	OnHoveredUnitChanged.Broadcast(NewHovered);
}

bool ATacticalPlayerController::IsSelectedUnitMoving() const
{
	// Общий предикат «в пути» (см. AUnitAIController::IsMoving): статус path
	// following ставится сразу при выдаче приказа — в отличие от velocity,
	// которая в кадр приказа ещё нулевая, — и учитывает паузы между отрезками
	// ломаной маршрута, иначе на каждом повороте боец «финишировал» бы.
	const AUnitAIController* UnitAI = SelectedUnit
		? Cast<AUnitAIController>(SelectedUnit->GetController()) : nullptr;
	return UnitAI && UnitAI->IsMoving();
}

void ATacticalPlayerController::UpdatePathPreviewUnderCursor()
{
	if (!MoveRangeVisualizer)
	{
		return;
	}

	// Превью уместно только когда юнит готов принять приказ и стоит на месте.
	const bool bCanPreview = CanIssueCommand(ETacticalPlayerCommand::Move);
	if (!bCanPreview)
	{
		MoveRangeVisualizer->HidePathPreview();
		LastPathPreviewGoal = FVector(TNumericLimits<float>::Max());
		return;
	}

	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, Hit) || !Hit.bBlockingHit ||
		Cast<AUnitBase>(Hit.GetActor())) // над юнитом — контекст выбора/атаки, не движения
	{
		MoveRangeVisualizer->HidePathPreview();
		LastPathPreviewGoal = FVector(TNumericLimits<float>::Max());
		return;
	}

	// Перезапрашиваем путь только при заметном сдвиге курсора (25 см).
	if (FVector::DistSquared2D(Hit.Location, LastPathPreviewGoal) < 625.f)
	{
		return;
	}
	LastPathPreviewGoal = Hit.Location;
	MoveRangeVisualizer->UpdatePathPreview(Hit.Location);
}

// --- Выбор юнита ---------------------------------------------------------------

void ATacticalPlayerController::NotifyCommandDenied()
{
	if (UTacticsAudioSubsystem* Audio = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
	{
		Audio->PlayUIDenied();
	}

	// HUD показывает причину из Action Gate, если шаг обучения её задал.
	if (const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this))
	{
		const FText Reason = Gate->GetDenialReason();
		if (!Reason.IsEmpty())
		{
			LastDenialReason = Reason;
			LastDenialTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			UE_LOG(LogXRU1Quest, Verbose, TEXT("Команда отклонена: %s"), *Reason.ToString());
		}
	}
}

// --- Подсказки обучения (данные для Slate-оверлея) -----------------------------

FText ATacticalPlayerController::GetTutorialQuestTitle() const
{
	const UQuestSubsystem* Quests = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UQuestSubsystem>() : nullptr;
	if (!Quests)
	{
		return FText::GetEmpty();
	}
	const FGameplayTag Tracked = Quests->GetTrackedQuest();
	const UQuestInstance* Instance = Tracked.IsValid() ? Quests->GetQuestInstance(Tracked) : nullptr;
	if (!Instance || !Instance->Definition ||
		Quests->GetQuestState(Tracked) != EQuestState::Active)
	{
		return FText::GetEmpty();
	}
	return Instance->Definition->DisplayName;
}

FText ATacticalPlayerController::GetTutorialObjectiveLines() const
{
	const UQuestSubsystem* Quests = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UQuestSubsystem>() : nullptr;
	if (!Quests)
	{
		return FText::GetEmpty();
	}
	const FGameplayTag Tracked = Quests->GetTrackedQuest();
	const UQuestInstance* Instance = Tracked.IsValid() ? Quests->GetQuestInstance(Tracked) : nullptr;
	if (!Instance || Quests->GetQuestState(Tracked) != EQuestState::Active)
	{
		return FText::GetEmpty();
	}

	// Активные цели снимка прогресса. Скрытые условия (пустой Description)
	// в HUD не показываются — они существуют только для StateTree.
	FString Lines;
	for (const FObjectiveProgress& Objective : Instance->Progress.Objectives)
	{
		if (Objective.State != EObjectiveState::Active || Objective.Description.IsEmpty())
		{
			continue;
		}
		FString Line = Objective.Description.ToString();
		if (Objective.Required > 1)
		{
			Line += FString::Printf(TEXT("  (%d/%d)"), Objective.Current, Objective.Required);
		}
		if (!Lines.IsEmpty())
		{
			Lines += TEXT("\n");
		}
		Lines += TEXT("• ") + Line;
	}

	// У шага не заполнены Description — показываем инструкцию политики шага:
	// DenialReason сформулирован как «что сделать» и лучше, чем пустой экран.
	if (Lines.IsEmpty())
	{
		if (const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this);
			Gate && Gate->IsGateActive() && !Gate->GetDenialReason().IsEmpty())
		{
			Lines = Gate->GetDenialReason().ToString();
		}
	}
	return FText::FromString(Lines);
}

FText ATacticalPlayerController::GetTutorialDenialText() const
{
	if (LastDenialReason.IsEmpty() || !GetWorld() ||
		GetWorld()->GetTimeSeconds() - LastDenialTimeSeconds > 3.f)
	{
		return FText::GetEmpty();
	}
	return LastDenialReason;
}

FText ATacticalPlayerController::GetTutorialBeatSubtitle() const
{
	const UWorld* World = GetWorld();
	const UTutorialPresentationSubsystem* Presentation = World
		? World->GetSubsystem<UTutorialPresentationSubsystem>() : nullptr;
	if (!Presentation || !Presentation->IsBeatActive())
	{
		return FText::GetEmpty();
	}

	const FTacticalTutorialBeat Beat = Presentation->GetActiveBeat();
	if (Beat.Subtitle.IsEmpty())
	{
		return FText::GetEmpty();
	}
	if (Beat.Speaker.IsEmpty())
	{
		return Beat.Subtitle;
	}
	return FText::Format(NSLOCTEXT("XRU1.Tutorial", "BeatSubtitle", "{0}: {1}"),
		Beat.Speaker, Beat.Subtitle);
}

bool ATacticalPlayerController::IsUnitSelectableByGate(const AUnitBase* Unit) const
{
	const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this);
	if (!Gate)
	{
		return true;
	}

	// Снятие выбора (nullptr) проверяется тем же правилом. Шаг, ограничивший
	// выбор одним бойцом, обязан запрещать и клик по пустому месту: иначе игрок
	// снимет выбор, а вернуть его тот же gate уже не даст — шаг встанет намертво.
	return Gate->IsActionAllowed(ETutorialAction::Select, Unit);
}

void ATacticalPlayerController::SelectUnit(AUnitBase* Unit)
{
	SelectUnitInternal(Unit, /*bPlayerInitiated=*/true);
}

void ATacticalPlayerController::SelectUnitInternal(AUnitBase* Unit, bool bPlayerInitiated)
{
	// Reaction-window — глобальная модальная транзакция: смена HUD/camera owner не должна
	// сорвать чужой Overwatch и возобновить остановленного mover уже в другой фазе.
	if (bReactionPlaying)
	{
		return;
	}

	// Шаг обучения может требовать конкретного бойца. Отказ происходит здесь, до
	// смены владельца HUD и камеры, поэтому мир остаётся нетронутым.
	if (bPlayerInitiated && !IsUnitSelectableByGate(Unit))
	{
		NotifyCommandDenied();
		return;
	}

	// Труп/эвакуированного выбрать нельзя (клик по портрету погибшего в HUD).
	// Downed выбирать можно: посмотреть, где лежит; кнопки погасит RefreshButtons.
	if (Unit && (Unit->IsDead() || Unit->IsEvacuated()))
	{
		return;
	}
	if (SelectedUnit == Unit)
	{
		return;
	}
	// Нельзя сменить владельца HUD/camera посреди fire transaction: AP уже могли
	// закончиться, но montage/return ещё обязаны дойти до terminal callback.
	if (SelectedUnit && IsUnitActionInProgress(SelectedUnit))
	{
		return;
	}

	// Отписка от AP/состояния предыдущего выбранного + погасить его кольцо.
	if (SelectedUnit)
	{
		if (UActionPointsComponent* OldAP = SelectedUnit->GetActionPoints())
		{
			OldAP->OnActionPointsChanged.RemoveDynamic(this, &ATacticalPlayerController::HandleSelectedUnitAPChanged);
		}
		SelectedUnit->OnUnitStateChanged.RemoveDynamic(this, &ATacticalPlayerController::HandleSelectedUnitStateChanged);
		SelectedUnit->SetSelectionHighlight(false);
	}

	// Сначала штатно выходим из модального targeting. ExitTargetingMode вернёт
	// глобальный пользовательский yaw/zoom; FocusOnActor ниже заменит только XY
	// цели полёта. Прежний pre-Abandon превращал временный yaw прицела в
	// постоянный и потому визуально «сбрасывал» поворот при смене бойца.
	SetTargetingMode(EPlayerTargetingMode::None);
	UE_LOG(LogXRU1Camera, Display, TEXT("[Select] %s → %s (byPlayer=%d)"),
		*GetNameSafe(SelectedUnit), *GetNameSafe(Unit), bPlayerInitiated ? 1 : 0);
	SelectedUnit = Unit;
	bSelectionByPlayer = bPlayerInitiated && Unit != nullptr;
	RefreshDownedReviveRings();

	if (SelectedUnit)
	{
		if (UActionPointsComponent* NewAP = SelectedUnit->GetActionPoints())
		{
			NewAP->OnActionPointsChanged.AddDynamic(this, &ATacticalPlayerController::HandleSelectedUnitAPChanged);
		}
		SelectedUnit->OnUnitStateChanged.AddDynamic(this, &ATacticalPlayerController::HandleSelectedUnitStateChanged);
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->FocusOnActor(SelectedUnit);
		}
		SelectedUnit->SetSelectionHighlight(!IsEnemyPhaseNow());
	}

	// Навмеш статичен, занятость — дисками: зона строится сразу, без задержек.
	RefreshMoveRange();
	// Маркеры точек шага зависят от выбранного бойца (личные точки маршрута):
	// Танк видит свою, Оса — свои, без выбора показывается весь набор.
	RefreshTutorialDestinationMarkers();
	OnSelectedUnitChanged.Broadcast(SelectedUnit);

	// Квест-событие публикуется ПОСЛЕ фактической смены canonical SelectedUnit и
	// только для пользовательского выбора: автовыбор XCOM в начале фазы не должен
	// закрывать шаг «выбери Медика» сам за игрока.
	if (bPlayerInitiated && SelectedUnit &&
		UTacticsCombatStatics::IsUnitAlive(SelectedUnit) &&
		UTacticalQuestEvents::IsPlayerSideUnit(this, SelectedUnit))
	{
		if (UTacticsAudioSubsystem* Audio = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
		{
			Audio->PlayUnitSelected();
		}
		UTacticalQuestEvents::BroadcastQuestEventEx(this,
			TacticalQuestTags::Event_Tactical_Unit_Selected, SelectedUnit, SelectedUnit);
	}
}

void ATacticalPlayerController::NotifyUnitMoveFinished(AUnitBase* Unit)
{
	// Финишировал ДРУГОЙ юнит: его диск занятости встал на новую позицию —
	// зона выбранного юнита пересчитывается сразу (всё синхронно, без задержек).
	if (Unit && Unit != SelectedUnit && SelectedUnit && IsPlayerPhase())
	{
		RefreshMoveRange();
	}

	// Финишировал ВЫБРАННЫЙ: с новой позиции могли открыться действия (бомба,
	// зона эвакуации) и изменился шанс попадания по цели под курсором.
	if (Unit && Unit == SelectedUnit)
	{
		OnAvailableActionsChanged.Broadcast();
	}
}

void ATacticalPlayerController::SelectUnitBySlot(int32 SlotIndex)
{
	const TArray<AUnitBase*> Squad = GetSquad();
	if (Squad.IsValidIndex(SlotIndex))
	{
		SelectUnit(Squad[SlotIndex]);
	}
}

void ATacticalPlayerController::SelectNextUnit()
{
	// Tab в режиме прицеливания листает ЦЕЛИ, а не бойцов (XCOM): игрок выбирает,
	// в кого стрелять, а не меняет активного юнита посреди прицеливания.
	if (IsTargetingAttack())
	{
		CycleAttackTarget(1);
		return;
	}

	if (SelectedUnit && IsUnitActionInProgress(SelectedUnit))
	{
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->AbandonShotFraming();
		}
		return;
	}

	const TArray<AUnitBase*> Squad = GetSquad();
	if (Squad.Num() == 0)
	{
		return;
	}

	// Со следующего после текущего — первый ЖИВОЙ юнит с оставшимися AP
	// (GetSquad теперь отдаёт и мёртвых/раненых: их Tab пропускает).
	const int32 StartIndex = FMath::Max(0, Squad.IndexOfByKey(SelectedUnit));
	for (int32 Offset = 1; Offset <= Squad.Num(); ++Offset)
	{
		AUnitBase* Candidate = Squad[(StartIndex + Offset) % Squad.Num()];
		if (Candidate && Candidate != SelectedUnit && UTacticsCombatStatics::IsUnitAlive(Candidate) &&
			IsUnitSelectableByGate(Candidate) &&
			Candidate->GetActionPoints() && Candidate->GetActionPoints()->HasActionsLeft())
		{
			// Tab — пользовательское действие: событие выбора публикуется.
			SelectUnitInternal(Candidate, /*bPlayerInitiated=*/true);
			return;
		}
	}
}

void ATacticalPlayerController::HandleTutorialPolicyChanged()
{
	const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this);
	const bool bNowLock = Gate && Gate->IsGateActive() &&
		Gate->GetActivePolicy().bLockGameplayInput;
	if (Gate && Gate->IsGateActive())
	{
		// Порог «осмотритесь» шага A1 отсчитывается от начала шага: всё, что игрок
		// накрутил камерой во время стриминга и до применения политики, не считается.
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->ReArmCameraAdjustedEvent();
		}

		if (Gate->RequiresExplicitUnitSelection() && SelectedUnit)
		{
			// Шаг ждёт выбор именно от игрока — снимаем ЛЮБОЙ уже сделанный выбор.
			// Даже если бойца успел выбрать сам игрок, его событие ушло ДО входа шага
			// и никем не услышано, а клик по уже выбранному события не публикует.
			SelectUnitInternal(nullptr, /*bPlayerInitiated=*/false);
		}
	}

	// Конец постановки (lock → не-lock): камера возвращается игроку. Во время
	// lock-шагов автовозвраты подавлены, поэтому этот переход — единственное
	// место, где взгляд штатно отдаётся обратно (Beat нового шага, если он есть,
	// выполнится позже и поставит свой фокус поверх).
	if (bTutorialPolicyWasLock && !bNowLock)
	{
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->ClearFollowTarget();
		}
		if (SelectedUnit)
		{
			if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
			{
				Camera->FocusOnActor(SelectedUnit);
			}
		}
		else
		{
			FocusCameraOnSquad();
		}
	}
	bTutorialPolicyWasLock = bNowLock;
	// Полное снятие политики раннего выхода не получает: маркеры и зона хода
	// обязаны очиститься/перестроиться и в этом случае.

	// Новая политика меняет и доступные действия, и допустимые точки: зона хода
	// была спрятана шагом без Move и сама не перестроится (её обновления привязаны
	// к выбору и тратам AP, а не к политике), кнопки HUD должны пересчитать
	// серость, маркеры точек назначения — перерисоваться.
	RefreshMoveRange();
	RefreshTutorialDestinationMarkers();
	RefreshDownedReviveRings();
	OnAvailableActionsChanged.Broadcast();

	// Шаг мог открыть EndTurn УЖЕ ПОСЛЕ того, как отряд сжёг все ОД
	// (Overwatch/Hunker завершают активацию без движения — прежний триггер
	// автозавершения по финишу бега не срабатывал). Проверка ОТЛОЖЕНА на тик:
	// синхронный EndTurn прямо из каскада входа нового шага (SetActive/Force/
	// программа врага) запускал фазу врага раньше, чем шаг закончил Enter.
	GetWorldTimerManager().SetTimerForNextTick(this,
		&ATacticalPlayerController::TryAutoEndTurn);
}

void ATacticalPlayerController::RefreshDownedReviveRings()
{
	for (const TWeakObjectPtr<UDecalComponent>& Ring : ReviveRingDecals)
	{
		if (Ring.IsValid())
		{
			Ring->DestroyComponent();
		}
	}
	ReviveRingDecals.Reset();

	// Кольцо рисуется, только если выбранный боец умеет поднимать/лечить целью
	// (медик): у остальных подсказка вводила бы в заблуждение.
	const UTacticalAbility* AbilityCDO = SelectedUnit && SelectedUnit->ClassAbilityClass
		? SelectedUnit->ClassAbilityClass->GetDefaultObject<UTacticalAbility>()
		: nullptr;
	if (!AbilityCDO || !AbilityCDO->bRequiresTargetActor ||
		AbilityCDO->GetTargetingRange() <= 0.f ||
		!UTacticsCombatStatics::IsUnitAlive(SelectedUnit))
	{
		return;
	}

	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	const UTacticalHUDStyleData* Theme = GameInstance ? GameInstance->GetUITheme() : nullptr;
	UMaterialInterface* RingMaterial = Theme
		? Theme->TutorialDestinationMarkerMaterial.LoadSynchronous() : nullptr;
	const UTurnManagerSubsystem* TurnManager =
		GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!RingMaterial || !TurnManager)
	{
		return;
	}

	const float Range = AbilityCDO->GetTargetingRange();
	for (AActor* Ally : TurnManager->GetSideUnits(SelectedUnit))
	{
		const AUnitBase* AllyUnit = Cast<AUnitBase>(Ally);
		if (!AllyUnit || !UTacticsCombatStatics::IsUnitDowned(AllyUnit) ||
			!UTacticalScenarioSubsystem::IsActorScenarioActive(AllyUnit))
		{
			continue;
		}
		UDecalComponent* Ring = UGameplayStatics::SpawnDecalAtLocation(
			this, RingMaterial, FVector(200.f, Range, Range),
			AllyUnit->GetActorLocation(), FRotator(-90.f, 0.f, 0.f), 0.f);
		if (Ring)
		{
			Ring->SetFadeScreenSize(0.f);
			ReviveRingDecals.Add(Ring);
		}
	}
}

void ATacticalPlayerController::HandleTutorialDestinationsChanged()
{
	// Точка маршрута пройдена: политика та же, меняются только маркеры и зона.
	RefreshTutorialDestinationMarkers();
	RefreshMoveRange();
	// «Сначала займи позицию» открывает способности ровно в момент прохода
	// точки — без этого сигнала кнопки HUD «загорались со временем», по
	// следующему случайному пересчёту (смена AP/выбора).
	OnAvailableActionsChanged.Broadcast();
}

void ATacticalPlayerController::RefreshTutorialDestinationMarkers()
{
	for (const TWeakObjectPtr<UDecalComponent>& Marker : TutorialDestinationMarkers)
	{
		if (Marker.IsValid())
		{
			Marker->DestroyComponent();
		}
	}
	TutorialDestinationMarkers.Reset();

	const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this);
	if (!Gate || !Gate->IsGateActive())
	{
		return;
	}
	// Только те точки, куда шаг разрешает идти ПРЯМО СЕЙЧАС: пройденные гаснут,
	// при последовательном маршруте открыта ровно одна следующая, а выбранный
	// боец видит лишь свои личные и общие точки (чужие маркеры — шум).
	const TArray<FName> OpenAnchors = Gate->GetOpenDestinationAnchors(SelectedUnit);
	if (OpenAnchors.IsEmpty())
	{
		return;
	}

	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	const UTacticalHUDStyleData* Theme = GameInstance ? GameInstance->GetUITheme() : nullptr;
	UMaterialInterface* MarkerMaterial = Theme
		? Theme->TutorialDestinationMarkerMaterial.LoadSynchronous() : nullptr;
	UTacticalScenarioSubsystem* Registry =
		GetWorld() ? GetWorld()->GetSubsystem<UTacticalScenarioSubsystem>() : nullptr;
	if (!MarkerMaterial || !Registry)
	{
		return;
	}

	// Радиус кольца = допуск проверки клика: игрок видит ровно ту область,
	// в которую разрешено тыкать. Размер регулируется DestinationTolerance шага.
	const float MarkerRadius = FMath::Max(50.f, Gate->GetActivePolicy().DestinationTolerance);
	for (const FName& AnchorId : OpenAnchors)
	{
		for (AActor* Point : Registry->FindScenarioActors(AnchorId))
		{
			if (!Point)
			{
				continue;
			}
			UDecalComponent* Marker = UGameplayStatics::SpawnDecalAtLocation(
				this, MarkerMaterial,
				FVector(200.f, MarkerRadius, MarkerRadius),
				Point->GetActorLocation(), FRotator(-90.f, 0.f, 0.f), /*LifeSpan=*/0.f);
			if (Marker)
			{
				// Маркер шага не должен исчезать при отдалении камеры.
				Marker->SetFadeScreenSize(0.f);
				TutorialDestinationMarkers.Add(Marker);
			}
		}
	}
}

void ATacticalPlayerController::SelectNextAvailableUnit()
{
	// Шаг «выбери такого-то бойца» обязан оставить выбор игроку: автоподстановка
	// сделала бы его сама, а повторный клик по уже выбранному не меняет canonical
	// SelectedUnit и не публикует Unit.Selected — шаг завис бы навсегда.
	if (const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this);
		Gate && Gate->RequiresExplicitUnitSelection())
	{
		// Автовыбор в таком шаге запрещён полностью; выбор самого игрока не трогаем.
		if (SelectedUnit && !bSelectionByPlayer)
		{
			SelectUnitInternal(nullptr, /*bPlayerInitiated=*/false);
		}
		return;
	}

	const TArray<AUnitBase*> Squad = GetSquad();
	const int32 Num = Squad.Num();
	if (Num == 0)
	{
		SelectUnitInternal(nullptr, /*bPlayerInitiated=*/false);
		return;
	}

	// Выбора нет — идём с начала отряда; есть — со следующего за текущим.
	const int32 CurrentIndex = Squad.IndexOfByKey(SelectedUnit);
	AUnitBase* AliveFallback = nullptr;
	for (int32 i = 0; i < Num; ++i)
	{
		const int32 Index = (CurrentIndex == INDEX_NONE) ? i : (CurrentIndex + 1 + i) % Num;
		AUnitBase* Candidate = Squad[Index];
		if (!Candidate || Candidate == SelectedUnit || !UTacticsCombatStatics::IsUnitAlive(Candidate) ||
			!IsUnitSelectableByGate(Candidate))
		{
			continue;
		}
		if (Candidate->GetActionPoints() && Candidate->GetActionPoints()->HasActionsLeft())
		{
			SelectUnitInternal(Candidate, /*bPlayerInitiated=*/false);
			return;
		}
		if (!AliveFallback)
		{
			AliveFallback = Candidate;
		}
	}
	// Шаг обучения, ограничивший выбор, оставляет текущего бойца: снять выбор
	// автоматически означало бы отдать игроку HUD без владельца и без права
	// выбрать нужного юнита заново.
	if (!AliveFallback && SelectedUnit && !IsUnitSelectableByGate(SelectedUnit))
	{
		return;
	}

	// Никого с AP: живой без AP (HUD остаётся на бойце), весь отряд выбит — nullptr.
	SelectUnitInternal(AliveFallback, /*bPlayerInitiated=*/false);
}

TArray<AUnitBase*> ATacticalPlayerController::GetSquad() const
{
	// Отряд = сторона игрока «как есть», включая мёртвых/раненых/эвакуированных:
	// портреты HUD стабильны, слоты 1–4 не переиндексируются при потерях,
	// заскриптованный Downed-союзник туториала получает портрет. Живость
	// фильтруют места использования (SelectUnit, SelectNextUnit).
	TArray<AUnitBase*> Squad;
	const UTurnManagerSubsystem* TurnManager = GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (TurnManager)
	{
		for (AActor* Unit : TurnManager->GetPlayerSideUnits())
		{
			if (AUnitBase* UnitBase = Cast<AUnitBase>(Unit))
			{
				Squad.Add(UnitBase);
			}
		}
	}
	return Squad;
}

// --- Клики ----------------------------------------------------------------------

bool ATacticalPlayerController::TraceUnderCursor(FHitResult& OutHit) const
{
	ATacticalPlayerController* MutableThis = const_cast<ATacticalPlayerController*>(this);

	const bool bPawnHit = MutableThis->GetHitResultUnderCursor(ECC_Pawn, /*bTraceComplex=*/false, OutHit);
	if (bPawnHit && Cast<AUnitBase>(OutHit.GetActor()))
	{
		return true; // живой юнит — попали сразу
	}

	// Промах по юниту (или упёрлись в пол за ним) — пробуем канал, на котором
	// павшие остаются блокирующими.
	FHitResult VisibilityHit;
	if (MutableThis->GetHitResultUnderCursor(ECC_Visibility, false, VisibilityHit))
	{
		// Хит по Pawn-каналу, который НЕ юнит, — это посторонний коллайдер
		// (например, бокс сценарной зоны). Отдавать его наружу нельзя: клик по
		// бойцу внутри зоны возвращал бы зону и все целевые действия срывались.
		OutHit = VisibilityHit;
		return true;
	}
	return bPawnHit && Cast<AUnitBase>(OutHit.GetActor()) != nullptr;
}

void ATacticalPlayerController::HandleSelectPressed()
{
	FHitResult Hit;
	TraceUnderCursor(Hit);

	AActor* Clicked = Hit.GetActor();
	if (!Clicked)
	{
		return;
	}

	// Режим выбора цели способности (медик): клик решает. Единый закон с
	// атакой: ЛКМ мимо юнитов выходит из режима, неверный боец показывает
	// причину отказа (targeting остаётся — можно кликнуть другого).
	if (IsTargetingAbility())
	{
		if (!Cast<AUnitBase>(Clicked))
		{
			CancelTargeting();
			return;
		}
		HandleAbilityTargetClick(Clicked);
		return;
	}

	// Режим прицеливания: ЛКМ по УЖЕ ВЫБРАННОЙ цели — выстрел (её камера уже
	// показала — подтверждение осознанное, как второй пробел); ЛКМ по ДРУГОМУ
	// достижимому врагу — переводит прицел на него (камера наводится, не
	// стреляет); клик мимо врагов — выход из режима.
	if (IsTargetingAttack())
	{
		if (HandleAttackTargetClick(Clicked))
		{
			return;
		}
		CancelTargeting();
	}

	if (AUnitBase* ClickedUnit = Cast<AUnitBase>(Clicked))
	{
		const bool bOwnUnit =
			ClickedUnit->GetGenericTeamId().GetId() == TacticsTeamIds::Player;
		if (bOwnUnit && !ClickedUnit->IsEvacuated() && !ClickedUnit->IsDead())
		{
			SelectUnit(ClickedUnit);
		}
		// Клик по врагу БЕЗ вооружённого режима прицеливания (кнопка «Огонь») —
		// намеренно ничего не делает (XCOM-правило, GDD §6): стреляем только
		// через явное «Огонь» → клик по цели. Прогноз (шанс/причина отказа)
		// всё равно виден по ховеру — UpdateTargetPanel не привязан к режиму.
	}
	else if (!bAutoSelectUnits)
	{
		// Ручной режим (автовыбор выключен) — ЛКМ по пустому месту снимает выбор.
		// В XCOM-режиме выбор не снимаем: активный боец есть всегда, иначе
		// случайный клик мимо юнита гасил бы панель действий и зону хода.
		SelectUnit(nullptr);
	}
}

bool ATacticalPlayerController::HandleAttackTargetClick(AActor* ClickedActor)
{
	AUnitBase* ClickedEnemy = Cast<AUnitBase>(ClickedActor);
	if (!ClickedEnemy ||
		ClickedEnemy->GetGenericTeamId().GetId() != TacticsTeamIds::Enemy)
	{
		return false;
	}

	if (ClickedEnemy == CurrentAttackTarget.Get())
	{
		ConfirmAttack();
	}
	else if (UGA_Attack::CanTargetActor(SelectedUnit, ClickedEnemy))
	{
		SetAttackTarget(ClickedEnemy);
	}
	return true;
}

void ATacticalPlayerController::HandleCommandPressed()
{
	// В режиме прицеливания ПКМ = отмена (XCOM), а не приказ на движение:
	// иначе игрок, передумав стрелять, случайно гнал бы бойца под клик.
	if (IsTargetingAttack() || IsTargetingAbility())
	{
		CancelTargeting();
		return;
	}

	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit)
	{
		TryMoveSelectedUnit(Hit.Location);
	}
}

// --- Приказы ----------------------------------------------------------------------

bool ATacticalPlayerController::IsPlayerPhase() const
{
	const UTurnManagerSubsystem* TurnManager = GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	return TurnManager && TurnManager->IsInCombat() && TurnManager->GetCurrentPhase() == ETurnPhase::Player;
}

bool ATacticalPlayerController::CanIssueCommand(ETacticalPlayerCommand Command) const
{
	// Общий инвариант одной тактической активации. Он живёт здесь один раз и
	// используется как Request*-методами, так и HUD.
	if (bReactionPlaying || !IsPlayerPhase() || !SelectedUnit || SelectedUnit->IsDead() ||
		SelectedUnit->IsDowned() || SelectedUnit->IsEvacuated() ||
		IsSelectedUnitMoving() || IsUnitActionInProgress(SelectedUnit))
	{
		return false;
	}

	// Targeting — модальный режим. Разрешено только подтвердить ТО действие,
	// для которого режим был открыт; все конкурирующие команды ждут ПКМ/Esc.
	const bool bConfirmsCurrentTargeting =
		(Command == ETacticalPlayerCommand::Attack &&
			TargetingMode == EPlayerTargetingMode::Attack) ||
		(Command == ETacticalPlayerCommand::ClassAbility &&
			TargetingMode == EPlayerTargetingMode::Ability);
	if (TargetingMode != EPlayerTargetingMode::None && !bConfirmsCurrentTargeting)
	{
		return false;
	}

	// Action Gate обучения — тот же единственный арбитр и для hotkey, и для
	// серости кнопок HUD: отказ происходит ДО траты AP, montage и quest-события.
	if (!UTutorialActionGateSubsystem::AllowsAction(
			this, TacticalCommandToTutorialAction(Command), SelectedUnit))
	{
		return false;
	}

	const UActionPointsComponent* ActionPoints = SelectedUnit->GetActionPoints();

	// Блок выполняющейся тактической GA относится ко ВСЕМ приказам, а не только
	// к запуску следующей способности: нельзя начать Move/Interact/Skip, пока
	// текущая GA ещё не завершила свой lifecycle.
	if (const UAbilitySystemComponent* ASC = SelectedUnit->GetAbilitySystemComponent())
	{
		FGameplayTagContainer ActionTags;
		ActionTags.AddTag(TacticsGameplayTags::Ability_TacticalAction);
		if (ASC->AreAbilityTagsBlocked(ActionTags))
		{
			return false;
		}
	}

	auto CanUseAbility = [this, ActionPoints](
		const TSubclassOf<UTacticalAbility>& AbilityClass)
	{
		if (!AbilityClass)
		{
			return false;
		}

		const UTacticalAbility* AbilityCDO =
			AbilityClass->GetDefaultObject<UTacticalAbility>();
		if (!AbilityCDO ||
			(AbilityCDO->ActionPointCost > 0 &&
				(!ActionPoints || !ActionPoints->CanSpend(AbilityCDO->ActionPointCost))) ||
			SelectedUnit->GetAbilityUsesRemaining(AbilityClass) == 0)
		{
			return false;
		}

		return true;
	};

	switch (Command)
	{
	case ETacticalPlayerCommand::Move:
	case ETacticalPlayerCommand::SkipUnitTurn:
		return ActionPoints && ActionPoints->HasActionsLeft();

	case ETacticalPlayerCommand::Attack:
		return CanUseAbility(SelectedUnit->AttackAbilityClass) &&
			UGA_Attack::HasAnyValidTarget(SelectedUnit);

	case ETacticalPlayerCommand::Overwatch:
		return CanUseAbility(SelectedUnit->OverwatchAbilityClass);

	case ETacticalPlayerCommand::HunkerDown:
		// Ф7: глухая оборона требует укрытия — серим кнопку без укрытия, чтобы
		// игрок не сжёг AP впустую. То же условие в UGA_HunkerDown::CanActivateAbility.
		return CanUseAbility(SelectedUnit->HunkerAbilityClass) &&
			SelectedUnit->GetCoverDetection() &&
			SelectedUnit->GetCoverDetection()->BestCoverAround != ECoverType::None;

	case ETacticalPlayerCommand::ClassAbility:
		return CanUseAbility(SelectedUnit->ClassAbilityClass);

	case ETacticalPlayerCommand::Interact:
	{
		ABombObjective* Bomb = nullptr;
		AEvacZone* Zone = nullptr;
		return FindAvailableInteraction(Bomb, Zone) != EInteractionKind::None;
	}
	default:
		return false;
	}
}

bool ATacticalPlayerController::IsEnemyPhaseNow() const
{
	const UTurnManagerSubsystem* TurnManager = GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	return TurnManager && TurnManager->IsInCombat() && TurnManager->GetCurrentPhase() != ETurnPhase::Player;
}

void ATacticalPlayerController::RefreshSelectionHighlight()
{
	// Кольцо видно только в свою фазу (вне боя — тоже): в ход врага прячем,
	// чтобы не загромождать картину. Ховер-обводка живёт отдельно.
	if (SelectedUnit)
	{
		SelectedUnit->SetSelectionHighlight(!IsEnemyPhaseNow());
	}
}

void ATacticalPlayerController::TryMoveSelectedUnit(const FVector& Goal)
{
	if (!CanIssueCommand(ETacticalPlayerCommand::Move))
	{
		return;
	}
	UActionPointsComponent* ActionPoints = SelectedUnit->GetActionPoints();
	if (!ActionPoints)
	{
		return;
	}

	// Шаг обучения может разрешать только определённые точки маршрута. Проверка
	// стоит до PlanMoveTo: отказ не должен ни строить путь, ни трогать камеру.
	if (const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this))
	{
		if (!Gate->IsDestinationAllowed(Goal, SelectedUnit))
		{
			NotifyCommandDenied();
			return;
		}
	}

	// ЕДИНЫЙ план приказа — тот же вызов, которым нарисованы зона и лента превью.
	// Он же приводит цель к полю: проецирует на навмеш и выталкивает из дисков
	// занятости. Поэтому «что подсвечено — то и кликается, и туда же побежим»
	// верно ПО ПОСТРОЕНИЮ: сравнивать нечего, результат буквально один.
	//
	// Метрика плана — волна, огибающая диски юнитов, а не прямой navmesh-запрос:
	// прямой не отличал «одиночного бойца обойдём» от «коридор перекрыт тремя».
	// Каждый отрезок маршрута проверен рэйкастом, поэтому длина плана — верхняя
	// оценка кратчайшего пути: пробежать БОЛЬШЕ обещанного боец не может.
	if (!MoveRangeVisualizer || !MoveRangeVisualizer->IsFieldBuiltFor(SelectedUnit))
	{
		return;
	}
	FMoveOrderPlan Plan;
	if (!MoveRangeVisualizer->PlanMoveTo(Goal, Plan) || !ActionPoints->CanSpend(Plan.ActionPointCost))
	{
		return; // недостижимо по занятости / вне оплачиваемой зоны
	}

	// Маршрут обучения идёт по отрезкам: рывок за 2 AP «через точку» даёт ОДНО
	// событие Settled вместо двух, счётчик шага замирает на 1/2, а End Turn в
	// таком шаге запрещён — получается тупик без выхода. Поэтому в шаге с
	// точками назначения каждый приказ стоит ровно 1 AP.
	if (Plan.ActionPointCost > 1)
	{
		if (const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this);
			Gate && Gate->IsGateActive() &&
			!Gate->GetActivePolicy().AllowedDestinationAnchors.IsEmpty())
		{
			NotifyCommandDenied();
			return;
		}
	}

	AUnitAIController* UnitAI = Cast<AUnitAIController>(SelectedUnit->GetController());
	if (!UnitAI)
	{
		return;
	}

	// Ведём бойца ПО ЛОМАНОЙ плана, а не «в точку»: приказ в конечную цель
	// заставлял навмеш строить свою прямую — сквозь стоящих бойцов, которых
	// он не видит. Боец упирался в них, а очко действия уже было списано.
	// Радиус приёмки финала — 10 см, т.е. практически «ровно в точку клика».
	// Прежние 50 см и были причиной «недобега»: path following считает цель
	// достигнутой, как только центр бойца вошёл в этот радиус, — боец замирал в
	// полуметре от курсора и не мог прижаться к укрытию. Ехать в саму точку
	// безопасно: цель уже спроецирована на навмеш (PlanMoveTo), а навмеш отступает
	// от стен на радиус агента — значит точка заведомо стояблая. Ноль не ставим:
	// небольшой допуск гасит «подползание» на торможении CharacterMovement, а
	// сорвавшийся финальный отрезок и так разбирается штатно в OnMoveCompleted.
	if (UnitAI->MoveAlongRoute(Plan.PathPoints, /*AcceptanceRadius=*/10.f) == EPathFollowingRequestResult::RequestSuccessful)
	{
		// AP списываем сразу (как XCOM), укрытие пересчитается в OnMoveCompleted.
		ActionPoints->TrySpendActionPoint(Plan.ActionPointCost);

		// Токен приказа игрока: quest-событие перемещения публикует ЕДИНСТВЕННАЯ
		// финализация settlement, и только для приказа, а не для служебного
		// подшага стрелка или маршрута AI. Точка приказа гасит точку шага.
		UnitAI->MarkPlayerOrderedMove(Plan.PathPoints.Last());
		SelectedUnit->PlayUnitSound(EUnitSoundEvent::MoveStart);

		// Камера сопровождает бегущего бойца (follow отпустится по остановке
		// в PlayerTick или по ручной панораме игрока).
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->SetFollowTarget(SelectedUnit);
		}
	}
}

bool ATacticalPlayerController::PlanMoveForUnit(AUnitBase* Unit, const FVector& Goal,
	int32 MaxActionPoints, FMoveOrderPlan& OutPlan)
{
	return MoveRangeVisualizer &&
		MoveRangeVisualizer->PlanMoveForUnit(Unit, Goal, MaxActionPoints, OutPlan);
}

void ATacticalPlayerController::TryAttackTarget(AActor* Target)
{
	if (!Target || !CanIssueCommand(ETacticalPlayerCommand::Attack) ||
		!UGA_Attack::CanTargetActor(SelectedUnit, Target))
	{
		return;
	}
	// Шаг A8/B5/C2 требует конкретную голограмму: чужая цель отклоняется до
	// отправки Event.Attack, поэтому AP и montage не расходуются.
	if (const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this))
	{
		if (!Gate->IsTargetAllowed(Target))
		{
			NotifyCommandDenied();
			return;
		}
	}

	// Событие Event.Attack: GA_Attack сама валидирует дальность/LOS и платит AP.
	FGameplayEventData Payload;
	Payload.Instigator = SelectedUnit;
	Payload.Target = Target;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		SelectedUnit, TacticsGameplayTags::Event_Attack, Payload);
}

void ATacticalPlayerController::HandleAbilityTargetClick(AActor* ClickedActor)
{
	if (TargetingMode != EPlayerTargetingMode::Ability || !ClickedActor ||
		!CanIssueCommand(ETacticalPlayerCommand::ClassAbility))
	{
		return;
	}

	AUnitBase* ActingUnit = SelectedUnit;
	const UTacticalAbility* AbilityCDO = ActingUnit && ActingUnit->ClassAbilityClass
		? ActingUnit->ClassAbilityClass->GetDefaultObject<UTacticalAbility>()
		: nullptr;
	const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this);
	if (!AbilityCDO || !AbilityCDO->bRequiresTargetActor ||
		!AbilityCDO->TargetedActivationEventTag.IsValid() ||
		!AbilityCDO->IsValidTargetActor(ActingUnit, ClickedActor) ||
		(Gate && !Gate->IsTargetAllowed(ClickedActor)))
	{
		// Невалидный клик не закрывает targeting: игрок может выбрать другую
		// цель. Но «ничего не произошло» неотличимо от зависания — причина
		// отказа обязана дойти до игрока (оверлей, 3 секунды + звук отказа).
		if (AUnitBase* ClickedUnit = Cast<AUnitBase>(ClickedActor))
		{
			FText Reason;
			const float Range = AbilityCDO ? AbilityCDO->GetTargetingRange() : 0.f;
			if (Gate && !Gate->IsTargetAllowed(ClickedUnit))
			{
				Reason = Gate->GetDenialReason();
			}
			else if (Range > 0.f && ActingUnit &&
				FVector::Dist(ActingUnit->GetActorLocation(),
					ClickedUnit->GetActorLocation()) > Range)
			{
				Reason = FText::Format(
					INVTEXT("Слишком далеко: подойдите вплотную (радиус {0} м)"),
					FText::AsNumber(FMath::RoundToInt(Range / 100.f)));
			}
			else if (!UTacticsCombatStatics::IsUnitDowned(ClickedUnit) &&
				ClickedUnit->GetHealth() >= ClickedUnit->GetMaxHealth() - KINDA_SMALL_NUMBER)
			{
				Reason = INVTEXT("Боец не ранен");
			}
			else
			{
				Reason = INVTEXT("Эту цель выбрать нельзя");
			}
			LastDenialReason = Reason;
			LastDenialTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			if (UTacticsAudioSubsystem* Audio = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
			{
				Audio->PlayUIDenied();
			}
		}
		UE_LOG(LogXRU1Combat, Display,
			TEXT("[Ability] Цель %s отклонена: cdo=%d target-механика=%d gate=%d"),
			*GetNameSafe(ClickedActor), AbilityCDO ? 1 : 0,
			AbilityCDO && AbilityCDO->IsValidTargetActor(ActingUnit, ClickedActor) ? 1 : 0,
			!Gate || Gate->IsTargetAllowed(ClickedActor) ? 1 : 0);
		return;
	}

	FGameplayEventData Payload;
	Payload.Instigator = ActingUnit;
	Payload.Target = ClickedActor;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		ActingUnit, AbilityCDO->TargetedActivationEventTag, Payload);

	// Сначала отправляем событие зафиксированному юниту, потом броадкастим смену
	// UI-режима. Так Blueprint-слушатель SetTargetingMode не может подменить
	// SelectedUnit между проверкой и фактическим выполнением команды.
	SetTargetingMode(EPlayerTargetingMode::None);
}

void ATacticalPlayerController::RequestEndTurn()
{
	if (bReactionPlaying)
	{
		return;
	}

	// Enter завершает ход и НЕ подтверждает выстрел (по просьбе: подтверждение —
	// только тем же пробелом «Огонь» или кликом по цели). В режиме прицеливания
	// Enter сперва выходит из него — иначе завершил бы ход мимо намерения.
	if (IsTargetingAttack() || IsTargetingAbility())
	{
		CancelTargeting();
		return;
	}

	// Конец хода — самостоятельное действие обучения (шаги A3 и D1): пока шаг его
	// не разрешил, Enter и кнопка HUD не должны передавать ход врагу.
	if (!UTutorialActionGateSubsystem::AllowsAction(this, ETutorialAction::EndTurn, SelectedUnit))
	{
		return;
	}

	// Во время исполнения приказа (бег/осадка любого бойца) ход не передаётся:
	// иначе смена фазы обрывала бы движение и ломала зачёт точки маршрута.
	for (const AUnitBase* Unit : GetSquad())
	{
		if (UTacticsCombatStatics::IsUnitInTransit(Unit))
		{
			NotifyCommandDenied();
			return;
		}
	}

	if (IsPlayerPhase() && !IsAnySquadActionInProgress())
	{
		if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->EndTurn();
		}
	}
}

void ATacticalPlayerController::RequestAttack()
{
	// Пробел / кнопка «Огонь» — ДВУХТАКТ, ровно как в XCOM 2: первое нажатие
	// входит в прицеливание (камера кадром «из-за плеча» СРАЗУ показывает цель —
	// стрелять вслепую нельзя, видно по кому), второе нажатие ТЕМ ЖЕ пробелом
	// подтверждает выстрел по взятой цели. Выход из режима — ПКМ/Esc.
	if (!CanIssueCommand(ETacticalPlayerCommand::Attack))
	{
		return;
	}
	if (IsTargetingAttack())
	{
		ConfirmAttack();
		return;
	}

	BeginAttackTargeting();
}

// --- Единый источник правды: режим взаимодействия ----------------------------------

void ATacticalPlayerController::SetTargetingMode(EPlayerTargetingMode NewMode)
{
	if (TargetingMode == NewMode)
	{
		return;
	}
	const EPlayerTargetingMode OldMode = TargetingMode;
	UE_LOG(LogXRU1Camera, Display, TEXT("[Targeting] %d → %d (0=None 1=Attack 2=Ability) unit=%s"),
		static_cast<int32>(OldMode), static_cast<int32>(NewMode), *GetNameSafe(SelectedUnit));
	ExitTargetingMode(OldMode);
	TargetingMode = NewMode;
	EnterTargetingMode(NewMode);

	// Зона/превью движения подчиняются тому же модальному арбитру: в targeting
	// прячутся, после отмены строятся снова.
	RefreshMoveRange();

	// HUD один раз на переход: баннер прицела, серость кнопок, панель цели.
	OnAvailableActionsChanged.Broadcast();
}

void ATacticalPlayerController::ExitTargetingMode(EPlayerTargetingMode OldMode)
{
	if (OldMode == EPlayerTargetingMode::Attack)
	{
		// Снять подсветку взятой цели.
		ClearAttackTarget();

		// Вернуть камеру — но ТОЛЬКО если она держит кадр ПРИЦЕЛА. Если сейчас
		// играет кадр ВЫСТРЕЛА (выход из-за подтверждения), не трогаем: он сам
		// вернёт ракурс по своему таймеру. Так один выход закрывает и отмену
		// (камера возвращается), и выстрел (кадр доигрывает) — без дублей.
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			// Кадр ПРЕЗЕНТАЦИИ тоже бессрочный, но принадлежит выстрелу и
			// снимается только его терминалом — выход из прицеливания его
			// трогать не имеет права.
			if (Camera->IsHoldingAimFrame() && !Camera->IsPlayingPresentationFrame())
			{
				Camera->ClearShotFraming();
			}
		}
	}
	if (OldMode == EPlayerTargetingMode::Ability)
	{
		ClearAbilityTargetingVisuals();
	}
	// Оверхед-худы союзников НЕ восстанавливаются здесь: их видимостью владеет
	// один декларативный расчёт в PlayerTick (UpdateSquadOverheadVisibility) —
	// иначе худ выскакивал в кадре выстрела раньше, чем вернулась камера.
}

void ATacticalPlayerController::EnterTargetingMode(EPlayerTargetingMode NewMode)
{
	// Побочные эффекты входа задаются точечно там, где известен контекст:
	// цель атаки берёт BeginAttackTargeting (нужен список целей).
	if (NewMode == EPlayerTargetingMode::Ability)
	{
		BeginAbilityTargetingVisuals();
	}

}

void ATacticalPlayerController::SetSquadOverheadHUDVisible(bool bVisible)
{
	for (AUnitBase* Unit : GetSquad())
	{
		if (!Unit || Unit->IsDead() || Unit->IsEvacuated())
		{
			continue;
		}
		// Downed скрывает шкалу своим состоянием — восстановление не должно
		// включить её обратно у лежащего.
		Unit->SetOverheadHUDVisible(bVisible && !Unit->IsDowned());
	}
}

void ATacticalPlayerController::UpdateSquadOverheadVisibility()
{
	// ЕДИНСТВЕННЫЙ владелец видимости оверхед-худов союзников. Правило
	// декларативно: худы скрыты, пока идёт прицеливание атаки, ЛЮБОЙ кадр
	// камеры (кадр выстрела живёт с Duration=-1 и под IsPlayingShotFrame не
	// попадал — худы выскакивали в момент выстрела) или реакция. Возврат
	// произойдёт ровно при ClearShotFraming терминала — синхронно с камерой.
	const ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn());
	const bool bCameraHoldsFrame = Camera &&
		(Camera->IsPlayingShotFrame() || Camera->IsHoldingAimFrame());
	const bool bWantHidden = TargetingMode == EPlayerTargetingMode::Attack ||
		bCameraHoldsFrame || bReactionPlaying;
	if (bWantHidden != bSquadOverheadHidden)
	{
		UE_LOG(LogXRU1Camera, Display,
			TEXT("[OverheadHUD] %s (targetingAttack=%d cameraFrame=%d reaction=%d)"),
			bWantHidden ? TEXT("скрыт") : TEXT("показан"),
			TargetingMode == EPlayerTargetingMode::Attack ? 1 : 0,
			bCameraHoldsFrame ? 1 : 0, bReactionPlaying ? 1 : 0);
		bSquadOverheadHidden = bWantHidden;
		SetSquadOverheadHUDVisible(!bWantHidden);
		return;
	}

	// Пока правило говорит «скрыто», оно ПЕРЕУТВЕРЖДАЕТСЯ каждый кадр: боец
	// имеет право включить свою шкалу по СОБСТВЕННОМУ состоянию (подняли с
	// земли, воскресили), и без переутверждения она всплыла бы посреди кадра
	// выстрела. Дёшево: SetHiddenInGame сам отсекает повтор того же значения.
	if (bWantHidden)
	{
		SetSquadOverheadHUDVisible(false);
	}
}

void ATacticalPlayerController::BeginAbilityTargetingVisuals()
{
	const UTacticalAbility* AbilityCDO = SelectedUnit && SelectedUnit->ClassAbilityClass
		? SelectedUnit->ClassAbilityClass->GetDefaultObject<UTacticalAbility>()
		: nullptr;
	if (!AbilityCDO)
	{
		return;
	}

	// Круг дальности способности вокруг бойца — игрок видит, кого достаёт,
	// ДО клика (подсказка радиуса аптечки, как в XCOM).
	const float Range = AbilityCDO->GetTargetingRange();
	const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
	const UTacticalHUDStyleData* Theme = GameInstance ? GameInstance->GetUITheme() : nullptr;
	UMaterialInterface* RingMaterial = Theme
		? Theme->TutorialDestinationMarkerMaterial.LoadSynchronous() : nullptr;
	if (Range > 0.f && RingMaterial)
	{
		AbilityRangeDecal = UGameplayStatics::SpawnDecalAtLocation(
			this, RingMaterial, FVector(200.f, Range, Range),
			SelectedUnit->GetActorLocation(), FRotator(-90.f, 0.f, 0.f), 0.f);
		if (AbilityRangeDecal.IsValid())
		{
			AbilityRangeDecal->SetFadeScreenSize(0.f);
		}
	}

	// Подсветка валидных целей кольцом выбора: медик сразу видит, кого может
	// лечить/поднять. Gate обучения сужает список так же, как сузит и клик.
	const UTutorialActionGateSubsystem* Gate = UTutorialActionGateSubsystem::Get(this);
	const UTurnManagerSubsystem* TurnManager =
		GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return;
	}
	for (AActor* Ally : TurnManager->GetSideUnits(SelectedUnit))
	{
		AUnitBase* AllyUnit = Cast<AUnitBase>(Ally);
		if (!AllyUnit || AllyUnit == SelectedUnit ||
			!AbilityCDO->IsValidTargetActor(SelectedUnit, AllyUnit) ||
			(Gate && !Gate->IsTargetAllowed(AllyUnit)))
		{
			continue;
		}
		AllyUnit->SetSelectionHighlight(true);
		AbilityHighlightedTargets.Add(AllyUnit);
	}
}

void ATacticalPlayerController::ClearAbilityTargetingVisuals()
{
	if (AbilityRangeDecal.IsValid())
	{
		AbilityRangeDecal->DestroyComponent();
	}
	AbilityRangeDecal.Reset();

	for (const TWeakObjectPtr<AUnitBase>& Target : AbilityHighlightedTargets)
	{
		// Выбранного бойца не гасим: его кольцо принадлежит выбору, а не режиму.
		if (Target.IsValid() && Target.Get() != SelectedUnit)
		{
			Target->SetSelectionHighlight(false);
		}
	}
	AbilityHighlightedTargets.Reset();
}

// --- Прицеливание по-XCOM'овски ----------------------------------------------------

TArray<AUnitBase*> ATacticalPlayerController::GetAttackTargets() const
{
	TArray<AUnitBase*> Targets;
	if (!SelectedUnit)
	{
		return Targets;
	}

	const UTurnManagerSubsystem* TurnManager = GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return Targets;
	}

	for (AActor* Enemy : TurnManager->GetOpposingUnits(SelectedUnit))
	{
		AUnitBase* EnemyUnit = Cast<AUnitBase>(Enemy);
		if (EnemyUnit && UGA_Attack::CanTargetActor(SelectedUnit, EnemyUnit))
		{
			Targets.Add(EnemyUnit);
		}
	}

	// По дальности от стрелка — стабильный, читаемый порядок обхода Tab'ом.
	const FVector Origin = SelectedUnit->GetActorLocation();
	Targets.Sort([Origin](const AUnitBase& A, const AUnitBase& B)
	{
		return FVector::DistSquared(Origin, A.GetActorLocation()) <
			FVector::DistSquared(Origin, B.GetActorLocation());
	});
	return Targets;
}

bool ATacticalPlayerController::BeginAttackTargeting()
{
	const TArray<AUnitBase*> Targets = GetAttackTargets();
	if (Targets.Num() == 0)
	{
		return false;
	}

	SetTargetingMode(EPlayerTargetingMode::Attack);

	// На прицел — ближайшую к курсору цель, если курсор на достижимом враге,
	// иначе первую (ближайшую к стрелку): игрок часто уже навёлся на нужного.
	AUnitBase* Initial = Targets[0];
	if (AUnitBase* Hovered = HoveredUnit.Get())
	{
		if (Targets.Contains(Hovered))
		{
			Initial = Hovered;
		}
	}
	SetAttackTarget(Initial); // сам броадкастит панель цели
	return true;
}

void ATacticalPlayerController::SetAttackTarget(AUnitBase* Target)
{
	// Подсветка цели (XCOM): взятый на прицел враг горит той же обводкой, что и
	// при наведении, и не гаснет, когда курсор уходит (UpdateHoverHighlight его
	// пропускает). Прежняя цель гаснет, если только над ней не курсор.
	if (AUnitBase* Previous = CurrentAttackTarget.Get())
	{
		if (Previous != Target && Previous != HoveredUnit.Get())
		{
			Previous->SetHoverHighlight(false);
		}
	}

	CurrentAttackTarget = Target;
	if (Target)
	{
		Target->SetHoverHighlight(true);

		// В открытом поле превью плавно доворачивает стрелка. В укрытии actor
		// сохраняет единую cover-позу: cycling меняет цель/camera, но не запускает
		// конкурирующий turn поверх прижимания и peek-анимации.
		if (SelectedUnit)
		{
			SelectedUnit->PreviewAimAtTarget(Target);
		}

		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->FrameShot(SelectedUnit, Target); // кадр держится, пока целимся
		}
	}
	OnAvailableActionsChanged.Broadcast(); // HUD пересчитает панель цели
}

void ATacticalPlayerController::ClearAttackTarget()
{
	// Снять прицел и его подсветку (кроме случая, когда над целью курсор —
	// тогда подсветку держит обычный ховер).
	if (AUnitBase* Target = CurrentAttackTarget.Get())
	{
		if (Target != HoveredUnit.Get())
		{
			Target->SetHoverHighlight(false);
		}
	}
	CurrentAttackTarget = nullptr;
}

void ATacticalPlayerController::CycleAttackTarget(int32 Direction)
{
	if (!IsTargetingAttack())
	{
		return;
	}
	const TArray<AUnitBase*> Targets = GetAttackTargets();
	if (Targets.Num() == 0)
	{
		CancelTargeting();
		return;
	}

	const int32 CurrentIndex = Targets.IndexOfByKey(CurrentAttackTarget.Get());
	const int32 Step = (Direction >= 0) ? 1 : -1;
	// Если текущей цели в списке нет (умерла/ушла из LOS) — начинаем с 0.
	const int32 Base = (CurrentIndex == INDEX_NONE) ? 0 : CurrentIndex + Step;
	const int32 NextIndex = ((Base % Targets.Num()) + Targets.Num()) % Targets.Num();
	SetAttackTarget(Targets[NextIndex]);
}

void ATacticalPlayerController::CancelTargeting()
{
	// Просто выход в обычный режим — весь откат (камера возвращается, подсветка
	// и баннер гаснут) делает ExitTargetingMode. Отдельной логики отмены больше
	// нет: один путь выхода, ничего не забудется.
	SetTargetingMode(EPlayerTargetingMode::None);
}

void ATacticalPlayerController::ConfirmAttack()
{
	AUnitBase* Target = CurrentAttackTarget.Get();
	if (TargetingMode != EPlayerTargetingMode::Attack || !Target ||
		!CanIssueCommand(ETacticalPlayerCommand::Attack))
	{
		return;
	}
	// Цель могла выйти из зоны поражения, пока целились (сдвиг мира скриптом).
	if (!UGA_Attack::CanTargetActor(SelectedUnit, Target))
	{
		return;
	}

	// Сначала закрываем aim-mode, затем запускаем GA: иначе новый action-frame
	// выстрела стирается выходом из прицеливания в тот же input callback.
	SetTargetingMode(EPlayerTargetingMode::None);
	TryAttackTarget(Target);
}

void ATacticalPlayerController::NotifyShotFired(AActor* Shooter, AActor* Target)
{
	UE_LOG(LogXRU1Camera, Display, TEXT("[Shot] Презентация выстрела: %s → %s (кадр до терминала)"),
		*GetNameSafe(Shooter), *GetNameSafe(Target));
	// Кадр обычного выстрела живёт до terminal callback fire-action: montage и
	// ReturnToAnchor, а не произвольный fixed timer, владеют его длительностью.
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->FrameShotForDuration(Shooter, Target, /*Duration=*/-1.f);
	}
}

bool ATacticalPlayerController::TryBeginReactionShot(AActor* Shooter, AActor* Target)
{
	if (bReactionPlaying)
	{
		return false; // очередь: второй наблюдатель отработает следующим опросом
	}
	UWorld* World = GetWorld();
	if (!World || !Shooter || !Target)
	{
		return false;
	}

	bReactionPlaying = true;
	TimeDilationBeforeReaction = UGameplayStatics::GetGlobalTimeDilation(World);
	UE_LOG(LogXRU1Camera, Display,
		TEXT("[Reaction] Окно реакции ОТКРЫТО: %s → %s, slow-mo %.2f (было %.2f)"),
		*GetNameSafe(Shooter), *GetNameSafe(Target),
		ReactionFireSloMoRate, TimeDilationBeforeReaction);

	// Пауза mover принадлежит UGA_Overwatch reaction subaction. Контроллер
	// владеет только camera/slow-mo presentation и держит кадр БЕЗ таймера до
	// terminal callback фактически запущенного montage. Кадр ПРЕЗЕНТАЦИИ (не
	// прицеливания): follow бегущего врага не имеет права его снять.
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->FrameShotForDuration(Shooter, Target, /*Duration=*/-1.f);
	}

	// Замедление мира снимает только Complete/Abort/End reaction-action.
	if (ReactionFireSloMoRate < 1.f)
	{
		UGameplayStatics::SetGlobalTimeDilation(World, ReactionFireSloMoRate);
	}
	return true;
}

void ATacticalPlayerController::EndReactionWindow()
{
	if (!bReactionPlaying)
	{
		return;
	}
	bReactionPlaying = false;
	UE_LOG(LogXRU1Camera, Display,
		TEXT("[Reaction] Окно реакции ЗАКРЫТО: dilation → %.2f, кадр снимается"),
		TimeDilationBeforeReaction);

	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, TimeDilationBeforeReaction);
	}
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->ClearShotFraming();
	}

	// Lethal Overwatch мог синхронно вызвать HandleSelectedUnitStateChanged, пока
	// barrier ещё был поднят. После закрытия окна повторяем только reconciliation
	// состояния (пользовательские клики, отклонённые во время реакции, не очередим).
	if (SelectedUnit && (SelectedUnit->IsDead() || SelectedUnit->IsEvacuated()))
	{
		HandleSelectedUnitStateChanged();
	}
}

void ATacticalPlayerController::EndShotPresentation()
{
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->ClearShotFraming();
	}
}

void ATacticalPlayerController::EndReactionShotPresentation()
{
	EndReactionWindow();
}


void ATacticalPlayerController::RequestOverwatch()
{
	if (!CanIssueCommand(ETacticalPlayerCommand::Overwatch))
	{
		NotifyCommandDenied();
		return;
	}
	if (UAbilitySystemComponent* ASC = SelectedUnit->GetAbilitySystemComponent())
	{
		ASC->TryActivateAbilityByClass(SelectedUnit->OverwatchAbilityClass);
	}
}

void ATacticalPlayerController::RequestHunkerDown()
{
	if (!CanIssueCommand(ETacticalPlayerCommand::HunkerDown))
	{
		NotifyCommandDenied();
		return;
	}
	if (UAbilitySystemComponent* ASC = SelectedUnit->GetAbilitySystemComponent())
	{
		ASC->TryActivateAbilityByClass(SelectedUnit->HunkerAbilityClass);
	}
}

void ATacticalPlayerController::RequestClassAbility()
{
	if (!CanIssueCommand(ETacticalPlayerCommand::ClassAbility))
	{
		UE_LOG(LogXRU1Combat, Display,
			TEXT("[Ability] Запрос отклонён CanIssueCommand (unit=%s, gate=%d, targeting=%d)"),
			*GetNameSafe(SelectedUnit),
			UTutorialActionGateSubsystem::AllowsAction(
				this, ETutorialAction::ClassAbility, SelectedUnit) ? 1 : 0,
			static_cast<int32>(TargetingMode));
		return;
	}

	// Способности с целью (медик) — режим выбора: следующий ЛКМ по союзнику.
	// AP/заряды/взаимоисключение уже проверил общий арбитр.
	const UTacticalAbility* AbilityCDO = SelectedUnit->ClassAbilityClass->GetDefaultObject<UTacticalAbility>();
	if (AbilityCDO && AbilityCDO->bRequiresTargetActor)
	{
		// Targeted-способность обязана объявить свой Gameplay Event в своём CDO:
		// контроллер больше не знает ни UGA_Heal, ни Event.Heal.
		if (AbilityCDO->TargetedActivationEventTag.IsValid())
		{
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Ability] %s: вход в режим выбора цели — кликните союзника"),
				*GetNameSafe(SelectedUnit));
			SetTargetingMode(EPlayerTargetingMode::Ability);
		}
		return;
	}

	if (UAbilitySystemComponent* ASC = SelectedUnit->GetAbilitySystemComponent())
	{
		ASC->TryActivateAbilityByClass(SelectedUnit->ClassAbilityClass);
	}
}

void ATacticalPlayerController::RequestSkipUnitTurn()
{
	if (!CanIssueCommand(ETacticalPlayerCommand::SkipUnitTurn))
	{
		return;
	}
	if (UActionPointsComponent* ActionPoints = SelectedUnit->GetActionPoints())
	{
		ActionPoints->SpendAllRemaining();
	}
}

EInteractionKind ATacticalPlayerController::FindAvailableInteraction(
	ABombObjective*& OutBomb, AEvacZone*& OutZone) const
{
	OutBomb = nullptr;
	OutZone = nullptr;

	if (!IsPlayerPhase() || !SelectedUnit || SelectedUnit->IsDowned())
	{
		return EInteractionKind::None;
	}

	for (TActorIterator<ABombObjective> It(GetWorld()); It; ++It)
	{
		if (It->CanDefuse(SelectedUnit))
		{
			OutBomb = *It;
			return EInteractionKind::DefuseBomb;
		}
	}
	for (TActorIterator<AEvacZone> It(GetWorld()); It; ++It)
	{
		if (It->CanEvacuate(SelectedUnit))
		{
			OutZone = *It;
			return EInteractionKind::Evacuate;
		}
	}
	return EInteractionKind::None;
}

EInteractionKind ATacticalPlayerController::GetAvailableInteraction() const
{
	ABombObjective* Bomb = nullptr;
	AEvacZone* Zone = nullptr;
	return FindAvailableInteraction(Bomb, Zone);
}

void ATacticalPlayerController::RequestInteract()
{
	if (!CanIssueCommand(ETacticalPlayerCommand::Interact))
	{
		return;
	}

	ABombObjective* Bomb = nullptr;
	AEvacZone* Zone = nullptr;
	switch (FindAvailableInteraction(Bomb, Zone))
	{
	case EInteractionKind::DefuseBomb:
		Bomb->TryDefuse(SelectedUnit);
		break;
	case EInteractionKind::Evacuate:
		// Переход выбора сделает HandleSelectedUnitStateChanged (Evacuate()
		// бросает OnUnitStateChanged) — здесь не дублируем, иначе двойной прыжок.
		// «Evac All» (v2.6, по правилу одноимённого мода XCOM 2): одно нажатие
		// уводит ВСЕХ бойцов, стоящих в зоне и имеющих 1 ОД, а не только
		// выбранного — индивидуальные нажатия каждым были рутиной.
		Zone->TryEvacuateAllInside();
		break;
	default:
		break;
	}
}

void ATacticalPlayerController::RequestPause()
{
	// Esc сперва гасит режим прицеливания (XCOM), и только «в пустоте» — пауза.
	if (IsTargetingAttack() || IsTargetingAbility())
	{
		CancelTargeting();
		return;
	}

	if (!PauseMenuClass)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UGameUIManagerSubsystem* UIManager = GameInstance ? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
	UPrimaryGameLayout* RootLayout = UIManager ? UIManager->GetRootLayout() : nullptr;
	if (RootLayout)
	{
		RootLayout->PushWidgetToLayer(EUILayer::Menu, PauseMenuClass);
		UGameplayStatics::SetGamePaused(this, true);
	}
}

// --- Камера/зона -------------------------------------------------------------------

void ATacticalPlayerController::HandleCameraPan(const FInputActionValue& Value)
{
	// Attack-targeting модален: панорама/edge scroll не должны бросать action
	// camera, оставляя TargetingMode активным без кадра.
	if (IsTargetingAttack())
	{
		return;
	}
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->AddPanInput(Value.Get<FVector2D>());
	}
}

void ATacticalPlayerController::HandleCameraRotate(const FInputActionValue& Value)
{
	const float Direction = Value.Get<float>();

	// Q/E в режиме прицеливания листают ЦЕЛИ, а не крутят камеру (XCOM): пока
	// целимся, вся навигация — по врагам. Камера и так стоит в кадре выстрела.
	if (IsTargetingAttack() && !FMath::IsNearlyZero(Direction))
	{
		CycleAttackTarget(Direction > 0.f ? 1 : -1);
		return;
	}

	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->AddRotationStep(Direction);
	}
}

void ATacticalPlayerController::HandleCameraZoom(const FInputActionValue& Value)
{
	if (IsTargetingAttack())
	{
		return;
	}
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->AddZoomInput(Value.Get<float>());
	}
}

void ATacticalPlayerController::RefreshMoveRange()
{
	if (!MoveRangeVisualizer)
	{
		return;
	}

	// Во время выполнения приказа зона прячется: она устарела (юнит уже не там)
	// и перестроится по остановке (PlayerTick ловит конец перемещения).
	const bool bShouldShow = CanIssueCommand(ETacticalPlayerCommand::Move);
	if (!bShouldShow)
	{
		MoveRangeVisualizer->Hide();
		return;
	}

	// Навмеш статичен — построение синхронное. false = юнит вне навмеша
	// (нештатная ситуация уровня, а не гонка) — просто логируем.
	if (!MoveRangeVisualizer->ShowForUnit(SelectedUnit))
	{
		UE_LOG(LogXRU1Combat, Warning, TEXT("[MoveRange] Зона не построилась: %s стоит вне навмеша"),
			*GetNameSafe(SelectedUnit));
	}
}

void ATacticalPlayerController::HandleTurnStarted(ETurnPhase Phase)
{
	bPendingAutoAdvance = false; // фаза сменилась — отложенный переход не актуален

	// Сначала штатно закрываем aim-mode (он вернёт глобальный yaw/zoom), затем
	// бросаем только возможный таймерный кадр уже совершённого выстрела. Новый
	// фазовый focus ниже заменит позицию, не ракурс игрока.
	SetTargetingMode(EPlayerTargetingMode::None);
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->AbandonShotFraming();
	}

	// Во время постановки (bLockGameplayInput) камерой владеет режиссура шага:
	// Beat сфокусировал точку, по которой сейчас бежит staged-боец, и мгновенная
	// смена фаз (пустой ход врага) не должна утаскивать взгляд обратно на отряд.
	const UTutorialActionGateSubsystem* TutorialGate = UTutorialActionGateSubsystem::Get(this);
	const bool bScriptedSequence = TutorialGate && TutorialGate->IsGateActive() &&
		TutorialGate->GetActivePolicy().bLockGameplayInput;

	// Сценарий уже поставил стартовый ракурс (InitialCameraAnchorId) — первый
	// автофокус на отряд его перезаписывал бы, поэтому этот один раз пропускаем.
	if (Phase == ETurnPhase::Player && (bScenarioCameraPlaced || bScriptedSequence))
	{
		bScenarioCameraPlaced = false;
		bInitialSquadFocusDone = true;
		// Follow не трогаем при scripted-шаге: камера может сопровождать бегущего
		// staged-бойца, и мгновенная смена фаз не должна отцеплять её на полпути.
		if (!bScriptedSequence)
		{
			if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
			{
				Camera->ClearFollowTarget();
			}
		}
	}
	else
	{
		// Смена фазы: камера бросает сопровождение (нового скажет следующий делегат).
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->ClearFollowTarget();

			// Начало нашего хода: вернуть камеру к выбранному бойцу или к отряду.
			if (Phase == ETurnPhase::Player && bInitialSquadFocusDone)
			{
				if (SelectedUnit)
				{
					Camera->FocusOnActor(SelectedUnit);
				}
				else
				{
					FocusCameraOnSquad();
				}
			}
		}

		if (Phase == ETurnPhase::Player && !bInitialSquadFocusDone)
		{
			// Старт боя: камеру ставим на центр отряда МГНОВЕННО (без полёта через
			// карту). Выбор бойца — ниже, общим автоселектом (или игроком вручную,
			// если bAutoSelectUnits выключен).
			FocusCameraOnSquad(/*bInstant=*/true);
			bInitialSquadFocusDone = true;
		}
	}

	// Автоселект (XCOM 2): ход игрока всегда начинается с бойца, который МОЖЕТ
	// действовать. Не только «выбора нет», но и «выбранный больше не боец»:
	// упавший тяжело раненым в ход врага иначе остался бы выбранным, и новый ход
	// начинался бы с мёртвой панели действий (IsUnitAlive ложна и для Downed,
	// и для эвакуированных).
	if (Phase == ETurnPhase::Player && bAutoSelectUnits && !bScriptedSequence &&
		(!SelectedUnit || !UTacticsCombatStatics::IsUnitAlive(SelectedUnit)))
	{
		SelectNextAvailableUnit();
	}

	RefreshMoveRange();
	RefreshSelectionHighlight(); // кольцо: показать в свою фазу, скрыть в ход врага
}

bool ATacticalPlayerController::IsVisibleToSquad(const AActor* Unit) const
{
	const UWorld* World = GetWorld();
	const UFogOfWarSubsystem* Fog = World ? World->GetSubsystem<UFogOfWarSubsystem>() : nullptr;
	return Fog && Fog->IsActorCurrentlyVisible(Unit);
}

FTacticalMovePreview ATacticalPlayerController::GetMovePreviewAt(const FVector& Location) const
{
	FTacticalMovePreview Preview;
	const UWorld* World = GetWorld();
	UCoverDetectionComponent* Cover = SelectedUnit ? SelectedUnit->GetCoverDetection() : nullptr;
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	const UFogOfWarSubsystem* Fog = World ? World->GetSubsystem<UFogOfWarSubsystem>() : nullptr;
	if (!Cover || !TurnManager || !Fog)
	{
		return Preview;
	}

	// Точка ПОЛА: высоты укрытия отсчитываются от неё, глаза — пол + капсула.
	const float HalfHeight = SelectedUnit->GetCapsuleComponent()
		? SelectedUnit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
	const FVector FloorPoint(Location.X, Location.Y, Location.Z);
	const FVector EyeAtPoint = FloorPoint +
		FVector(0.f, 0.f, HalfHeight + UTacticsCombatStatics::GetCoverTuning(World)->EyeHeightOffset);

	for (AActor* Enemy : TurnManager->GetOpposingUnits(SelectedUnit))
	{
		// Preview не имеет права раскрывать засаду: считаем угрозу только от тех,
		// кого отряд уже видит по тем же правилам, что камера и будущий fog-render.
		if (!Enemy || !UTacticsCombatStatics::IsUnitAlive(Enemy) ||
			!Fog->IsActorCurrentlyVisible(Enemy))
		{
			continue;
		}

		// «Простреливает» = есть линия огня в пределах дальности. Тот же
		// предикат, что решает выстрел, — превью не может разойтись с боем.
		const bool bInRange = FVector::Dist(FloorPoint, Enemy->GetActorLocation()) <= SelectedUnit->AttackRange;
		if (!bInRange ||
			!UTacticsCombatStatics::HasLineOfSightFromLocation(World, EyeAtPoint, Enemy, SelectedUnit))
		{
			continue;
		}
		++Preview.EnemiesSeeing;

		const ECoverType CoverHere = Cover->EvaluateCoverAtLocation(FloorPoint, Enemy->GetActorLocation());
		if (CoverHere != ECoverType::None)
		{
			++Preview.EnemiesCovered;
			Preview.BestCover = FMath::Max(Preview.BestCover, CoverHere);
		}
		else
		{
			++Preview.EnemiesExposed;
		}

		if (UTacticsCombatStatics::IsTargetFlankedByLocation(Enemy, FloorPoint))
		{
			++Preview.EnemiesFlanked;
		}
	}
	return Preview;
}

void ATacticalPlayerController::HandleEnemyUnitActivated(AActor* Unit)
{
	if (!Unit)
	{
		return;
	}

	// XCOM: камера сопровождает действующего врага, но только если отряд его
	// ВИДИТ — скрытые враги ходят «за кадром», их позицию камера не выдаёт.
	if (!IsVisibleToSquad(Unit))
	{
		// Пока не видим — берём на заметку. Как только он выйдет из-за угла,
		// PlayerTick подхватит его камерой прямо на бегу (XCOM показывает
		// вражеский ход с момента ОБНАРУЖЕНИЯ, а не только с его начала).
		PendingEnemyCameraUnit = Unit;
		return;
	}

	PendingEnemyCameraUnit = nullptr;
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->SetFollowTarget(Unit); // следуем весь его ход (движение + выстрел)
	}
}

void ATacticalPlayerController::FocusCameraOnSquad(bool bInstant)
{
	ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn());
	if (!Camera)
	{
		return;
	}

	FVector Centroid = FVector::ZeroVector;
	int32 Count = 0;
	for (AUnitBase* Unit : GetSquad())
	{
		if (Unit && !Unit->IsDead() && !Unit->IsEvacuated())
		{
			Centroid += Unit->GetActorLocation();
			++Count;
		}
	}
	if (Count > 0)
	{
		Camera->FocusOnLocation(Centroid / static_cast<float>(Count), bInstant);
	}
}

void ATacticalPlayerController::HandleSelectedUnitAPChanged(int32 NewCurrent, int32 /*Max*/)
{
	RefreshMoveRange();

	// XCOM-флоу: действие завершило активацию бойца (AP кончились) — выбор
	// переходит к следующему с AP. Бегущего не переключаем (камера сопровождает
	// его до финиша; переход сделает PlayerTick по остановке). Пока играет кадр
	// выстрела — тоже ждём (иначе переход выбора сорвал бы кадр в тот же тик):
	// доделает PlayerTick по окончании кадра.
	UE_LOG(LogXRU1Combat, Log, TEXT("[AutoAdv] AP=%d autoSel=%d playerPhase=%d moving=%d framingShot=%d"),
		NewCurrent, bAutoSelectUnits, IsPlayerPhase(), IsSelectedUnitMoving(), IsCameraFramingShot());
	if (NewCurrent == 0 && bAutoSelectUnits && IsPlayerPhase() && !IsSelectedUnitMoving())
	{
		// ВСЕГДА откладываем на PlayerTick (со штампом кадра), а не переходим тут
		// же. Причина — AP резервируются до начала montage, а fire-action ещё обязан
		// пройти StepOut → notify commit → ReturnToAnchor → terminal. Немедленный
		// SelectNextUnit сменил бы HUD/camera owner посреди этой транзакции. Штамп
		// кадра плюс action/reaction barriers разрешают переход только после terminal.
		// Ходьба на 2 AP идёт другим путём (по остановке в PlayerTick).
		UE_LOG(LogXRU1Combat, Log, TEXT("[AutoAdv] AP=0 → откладываем переход (ждём возможный кадр выстрела)"));
		bPendingAutoAdvance = true;
		PendingAutoAdvanceFrame = GFrameCounter;
	}
}

bool ATacticalPlayerController::IsUnitActionInProgress(const AUnitBase* Unit,
	FGuid* OutActionId) const
{
	FGuid ActionId;
	const bool bInProgress = UGA_Attack::GetAttackActionInProgressFor(Unit, ActionId) ||
		UGA_Overwatch::GetReactionActionInProgressFor(Unit, ActionId);
	if (OutActionId)
	{
		*OutActionId = bInProgress ? ActionId : FGuid();
	}
	return bInProgress;
}

bool ATacticalPlayerController::IsAnySquadActionInProgress() const
{
	for (const AUnitBase* Unit : GetSquad())
	{
		if (IsUnitActionInProgress(Unit))
		{
			return true;
		}
	}
	return false;
}

bool ATacticalPlayerController::IsCameraFramingShot() const
{
	const ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn());
	if (!Camera)
	{
		return false;
	}
	// Ждём ЛЮБОЙ кадр ПРЕЗЕНТАЦИИ (в т.ч. бессрочный, живущий до терминала
	// fire-action): иначе автопереход срывал бы выстрел в окне до старта
	// montage. Кадр ПРИЦЕЛИВАНИЯ презентацией не считается и ход не блокирует —
	// UI-режим не должен уметь навечно остановить игру при ошибке маршрутизации.
	return Camera->IsPlayingPresentationFrame() || Camera->IsPlayingShotFrame();
}

void ATacticalPlayerController::TryAutoEndTurn()
{
	if (bReactionPlaying || !bAutoEndTurnWhenExhausted || !IsPlayerPhase())
	{
		return;
	}
	// В обучении конец хода — отдельный шаг (A3, D1). Автозавершение сделало бы
	// его за игрока, и инструкция «нажми Завершить ход» осталась бы без действия.
	if (!UTutorialActionGateSubsystem::AllowsAction(this, ETutorialAction::EndTurn, SelectedUnit))
	{
		return;
	}
	// Пока кто-то ещё бежит, ход не завершаем: последний AP мог уйти на движение,
	// и юнит ещё в пути (его действие фактически не закончилось).
	if (IsSelectedUnitMoving())
	{
		return;
	}

	for (AUnitBase* Unit : GetSquad())
	{
		if (IsUnitActionInProgress(Unit))
		{
			return; // AP уже могли закончиться, но fire/reaction ещё не terminal.
		}
		if (Unit && UTacticsCombatStatics::IsUnitAlive(Unit) &&
			Unit->GetActionPoints() && Unit->GetActionPoints()->HasActionsLeft())
		{
			return; // есть кому ходить — ход не заканчиваем
		}
		// Юнит в пути (тратит последний AP на движение) — дождёмся его финиша.
		if (Unit && UTacticsCombatStatics::IsUnitInTransit(Unit))
		{
			return;
		}
	}

	// Ни у кого нет AP и никто не бежит — ход автоматически уходит врагу.
	if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
	{
		TurnManager->EndTurn();
	}
}

void ATacticalPlayerController::HandleSelectedUnitStateChanged()
{
	// Любое изменение состояния может снять GAS-block (EndAbility) или вернуть
	// юниту возможность принимать команды. Обновляем оба consumer единым сигналом:
	// зона хода не остаётся скрытой после окончания Hunker/Taunt/Overwatch.
	RefreshMoveRange();
	OnAvailableActionsChanged.Broadcast();

	// Выбранный погиб или эвакуирован — труп не принимает приказов: в свою фазу
	// выбор переходит к следующему бойцу (XCOM). В фазу врага — просто снимаем
	// (SelectUnit дёргает камеру, нельзя рвать показ хода врага; в начале
	// следующей фазы игрока бойца подберёт автоселект).
	if (SelectedUnit && (SelectedUnit->IsDead() || SelectedUnit->IsEvacuated()))
	{
		if (bAutoSelectUnits && IsPlayerPhase())
		{
			SelectNextAvailableUnit();
		}
		else
		{
			SelectUnit(nullptr);
		}
	}
}
