#include "TacticalPlayerController.h"
#include "UnitBase.h"
#include "UnitAIController.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "TacticalAbility.h"
#include "TacticalCameraPawn.h"
#include "GA_Attack.h"
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

		// Отложенный XCOM-автопереход: бегун потратил последние AP — теперь,
		// когда он остановился, выбор уходит следующему бойцу с AP, а если
		// такого нет — ход автоматически завершается (переходит врагу).
		if (bAutoSelectUnits && IsPlayerPhase() && SelectedUnit &&
			SelectedUnit->GetActionPoints() && !SelectedUnit->GetActionPoints()->HasActionsLeft())
		{
			SelectNextUnit();
			TryAutoEndTurn();
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

	// Смена бойца снимает прицеливание вместе с кадром камеры: прицел старого
	// бойца к новому не относится (камера уедет к нему ниже через FocusOnActor).
	if (bAwaitingAttackTarget)
	{
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->ClearShotFraming();
		}
	}
	bAwaitingAbilityTarget = false;
	bAwaitingAttackTarget = false;
	CurrentAttackTarget = nullptr;
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
	if (bAwaitingAttackTarget)
	{
		CycleAttackTarget(1);
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
			Candidate->GetActionPoints() && Candidate->GetActionPoints()->HasActionsLeft())
		{
			SelectUnit(Candidate);
			return;
		}
	}
}

void ATacticalPlayerController::SelectNextAvailableUnit()
{
	const TArray<AUnitBase*> Squad = GetSquad();
	const int32 Num = Squad.Num();
	if (Num == 0)
	{
		SelectUnit(nullptr);
		return;
	}

	// Выбора нет — идём с начала отряда; есть — со следующего за текущим.
	const int32 CurrentIndex = Squad.IndexOfByKey(SelectedUnit);
	AUnitBase* AliveFallback = nullptr;
	for (int32 i = 0; i < Num; ++i)
	{
		const int32 Index = (CurrentIndex == INDEX_NONE) ? i : (CurrentIndex + 1 + i) % Num;
		AUnitBase* Candidate = Squad[Index];
		if (!Candidate || Candidate == SelectedUnit || !UTacticsCombatStatics::IsUnitAlive(Candidate))
		{
			continue;
		}
		if (Candidate->GetActionPoints() && Candidate->GetActionPoints()->HasActionsLeft())
		{
			SelectUnit(Candidate);
			return;
		}
		if (!AliveFallback)
		{
			AliveFallback = Candidate;
		}
	}
	// Никого с AP: живой без AP (HUD остаётся на бойце), весь отряд выбит — nullptr.
	SelectUnit(AliveFallback);
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

	// Режим прицеливания атаки (кнопка «Огонь»): клик по врагу-цели — выстрел.
	// Клик по ДРУГОМУ достижимому врагу — переводит прицел на него (как ховер в
	// XCOM), а не стреляет сразу; клик мимо — выходит из режима.
	if (bAwaitingAttackTarget)
	{
		if (AUnitBase* ClickedEnemy = Cast<AUnitBase>(Clicked))
		{
			if (ClickedEnemy->GetGenericTeamId().GetId() != 1)
			{
				if (ClickedEnemy == CurrentAttackTarget.Get())
				{
					ConfirmAttack();
				}
				else if (UGA_Attack::CanTargetActor(SelectedUnit, ClickedEnemy))
				{
					SetAttackTarget(ClickedEnemy); // навёл на другого — берём его на прицел
				}
				return;
			}
		}
		CancelTargeting(); // клик мимо врагов — выходим из прицеливания
	}

	if (AUnitBase* ClickedUnit = Cast<AUnitBase>(Clicked))
	{
		const bool bOwnUnit = ClickedUnit->GetGenericTeamId().GetId() == 1;
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

void ATacticalPlayerController::HandleCommandPressed()
{
	// В режиме прицеливания ПКМ = отмена (XCOM), а не приказ на движение:
	// иначе игрок, передумав стрелять, случайно гнал бы бойца под клик.
	if (bAwaitingAttackTarget || bAwaitingAbilityTarget)
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

	// Приказ во время выполнения предыдущего не принимаем: зона в этот момент
	// спрятана (позиция юнита устарела), значит и affordance у игрока нет.
	if (IsSelectedUnitMoving())
	{
		return;
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

	AUnitAIController* UnitAI = Cast<AUnitAIController>(SelectedUnit->GetController());
	if (!UnitAI)
	{
		return;
	}

	// Ведём бойца ПО ЛОМАНОЙ плана, а не «в точку»: приказ в конечную цель
	// заставлял навмеш строить свою прямую — сквозь стоящих бойцов, которых
	// он не видит. Боец упирался в них, а очко действия уже было списано.
	if (UnitAI->MoveAlongRoute(Plan.PathPoints, /*AcceptanceRadius=*/50.f) == EPathFollowingRequestResult::RequestSuccessful)
	{
		// AP списываем сразу (как XCOM), укрытие пересчитается в OnMoveCompleted.
		ActionPoints->TrySpendActionPoint(Plan.ActionPointCost);

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
	// В режиме прицеливания Enter = «выстрелить по выбранной цели», а не
	// «завершить ход» (XCOM: подтверждение выстрела). Атака способностью с целью
	// подтверждается кликом — там Enter оставляем на завершение хода.
	if (bAwaitingAttackTarget)
	{
		ConfirmAttack();
		return;
	}

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
	// Кнопка «Огонь» / хоткей (GDD §6): вход в режим прицеливания. Дальше цель
	// листается Tab'ом, подтверждается ЛКМ по ней или Enter. Если целей нет —
	// в режим не входим (кнопка «Огонь» и так серая по HasAnyValidTarget).
	if (IsPlayerPhase() && SelectedUnit && !SelectedUnit->IsDowned() &&
		SelectedUnit->GetActionPoints() && SelectedUnit->GetActionPoints()->HasActionsLeft())
	{
		bAwaitingAbilityTarget = false;
		BeginAttackTargeting();
	}
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

	bAwaitingAttackTarget = true;

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
	SetAttackTarget(Initial);
	OnAvailableActionsChanged.Broadcast(); // HUD: показать баннер/панель цели
	return true;
}

void ATacticalPlayerController::SetAttackTarget(AUnitBase* Target)
{
	CurrentAttackTarget = Target;
	if (Target)
	{
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
		{
			Camera->FrameShot(SelectedUnit, Target); // кадр держится, пока целимся
		}
	}
	OnAvailableActionsChanged.Broadcast(); // HUD пересчитает панель цели
}

void ATacticalPlayerController::CycleAttackTarget(int32 Direction)
{
	if (!bAwaitingAttackTarget)
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
	const bool bWasTargeting = bAwaitingAttackTarget || bAwaitingAbilityTarget;
	bAwaitingAttackTarget = false;
	bAwaitingAbilityTarget = false;
	CurrentAttackTarget = nullptr;

	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->ClearShotFraming();
		if (SelectedUnit)
		{
			Camera->FocusOnActor(SelectedUnit); // вернуть камеру к бойцу
		}
	}
	if (bWasTargeting)
	{
		OnAvailableActionsChanged.Broadcast(); // HUD: убрать баннер прицеливания
	}
}

void ATacticalPlayerController::ConfirmAttack()
{
	AUnitBase* Target = CurrentAttackTarget.Get();
	if (!bAwaitingAttackTarget || !Target)
	{
		return;
	}
	// Цель могла выйти из зоны поражения, пока целились (сдвиг мира скриптом).
	if (!UGA_Attack::CanTargetActor(SelectedUnit, Target))
	{
		return;
	}

	bAwaitingAttackTarget = false;
	CurrentAttackTarget = nullptr;

	// Переводим ДЕРЖАЩИЙСЯ кадр прицеливания в таймерный ДО отправки события:
	// если выстрел не состоится (способность откажет по AP — ResolveShot не
	// дойдёт до NotifyShotFired), бесконечный кадр иначе застрял бы навсегда.
	// Состоявшийся выстрел просто переустановит тот же таймер в ResolveShot.
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->FrameShotForDuration(SelectedUnit, Target, /*Duration=*/-1.f);
	}

	TryAttackTarget(Target); // событие Event.Attack; кадр выстрела — в NotifyShotFired
	OnAvailableActionsChanged.Broadcast();
}

void ATacticalPlayerController::NotifyShotFired(AActor* Shooter, AActor* Target)
{
	// Кадр самого выстрела — на время полёта пули/реакции, и для игрока, и для
	// врага. Держим ограниченное время: по выходу камера вернёт ракурс сама.
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->FrameShotForDuration(Shooter, Target, /*Duration=*/-1.f);
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
	// Перед входом в режим — те же проверки, что серят кнопку в HUD: без AP или
	// зарядов хоткей R не должен включать пустое прицеливание.
	const UTacticalAbility* AbilityCDO = SelectedUnit->ClassAbilityClass->GetDefaultObject<UTacticalAbility>();
	if (AbilityCDO && AbilityCDO->bRequiresTargetActor)
	{
		const UActionPointsComponent* ActionPoints = SelectedUnit->GetActionPoints();
		const bool bHasActionPoints = AbilityCDO->ActionPointCost <= 0 ||
			(ActionPoints && ActionPoints->CanSpend(AbilityCDO->ActionPointCost));
		const bool bHasUses = SelectedUnit->GetAbilityUsesRemaining(SelectedUnit->ClassAbilityClass) != 0;
		if (bHasActionPoints && bHasUses)
		{
			bAwaitingAbilityTarget = true;
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
		// Переход выбора сделает HandleSelectedUnitStateChanged (Evacuate()
		// бросает OnUnitStateChanged) — здесь не дублируем, иначе двойной прыжок.
		Zone->TryEvacuate(SelectedUnit);
		break;
	default:
		break;
	}
}

void ATacticalPlayerController::RequestPause()
{
	// Esc сперва гасит режим прицеливания (XCOM), и только «в пустоте» — пауза.
	if (bAwaitingAttackTarget || bAwaitingAbilityTarget)
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
	if (bAwaitingAttackTarget && !FMath::IsNearlyZero(Direction))
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
	CurrentAttackTarget = nullptr;

	// Смена фазы: камера бросает сопровождение (нового скажет следующий делегат).
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn()))
	{
		Camera->AbandonShotFraming(); // прицел прошлой фазы к новой не относится
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

	// Автоселект (XCOM 2): ход игрока всегда начинается с бойца, который МОЖЕТ
	// действовать. Не только «выбора нет», но и «выбранный больше не боец»:
	// упавший тяжело раненым в ход врага иначе остался бы выбранным, и новый ход
	// начинался бы с мёртвой панели действий (IsUnitAlive ложна и для Downed,
	// и для эвакуированных).
	if (Phase == ETurnPhase::Player && bAutoSelectUnits &&
		(!SelectedUnit || !UTacticsCombatStatics::IsUnitAlive(SelectedUnit)))
	{
		SelectNextAvailableUnit();
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
	// Проверяем КАЖДОГО живого бойца (SquadHasLineOfSight не годится: она
	// проверяет союзников юнита, исключая его самого).
	bool bVisibleToSquad = false;
	for (const AUnitBase* Member : GetSquad())
	{
		if (Member && UTacticsCombatStatics::IsUnitAlive(Member) &&
			FVector::Dist(Member->GetActorLocation(), Unit->GetActorLocation())
				<= UTacticsCombatStatics::SquadVisionRange &&
			UTacticsCombatStatics::HasLineOfSight(Member, Unit))
		{
			bVisibleToSquad = true;
			break;
		}
	}
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

void ATacticalPlayerController::HandleSelectedUnitAPChanged(int32 NewCurrent, int32 /*Max*/)
{
	RefreshMoveRange();

	// XCOM-флоу: действие завершило активацию бойца (AP кончились) — выбор
	// переходит к следующему с AP. Бегущего не переключаем (камера сопровождает
	// его до финиша; переход сделает PlayerTick по остановке).
	if (NewCurrent == 0 && bAutoSelectUnits && IsPlayerPhase() && !IsSelectedUnitMoving())
	{
		SelectNextUnit();
		TryAutoEndTurn(); // если следующего с AP не нашлось — авто-конец хода
	}
}

void ATacticalPlayerController::TryAutoEndTurn()
{
	if (!bAutoEndTurnWhenExhausted || !IsPlayerPhase())
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
