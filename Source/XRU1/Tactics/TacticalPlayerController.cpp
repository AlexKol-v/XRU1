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
#include "CoreGlobals.h" // GFrameCounter — штамп кадра для отложенного автоперехода

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
			if (IsCameraFramingShot())
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
	if (bPendingAutoAdvance && GFrameCounter != PendingAutoAdvanceFrame &&
		IsPlayerPhase() && !bMovingNow && !IsCameraFramingShot())
	{
		UE_LOG(LogTemp, Log, TEXT("[AutoAdv] pending дозрел (кадр выстрела кончился) → переход"));
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
				for (AActor* Enemy : TurnManager->GetOpposingUnits(SelectedUnit))
				{
					if (Enemy && UTacticsCombatStatics::IsUnitAlive(Enemy))
					{
						UTacticsCombatStatics::HasLineOfSight(SelectedUnit, Enemy);
					}
				}
			}
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
	if (GetHitResultUnderCursor(ECC_Pawn, /*bTraceComplex=*/false, Hit))
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

	// Сначала штатно выходим из модального targeting. ExitTargetingMode вернёт
	// глобальный пользовательский yaw/zoom; FocusOnActor ниже заменит только XY
	// цели полёта. Прежний pre-Abandon превращал временный yaw прицела в
	// постоянный и потому визуально «сбрасывал» поворот при смене бойца.
	SetTargetingMode(EPlayerTargetingMode::None);
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
	if (IsTargetingAttack())
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
	if (IsTargetingAbility())
	{
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
		ClickedEnemy->GetGenericTeamId().GetId() == TacticsTeamIds::Player)
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
	if (!IsPlayerPhase() || !SelectedUnit || SelectedUnit->IsDead() ||
		SelectedUnit->IsDowned() || SelectedUnit->IsEvacuated() ||
		IsSelectedUnitMoving())
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
	if (!Target || !CanIssueCommand(ETacticalPlayerCommand::Attack) ||
		!UGA_Attack::CanTargetActor(SelectedUnit, Target))
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
	if (TargetingMode != EPlayerTargetingMode::Ability || !ClickedActor ||
		!CanIssueCommand(ETacticalPlayerCommand::ClassAbility))
	{
		return;
	}

	AUnitBase* ActingUnit = SelectedUnit;
	const UTacticalAbility* AbilityCDO = ActingUnit && ActingUnit->ClassAbilityClass
		? ActingUnit->ClassAbilityClass->GetDefaultObject<UTacticalAbility>()
		: nullptr;
	if (!AbilityCDO || !AbilityCDO->bRequiresTargetActor ||
		!AbilityCDO->TargetedActivationEventTag.IsValid() ||
		!AbilityCDO->IsValidTargetActor(ActingUnit, ClickedActor))
	{
		// Невалидный клик не закрывает targeting: игрок может выбрать другую цель.
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
	// Enter завершает ход и НЕ подтверждает выстрел (по просьбе: подтверждение —
	// только тем же пробелом «Огонь» или кликом по цели). В режиме прицеливания
	// Enter сперва выходит из него — иначе завершил бы ход мимо намерения.
	if (IsTargetingAttack() || IsTargetingAbility())
	{
		CancelTargeting();
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
			if (Camera->IsHoldingAimFrame())
			{
				Camera->ClearShotFraming();
			}
		}
	}
	// Ability-режим побочных эффектов, требующих отката, не имеет
	// (подсветка/камера у него не менялись) — но ветка есть для симметрии.
}

void ATacticalPlayerController::EnterTargetingMode(EPlayerTargetingMode /*NewMode*/)
{
	// Побочные эффекты входа задаются точечно там, где известен контекст:
	// цель атаки берёт BeginAttackTargeting (нужен список целей). Метод оставлен
	// точкой расширения — новое состояние добавит сюда свой вход.
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

		// Стрелок разворачивается лицом к взятой цели (XCOM: читаемость наводки —
		// видно, в кого целится). Тот же хелпер, что и у выстрела, — поворот
		// прицела и поворот выстрела едины.
		if (SelectedUnit)
		{
			UTacticsCombatStatics::FaceActorTowards(SelectedUnit, Target->GetActorLocation());
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

	// Gameplay Event отправляется ДО броадкаста выхода из режима. Реальный
	// GA_Attack синхронно вызовет NotifyShotFired и сам превратит удерживаемый
	// кадр прицела в таймерный кадр выстрела. Если GA отклонит активацию,
	// ExitTargetingMode просто снимет старый aim-frame без ложной задержки.
	TryAttackTarget(Target);
	SetTargetingMode(EPlayerTargetingMode::None);
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
	if (!CanIssueCommand(ETacticalPlayerCommand::Overwatch))
	{
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
		Zone->TryEvacuate(SelectedUnit);
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
		UE_LOG(LogTemp, Warning, TEXT("[MoveRange] Зона не построилась: %s стоит вне навмеша"),
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
	// его до финиша; переход сделает PlayerTick по остановке). Пока играет кадр
	// выстрела — тоже ждём (иначе переход выбора сорвал бы кадр в тот же тик):
	// доделает PlayerTick по окончании кадра.
	UE_LOG(LogTemp, Log, TEXT("[AutoAdv] AP=%d autoSel=%d playerPhase=%d moving=%d framingShot=%d"),
		NewCurrent, bAutoSelectUnits, IsPlayerPhase(), IsSelectedUnitMoving(), IsCameraFramingShot());
	if (NewCurrent == 0 && bAutoSelectUnits && IsPlayerPhase() && !IsSelectedUnitMoving())
	{
		// ВСЕГДА откладываем на PlayerTick (со штампом кадра), а не переходим тут
		// же. Причина — ГОНКА с кадром выстрела: AP списываются в CommitAbility
		// (ApplyCost), а камера начинает кадр «из-за плеча» позже в том же вызове
		// (ResolveShot → NotifyShotFired). В момент этого делегата кадр ещё НЕ
		// начался, поэтому немедленный SelectNextUnit оборвал бы кинематограф и
		// перескочил на след. юнита прямо посреди выстрела. Штамп кадра не даёт
		// отложенному переходу сработать в тот же кадр — он дождётся, пока кадр
		// выстрела начнётся и доиграет (или, если выстрела нет — дозреет на след.
		// кадре). Ходьба на 2 AP идёт другим путём (по остановке в PlayerTick).
		UE_LOG(LogTemp, Log, TEXT("[AutoAdv] AP=0 → откладываем переход (ждём возможный кадр выстрела)"));
		bPendingAutoAdvance = true;
		PendingAutoAdvanceFrame = GFrameCounter;
	}
}

bool ATacticalPlayerController::IsCameraFramingShot() const
{
	const ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(GetPawn());
	// Автопереход ждёт только конечный кадр совершённого выстрела. Бессрочное
	// прицеливание — UI-mode, оно не должно уметь навечно заблокировать ход даже
	// при будущей ошибке в маршрутизации команды.
	return Camera && Camera->IsPlayingShotFrame();
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
