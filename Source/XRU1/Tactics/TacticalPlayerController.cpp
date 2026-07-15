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

	// Юнит закончил перемещение — зона хода перестраивается от новой позиции.
	const bool bMovingNow = IsSelectedUnitMoving();
	if (bSelectedUnitWasMoving && !bMovingNow)
	{
		RefreshMoveRange();
	}
	bSelectedUnitWasMoving = bMovingNow;

	UpdatePathPreviewUnderCursor();
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
	if (SelectedUnit == Unit)
	{
		return;
	}

	// Отписка от AP предыдущего выбранного.
	if (SelectedUnit)
	{
		if (UActionPointsComponent* OldAP = SelectedUnit->GetActionPoints())
		{
			OldAP->OnActionPointsChanged.RemoveDynamic(this, &ATacticalPlayerController::HandleSelectedUnitAPChanged);
		}
	}

	bAwaitingAbilityTarget = false;
	SelectedUnit = Unit;

	if (SelectedUnit)
	{
		if (UActionPointsComponent* NewAP = SelectedUnit->GetActionPoints())
		{
			NewAP->OnActionPointsChanged.AddDynamic(this, &ATacticalPlayerController::HandleSelectedUnitAPChanged);
		}
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->FocusOnActor(SelectedUnit);
		}
	}

	RefreshMoveRange();
	OnSelectedUnitChanged.Broadcast(SelectedUnit);
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

	// Со следующего после текущего — первый юнит с оставшимися AP.
	const int32 StartIndex = FMath::Max(0, Squad.IndexOfByKey(SelectedUnit));
	for (int32 Offset = 1; Offset <= Squad.Num(); ++Offset)
	{
		AUnitBase* Candidate = Squad[(StartIndex + Offset) % Squad.Num()];
		if (Candidate && Candidate->GetActionPoints() && Candidate->GetActionPoints()->HasActionsLeft())
		{
			SelectUnit(Candidate);
			return;
		}
	}
}

TArray<AUnitBase*> ATacticalPlayerController::GetSquad() const
{
	TArray<AUnitBase*> Squad;
	const UTurnManagerSubsystem* TurnManager = GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!TurnManager)
	{
		return Squad;
	}

	// Отряд = юниты стороны игрока. До начала боя список пуст — собираем по TeamId.
	if (TurnManager->IsInCombat())
	{
		AUnitBase* AnyPlayerUnit = nullptr;
		for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
		{
			if (It->GetGenericTeamId().GetId() == 1)
			{
				AnyPlayerUnit = *It;
				break;
			}
		}
		for (AActor* Unit : TurnManager->GetSideUnits(AnyPlayerUnit))
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

	// Бюджет по ДЛИНЕ ПУТИ (не радиусу): 1 AP = MoveRange, 2 AP = 2×MoveRange.
	const float PathLength = UTacticsCombatStatics::GetNavPathLength(
		this, SelectedUnit->GetActorLocation(), Goal);
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

	if (UnitAI->MoveToLocation(Goal, /*AcceptanceRadius=*/50.f) == EPathFollowingRequestResult::RequestSuccessful)
	{
		// AP списываем сразу (как XCOM), укрытие пересчитается в OnMoveCompleted.
		ActionPoints->TrySpendActionPoint(Cost);
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

void ATacticalPlayerController::RequestInteract()
{
	if (!IsPlayerPhase() || !SelectedUnit || SelectedUnit->IsDowned())
	{
		return;
	}

	// Приоритет: бомба рядом → эвакуация в зоне.
	for (TActorIterator<ABombObjective> It(GetWorld()); It; ++It)
	{
		if (It->TryDefuse(SelectedUnit))
		{
			return;
		}
	}
	for (TActorIterator<AEvacZone> It(GetWorld()); It; ++It)
	{
		if (It->TryEvacuate(SelectedUnit))
		{
			// Эвакуированный выбор не имеет смысла — перейти к следующему бойцу.
			SelectNextUnit();
			return;
		}
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
	if (SelectedUnit && IsPlayerPhase() && !SelectedUnit->IsDowned() && !SelectedUnit->IsEvacuated()
		&& !IsSelectedUnitMoving())
	{
		MoveRangeVisualizer->ShowForUnit(SelectedUnit);
	}
	else
	{
		MoveRangeVisualizer->Hide();
	}
}

void ATacticalPlayerController::HandleTurnStarted(ETurnPhase Phase)
{
	bAwaitingAbilityTarget = false;

	if (Phase == ETurnPhase::Player)
	{
		// Новый ход: если никто не выбран — взять первого бойца с AP.
		if (!SelectedUnit)
		{
			SelectNextUnit();
		}
	}
	RefreshMoveRange();
}

void ATacticalPlayerController::HandleSelectedUnitAPChanged(int32 /*NewCurrent*/, int32 /*Max*/)
{
	RefreshMoveRange();
}
