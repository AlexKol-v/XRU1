#include "TacticalHUDWidget.h"
#include "XRU1Log.h"
#include "FogOfWarSubsystem.h"
#include "QuestGameplayTags.h"   // родительский канал Quest.Event для подписки
#include "QuestTypes.h"          // FQuestEventData
#include "TacticalEncounter.h"   // маяк подкреплений: отсчёт для баннера
#include "TacticalHUDStyleData.h"
#include "TacticalQuestEvents.h" // каналы Reinforcements.Signaled/Arrived
#include "TacticsAudioSubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticalPlayerController.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "TacticsCombatStatics.h"
#include "GA_Attack.h"
#include "TacticalAbility.h"        // имя, заряды и стоимость для подсказки кнопки
#include "AbilitySystemComponent.h" // живой экземпляр способности знает остаток применений
#include "Engine/World.h"
#include "EngineUtils.h"  // TActorIterator по маякам подкреплений
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Styling/SlateTypes.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeExit.h"

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
	 * Юнит карточки отряда. WBP_TacticalHUD уже передаёт переменную «Unit»
	 * через существующий pin CreateWidget; C++ базы у карточки нет, поэтому
	 * читаем свойство по имени через рефлексию.
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

int32 UTacticalHUDWidget::GetAliveEnemyCount() const
{
	// Критерий «жив» живёт в TurnManager (общий с условием конца боя).
	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	return TurnManager ? TurnManager->GetAliveEnemyCount() : 0;
}

int32 UTacticalHUDWidget::GetVisibleEnemyCount() const
{
	const UWorld* World = GetWorld();
	const UFogOfWarSubsystem* Fog = World ? World->GetSubsystem<UFogOfWarSubsystem>() : nullptr;
	return Fog ? Fog->GetCurrentlyVisibleEnemyCount() : 0;
}

UTacticalHUDStyleData* UTacticalHUDWidget::GetUITheme() const
{
	// Runtime source of truth is the GameInstance. The local property exists only
	// because Designer preview has no project GameInstance.
	if (const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>())
	{
		if (UTacticalHUDStyleData* GlobalTheme = GameInstance->GetUITheme())
		{
			return GlobalTheme;
		}
	}
	return Style;
}

// --- Стиль из DataAsset -----------------------------------------------------------

void UTacticalHUDWidget::ApplyStyle()
{
	const UTacticalHUDStyleData* Theme = GetUITheme();
	if (!Theme)
	{
		return;
	}

	// Сохраняем shape/resource брашей из Designer, но цвет каждого состояния
	// берём из темы. PNG использует Button Foreground: прозрачный glyph остаётся
	// ярким в Normal и меняет tint на Hovered/Pressed/Disabled автоматически.
	auto StyleIconButton = [](UButton* Button, UTexture2D* IconTexture,
		const FVector2D& IconSize, const FMargin& NormalPadding,
		const FMargin& PressedPadding, bool bUnifyPadding,
		const FXRU1UIButtonPalette& Palette)
	{
		if (!Button)
		{
			return;
		}

		FButtonStyle ButtonStyle = Button->GetStyle();
		ButtonStyle.NormalPadding = NormalPadding;
		ButtonStyle.PressedPadding = bUnifyPadding ? NormalPadding : PressedPadding;
		ButtonStyle.Normal.TintColor = FSlateColor(Palette.NormalBackground);
		ButtonStyle.Hovered.TintColor = FSlateColor(Palette.HoveredBackground);
		ButtonStyle.Pressed.TintColor = FSlateColor(Palette.PressedBackground);

		// У многих стандартных UButton Disabled — пустой brush. Клонируем форму
		// Normal, чтобы отключённая кнопка оставалась читаемой, но явно приглушённой.
		ButtonStyle.Disabled = ButtonStyle.Normal;
		ButtonStyle.Disabled.TintColor = FSlateColor(Palette.DisabledBackground);
		ButtonStyle.NormalForeground = FSlateColor(Palette.NormalForeground);
		ButtonStyle.HoveredForeground = FSlateColor(Palette.HoveredForeground);
		ButtonStyle.PressedForeground = FSlateColor(Palette.PressedForeground);
		ButtonStyle.DisabledForeground = FSlateColor(Palette.DisabledForeground);
		Button->SetStyle(ButtonStyle);
		Button->SetBackgroundColor(FLinearColor::White);
		Button->SetColorAndOpacity(FLinearColor::White);

		if (UImage* Icon = FindFirstChildImage(Button))
		{
			if (IconTexture)
			{
				Icon->SetBrushFromTexture(IconTexture);
			}
			Icon->SetDesiredSizeOverride(IconSize);
			Icon->SetColorAndOpacity(FLinearColor::White);
			Icon->SetBrushTintColor(FSlateColor::UseForeground());
		}
	};

	StyleIconButton(AttackBtn, Theme->AttackIcon, Theme->ActionIconSize,
		Theme->ActionButtonPadding, Theme->ActionButtonPressedPadding,
		Theme->bUnifyPressedPadding, Theme->ActionButtonPalette);
	StyleIconButton(OverwatchBtn, Theme->OverwatchIcon, Theme->ActionIconSize,
		Theme->ActionButtonPadding, Theme->ActionButtonPressedPadding,
		Theme->bUnifyPressedPadding, Theme->ActionButtonPalette);
	StyleIconButton(HunkerBtn, Theme->HunkerIcon, Theme->ActionIconSize,
		Theme->ActionButtonPadding, Theme->ActionButtonPressedPadding,
		Theme->bUnifyPressedPadding, Theme->ActionButtonPalette);
	StyleIconButton(AbilityBtn, Theme->AbilityIcon, Theme->ActionIconSize,
		Theme->ActionButtonPadding, Theme->ActionButtonPressedPadding,
		Theme->bUnifyPressedPadding, Theme->ActionButtonPalette);
	StyleIconButton(InteractBtn, Theme->InteractDefuseIcon, Theme->ActionIconSize,
		Theme->ActionButtonPadding, Theme->ActionButtonPressedPadding,
		Theme->bUnifyPressedPadding, Theme->ActionButtonPalette);
	StyleIconButton(SkipBtn, Theme->SkipIcon, Theme->ActionIconSize,
		Theme->ActionButtonPadding, Theme->ActionButtonPressedPadding,
		Theme->bUnifyPressedPadding, Theme->ActionButtonPalette);

	StyleIconButton(EndTurnBtn, Theme->EndTurnIcon, Theme->EndTurnIconSize,
		Theme->EndTurnButtonPadding, Theme->EndTurnButtonPressedPadding,
		Theme->bUnifyEndTurnPressedPadding, Theme->EndTurnButtonPalette);

	if (EnemyCountIcon)
	{
		if (Theme->EnemyCountIcon)
		{
			EnemyCountIcon->SetBrushFromTexture(Theme->EnemyCountIcon);
		}
		EnemyCountIcon->SetDesiredSizeOverride(Theme->EnemyCounterIconSize);
		EnemyCountIcon->SetColorAndOpacity(FLinearColor::White);
		EnemyCountIcon->SetBrushTintColor(FSlateColor(FLinearColor::White));
	}
	if (EnemyCounterBackground)
	{
		// Даже при Texture=None UBorder использует свой однотонный Slate brush:
		// shape остаётся в Designer, а визуальные параметры — только в UITheme.
		EnemyCounterBackground->SetBrushFromTexture(
			Theme->EnemyCounterBackgroundTexture);
		EnemyCounterBackground->SetBrushColor(
			Theme->EnemyCounterBackgroundColor);
		EnemyCounterBackground->SetPadding(
			Theme->EnemyCounterBackgroundPadding);
	}

	if (TargetCoverIcon)
	{
		TargetCoverIcon->SetDesiredSizeOverride(Theme->TargetCoverIconSize);
	}
}

void UTacticalHUDWidget::ApplyPortraitCardLayout()
{
	const UTacticalHUDStyleData* Theme = GetUITheme();
	if (!Theme || !SquadPanel)
	{
		return;
	}

	// Status/Cover — WidgetTree-переменные вложенного WBP. Родительский HUD
	// применяет общую тему после того, как BP уже создал портреты. Cover-ветка
	// создаётся программно, если её ещё нет в старой версии WBP_UnitPortrait:
	// это сохраняет совместимость и не требует хрупкой большой BP-цепочки.
	for (int32 Index = 0; Index < SquadPanel->GetChildrenCount(); ++Index)
	{
		UUserWidget* Card = Cast<UUserWidget>(SquadPanel->GetChildAt(Index));
		if (!Card || !Card->WidgetTree)
		{
			continue;
		}
		if (USizeBox* StatusSizeBox = Cast<USizeBox>(
			Card->WidgetTree->FindWidget(TEXT("StatusSizeBox"))))
		{
			StatusSizeBox->SetWidthOverride(Theme->PortraitStatusIconSize.X);
			StatusSizeBox->SetHeightOverride(Theme->PortraitStatusIconSize.Y);
			if (UHorizontalBoxSlot* StatusSlot =
				Cast<UHorizontalBoxSlot>(StatusSizeBox->Slot))
			{
				StatusSlot->SetPadding(Theme->PortraitStatusIconPadding);
				StatusSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		USizeBox* CoverSizeBox = Cast<USizeBox>(
			Card->WidgetTree->FindWidget(TEXT("CoverSizeBox")));
		UImage* CoverIcon = Cast<UImage>(
			Card->WidgetTree->FindWidget(TEXT("CoverIcon")));
		UHorizontalBox* HeaderRow = Cast<UHorizontalBox>(
			Card->WidgetTree->FindWidget(TEXT("HeaderRow")));

		if (!CoverSizeBox && HeaderRow)
		{
			CoverSizeBox = Card->WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("CoverSizeBox"));
			if (UHorizontalBoxSlot* CoverSlot =
				HeaderRow->AddChildToHorizontalBox(CoverSizeBox))
			{
				CoverSlot->SetPadding(Theme->PortraitCoverIconPadding);
				CoverSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		if (!CoverIcon && CoverSizeBox)
		{
			CoverIcon = Card->WidgetTree->ConstructWidget<UImage>(
				UImage::StaticClass(), TEXT("CoverIcon"));
			CoverSizeBox->AddChild(CoverIcon);
		}

		if (!CoverSizeBox || !CoverIcon)
		{
			continue;
		}

		CoverSizeBox->SetWidthOverride(Theme->PortraitCoverIconSize.X);
		CoverSizeBox->SetHeightOverride(Theme->PortraitCoverIconSize.Y);
		if (UHorizontalBoxSlot* CoverSlot =
			Cast<UHorizontalBoxSlot>(CoverSizeBox->Slot))
		{
			CoverSlot->SetPadding(Theme->PortraitCoverIconPadding);
			CoverSlot->SetVerticalAlignment(VAlign_Center);
		}
		CoverIcon->SetDesiredSizeOverride(Theme->PortraitCoverIconSize);
		CoverIcon->SetColorAndOpacity(FLinearColor::White);

		bool bResolved = false;
		const AUnitBase* Unit = GetPortraitUnit(Card, bResolved);
		UTexture2D* CoverTexture =
			bResolved ? Theme->GetCoverIconForUnit(Unit) : nullptr;
		if (CoverTexture)
		{
			CoverIcon->SetBrushFromTexture(CoverTexture);
			CoverSizeBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			CoverSizeBox->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UTacticalHUDWidget::RefreshAbilityIcon(AUnitBase* Selected)
{
	const UTacticalHUDStyleData* Theme = GetUITheme();
	UImage* Icon = AbilityBtn ? FindFirstChildImage(AbilityBtn) : nullptr;
	if (!Theme || !Icon)
	{
		return;
	}
	if (UTexture2D* AbilityTexture = Theme->GetAbilityIconForUnit(Selected))
	{
		Icon->SetBrushFromTexture(AbilityTexture);
	}
}

// --- Кнопки действий --------------------------------------------------------------

void UTacticalHUDWidget::RefreshActionButtons()
{
	// Перенесено из WBP: в Blueprint AND не short-circuit — Pure-цепочки от
	// невалидного S всё равно вычислялись и сыпали «Accessed None» на старте.
	//
	// Фаза входит в условие ЯВНО: держать серость в ход врага только на том,
	// что BP гасит всю ActionsPanel, — скрытая связанность с графом.
	// C++ самодостаточен, а disabled-панель в BP — просто дублирующая страховка.
	const ATacticalPlayerController* Controller = GetTacticalController();
	AUnitBase* Selected = Controller ? Controller->GetSelectedUnit() : nullptr;
	const UTacticalHUDStyleData* Theme = GetUITheme();
	RefreshAbilityIcon(Selected);

	// Без выбранного бойца или вне фазы игрока панель действий прячется целиком:
	// ряд серых квадратов без владельца читается как сломанный HUD, а не как
	// «выберите бойца». RefreshActionButtons вызывается на смене выбора и фазы,
	// поэтому видимость всегда актуальна.
	if (ActionsPanel)
	{
		const UTurnManagerSubsystem* Turns = GetTurnManager();
		const bool bPlayerPhase = Turns && Turns->GetCurrentPhase() == ETurnPhase::Player;
		ActionsPanel->SetVisibility(Selected && bPlayerPhase
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	auto SetEnabled = [](UWidget* Widget, bool bEnabled)
	{
		if (Widget)
		{
			Widget->SetIsEnabled(bEnabled);
		}
	};

	// Живой экземпляр классовой способности: у него, в отличие от CDO, актуален
	// остаток применений — а именно его игроку и надо показать.
	auto GetSelectedClassAbility = [](AUnitBase* Unit) -> const UTacticalAbility*
	{
		if (!Unit || !Unit->ClassAbilityClass)
		{
			return nullptr;
		}
		UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent();
		const FGameplayAbilitySpec* Spec = ASC
			? ASC->FindAbilitySpecFromClass(Unit->ClassAbilityClass) : nullptr;
		if (!Spec)
		{
			return nullptr;
		}
		if (const UTacticalAbility* Instance = Cast<UTacticalAbility>(Spec->GetPrimaryInstance()))
		{
			return Instance;
		}
		// Способность ещё ни разу не активировалась — экземпляра нет, но CDO уже
		// знает имя, стоимость и лимит.
		return Unit->ClassAbilityClass->GetDefaultObject<UTacticalAbility>();
	};

	// Кнопка «Огонь» гасится и без единой доступной цели (как в XCOM) — иначе
	// игрок вооружает прицеливание вслепую и узнаёт об этом только по ховеру.
	SetEnabled(AttackBtn, Controller &&
		Controller->CanIssueCommand(ETacticalPlayerCommand::Attack));
	SetEnabled(OverwatchBtn, Controller &&
		Controller->CanIssueCommand(ETacticalPlayerCommand::Overwatch));
	SetEnabled(HunkerBtn, Controller &&
		Controller->CanIssueCommand(ETacticalPlayerCommand::HunkerDown));
	SetEnabled(SkipBtn, Controller &&
		Controller->CanIssueCommand(ETacticalPlayerCommand::SkipUnitTurn));
	// Пассивка Осы (ClassAbilityClass пуст): кнопка — ИНДИКАТОР «Прицела отряда».
	//
	// ⚠️ Признак «Squadsight сейчас работает» — это «цель ДАЛЬШЕ собственного
	// обзора и всё равно доступна». Условие «доступна, но своей LOS нет»
	// недостижимо по построению: `GetTargetStatus`
	// требует геометрическую линию огня ВСЕГДА, значит `CanTargetActor` и
	// `!HasLineOfSight` не могут быть истинны одновременно — кнопка была бы серой
	// навсегда. Разбор — docs/03_ARCHITECTURE.md §4.
	bool bAbilityEnabled = Controller &&
		Controller->CanIssueCommand(ETacticalPlayerCommand::ClassAbility);
	if (Selected && !Selected->ClassAbilityClass)
	{
		bAbilityEnabled = false;
		if (const UTurnManagerSubsystem* Turns2 = GetTurnManager())
		{
			for (AActor* Enemy : Turns2->GetOpposingUnits(Selected))
			{
				if (!Enemy)
				{
					continue;
				}
				const float Distance =
					FVector::Dist(Selected->GetActorLocation(), Enemy->GetActorLocation());
				if (Distance > UTacticsCombatStatics::SquadVisionRange &&
					UGA_Attack::CanTargetActor(Selected, Enemy))
				{
					bAbilityEnabled = true;
					break;
				}
			}
		}
	}
	SetEnabled(AbilityBtn, bAbilityEnabled);

	// ⚠️ ПОДСКАЗКА, а не подпись. Переименовать кнопку в вёрстке нельзя из кода,
	// поэтому имя способности, остаток применений и стоимость уходят в tooltip —
	// он живёт на самой кнопке и не требует нового виджета. Без него у всех
	// классов на кнопке стоит родовое «классовая способность», а сколько
	// осталось зарядов, не видно нигде.
	if (AbilityBtn)
	{
		FText AbilityTip;
		if (const UTacticalAbility* Ability = GetSelectedClassAbility(Selected))
		{
			AbilityTip = Ability->GetTooltipText();
		}
		else if (Selected && !Selected->ClassAbilityClass)
		{
			// Пассивка Осы: у неё нет ability-объекта, но объяснить кнопку надо.
			AbilityTip = NSLOCTEXT("XRU1", "AbilitySquadsight",
				"Прицел отряда\nСнайпер стреляет по цели, которую видит союзник.\n"
				"Индикатор загорается, когда такая цель есть.");
		}
		AbilityBtn->SetToolTipText(AbilityTip);
	}

	// Те же подсказки для остальных действий — из их же способностей, чтобы
	// стоимость и правило «завершает ход» не пришлось дублировать текстом в BP.
	auto SetAbilityTip = [](UButton* Button, const TSubclassOf<UTacticalAbility>& AbilityClass)
	{
		if (Button && AbilityClass)
		{
			Button->SetToolTipText(AbilityClass->GetDefaultObject<UTacticalAbility>()->GetTooltipText());
		}
	};
	if (Selected)
	{
		SetAbilityTip(AttackBtn, Selected->AttackAbilityClass);
		SetAbilityTip(OverwatchBtn, Selected->OverwatchAbilityClass);
		SetAbilityTip(HunkerBtn, Selected->HunkerAbilityClass);
	}

	// Кнопки без способности за спиной объясняются сами: молчащая кнопка в ряду
	// говорящих читается как недоделанная.
	if (SkipBtn)
	{
		SkipBtn->SetToolTipText(NSLOCTEXT("XRU1", "TipSkip",
			"Пропустить бойца\nХод переходит следующему, очки действия сгорают."));
	}

	// Контекстное F: вид интеракции определяет и доступность, и иконку.
	const EInteractionKind Interaction = Controller
		? Controller->GetAvailableInteraction()
		: EInteractionKind::None;
	SetEnabled(InteractBtn, Controller &&
		Controller->CanIssueCommand(ETacticalPlayerCommand::Interact));

	// Подсказка контекстного действия зависит от того, что рядом с бойцом.
	if (InteractBtn)
	{
		InteractBtn->SetToolTipText(Interaction == EInteractionKind::Evacuate
			? NSLOCTEXT("XRU1", "TipEvac", "Эвакуация\nБоец покидает поле боя из зоны эвакуации.")
			: Interaction == EInteractionKind::DefuseBomb
				? NSLOCTEXT("XRU1", "TipDefuse", "Обезвредить заряд\nТребуется несколько ходов рядом с зарядом.")
				: NSLOCTEXT("XRU1", "TipInteractNone", "Взаимодействие\nПодойдите к цели миссии."));
	}

	if (InteractIcon && Theme)
	{
		UTexture2D* KindIcon = (Interaction == EInteractionKind::Evacuate)
			? Theme->InteractEvacIcon.Get()
			: Theme->InteractDefuseIcon.Get();
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
		// Разные тексты для «слишком далеко» и «нет линии огня»: схлопнутые в
		// один -1 причины заставляли HUD всегда писать «Нет линии огня»,
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
		case EAttackTargetStatus::OutOfSight:
			HitChanceText->SetText(INVTEXT("Цель вне обзора"));
			break;
		default:
			HitChanceText->SetText(INVTEXT("Нет линии огня"));
			break;
		}
	}
	if (TargetCoverIcon)
	{
		// Три состояния XCOM: синий щит — укрытие работает против нашего
		// стрелка; жёлтый — цель В укрытии, но мы зашли во фланг (шанс НЕ
		// снижен); нет щита — цель в чистом поле. Схлопнуть жёлтое и «нет щита»
		// в отсутствие иконки нельзя: заработанный манёвром фланг ничем
		// не отличался бы от открытой цели.
		ECoverType ShieldCover = ECoverType::None;
		const ECoverShield Shield = UTacticsCombatStatics::GetCoverShieldAgainst(
			Hovered, Shooter, ShieldCover);

		UTexture2D* CoverTexture = nullptr;
		FLinearColor ShieldTint = FLinearColor::White;
		if (const UTacticalHUDStyleData* Theme = GetUITheme())
		{
			CoverTexture = Theme->GetCoverShieldIcon(Shield, ShieldCover);
			ShieldTint = Theme->GetCoverShieldTint(Shield);
		}
		if (Shield != ECoverShield::None)
		{
			if (CoverTexture)
			{
				TargetCoverIcon->SetBrushFromTexture(CoverTexture);
			}
			TargetCoverIcon->SetColorAndOpacity(ShieldTint);
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

// --- Баннер подкреплений врага -----------------------------------------------------

UWidget* UTacticalHUDWidget::GetReinforcementBannerWidget() const
{
	if (ReinforcementPanel)
	{
		return ReinforcementPanel;
	}
	return ReinforcementText;
}

void UTacticalHUDWidget::HandleQuestEventForReinforcements(FGameplayTag Channel,
	const FQuestEventData& /*Payload*/)
{
	// Каналы сравниваются точно (leaf) — как у слоя VFX: родительские дубли
	// одного события не должны дёргать баннер дважды.
	if (Channel == TacticalQuestTags::Event_Tactical_Reinforcements_Signaled)
	{
		// Новый сигнал важнее висящего «высадилось»: началась следующая волна.
		bReinforcementArrivedShowing = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ArrivedBannerTimer);
		}
		RefreshReinforcementBanner();
	}
	else if (Channel == TacticalQuestTags::Event_Tactical_Reinforcements_Arrived)
	{
		UWidget* Banner = GetReinforcementBannerWidget();
		if (!Banner)
		{
			return;
		}
		bReinforcementArrivedShowing = true;
		if (ReinforcementText)
		{
			ReinforcementText->SetText(NSLOCTEXT("XRU1.HUD", "ReinforcementArrived",
				"ПОДКРЕПЛЕНИЕ ПРОТИВНИКА ВЫСАДИЛОСЬ"));
		}
		Banner->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(ArrivedBannerTimer, this,
				&UTacticalHUDWidget::HandleArrivedBannerExpired,
				FMath::Max(0.5f, ReinforcementArrivedBannerSeconds), false);
		}
	}
}

void UTacticalHUDWidget::HandleArrivedBannerExpired()
{
	bReinforcementArrivedShowing = false;
	RefreshReinforcementBanner();
}

void UTacticalHUDWidget::RefreshReinforcementBanner()
{
	UWidget* Banner = GetReinforcementBannerWidget();
	if (!Banner)
	{
		return; // в WBP баннера нет — HUD работает без него
	}
	if (bReinforcementArrivedShowing)
	{
		return; // короткое «высадилось» отсчёт не перетирает
	}

	// Источник — сами маяки уровня: событие сигнала лишь повод перечитать их
	// состояние, поэтому баннер не может разойтись с фактическим отсчётом
	// (например, после пересоздания HUD посреди боя).
	int32 TurnsLeft = -1;
	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	if (TurnManager && TurnManager->IsInCombat())
	{
		for (TActorIterator<ATacticalReinforcementBeacon> It(GetWorld()); It; ++It)
		{
			if (It->IsWavePending())
			{
				TurnsLeft = FMath::Max(TurnsLeft, It->GetCountdownLeft());
			}
		}
	}

	if (TurnsLeft < 0)
	{
		Banner->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	if (ReinforcementText)
	{
		// Отсчёт в ходах ВРАГА, как у маяка: «через 1 ход» = волна высадится в
		// начале ближайшей вражеской фазы, на реакцию остаётся текущий ход.
		ReinforcementText->SetText(FText::Format(
			NSLOCTEXT("XRU1.HUD", "ReinforcementIncoming",
				"ПОДКРЕПЛЕНИЕ ПРОТИВНИКА ЧЕРЕЗ {0} {0}|plural(one=ХОД,few=ХОДА,many=ХОДОВ,other=ХОДА)"),
			FMath::Max(1, TurnsLeft)));
	}
	Banner->SetVisibility(ESlateVisibility::HitTestInvisible);
}

// --- Карточки отряда (XCOM-стиль) -------------------------------------------------

void UTacticalHUDWidget::UpdateSquadCardVisibility(AUnitBase* Selected)
{
	if (!SquadPanel)
	{
		return;
	}

	// Фаза врага с активной карточкой врага: панель показывает только её —
	// карточки отряда скрываются независимо от bShowOnlySelectedCard.
	if (ActiveEnemyCard && ActiveEnemyCard->GetParent() == SquadPanel)
	{
		for (int32 Index = 0; Index < SquadPanel->GetChildrenCount(); ++Index)
		{
			UWidget* Child = SquadPanel->GetChildAt(Index);
			if (Child)
			{
				Child->SetVisibility(Child == ActiveEnemyCard
					? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
		return;
	}

	if (!bShowOnlySelectedCard)
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
			UE_LOG(LogXRU1UI, Warning, TEXT("[HUD] В %s нет переменной «Unit» — карточки отряда ")
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
			UCoverDetectionComponent* Cover =
				Unit ? Unit->GetCoverDetection() : nullptr;
			if (Cover && !CoverSubscribedComponents.Contains(Cover))
			{
				Cover->OnCoverStateChanged.AddDynamic(
					this, &UTacticalHUDWidget::HandleUnitCoverStateChanged);
				CoverSubscribedComponents.Add(Cover);
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
		// Состав сторон меняется посреди боя (staged-бойцы туториала): панель
		// отряда и подписки на state обязаны пересобраться, иначе новые бойцы
		// не получают карточек, а убранные продолжают «гореть».
		TurnManager->OnUnitsChanged.AddDynamic(this, &UTacticalHUDWidget::HandleCombatUnitsChanged);
		TurnManager->OnTurnLimitChanged.AddDynamic(this, &UTacticalHUDWidget::HandleTurnLimitChanged);
		// Карточка действующего врага в его фазу (низ-лево, кликать нельзя).
		TurnManager->OnEnemyUnitActivated.AddDynamic(this, &UTacticalHUDWidget::HandleEnemyUnitActivated);

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

	// Баннер подкреплений слушает шину квест-событий (тот же вход, что у слоёв
	// VFX и реплик): сигнал маяка и высадка волны — подтверждённые факты
	// механики, а не предположения UI о состоянии маяков.
	if (UGameplayMessageSubsystem::HasInstance(this))
	{
		QuestEventListenerHandle = UGameplayMessageSubsystem::Get(this).RegisterListener<FQuestEventData>(
			QuestGameplayTags::Quest_Event,
			[this](FGameplayTag Channel, const FQuestEventData& Data)
			{
				HandleQuestEventForReinforcements(Channel, Data);
			},
			EGameplayMessageMatch::PartialMatch);
	}
	RefreshReinforcementBanner();

	// Клавиатура принадлежит игре, а не кнопкам HUD (фикс «пробел повторяет
	// последнее нажатое действие»).
	ApplyPortraitCardLayout();
	EnsureButtonsDontStealFocus();
}

void UTacticalHUDWidget::EnsureButtonsDontStealFocus()
{
	// Рекурсивный обход: своё дерево + деревья вложенных UserWidget'ов
	// (портреты отряда и т.п. — их кнопки живут в СВОИХ WidgetTree).
	TArray<UUserWidget*, TInlineAllocator<16>> Pending;
	Pending.Add(this);
	while (Pending.Num() > 0)
	{
		UUserWidget* Current = Pending.Pop(EAllowShrinking::No);
		if (!Current || !Current->WidgetTree)
		{
			continue;
		}
		Current->WidgetTree->ForEachWidget([this, &Pending](UWidget* Widget)
		{
			if (UButton* Button = Cast<UButton>(Widget))
			{
				Button->OnClicked.AddUniqueDynamic(this, &UTacticalHUDWidget::HandleAnyButtonClickedResetFocus);
			}
			else if (UUserWidget* Nested = Cast<UUserWidget>(Widget))
			{
				Pending.Add(Nested);
			}
		});
	}
}

void UTacticalHUDWidget::HandleAnyButtonClickedResetFocus()
{
	// Вернуть фокус вьюпорту: иначе кликнутая кнопка держит его, и следующий
	// пробел/Enter «нажимает» её повторно средствами Slate — мимо Enhanced Input.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}

	// Звук клика — здесь же: подписка уже рекурсивная по всем кнопкам HUD, и
	// один хук дешевле, чем ручной вызов в каждом обработчике.
	if (UTacticsAudioSubsystem* Audio = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
	{
		Audio->PlayUIClick();
	}
}

void UTacticalHUDWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EnemyCardVisibilityTimer);
		World->GetTimerManager().ClearTimer(ArrivedBannerTimer);
	}
	if (QuestEventListenerHandle.IsValid())
	{
		QuestEventListenerHandle.Unregister();
	}
	if (AttackBtn)
	{
		AttackBtn->OnClicked.RemoveDynamic(this, &UTacticalHUDWidget::HandleAttackClicked);
	}
	if (UTurnManagerSubsystem* TurnManager = GetTurnManager())
	{
		TurnManager->OnTurnStarted.RemoveDynamic(this, &UTacticalHUDWidget::HandleTurnStarted);
		TurnManager->OnCombatEnded.RemoveDynamic(this, &UTacticalHUDWidget::HandleCombatEnded);
		TurnManager->OnTurnLimitChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleTurnLimitChanged);
		TurnManager->OnUnitsChanged.RemoveDynamic(this, &UTacticalHUDWidget::HandleCombatUnitsChanged);
		TurnManager->OnEnemyUnitActivated.RemoveDynamic(this, &UTacticalHUDWidget::HandleEnemyUnitActivated);
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
	for (const TWeakObjectPtr<UCoverDetectionComponent>& Component :
		CoverSubscribedComponents)
	{
		if (UCoverDetectionComponent* Alive = Component.Get())
		{
			Alive->OnCoverStateChanged.RemoveDynamic(
				this, &UTacticalHUDWidget::HandleUnitCoverStateChanged);
		}
	}
	CoverSubscribedComponents.Reset();
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

	// Возврат хода игроку (или конец боя) убирает карточку действующего врага.
	if (Phase != ETurnPhase::Enemy)
	{
		ClearActiveEnemyCard();
	}

	const UTurnManagerSubsystem* TurnManager = GetTurnManager();
	OnPhaseChanged(Phase,
		TurnManager ? TurnManager->GetTurnNumber() : 0,
		TurnManager ? TurnManager->GetTurnsRemaining() : -1);
	RefreshActionButtons();

	// Смена фазы меняет и право стрелять: панель цели под курсором должна
	// исчезнуть на ход врага и вернуться в наш (курсор мог не двигаться).
	// Прицеливание фаза тоже сбрасывает — баннер обязан погаснуть.
	UpdateTargetingBanner();
	// Отсчёт подкрепления тикает по фазам врага — перечитать его на границе.
	RefreshReinforcementBanner();
	if (const ATacticalPlayerController* Controller = GetTacticalController())
	{
		UpdateTargetPanel(Controller->GetHoveredUnit());
	}
}

void UTacticalHUDWidget::HandleCombatEnded(bool bPlayerWon)
{
	// Исход объявлен — отсчёт и «высадилось» больше не в тему.
	bReinforcementArrivedShowing = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ArrivedBannerTimer);
	}
	if (UWidget* Banner = GetReinforcementBannerWidget())
	{
		Banner->SetVisibility(ESlateVisibility::Collapsed);
	}
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

void UTacticalHUDWidget::HandleCombatUnitsChanged()
{
	// Новые бойцы: подписаться на их state (AddUniqueDynamic — дубли безопасны),
	// пересобрать карточки под новый состав и обновить всё остальное.
	SubscribeToUnitStates();
	RebuildSquadPanel();
	HandleUnitStateChanged();
}

void UTacticalHUDWidget::RebuildSquadPanel()
{
	// Класс карточки известен только из уже построенной BP панели: пустая панель
	// означает, что Construct ещё не отработал — он сам построит актуальный состав.
	// Класс кэшируется: если состав временно опустеет, следующий ребилд не должен
	// остаться без образца.
	if (!SquadPanel)
	{
		return;
	}

	// Карточка врага не входит в состав отряда: на время сравнения/пересборки
	// снимается с панели и возвращается на любом пути выхода, пока идёт
	// активация её врага.
	RemoveActiveEnemyCardFromPanel();
	ON_SCOPE_EXIT
	{
		if (ActiveEnemyUnit.IsValid())
		{
			ShowActiveEnemyCard(ActiveEnemyUnit.Get());
		}
	};

	UClass* CardClass = ResolvePortraitCardClass();
	if (!CardClass)
	{
		return;
	}
	const TArray<AUnitBase*> Squad = GetSquad();

	// Состав не менялся — панель не трогаем: пересоздание карточек сбрасывает
	// их внутренние анимации и стоит мусора на каждом событии state.
	if (SquadPanel->GetChildrenCount() == Squad.Num())
	{
		bool bSame = true;
		for (int32 Index = 0; Index < Squad.Num(); ++Index)
		{
			bool bResolved = false;
			if (GetPortraitUnit(Cast<UUserWidget>(SquadPanel->GetChildAt(Index)), bResolved)
					!= Squad[Index] || !bResolved)
			{
				bSame = false;
				break;
			}
		}
		if (bSame)
		{
			return;
		}
	}

	const FObjectProperty* UnitProperty =
		FindFProperty<FObjectProperty>(CardClass, TEXT("Unit"));
	if (!UnitProperty)
	{
		// Без свойства «Unit» новая карточка не узнает своего бойца — оставляем
		// панель BP как есть (fail-open, тот же контракт, что у GetPortraitUnit).
		return;
	}

	SquadPanel->ClearChildren();
	for (AUnitBase* Unit : Squad)
	{
		UUserWidget* Card = CreateWidget<UUserWidget>(GetOwningPlayer(), CardClass);
		if (!Card)
		{
			continue;
		}
		// Unit проставляется ДО добавления в дерево: Construct карточки читает его
		// так же, как при CreateWidget с ExposeOnSpawn-пином в BP.
		UnitProperty->SetObjectPropertyValue_InContainer(Card, Unit);
		SquadPanel->AddChild(Card);
	}
	UE_LOG(LogXRU1UI, Display, TEXT("[HUD] Панель отряда пересобрана: %d карточек"), Squad.Num());

	ApplyPortraitCardLayout();
	EnsureButtonsDontStealFocus();
	if (const ATacticalPlayerController* Controller = GetTacticalController())
	{
		UpdateSquadCardVisibility(Controller->GetSelectedUnit());
	}
}

// --- Карточка действующего врага (фаза Enemy) --------------------------------

void UTacticalHUDWidget::HandleEnemyUnitActivated(AActor* Unit)
{
	AUnitBase* EnemyUnit = Cast<AUnitBase>(Unit);
	if (!EnemyUnit)
	{
		return;
	}

	// Предыдущая карточка снимается сразу: активировался другой боец.
	RemoveActiveEnemyCardFromPanel();
	ActiveEnemyUnit = EnemyUnit;

	// Видимость проверяется не только сейчас: враг часто активируется за
	// укрытием и выходит из тумана уже в движении — тогда карточка появится
	// по ходу его хода, а не «никогда».
	RefreshActiveEnemyCardVisibility();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(EnemyCardVisibilityTimer, this,
			&UTacticalHUDWidget::RefreshActiveEnemyCardVisibility,
			FMath::Max(0.05f, EnemyCardVisibilityCheckInterval), /*bLoop=*/true);
	}
}

void UTacticalHUDWidget::RefreshActiveEnemyCardVisibility()
{
	AUnitBase* EnemyUnit = ActiveEnemyUnit.Get();
	if (!EnemyUnit || !UTacticsCombatStatics::IsUnitAlive(EnemyUnit))
	{
		ClearActiveEnemyCard();
		return;
	}

	// Туман войны: невидимый отряду враг не раскрывается карточкой (docs/03_ARCHITECTURE.md §11).
	const UWorld* World = GetWorld();
	const UFogOfWarSubsystem* Fog = World ? World->GetSubsystem<UFogOfWarSubsystem>() : nullptr;
	const bool bVisible = !Fog || Fog->IsActorCurrentlyVisible(EnemyUnit);

	// Пересоздаём и когда карточки нет, и когда BP пересобрал панель под нами
	// (карточка жива, но уже не в SquadPanel — на экране её не видно).
	if (bVisible && (!ActiveEnemyCard || ActiveEnemyCard->GetParent() != SquadPanel))
	{
		ShowActiveEnemyCard(EnemyUnit);
	}
	else if (!bVisible && ActiveEnemyCard)
	{
		// Ушёл обратно в туман — карточка гаснет, но враг остаётся отслеживаемым.
		RemoveActiveEnemyCardFromPanel();
		const ATacticalPlayerController* Controller = GetTacticalController();
		UpdateSquadCardVisibility(Controller ? Controller->GetSelectedUnit() : nullptr);
	}
}

UClass* UTacticalHUDWidget::ResolvePortraitCardClass()
{
	if (PortraitCardClass)
	{
		return PortraitCardClass;
	}
	if (CachedPortraitClass)
	{
		return CachedPortraitClass;
	}
	// Образец у уже построенной BP панели: класс карточки нигде больше не задан.
	if (SquadPanel)
	{
		for (int32 Index = 0; Index < SquadPanel->GetChildrenCount(); ++Index)
		{
			if (const UUserWidget* Card = Cast<UUserWidget>(SquadPanel->GetChildAt(Index)))
			{
				CachedPortraitClass = Card->GetClass();
				break;
			}
		}
	}
	return CachedPortraitClass;
}

void UTacticalHUDWidget::ShowActiveEnemyCard(AUnitBase* EnemyUnit)
{
	if (!SquadPanel || !EnemyUnit)
	{
		return;
	}
	UClass* CardClass = ResolvePortraitCardClass();
	const FObjectProperty* UnitProperty = CardClass
		? FindFProperty<FObjectProperty>(CardClass, TEXT("Unit")) : nullptr;
	if (!UnitProperty)
	{
		// Тихий отказ выглядит как «фича не работает» — причина должна быть
		// видна в логе (один раз, тем же флагом, что и у карточек отряда).
		if (!bPortraitUnitLookupWarned)
		{
			bPortraitUnitLookupWarned = true;
			UE_LOG(LogXRU1UI, Warning,
				TEXT("[HUD] Карточка действующего врага не создана: %s. Задай PortraitCardClass ")
				TEXT("в Class Defaults WBP_TacticalHUD."),
				CardClass ? TEXT("в классе карточки нет переменной «Unit»")
						  : TEXT("неизвестен класс карточки отряда"));
		}
		return;
	}

	// Карточка пересоздаётся на каждую активацию: Construct вложенного WBP
	// читает Unit один раз, повторная смена значения его не обновила бы.
	RemoveActiveEnemyCardFromPanel();
	ActiveEnemyUnit = EnemyUnit;

	UUserWidget* Card = CreateWidget<UUserWidget>(GetOwningPlayer(), CardClass);
	if (!Card)
	{
		return;
	}
	UnitProperty->SetObjectPropertyValue_InContainer(Card, EnemyUnit);
	SquadPanel->AddChild(Card);
	// Кликать в карточку врага нельзя — только информирование.
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);
	ActiveEnemyCard = Card;

	ApplyPortraitCardLayout();
	UpdateSquadCardVisibility(nullptr);
}

void UTacticalHUDWidget::RemoveActiveEnemyCardFromPanel()
{
	if (ActiveEnemyCard)
	{
		ActiveEnemyCard->RemoveFromParent();
		ActiveEnemyCard = nullptr;
	}
}

void UTacticalHUDWidget::ClearActiveEnemyCard()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EnemyCardVisibilityTimer);
	}
	const bool bHadCard = ActiveEnemyCard != nullptr;
	RemoveActiveEnemyCardFromPanel();
	ActiveEnemyUnit.Reset();
	if (bHadCard)
	{
		// Вернуть обычную XCOM-видимость карточек отряда.
		const ATacticalPlayerController* Controller = GetTacticalController();
		UpdateSquadCardVisibility(Controller ? Controller->GetSelectedUnit() : nullptr);
	}
}

void UTacticalHUDWidget::HandleUnitStateChanged()
{
	OnUnitsStateChanged();
	RefreshActionButtons();
	ApplyPortraitCardLayout();

	// BP мог пересобрать портреты (смерть/эвакуация) — у новых кнопок тоже
	// не должно оставаться фокуса после клика (AddUniqueDynamic, дубли не страшны).
	EnsureButtonsDontStealFocus();
}

void UTacticalHUDWidget::HandleUnitCoverStateChanged(ECoverType /*NewCover*/)
{
	// Overhead cover-widget обновляется своим delegate; здесь нужен только
	// второй consumer — cover badge внутри Unit Flag выбранного бойца.
	ApplyPortraitCardLayout();
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
