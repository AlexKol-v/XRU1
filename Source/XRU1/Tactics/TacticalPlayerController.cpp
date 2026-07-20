#include "TacticalPlayerController.h"
#include "UnitBase.h"
#include "UnitAIController.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "TacticalAbility.h"
#include "TacticalCameraPawn.h"
#include "MoveRangeVisualizer.h"
#include "MissionObjectives.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameplayTags.h"
#include "TurnManagerSubsystem.h"
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
#include "EngineUtils.h"
#include "Engine/World.h"

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
		UE_LOG(LogTemp, Warning, TEXT("[MoveRange] MoveRangeVisualizerClass не назначен в BP-контроллере — зона хода и превью пути не будут видны"));
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
	}
	bSelectedUnitWasMoving = bMovingNow;

	UpdateEdgeScroll();
	UpdateHoverHighlight();
	UpdatePathPreviewUnderCursor();
}

void ATacticalPlayerController::UpdateEdgeScroll()
{
	if (!bEdgeScrollEnabled)
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
	if (GetHitResultUnderCursor(ECC_Pawn, /*bTraceComplex=*/false, Hit))
	{
		AUnitBase* Unit = Cast<AUnitBase>(Hit.GetActor());
		if (Unit && !Unit->IsDead() && !Unit->IsEvacuated())
		{
			// В фазу врага свои юниты не интерактивны — наведением их не подсвечиваем
			// (как в XCOM). Врагов подсвечиваем всегда, чтобы читать их ход.
			const bool bAlly = Unit->GetGenericTeamId().GetId() == 1;
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
		OldHovered->SetHoverHighlight(false);
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
	// Статус path following ставится сразу при выдаче приказа (в отличие от
	// velocity, которая в кадр приказа ещё нулевая).
	const AUnitAIController* UnitAI = SelectedUnit
		? Cast<AUnitAIController>(SelectedUnit->GetController()) : nullptr;
	return UnitAI && UnitAI->GetMoveStatus() != EPathFollowingStatus::Idle;
}

void ATacticalPlayerController::UpdatePathPreviewUnderCursor()
{
	if (!MoveRangeVisualizer)
	{
		return;
	}

	// Превью уместно только когда юнит готов принять приказ и стоит на месте.
	const bool bCanPreview = IsPlayerPhase() && SelectedUnit && !SelectedUnit->IsDowned()
		&& !SelectedUnit->IsEvacuated() && !bAwaitingAbilityTarget && !IsSelectedUnitMoving();
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

void ATacticalPlayerController::SelectUnit(AUnitBase* Unit)
{
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

	bAwaitingAbilityTarget = false;
	bAwaitingAttackTarget = false;
	SelectedUnit = Unit;

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
	OnSelectedUnitChanged.Broadcast(SelectedUnit);
}

void ATacticalPlayerController::NotifyUnitMoveFinished(AUnitBase* Unit)
{
	// Финишировал ДРУГОЙ юнит: его диск занятости встал на новую позицию —
	// зона выбранного юнита пересчитывается сразу (всё синхронно, без задержек).
	if (Unit && Unit != SelectedUnit && SelectedUnit && IsPlayerPhase())
	{
		RefreshMoveRange();
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
		if (Candidate && UTacticsCombatStatics::IsUnitAlive(Candidate) &&
			Candidate->GetActionPoints() && Candidate->GetActionPoints()->HasActionsLeft())
		{
			SelectUnit(Candidate);
			return;
		}
	}
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

void ATacticalPlayerController::HandleSelectPressed()
{
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Pawn, /*bTraceComplex=*/false, Hit))
	{
		GetHitResultUnderCursor(ECC_Visibility, false, Hit);
	}

	AActor* Clicked = Hit.GetActor();
	if (!Clicked)
	{
		return;
	}

	// Режим выбора цели способности (медик): клик решает.
	if (bAwaitingAbilityTarget)
	{
		HandleAbilityTargetClick(Clicked);
		return;
	}

	// Режим прицеливания атаки (кнопка «Огонь»): клик по врагу — выстрел;
	// любой другой клик снимает режим и обрабатывается как обычно.
	if (bAwaitingAttackTarget)
	{
		bAwaitingAttackTarget = false;
		if (AUnitBase* AttackTarget = Cast<AUnitBase>(Clicked))
		{
			if (AttackTarget->GetGenericTeamId().GetId() != 1)
			{
				TryAttackTarget(AttackTarget);
				return;
			}
		}
	}

	if (AUnitBase* ClickedUnit = Cast<AUnitBase>(Clicked))
	{
		const bool bOwnUnit = ClickedUnit->GetGenericTeamId().GetId() == 1;
		if (bOwnUnit && !ClickedUnit->IsEvacuated() && !ClickedUnit->IsDead())
		{
			SelectUnit(ClickedUnit);
		}
		else if (!bOwnUnit)
		{
			TryAttackTarget(ClickedUnit);
		}
	}
	else
	{
		// ЛКМ по пустому месту — снять выбор (зона хода прячется).
		SelectUnit(nullptr);
	}
}

void ATacticalPlayerController::HandleCommandPressed()
{
	bAwaitingAbilityTarget = false;
	bAwaitingAttackTarget = false;

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
	if (!IsPlayerPhase() || !SelectedUnit || SelectedUnit->IsDowned())
	{
		return;
	}
	UActionPointsComponent* ActionPoints = SelectedUnit->GetActionPoints();
	if (!ActionPoints || !ActionPoints->HasActionsLeft())
	{
		return;
	}

	// Точка в диске занятости другого юнита — выталкиваем на край (клик «в»
	// стоящего бойца); не вытолкнулась (толпа) — приказ отклоняется.
	FVector AdjustedGoal = Goal;
	if (!UTacticsCombatStatics::AdjustGoalOutOfUnits(GetWorld(), SelectedUnit, AdjustedGoal))
	{
		return;
	}

	// Бюджет по ДЛИНЕ ПУТИ (не радиусу): 1 AP = MoveRange, 2 AP = 2×MoveRange.
	const float PathLength = UTacticsCombatStatics::GetNavPathLength(
		this, SelectedUnit->GetActorLocation(), AdjustedGoal);
	if (PathLength < 0.f)
	{
		return; // путь не построился
	}

	int32 Cost = 0;
	if (PathLength <= SelectedUnit->MoveRange)
	{
		Cost = 1;
	}
	else if (PathLength <= SelectedUnit->MoveRange * 2.f && ActionPoints->CanSpend(2))
	{
		Cost = 2;
	}
	else
	{
		return; // вне оплачиваемой зоны — приказ игнорируется
	}

	AUnitAIController* UnitAI = Cast<AUnitAIController>(SelectedUnit->GetController());
	if (!UnitAI)
	{
		return;
	}

	if (UnitAI->MoveToLocation(AdjustedGoal, /*AcceptanceRadius=*/50.f) == EPathFollowingRequestResult::RequestSuccessful)
	{
		// AP списываем сразу (как XCOM), укрытие пересчитается в OnMoveCompleted.
		ActionPoints->TrySpendActionPoint(Cost);

		// Камера сопровождает бегущего бойца (follow отпустится по остановке
		// в PlayerTick или по ручной панораме игрока).
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->SetFollowTarget(SelectedUnit);
		}
	}
}

void ATacticalPlayerController::TryAttackTarget(AActor* Target)
{
	if (!IsPlayerPhase() || !SelectedUnit || SelectedUnit->IsDowned() || !Target)
	{
		return;
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
	bAwaitingAbilityTarget = false;
	if (!IsPlayerPhase() || !SelectedUnit || !ClickedActor)
	{
		return;
	}

	FGameplayEventData Payload;
	Payload.Instigator = SelectedUnit;
	Payload.Target = ClickedActor;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		SelectedUnit, TacticsGameplayTags::Event_Heal, Payload);
}

void ATacticalPlayerController::RequestEndTurn()
{
	if (IsPlayerPhase())
	{
		if (UTurnManagerSubsystem* TurnManager = GetWorld()->GetSubsystem<UTurnManagerSubsystem>())
		{
			TurnManager->EndTurn();
		}
	}
}

void ATacticalPlayerController::RequestAttack()
{
	// Кнопка «Огонь» (GDD §6): включаем режим прицеливания — следующий ЛКМ
	// по врагу стреляет. Сама валидация цели/AP — в GA_Attack по клику.
	if (IsPlayerPhase() && SelectedUnit && !SelectedUnit->IsDowned() &&
		SelectedUnit->GetActionPoints() && SelectedUnit->GetActionPoints()->HasActionsLeft())
	{
		bAwaitingAbilityTarget = false;
		bAwaitingAttackTarget = true;
	}
}

void ATacticalPlayerController::RequestOverwatch()
{
	if (IsPlayerPhase() && SelectedUnit && !SelectedUnit->IsDowned() && SelectedUnit->OverwatchAbilityClass)
	{
		if (UAbilitySystemComponent* ASC = SelectedUnit->GetAbilitySystemComponent())
		{
			ASC->TryActivateAbilityByClass(SelectedUnit->OverwatchAbilityClass);
		}
	}
}

void ATacticalPlayerController::RequestHunkerDown()
{
	if (IsPlayerPhase() && SelectedUnit && !SelectedUnit->IsDowned() && SelectedUnit->HunkerAbilityClass)
	{
		if (UAbilitySystemComponent* ASC = SelectedUnit->GetAbilitySystemComponent())
		{
			ASC->TryActivateAbilityByClass(SelectedUnit->HunkerAbilityClass);
		}
	}
}

void ATacticalPlayerController::RequestClassAbility()
{
	if (!IsPlayerPhase() || !SelectedUnit || SelectedUnit->IsDowned() || !SelectedUnit->ClassAbilityClass)
	{
		return;
	}

	// Способности с целью (медик) — режим выбора: следующий ЛКМ по союзнику.
	const UTacticalAbility* AbilityCDO = SelectedUnit->ClassAbilityClass->GetDefaultObject<UTacticalAbility>();
	if (AbilityCDO && AbilityCDO->bRequiresTargetActor)
	{
		bAwaitingAbilityTarget = true;
		return;
	}

	if (UAbilitySystemComponent* ASC = SelectedUnit->GetAbilitySystemComponent())
	{
		ASC->TryActivateAbilityByClass(SelectedUnit->ClassAbilityClass);
	}
}

void ATacticalPlayerController::RequestSkipUnitTurn()
{
	if (IsPlayerPhase() && SelectedUnit)
	{
		if (UActionPointsComponent* ActionPoints = SelectedUnit->GetActionPoints())
		{
			ActionPoints->SpendAllRemaining();
		}
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
	ABombObjective* Bomb = nullptr;
	AEvacZone* Zone = nullptr;
	switch (FindAvailableInteraction(Bomb, Zone))
	{
	case EInteractionKind::DefuseBomb:
		Bomb->TryDefuse(SelectedUnit);
		break;
	case EInteractionKind::Evacuate:
		if (Zone->TryEvacuate(SelectedUnit))
		{
			// Эвакуированный выбор не имеет смысла — перейти к следующему бойцу.
			SelectNextUnit();
		}
		break;
	default:
		break;
	}
}

void ATacticalPlayerController::RequestPause()
{
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
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->AddPanInput(Value.Get<FVector2D>());
	}
}

void ATacticalPlayerController::HandleCameraRotate(const FInputActionValue& Value)
{
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->AddRotationStep(Value.Get<float>());
	}
}

void ATacticalPlayerController::HandleCameraZoom(const FInputActionValue& Value)
{
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
	const bool bShouldShow = SelectedUnit && IsPlayerPhase() && !SelectedUnit->IsDowned()
		&& !SelectedUnit->IsEvacuated() && !IsSelectedUnitMoving();
	if (!bShouldShow)
	{
		MoveRangeVisualizer->Hide();
		return;
	}

	// Навмеш статичен — построение синхронное. false = юнит вне навмеша
	// (нештатная ситуация уровня, а не гонка) — просто логируем.
	if (!MoveRangeVisualizer->ShowForUnit(SelectedUnit))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MoveRange] Зона не построилась: %s стоит вне навмеша"),
			*GetNameSafe(SelectedUnit));
	}
}

void ATacticalPlayerController::HandleTurnStarted(ETurnPhase Phase)
{
	bAwaitingAbilityTarget = false;
	bAwaitingAttackTarget = false;

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
		// карту), но НИКОГО не выбираем — первого бойца игрок берёт сам.
		FocusCameraOnSquad(/*bInstant=*/true);
		bInitialSquadFocusDone = true;
	}

	RefreshMoveRange();
	RefreshSelectionHighlight(); // кольцо: показать в свою фазу, скрыть в ход врага
}

void ATacticalPlayerController::HandleEnemyUnitActivated(AActor* Unit)
{
	if (!Unit)
	{
		return;
	}

	// XCOM: камера сопровождает действующего врага, но только если отряд его
	// ВИДИТ — скрытые враги ходят «за кадром», их позицию камера не выдаёт.
	const TArray<AUnitBase*> Squad = GetSquad();
	const bool bVisibleToSquad = Squad.Num() > 0
		&& UTacticsCombatStatics::SquadHasLineOfSight(Squad[0], Unit);
	if (!bVisibleToSquad)
	{
		return;
	}

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

void ATacticalPlayerController::HandleSelectedUnitAPChanged(int32 /*NewCurrent*/, int32 /*Max*/)
{
	RefreshMoveRange();
}

void ATacticalPlayerController::HandleSelectedUnitStateChanged()
{
	// Выбранный погиб или эвакуирован — выбор снимается: труп не принимает
	// приказов, зона хода и кнопки не должны работать от его имени.
	if (SelectedUnit && (SelectedUnit->IsDead() || SelectedUnit->IsEvacuated()))
	{
		SelectUnit(nullptr);
	}
}
