#include "TacticalHUDWidget.h"
#include "TacticalHUDStyleData.h"
#include "TacticalPlayerController.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "GA_Attack.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Styling/SlateTypes.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Первый UImage в поддереве виджета (иконка внутри кнопки). */
	UImage* FindFirstChildImage(UWidget* Root)
	{
		if (!Root)
		{
			return nullptr;
		}
		if (UImage* AsImage = Cast<UImage>(Root))
		{
			return AsImage;
		}
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Root))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (UImage* Found = FindFirstChildImage(Panel->GetChildAt(Index)))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	/**
	 * Юнит карточки отряда. Портрет — WBP (переменная «Unit», Expose on Spawn),
	 * C++ базы у него нет — читаем свойство по имени через рефлексию.
	 * bOutResolved = false, если свойства «Unit» в WBP нет (переименовали) —
	 * вызывающий обязан в этом случае НЕ прятать карточку (fail-open).
	 */
	AUnitBase* GetPortraitUnit(UUserWidget* Portrait, bool& bOutResolved)
	{
		bOutResolved = false;
		if (!Portrait)
		{
			return nullptr;
		}
		const FObjectProperty* UnitProperty =
			FindFProperty<FObjectProperty>(Portrait->GetClass(), TEXT("Unit"));
		if (!UnitProperty)
		{
			return nullptr;
		}
		bOutResolved = true;
		return Cast<AUnitBase>(UnitProperty->GetObjectPropertyValue_InContainer(Portrait));
	}
}

ATacticalPlayerController* UTacticalHUDWidget::GetTacticalController() const
{
	return Cast<ATacticalPlayerController>(GetOwningPlayer());
}

UTurnManagerSubsystem* UTacticalHUDWidget::GetTurnManager() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
}

TArray<AUnitBase*> UTacticalHUDWidget::GetSquad() const
{
	const ATacticalPlayerController* Controller = GetTacticalController();
	return Controller ? Controller->GetSquad() : TArray<AUnitBase*>();
}

float UTacticalHUDWidget::GetHitChanceOnTarget(AActor* Target) const
{
	// Единый расчёт с фактическим выстрелом (включая штраф Squadsight) — HUD
	// никогда не обещает процент, которого не будет в GA_Attack.
	const ATacticalPlayerController* Controller = GetTacticalController();
	const AUnitBase* Shooter = Controller ? Controller->GetSelectedUnit() : nullptr;
	return UGA_Attack::ComputeAttackHitChance(Shooter, Target);
}

ECoverType UTacticalHUDWidget::GetTargetCoverAgainstSelected(AActor* Target) const
{
	const ATacticalPlayerController* Controller = GetTacticalController();
	const AUnitBase* Shooter = Controller ? Controller->GetSelectedUnit() : nullptr;
	if (!Shooter || !Target)
	{
		return ECoverType::None;
	}
	const UCoverDetectionComponent* Cover = Target->FindComponentByClass<UCoverDetectionComponent>();
	return Cover ? Cover->GetCoverAgainst(Shooter) : ECoverType::None;
}

int32 UTacticalHUDWidget::GetAliveEnemyCount() const
{
	// Критерий «жив» живёт в TurnManager (общий с условием конца боя).
	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	return TurnManager ? TurnManager->GetAliveEnemyCount() : 0;
}

// --- Стиль из DataAsset -----------------------------------------------------------

void UTacticalHUDWidget::ApplyStyle()
{
	if (!Style)
	{
		return;
	}

	// Кнопка действия: иконка/размер из ассета + единый отступ + фикс «прыжка».
	auto StyleActionButton = [this](UButton* Button, UTexture2D* IconTexture)
	{
		if (!Button)
		{
			return;
		}
		FButtonStyle ButtonStyle = Button->GetStyle();
		ButtonStyle.NormalPadding = Style->ActionButtonPadding;
		ButtonStyle.PressedPadding = Style->bUnifyPressedPadding
			? Style->ActionButtonPadding
			: ButtonStyle.PressedPadding;
		Button->SetStyle(ButtonStyle);
		if (UImage* Icon = FindFirstChildImage(Button))
		{
			if (IconTexture)
			{
				Icon->SetBrushFromTexture(IconTexture);
			}
			Icon->SetDesiredSizeOverride(Style->ActionIconSize);
		}
	};

	StyleActionButton(AttackBtn, Style->AttackIcon);
	StyleActionButton(OverwatchBtn, Style->OverwatchIcon);
	StyleActionButton(HunkerBtn, Style->HunkerIcon);
	StyleActionButton(AbilityBtn, Style->AbilityIcon);
	StyleActionButton(InteractBtn, Style->InteractDefuseIcon);
	StyleActionButton(SkipBtn, Style->SkipIcon);

	// «Завершить ход»: свой размер иконки (кнопка с текстом), тот же единый отступ.
	if (EndTurnBtn)
	{
		FButtonStyle ButtonStyle = EndTurnBtn->GetStyle();
		ButtonStyle.NormalPadding = Style->ActionButtonPadding;
		ButtonStyle.PressedPadding = Style->bUnifyPressedPadding
			? Style->ActionButtonPadding
			: ButtonStyle.PressedPadding;
		EndTurnBtn->SetStyle(ButtonStyle);
		if (UImage* Icon = FindFirstChildImage(EndTurnBtn))
		{
			if (Style->EndTurnIcon)
			{
				Icon->SetBrushFromTexture(Style->EndTurnIcon);
			}
			Icon->SetDesiredSizeOverride(Style->EndTurnIconSize);
		}
	}

	if (TargetCoverIcon)
	{
		TargetCoverIcon->SetDesiredSizeOverride(Style->TargetCoverIconSize);
	}
}

// --- Кнопки действий --------------------------------------------------------------

void UTacticalHUDWidget::RefreshActionButtons()
{
	// Перенесено из WBP: в Blueprint AND не short-circuit — Pure-цепочки от
	// невалидного S всё равно вычислялись и сыпали «Accessed None» на старте.
	//
	// Фаза входит в условие ЯВНО: раньше серость в ход врага держалась только на
	// том, что BP гасит всю ActionsPanel — скрытая связанность с графом. Теперь
	// C++ самодостаточен, а disabled-панель в BP — просто дублирующая страховка.
	const ATacticalPlayerController* Controller = GetTacticalController();
	AUnitBase* Selected = Controller ? Controller->GetSelectedUnit() : nullptr;
	const UActionPointsComponent* ActionPoints = Selected ? Selected->GetActionPoints() : nullptr;
	const bool bCanAct = Controller && Controller->IsPlayerPhase() &&
		Selected && !Selected->IsDowned() && ActionPoints && ActionPoints->CanSpend(1);

	auto SetEnabled = [](UWidget* Widget, bool bEnabled)
	{
		if (Widget)
		{
			Widget->SetIsEnabled(bEnabled);
		}
	};

	// Кнопка «Огонь» гасится и без единой доступной цели (как в XCOM) — иначе
	// игрок вооружает прицеливание вслепую и узнаёт об этом только по ховеру.
	SetEnabled(AttackBtn, bCanAct && Selected && UGA_Attack::HasAnyValidTarget(Selected));
	SetEnabled(OverwatchBtn, bCanAct);
	SetEnabled(HunkerBtn, bCanAct);
	SetEnabled(SkipBtn, bCanAct);

	// Способность класса: дополнительно лимит использований (0 — потрачена).
	const int32 AbilityUses = Selected
		? Selected->GetAbilityUsesRemaining(Selected->ClassAbilityClass)
		: 0;
	SetEnabled(AbilityBtn, bCanAct && AbilityUses != 0);

	// Контекстное F: вид интеракции определяет и доступность, и иконку.
	const EInteractionKind Interaction = Controller
		? Controller->GetAvailableInteraction()
		: EInteractionKind::None;
	SetEnabled(InteractBtn, bCanAct && Interaction != EInteractionKind::None);
	if (InteractIcon && Style)
	{
		UTexture2D* KindIcon = (Interaction == EInteractionKind::Evacuate)
			? Style->InteractEvacIcon.Get()
			: Style->InteractDefuseIcon.Get();
		if (KindIcon)
		{
			InteractIcon->SetBrushFromTexture(KindIcon);
		}
	}
}

// --- Панель цели ------------------------------------------------------------------

void UTacticalHUDWidget::UpdateTargetPanel(AUnitBase* Hovered)
{
	if (!TargetPanel)
	{
		return;
	}

	// Панель прицеливания принадлежит активному бойцу (XCOM): без выбранного
	// бойца, без ховера, по своему юниту — прячем. И в чужую фазу тоже: стрелять
	// нельзя, значит показывать огневое решение (шанс/укрытие) нечестно.
	const ATacticalPlayerController* Controller = GetTacticalController();
	const AUnitBase* Shooter = Controller ? Controller->GetSelectedUnit() : nullptr;

	// В режиме прицеливания панель ведёт ВЗЯТАЯ НА ПРИЦЕЛ цель (Tab/клик), а не
	// случайный ховер: как в XCOM, где решение по стрельбе привязано к выбранной
	// цели, а не к тому, над чем сейчас мышь.
	if (Controller && Controller->IsTargetingAttack())
	{
		if (AUnitBase* Aimed = Controller->GetCurrentAttackTarget())
		{
			Hovered = Aimed;
		}
	}

	const bool bEnemyHovered = Hovered && Shooter && Controller->IsPlayerPhase() &&
		!GetSquad().Contains(Hovered);
	if (!bEnemyHovered)
	{
		TargetPanel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	if (TargetNameText)
	{
		TargetNameText->SetText(Hovered->UnitDisplayName);
	}
	if (TargetHPBar)
	{
		const float MaxHealth = Hovered->GetMaxHealth();
		TargetHPBar->SetPercent(MaxHealth > 0.f ? Hovered->GetHealth() / MaxHealth : 0.f);
	}
	if (HitChanceText)
	{
		// Разные тексты для «слишком далеко» и «нет линии огня» — раньше обе
		// причины схлопывались в один -1 и HUD всегда писал «Нет линии огня»,
		// даже когда дело было только в дальности (см. EAttackTargetStatus).
		switch (UGA_Attack::GetTargetStatus(Shooter, Hovered))
		{
		case EAttackTargetStatus::Valid:
			HitChanceText->SetText(FText::Format(INVTEXT("Попадание: {0}%"),
				FText::AsNumber(FMath::RoundToInt(GetHitChanceOnTarget(Hovered)))));
			break;
		case EAttackTargetStatus::OutOfRange:
			HitChanceText->SetText(INVTEXT("Слишком далеко"));
			break;
		default:
			HitChanceText->SetText(INVTEXT("Нет линии огня"));
			break;
		}
	}
	if (TargetCoverIcon)
	{
		// Щит виден, только когда укрытие цели реально работает против стрелка
		// (фланкирована/открыта — иконки нет), как в XCOM 2.
		const ECoverType Cover = GetTargetCoverAgainstSelected(Hovered);
		UTexture2D* CoverTexture = nullptr;
		if (Style)
		{
			CoverTexture = (Cover == ECoverType::Full) ? Style->FullCoverIcon.Get()
				: (Cover == ECoverType::Half) ? Style->HalfCoverIcon.Get()
				: nullptr;
		}
		if (Cover != ECoverType::None)
		{
			if (CoverTexture)
			{
				TargetCoverIcon->SetBrushFromTexture(CoverTexture);
			}
			TargetCoverIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			TargetCoverIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// HitTestInvisible: панель информационная, клики мыши по миру не перехватывает.
	TargetPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTacticalHUDWidget::UpdateTargetingBanner()
{
	const ATacticalPlayerController* Controller = GetTacticalController();
	const bool bTargeting = Controller && Controller->IsTargetingAttack();

	if (TargetingBanner)
	{
		TargetingBanner->SetVisibility(bTargeting
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	OnTargetingModeChanged(bTargeting);
}

// --- Карточки отряда (XCOM-стиль) -------------------------------------------------

void UTacticalHUDWidget::UpdateSquadCardVisibility(AUnitBase* Selected)
{
	if (!SquadPanel || !bShowOnlySelectedCard)
	{
		return;
	}
	for (int32 Index = 0; Index < SquadPanel->GetChildrenCount(); ++Index)
	{
		UUserWidget* Card = Cast<UUserWidget>(SquadPanel->GetChildAt(Index));
		if (!Card)
		{
			continue;
		}

		bool bResolved = false;
		const AUnitBase* CardUnit = GetPortraitUnit(Card, bResolved);
		if (!bResolved && !bPortraitUnitLookupWarned)
		{
			bPortraitUnitLookupWarned = true;
			UE_LOG(LogTemp, Warning, TEXT("[HUD] В %s нет переменной «Unit» — карточки отряда ")
				TEXT("показываются все (проверь имя переменной в WBP_UnitPortrait)"), *GetNameSafe(Card));
		}

		// Выбран боец — видна только его карточка; выбора нет ИЛИ карточка не
		// отдала свой юнит — показываем (лучше лишние карточки, чем пустая
		// панель отряда: скрыть всё = отнять у игрока способ выбрать бойца).
		const bool bVisible = !bResolved || !Selected || CardUnit == Selected;
		Card->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

// --- Подписки и жизненный цикл ----------------------------------------------------

void UTacticalHUDWidget::SubscribeToUnitStates()
{
	UTurnManagerSubsystem* TurnManager = GetTurnManager();
	if (!TurnManager)
	{
		return;
	}

	// Смерть/ранение/эвакуация любого юнита обновляет портреты и счётчик врагов
	// одним событием OnUnitsStateChanged.
	auto SubscribeSide = [this](const TArray<AActor*>& Side)
	{
		for (AActor* Actor : Side)
		{
			AUnitBase* Unit = Cast<AUnitBase>(Actor);
			if (Unit && !StateSubscribedUnits.Contains(Unit))
			{
				Unit->OnUnitStateChanged.AddDynamic(this, &UTacticalHUDWidget::HandleUnitStateChanged);
				StateSubscribedUnits.Add(Unit);
			}
		}
	};
	SubscribeSide(TurnManager->GetPlayerSideUnits());
	SubscribeSide(TurnManager->GetEnemySideUnits());

	// AP отряда: трата очков ЛЮБЫМ бойцом пересчитывает серость кнопок текущего
	// выбранного (одна общая подписка вместо переподписок при смене выбора).
	for (AActor* Actor : TurnManager->GetPlayerSideUnits())
	{
		const AUnitBase* Unit = Cast<AUnitBase>(Actor);
		UActionPointsComponent* ActionPoints = Unit ? Unit->GetActionPoints() : nullptr;
		if (ActionPoints && !APSubscribedComponents.Contains(ActionPoints))
		{
			ActionPoints->OnActionPointsChanged.AddDynamic(this, &UTacticalHUDWidget::HandleSquadAPChanged);
			APSubscribedComponents.Add(ActionPoints);
		}
	}
}

void UTacticalHUDWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// Стиль применяется и в превью Designer — дизайнер сразу видит размеры/иконки.
	ApplyStyle();
}

void UTacticalHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Кнопка «Огонь» подключена из C++ (в BP OnClicked для неё не добавлять,
	// иначе выстрел уйдёт дважды).
	if (AttackBtn)
	{
		AttackBtn->OnClicked.AddDynamic(this, &UTacticalHUDWidget::HandleAttackClicked);
	}

	if (UTurnManagerSubsystem* TurnManager = GetTurnManager())
	{
		TurnManager->OnTurnStarted.AddDynamic(this, &UTacticalHUDWidget::HandleTurnStarted);
		TurnManager->OnCombatEnded.AddDynamic(this, &UTacticalHUDWidget::HandleCombatEnded);
		TurnManager->OnTurnLimitChanged.AddDynamic(this, &UTacticalHUDWidget::HandleTurnLimitChanged);

		// Первичная инициализация индикаторов (бой мог начаться до создания HUD;
		// подписка на юнитов — внутри HandleTurnStarted).
		HandleTurnStarted(TurnManager->GetCurrentPhase());
		OnUnitsStateChanged();
	}
	if (ATacticalPlayerController* Controller = GetTacticalController())
	{
		Controller->OnSelectedUnitChanged.AddDynamic(this, &UTacticalHUDWidget::HandleSelectedUnitChanged);
		Controller->OnHoveredUnitChanged.AddDynamic(this, &UTacticalHUDWidget::HandleHoveredUnitChanged);
		Controller->OnAvailableActionsChanged.AddDynamic(this, &UTacticalHUDWidget::HandleAvailableActionsChanged);
		HandleSelectedUnitChanged(Controller->GetSelectedUnit());
		HandleHoveredUnitChanged(Controller->GetHoveredUnit());
	}
}

void UTacticalHUDWidget::NativeDestruct()
{
	if (AttackBtn)
	{
		AttackBtn->OnClicked.RemoveDynamic(this, &UTacticalHUDWidget::HandleAttackClicked);
	}
	if (UTurnManagerSubsystem* TurnManager = GetTurnManager())
	{
		TurnManager->OnTurnStarted.RemoveDynamic(this, &UTacticalHUDWidget::HandleTurnStarted);
		TurnManager->OnCombatEnded.RemoveDynamic(this, &UTacticalHUDWidget::HandleCombatEnded);
		TurnManager->OnTurnLimitChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleTurnLimitChanged);
	}
	if (ATacticalPlayerController* Controller = GetTacticalController())
	{
		Controller->OnSelectedUnitChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleSelectedUnitChanged);
		Controller->OnHoveredUnitChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleHoveredUnitChanged);
		Controller->OnAvailableActionsChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleAvailableActionsChanged);
	}
	for (const TWeakObjectPtr<AUnitBase>& Unit : StateSubscribedUnits)
	{
		if (AUnitBase* Alive = Unit.Get())
		{
			Alive->OnUnitStateChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleUnitStateChanged);
		}
	}
	StateSubscribedUnits.Reset();
	for (const TWeakObjectPtr<UActionPointsComponent>& Component : APSubscribedComponents)
	{
		if (UActionPointsComponent* Alive = Component.Get())
		{
			Alive->OnActionPointsChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleSquadAPChanged);
		}
	}
	APSubscribedComponents.Reset();
	Super::NativeDestruct();
}

void UTacticalHUDWidget::HandleTurnStarted(ETurnPhase Phase)
{
	// Ленивая идемпотентная подписка: покрывает «HUD создан до StartCombat»
	// и юнитов, добавленных в бой после создания HUD.
	SubscribeToUnitStates();

	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	OnPhaseChanged(Phase,
		TurnManager ? TurnManager->GetTurnNumber() : 0,
		TurnManager ? TurnManager->GetTurnsRemaining() : -1);
	RefreshActionButtons();

	// Смена фазы меняет и право стрелять: панель цели под курсором должна
	// исчезнуть на ход врага и вернуться в наш (курсор мог не двигаться).
	// Прицеливание фаза тоже сбрасывает — баннер обязан погаснуть.
	UpdateTargetingBanner();
	if (const ATacticalPlayerController* Controller = GetTacticalController())
	{
		UpdateTargetPanel(Controller->GetHoveredUnit());
	}
}

void UTacticalHUDWidget::HandleCombatEnded(bool bPlayerWon)
{
	OnCombatFinished(bPlayerWon);
}

void UTacticalHUDWidget::HandleSelectedUnitChanged(AUnitBase* Selected)
{
	OnSelectedUnitChanged(Selected);
	UpdateSquadCardVisibility(Selected);
	RefreshActionButtons();

	// Смена стрелка меняет и валидность панели цели (шанс/укрытие считаются
	// от выбранного бойца).
	if (const ATacticalPlayerController* Controller = GetTacticalController())
	{
		UpdateTargetPanel(Controller->GetHoveredUnit());
	}
}

void UTacticalHUDWidget::HandleHoveredUnitChanged(AUnitBase* Hovered)
{
	OnHoveredUnitChanged(Hovered);
	UpdateTargetPanel(Hovered);
}

void UTacticalHUDWidget::HandleUnitStateChanged()
{
	OnUnitsStateChanged();
	RefreshActionButtons();
}

void UTacticalHUDWidget::HandleSquadAPChanged(int32 NewCurrent, int32 Max)
{
	RefreshActionButtons();
}

void UTacticalHUDWidget::HandleTurnLimitChanged()
{
	// Заряд обезврежен — таймер снят: перерисовываем индикатор фазы теми же
	// данными, что и при смене хода (BombTimerText спрячется по TurnsRemaining < 0).
	if (const UTurnManagerSubsystem* TurnManager = GetTurnManager())
	{
		OnPhaseChanged(TurnManager->GetCurrentPhase(),
			TurnManager->GetTurnNumber(),
			TurnManager->GetTurnsRemaining());
	}
}

void UTacticalHUDWidget::HandleAvailableActionsChanged()
{
	// Боец добежал: у бомбы/зоны эвакуации ожила кнопка F, а шанс попадания
	// по цели под курсором считается уже с новой позиции. Этот же сигнал шлёт
	// контроллер при входе/смене/выходе прицеливания — обновляем баннер и панель.
	RefreshActionButtons();
	UpdateTargetingBanner();
	if (const ATacticalPlayerController* Controller = GetTacticalController())
	{
		UpdateTargetPanel(Controller->GetHoveredUnit());
	}
}

void UTacticalHUDWidget::HandleAttackClicked()
{
	if (ATacticalPlayerController* Controller = GetTacticalController())
	{
		Controller->RequestAttack();
	}
}
