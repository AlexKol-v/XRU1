#include "UnitStatusIconWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Image.h"

#include "TacticalHUDStyleData.h"
#include "TacticsGameInstance.h"
#include "UnitBase.h"

void UUnitStatusIconWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree)
	{
		BadgeBackground = Cast<UBorder>(
			WidgetTree->FindWidget(TEXT("StatusBadgeBackground")));
		IconImage = Cast<UImage>(
			WidgetTree->FindWidget(TEXT("StatusIcon")));

		if (!BadgeBackground)
		{
			UWidget* PreviousRoot = WidgetTree->RootWidget;
			BadgeBackground = WidgetTree->ConstructWidget<UBorder>(
				UBorder::StaticClass(), TEXT("StatusBadgeBackground"));
			BadgeBackground->SetHorizontalAlignment(HAlign_Center);
			BadgeBackground->SetVerticalAlignment(VAlign_Center);
			WidgetTree->RootWidget = BadgeBackground;

			if (PreviousRoot)
			{
				// BP-наследник сохраняет своё Designer-дерево: нативная
				// подложка лишь оборачивает существующий root.
				BadgeBackground->AddChild(PreviousRoot);
			}
			else
			{
				IconImage = WidgetTree->ConstructWidget<UImage>(
					UImage::StaticClass(), TEXT("StatusIcon"));
				BadgeBackground->AddChild(IconImage);
			}
		}

		BadgeBackground->SetHorizontalAlignment(HAlign_Center);
		BadgeBackground->SetVerticalAlignment(VAlign_Center);
		if (UWidget* Content = BadgeBackground->GetContent())
		{
			if (UBorderSlot* ContentSlot =
				Cast<UBorderSlot>(Content->Slot))
			{
				ContentSlot->SetHorizontalAlignment(HAlign_Center);
				ContentSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
	}

	if (IconImage)
	{
		IconImage->SetColorAndOpacity(FLinearColor::White);
	}
}

void UUnitStatusIconWidget::ApplyStyle(
	const FVector2D& InSize, const FLinearColor& InColor)
{
	const UTacticalHUDStyleData* Theme = GetUITheme();
	const FMargin BackgroundPadding = Theme
		? Theme->UnitOverheadStatusBackgroundPadding
		: FMargin(2.f);
	const FVector2D AvailableGlyphSize(
		FMath::Max(1.f,
			InSize.X - BackgroundPadding.Left - BackgroundPadding.Right),
		FMath::Max(1.f,
			InSize.Y - BackgroundPadding.Top - BackgroundPadding.Bottom));
	const FVector2D RequestedGlyphSize =
		Theme ? Theme->UnitOverheadStatusIconSize : AvailableGlyphSize;
	const FVector2D ResolvedSize(
		FMath::Min(RequestedGlyphSize.X, AvailableGlyphSize.X),
		FMath::Min(RequestedGlyphSize.Y, AvailableGlyphSize.Y));

	if (!bWarnedAboutUndersizedSlot &&
		(RequestedGlyphSize.X > AvailableGlyphSize.X ||
		 RequestedGlyphSize.Y > AvailableGlyphSize.Y))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UnitStatusIconWidget: slot %.0fx%.0f мал для glyph %.0fx%.0f ")
			TEXT("и background padding L%.0f T%.0f R%.0f B%.0f; glyph уменьшен"),
			InSize.X, InSize.Y,
			RequestedGlyphSize.X, RequestedGlyphSize.Y,
			BackgroundPadding.Left, BackgroundPadding.Top,
			BackgroundPadding.Right, BackgroundPadding.Bottom);
		bWarnedAboutUndersizedSlot = true;
	}

	Super::ApplyStyle(ResolvedSize, InColor);

	if (BadgeBackground)
	{
		const FLinearColor BackgroundColor = Theme
			? Theme->UnitOverheadStatusBackgroundColor
			: FLinearColor(0.01f, 0.025f, 0.04f, 0.92f);

		BadgeBackground->SetPadding(BackgroundPadding);
		if (Theme && Theme->UnitOverheadStatusBackgroundTexture)
		{
			BadgeBackground->SetBrushFromTexture(
				Theme->UnitOverheadStatusBackgroundTexture);
		}
		else
		{
			// FSlateColorBrush гарантированно рисует фон даже без отдельной
			// Texture2D: DataAsset может оставить texture-поле пустым.
			BadgeBackground->SetBrush(FSlateColorBrush(FLinearColor::White));
		}
		BadgeBackground->SetBrushColor(BackgroundColor);
		BadgeBackground->SetContentColorAndOpacity(FLinearColor::White);
	}

	if (IconImage)
	{
		IconImage->SetDesiredSizeOverride(ResolvedSize);
		// White сохраняет исходные цвета PNG; DataAsset при желании может
		// намеренно задать другой tint только для overhead badge.
		IconImage->SetColorAndOpacity(InColor);
	}
}

void UUnitStatusIconWidget::BindDelegates()
{
	Unit = Cast<AUnitBase>(ResolveAvatarActor());
	if (AUnitBase* BoundUnit = Unit.Get())
	{
		BoundUnit->OnUnitStateChanged.AddDynamic(
			this, &UUnitStatusIconWidget::OnUnitStateChanged);
	}
}

void UUnitStatusIconWidget::UnbindDelegates()
{
	if (AUnitBase* BoundUnit = Unit.Get())
	{
		BoundUnit->OnUnitStateChanged.RemoveDynamic(
			this, &UUnitStatusIconWidget::OnUnitStateChanged);
	}
	Unit = nullptr;
}

void UUnitStatusIconWidget::RefreshFromASC()
{
	Redraw();
}

void UUnitStatusIconWidget::OnUnitStateChanged()
{
	Redraw();
}

void UUnitStatusIconWidget::Redraw()
{
	if (!IconImage)
	{
		return;
	}

	const UTacticalHUDStyleData* Theme = GetUITheme();
	UTexture2D* Texture =
		Theme ? Theme->GetStatusIconForUnit(Unit.Get()) : nullptr;
	if (!Texture)
	{
		SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	IconImage->SetBrushFromTexture(Texture);
	IconImage->SetColorAndOpacity(BaseColor);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

const UTacticalHUDStyleData* UUnitStatusIconWidget::GetUITheme() const
{
	const UTacticsGameInstance* GameInstance =
		GetGameInstance<UTacticsGameInstance>();
	return GameInstance ? GameInstance->GetUITheme() : nullptr;
}
