#include "TacticalHUDStyleData.h"

#include "AbilitySystemComponent.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "CoverDetectionComponent.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameplayTags.h"
#include "UnitBase.h"
#include "UnitClasses.h"

namespace
{
	template <typename TUnit>
	bool IsUnitClass(const AUnitBase* Unit)
	{
		return Unit && Unit->IsA<TUnit>();
	}

	bool IsNegativeMargin(const FMargin& Margin)
	{
		return Margin.Left < 0.f || Margin.Top < 0.f ||
			Margin.Right < 0.f || Margin.Bottom < 0.f;
	}
}

UTexture2D* UTacticalHUDStyleData::GetPortraitForUnit(const AUnitBase* Unit) const
{
	if (!Unit)
	{
		return nullptr;
	}
	if (IsUnitClass<AUnit_Assault>(Unit))
	{
		return AssaultPortrait.Get();
	}
	if (IsUnitClass<AUnit_Sniper>(Unit))
	{
		return SniperPortrait.Get();
	}
	if (IsUnitClass<AUnit_Healer>(Unit))
	{
		return MedicPortrait.Get();
	}
	if (IsUnitClass<AUnit_Tank>(Unit))
	{
		return TankPortrait.Get();
	}

	// По GDD единственный прочий тип — BP_Unit_Marauder от AUnitBase.
	return MarauderPortrait.Get();
}

UTexture2D* UTacticalHUDStyleData::GetClassIconForUnit(const AUnitBase* Unit) const
{
	if (!Unit)
	{
		return nullptr;
	}
	if (IsUnitClass<AUnit_Assault>(Unit))
	{
		return AssaultClassIcon.Get();
	}
	if (IsUnitClass<AUnit_Sniper>(Unit))
	{
		return SniperClassIcon.Get();
	}
	if (IsUnitClass<AUnit_Healer>(Unit))
	{
		return MedicClassIcon.Get();
	}
	if (IsUnitClass<AUnit_Tank>(Unit))
	{
		return TankClassIcon.Get();
	}
	return MarauderClassIcon.Get();
}

UTexture2D* UTacticalHUDStyleData::GetAbilityIconForUnit(const AUnitBase* Unit) const
{
	UTexture2D* SpecificIcon = nullptr;
	if (IsUnitClass<AUnit_Assault>(Unit))
	{
		SpecificIcon = AssaultAbilityIcon.Get();
	}
	else if (IsUnitClass<AUnit_Sniper>(Unit))
	{
		SpecificIcon = SniperAbilityIcon.Get();
	}
	else if (IsUnitClass<AUnit_Healer>(Unit))
	{
		SpecificIcon = MedicAbilityIcon.Get();
	}
	else if (IsUnitClass<AUnit_Tank>(Unit))
	{
		SpecificIcon = TankAbilityIcon.Get();
	}
	return SpecificIcon ? SpecificIcon : AbilityIcon.Get();
}

UTexture2D* UTacticalHUDStyleData::GetCoverIcon(ECoverType Cover) const
{
	switch (Cover)
	{
	case ECoverType::Half:
		return HalfCoverIcon.Get();
	case ECoverType::Full:
		return FullCoverIcon.Get();
	default:
		return nullptr;
	}
}

UTexture2D* UTacticalHUDStyleData::GetCoverIconForUnit(
	const AUnitBase* Unit) const
{
	const UCoverDetectionComponent* Cover =
		Unit ? Unit->GetCoverDetection() : nullptr;
	return Cover ? GetCoverIcon(Cover->BestCoverAround) : nullptr;
}

UTexture2D* UTacticalHUDStyleData::GetStatusIcon(EXRU1UIStatusIcon Status) const
{
	switch (Status)
	{
	case EXRU1UIStatusIcon::Overwatch:
		return StatusOverwatchIcon ? StatusOverwatchIcon.Get() : OverwatchIcon.Get();
	case EXRU1UIStatusIcon::HunkerDown:
		return StatusHunkerIcon ? StatusHunkerIcon.Get() : HunkerIcon.Get();
	case EXRU1UIStatusIcon::Taunt:
		return StatusTauntIcon.Get();
	case EXRU1UIStatusIcon::Downed:
		return StatusDownedIcon.Get();
	case EXRU1UIStatusIcon::Evacuated:
		return StatusEvacuatedIcon ? StatusEvacuatedIcon.Get() : InteractEvacIcon.Get();
	case EXRU1UIStatusIcon::Moving:
		return StatusMovingIcon ? StatusMovingIcon.Get() : MoveIcon.Get();
	default:
		return nullptr;
	}
}

EXRU1UIStatusIcon UTacticalHUDStyleData::GetStatusForUnit(const AUnitBase* Unit) const
{
	if (!Unit)
	{
		return EXRU1UIStatusIcon::None;
	}
	if (Unit->IsDowned())
	{
		return EXRU1UIStatusIcon::Downed;
	}
	if (Unit->IsEvacuated())
	{
		return EXRU1UIStatusIcon::Evacuated;
	}

	if (const UAbilitySystemComponent* ASC = Unit->GetAbilitySystemComponent())
	{
		if (ASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Taunting))
		{
			return EXRU1UIStatusIcon::Taunt;
		}
		if (ASC->HasMatchingGameplayTag(TacticsGameplayTags::State_HunkeredDown))
		{
			return EXRU1UIStatusIcon::HunkerDown;
		}
		if (ASC->HasMatchingGameplayTag(TacticsGameplayTags::State_Overwatch))
		{
			return EXRU1UIStatusIcon::Overwatch;
		}
	}

	// Path-following становится активным в кадр выдачи приказа, ещё до разгона
	// CharacterMovement. Поэтому velocity здесь давала поздний/мигающий статус.
	return UTacticsCombatStatics::IsUnitInTransit(Unit)
		? EXRU1UIStatusIcon::Moving
		: EXRU1UIStatusIcon::None;
}

UTexture2D* UTacticalHUDStyleData::GetStatusIconForUnit(const AUnitBase* Unit) const
{
	return GetStatusIcon(GetStatusForUnit(Unit));
}

FXRU1UIScreenArtwork UTacticalHUDStyleData::GetScreenArtwork(EXRU1UIScreenArt Screen) const
{
	switch (Screen)
	{
	case EXRU1UIScreenArt::MainMenu: return MainMenuArt;
	case EXRU1UIScreenArt::Difficulty: return DifficultyArt;
	case EXRU1UIScreenArt::Settings: return SettingsArt;
	case EXRU1UIScreenArt::About: return AboutArt;
	case EXRU1UIScreenArt::Pause: return PauseArt;
	case EXRU1UIScreenArt::TutorialBriefing: return TutorialBriefingArt;
	case EXRU1UIScreenArt::MissionBriefing: return MissionBriefingArt;
	case EXRU1UIScreenArt::VictoryResult: return VictoryResultArt;
	case EXRU1UIScreenArt::DefeatResult: return DefeatResultArt;
	case EXRU1UIScreenArt::DemoComplete: return DemoCompleteArt;
	case EXRU1UIScreenArt::IntroFallback: return IntroFallbackArt;
	default: return FXRU1UIScreenArtwork();
	}
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UTacticalHUDStyleData::IsDataValid(FDataValidationContext& Context) const
{
	// UDataAsset по умолчанию возвращает NotValidated. Наш валидатор должен
	// завершаться Valid, если собственные проверки не нашли ошибок.
	EDataValidationResult Result = CombineDataValidationResults(
		Super::IsDataValid(Context), EDataValidationResult::Valid);

	auto RequirePositiveSize = [&Context, &Result](const FVector2D& Size, const TCHAR* Name)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("UITheme: %s должен иметь положительные X и Y"), Name)));
			Result = EDataValidationResult::Invalid;
		}
	};

	auto ValidateBlock = [&Context, &Result](const FXRU1UIBlockLayout& Block, const TCHAR* Name)
	{
		if (Block.DesiredSize.X < 0.f || Block.DesiredSize.Y < 0.f ||
			IsNegativeMargin(Block.Padding) || IsNegativeMargin(Block.ItemSpacing))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("UITheme: блок %s содержит отрицательный размер или отступ"), Name)));
			Result = EDataValidationResult::Invalid;
		}
	};
	auto ValidateArtwork = [&Context, &Result](const FXRU1UIScreenArtwork& Artwork, const TCHAR* Name)
	{
		if (Artwork.DesiredSize.X < 0.f || Artwork.DesiredSize.Y < 0.f ||
			IsNegativeMargin(Artwork.Padding))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("UITheme: арт %s содержит отрицательный размер или отступ"), Name)));
			Result = EDataValidationResult::Invalid;
		}
	};

	auto WarnMissing = [&Context](const UObject* Asset, const TCHAR* Name)
	{
		if (!Asset)
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("UITheme: не заполнено обязательное поле %s"), Name)));
		}
	};
	auto WarnMissingSoft = [&Context](bool bIsNull, const TCHAR* Name)
	{
		if (bIsNull)
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("UITheme: не заполнено soft-reference поле %s"), Name)));
		}
	};

	RequirePositiveSize(ActionIconSize, TEXT("ActionIconSize"));
	RequirePositiveSize(EndTurnIconSize, TEXT("EndTurnIconSize"));
	RequirePositiveSize(PortraitImageSize, TEXT("PortraitImageSize"));
	RequirePositiveSize(PortraitClassIconSize, TEXT("PortraitClassIconSize"));
	RequirePositiveSize(PortraitStatusIconSize, TEXT("PortraitStatusIconSize"));
	RequirePositiveSize(PortraitCoverIconSize, TEXT("PortraitCoverIconSize"));
	RequirePositiveSize(TargetCoverIconSize, TEXT("TargetCoverIconSize"));
	RequirePositiveSize(EnemyCounterIconSize, TEXT("EnemyCounterIconSize"));
	RequirePositiveSize(UnitOverheadCoverIconSize, TEXT("UnitOverheadCoverIconSize"));
	RequirePositiveSize(UnitOverheadStatusIconSize, TEXT("UnitOverheadStatusIconSize"));
	// WBP_UnitPortrait намеренно имеет управляемый темой фиксированный размер и
	// безусловно передаёт его в RootSizeBox; ноль здесь не является Auto.
	RequirePositiveSize(PortraitCardLayout.DesiredSize,
		TEXT("PortraitCardLayout.DesiredSize"));

	if (IsNegativeMargin(ActionButtonPadding) ||
		IsNegativeMargin(ActionButtonPressedPadding) ||
		IsNegativeMargin(EndTurnButtonPadding) ||
		IsNegativeMargin(EndTurnButtonPressedPadding) ||
		IsNegativeMargin(PortraitStatusIconPadding) ||
		IsNegativeMargin(PortraitCoverIconPadding) ||
		IsNegativeMargin(EnemyCounterBackgroundPadding) ||
		IsNegativeMargin(UnitOverheadStatusBackgroundPadding) ||
		IsNegativeMargin(SelectionFramePadding))
	{
		Context.AddError(INVTEXT(
			"UITheme: отступы кнопок, рамки и HUD background "
			"не могут быть отрицательными"));
		Result = EDataValidationResult::Invalid;
	}

	if (EnemyCounterBackgroundColor.A < 0.5f)
	{
		Context.AddWarning(INVTEXT(
			"UITheme: EnemyCounterBackgroundColor имеет Alpha < 0.5; "
			"прозрачная PNG-иконка счётчика может теряться на фоне уровня"));
	}

	if (UnitOverheadStatusBackgroundColor.A < 0.5f)
	{
		Context.AddWarning(INVTEXT(
			"UITheme: UnitOverheadStatusBackgroundColor имеет Alpha < 0.5; "
			"прозрачные status-glyph могут снова теряться на фоне уровня"));
	}

	ValidateBlock(ActionsPanelLayout, TEXT("ActionsPanelLayout"));
	ValidateBlock(EndTurnLayout, TEXT("EndTurnLayout"));
	ValidateBlock(SquadPanelLayout, TEXT("SquadPanelLayout"));
	ValidateBlock(PortraitCardLayout, TEXT("PortraitCardLayout"));
	ValidateBlock(TargetPanelLayout, TEXT("TargetPanelLayout"));
	ValidateBlock(TurnPhaseLayout, TEXT("TurnPhaseLayout"));
	ValidateBlock(EnemyCounterLayout, TEXT("EnemyCounterLayout"));
	ValidateBlock(TargetingBannerLayout, TEXT("TargetingBannerLayout"));
	ValidateBlock(UnitOverheadLayout, TEXT("UnitOverheadLayout"));
	ValidateBlock(MainMenuContentLayout, TEXT("MainMenuContentLayout"));
	ValidateBlock(DifficultyContentLayout, TEXT("DifficultyContentLayout"));
	ValidateBlock(SettingsContentLayout, TEXT("SettingsContentLayout"));
	ValidateBlock(AboutContentLayout, TEXT("AboutContentLayout"));
	ValidateBlock(PauseContentLayout, TEXT("PauseContentLayout"));
	ValidateBlock(BriefingContentLayout, TEXT("BriefingContentLayout"));
	ValidateBlock(ResultContentLayout, TEXT("ResultContentLayout"));
	ValidateBlock(IntroOverlayLayout, TEXT("IntroOverlayLayout"));
	ValidateArtwork(MainMenuArt, TEXT("MainMenuArt"));
	ValidateArtwork(DifficultyArt, TEXT("DifficultyArt"));
	ValidateArtwork(SettingsArt, TEXT("SettingsArt"));
	ValidateArtwork(AboutArt, TEXT("AboutArt"));
	ValidateArtwork(PauseArt, TEXT("PauseArt"));
	ValidateArtwork(TutorialBriefingArt, TEXT("TutorialBriefingArt"));
	ValidateArtwork(MissionBriefingArt, TEXT("MissionBriefingArt"));
	ValidateArtwork(VictoryResultArt, TEXT("VictoryResultArt"));
	ValidateArtwork(DefeatResultArt, TEXT("DefeatResultArt"));
	ValidateArtwork(DemoCompleteArt, TEXT("DemoCompleteArt"));
	ValidateArtwork(IntroFallbackArt, TEXT("IntroFallbackArt"));

	WarnMissing(AttackIcon.Get(), TEXT("AttackIcon"));
	WarnMissing(OverwatchIcon.Get(), TEXT("OverwatchIcon"));
	WarnMissing(HunkerIcon.Get(), TEXT("HunkerIcon"));
	WarnMissing(AbilityIcon.Get(), TEXT("AbilityIcon"));
	WarnMissing(InteractDefuseIcon.Get(), TEXT("InteractDefuseIcon"));
	WarnMissing(InteractEvacIcon.Get(), TEXT("InteractEvacIcon"));
	WarnMissing(SkipIcon.Get(), TEXT("SkipIcon"));
	WarnMissing(EndTurnIcon.Get(), TEXT("EndTurnIcon"));
	WarnMissing(HalfCoverIcon.Get(), TEXT("HalfCoverIcon"));
	WarnMissing(FullCoverIcon.Get(), TEXT("FullCoverIcon"));
	WarnMissing(MoveIcon.Get(), TEXT("MoveIcon"));
	WarnMissing(EnemyCountIcon.Get(), TEXT("EnemyCountIcon"));
	WarnMissing(StatusOverwatchIcon.Get(), TEXT("StatusOverwatchIcon"));
	WarnMissing(StatusHunkerIcon.Get(), TEXT("StatusHunkerIcon"));
	WarnMissing(StatusTauntIcon.Get(), TEXT("StatusTauntIcon"));
	WarnMissing(StatusDownedIcon.Get(), TEXT("StatusDownedIcon"));
	WarnMissing(StatusEvacuatedIcon.Get(), TEXT("StatusEvacuatedIcon"));
	WarnMissing(StatusMovingIcon.Get(), TEXT("StatusMovingIcon"));
	WarnMissing(SelectionFrameTexture.Get(), TEXT("SelectionFrameTexture"));
	WarnMissing(AssaultPortrait.Get(), TEXT("AssaultPortrait"));
	WarnMissing(SniperPortrait.Get(), TEXT("SniperPortrait"));
	WarnMissing(MedicPortrait.Get(), TEXT("MedicPortrait"));
	WarnMissing(TankPortrait.Get(), TEXT("TankPortrait"));
	WarnMissing(MarauderPortrait.Get(), TEXT("MarauderPortrait"));
	WarnMissing(AssaultClassIcon.Get(), TEXT("AssaultClassIcon"));
	WarnMissing(SniperClassIcon.Get(), TEXT("SniperClassIcon"));
	WarnMissing(MedicClassIcon.Get(), TEXT("MedicClassIcon"));
	WarnMissing(TankClassIcon.Get(), TEXT("TankClassIcon"));
	WarnMissing(AssaultAbilityIcon.Get(), TEXT("AssaultAbilityIcon"));
	WarnMissing(SniperAbilityIcon.Get(), TEXT("SniperAbilityIcon"));
	WarnMissing(MedicAbilityIcon.Get(), TEXT("MedicAbilityIcon"));
	WarnMissing(TankAbilityIcon.Get(), TEXT("TankAbilityIcon"));
	WarnMissing(PrimaryMenuButtonStyle.Get(), TEXT("PrimaryMenuButtonStyle"));
	WarnMissing(SecondaryMenuButtonStyle.Get(), TEXT("SecondaryMenuButtonStyle"));
	WarnMissing(DangerMenuButtonStyle.Get(), TEXT("DangerMenuButtonStyle"));
	WarnMissing(TitleTextStyle.Get(), TEXT("TitleTextStyle"));
	WarnMissing(BodyTextStyle.Get(), TEXT("BodyTextStyle"));
	WarnMissing(CaptionTextStyle.Get(), TEXT("CaptionTextStyle"));

	WarnMissingSoft(MainMenuArt.Texture.IsNull(), TEXT("MainMenuArt.Texture"));
	WarnMissingSoft(DifficultyArt.Texture.IsNull(), TEXT("DifficultyArt.Texture"));
	WarnMissingSoft(SettingsArt.Texture.IsNull(), TEXT("SettingsArt.Texture"));
	WarnMissingSoft(AboutArt.Texture.IsNull(), TEXT("AboutArt.Texture"));
	WarnMissingSoft(PauseArt.Texture.IsNull(), TEXT("PauseArt.Texture"));
	WarnMissingSoft(TutorialBriefingArt.Texture.IsNull(), TEXT("TutorialBriefingArt.Texture"));
	WarnMissingSoft(MissionBriefingArt.Texture.IsNull(), TEXT("MissionBriefingArt.Texture"));
	WarnMissingSoft(VictoryResultArt.Texture.IsNull(), TEXT("VictoryResultArt.Texture"));
	WarnMissingSoft(DefeatResultArt.Texture.IsNull(), TEXT("DefeatResultArt.Texture"));
	WarnMissingSoft(DemoCompleteArt.Texture.IsNull(), TEXT("DemoCompleteArt.Texture"));
	WarnMissingSoft(IntroFallbackArt.Texture.IsNull(), TEXT("IntroFallbackArt.Texture"));
	WarnMissingSoft(IntroMediaSource.IsNull(), TEXT("IntroMediaSource"));
	WarnMissingSoft(IntroVideoMaterial.IsNull(), TEXT("IntroVideoMaterial"));

	return Result;
}
#endif
