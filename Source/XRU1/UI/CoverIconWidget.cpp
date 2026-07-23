#include "CoverIconWidget.h"

#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"

#include "CoverDetectionComponent.h"
#include "TacticalHUDStyleData.h"
#include "TacticsGameInstance.h"

void UCoverIconWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (!IconImage && WidgetTree)
    {
        IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CoverIcon"));
        WidgetTree->RootWidget = IconImage;
    }

    if (IconImage)
    {
        IconImage->SetColorAndOpacity(BaseColor);
    }
}

void UCoverIconWidget::ApplyStyle(const FVector2D& InSize, const FLinearColor& InColor)
{
    const UTacticalHUDStyleData* Theme = GetUITheme();
    const FVector2D ResolvedSize = Theme ? Theme->UnitOverheadCoverIconSize : InSize;
    Super::ApplyStyle(ResolvedSize, InColor);

    if (IconImage)
    {
        IconImage->SetColorAndOpacity(InColor);
        IconImage->SetDesiredSizeOverride(ResolvedSize);
    }
}

void UCoverIconWidget::BindDelegates()
{
    AActor* Avatar = ResolveAvatarActor();
    if (!Avatar) return;

    CoverDetection = Avatar->FindComponentByClass<UCoverDetectionComponent>();
    if (CoverDetection.IsValid())
    {
        CoverDetection->OnCoverStateChanged.AddDynamic(this, &UCoverIconWidget::OnCoverStateChanged);
    }
}

void UCoverIconWidget::UnbindDelegates()
{
    if (UCoverDetectionComponent* Cover = CoverDetection.Get())
    {
        Cover->OnCoverStateChanged.RemoveDynamic(this, &UCoverIconWidget::OnCoverStateChanged);
    }
    CoverDetection = nullptr;
}

void UCoverIconWidget::RefreshFromASC() { Redraw(); }

void UCoverIconWidget::OnCoverStateChanged(ECoverType /*NewBestCover*/) { Redraw(); }

void UCoverIconWidget::Redraw()
{
    const UCoverDetectionComponent* Cover = CoverDetection.Get();
    if (!IconImage || !Cover)
    {
        return;
    }

    const ECoverType CoverType = Cover->BestCoverAround;
    if (CoverType == ECoverType::None)
    {
        SetVisibility(ESlateVisibility::Hidden);
        return;
    }

    UTexture2D* Texture = nullptr;
    if (const UTacticalHUDStyleData* Theme = GetUITheme())
    {
        Texture = Theme->GetCoverIcon(CoverType);
    }
    if (!Texture)
    {
        Texture = (CoverType == ECoverType::Full)
            ? FullCoverTexture.Get()
            : HalfCoverTexture.Get();
    }

    // Ставим кисть безусловно: незаданная текстура даёт квадрат BaseColor,
    // но не залипшую иконку ПРЕДЫДУЩЕГО типа укрытия.
    IconImage->SetBrushFromTexture(Texture);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

const UTacticalHUDStyleData* UCoverIconWidget::GetUITheme() const
{
    const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
    return GameInstance ? GameInstance->GetUITheme() : nullptr;
}
