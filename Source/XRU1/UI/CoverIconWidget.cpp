#include "CoverIconWidget.h"

#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"

#include "CoverDetectionComponent.h"
#include "TacticalHUDStyleData.h"
#include "TacticalPlayerController.h"
#include "TacticsCombatStatics.h"
#include "TacticsGameInstance.h"
#include "UnitBase.h" // полный тип: GetSelectedUnit() отдаёт AUnitBase*, приводим к AActor*

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

void UCoverIconWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!bShowFlankedWhileTargeting)
    {
        return;
    }

    // Перерисовываем ТОЛЬКО при смене стрелка. Пока игрок целится, ни он, ни
    // цель не двигаются, поэтому пересчитывать щит каждый кадр незачем — а он
    // делает трейс укрытия. Перемещения ловит OnCoverStateChanged.
    AActor* const Shooter = ResolveActiveShooter();
    if (LastShooter.Get() != Shooter)
    {
        LastShooter = Shooter;
        Redraw();
    }
}

AActor* UCoverIconWidget::ResolveActiveShooter() const
{
    if (!bShowFlankedWhileTargeting)
    {
        return nullptr;
    }

    const ATacticalPlayerController* Controller =
        Cast<ATacticalPlayerController>(GetOwningPlayer());
    if (!Controller || !Controller->IsTargetingAttack())
    {
        return nullptr; // вне прицеливания щит «против стрелка» смысла не имеет
    }

    AActor* const Shooter = Controller->GetSelectedUnit();
    // Свой же боец не «фланкирует» сам себя — щит над союзником остаётся локальным.
    return UTacticsCombatStatics::AreHostile(Shooter, ResolveAvatarActor()) ? Shooter : nullptr;
}

void UCoverIconWidget::Redraw()
{
    const UCoverDetectionComponent* Cover = CoverDetection.Get();
    if (!IconImage || !Cover)
    {
        return;
    }

    const UTacticalHUDStyleData* Theme = GetUITheme();

    // Есть активный стрелок — считаем щит ПРОТИВ НЕГО (три состояния);
    // иначе показываем локальное укрытие юнита, как и раньше.
    ECoverType IconCover = Cover->BestCoverAround;
    FLinearColor Tint = BaseColor;
    bool bVisible = IconCover != ECoverType::None;

    if (AActor* const Shooter = ResolveActiveShooter())
    {
        ECoverType ShieldCover = ECoverType::None;
        const ECoverShield Shield = UTacticsCombatStatics::GetCoverShieldAgainst(
            ResolveAvatarActor(), Shooter, ShieldCover);

        IconCover = ShieldCover;
        bVisible = Shield != ECoverShield::None;
        // Цвет подменяем ТОЛЬКО для фланга. Иначе обычный щит во время
        // прицеливания менял бы дизайнерский BaseColor на белый из темы — то
        // есть иконка мигала бы цветом просто от входа в режим наведения.
        if (Theme && Shield == ECoverShield::Flanked)
        {
            Tint = Theme->GetCoverShieldTint(Shield);
        }
    }

    if (!bVisible)
    {
        SetVisibility(ESlateVisibility::Hidden);
        return;
    }

    UTexture2D* Texture = Theme ? Theme->GetCoverIcon(IconCover) : nullptr;
    if (!Texture)
    {
        Texture = (IconCover == ECoverType::Full)
            ? FullCoverTexture.Get()
            : HalfCoverTexture.Get();
    }

    // Ставим кисть безусловно: незаданная текстура даёт квадрат BaseColor,
    // но не залипшую иконку ПРЕДЫДУЩЕГО типа укрытия.
    IconImage->SetBrushFromTexture(Texture);
    IconImage->SetColorAndOpacity(Tint);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

const UTacticalHUDStyleData* UCoverIconWidget::GetUITheme() const
{
    const UTacticsGameInstance* GameInstance = GetGameInstance<UTacticsGameInstance>();
    return GameInstance ? GameInstance->GetUITheme() : nullptr;
}
