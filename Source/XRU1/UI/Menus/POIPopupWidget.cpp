#include "POIPopupWidget.h"
#include "Components/TextBlock.h"

void UPOIPopupWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// SelfHitTestInvisible недостаточно: мышь перехватывают именно дети (панели
	// и тексты), поэтому прозрачным должно стать всё дерево.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UPOIPopupWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UPOIPopupWidget::SetupFromPOI(const FText& InTitle, const FText& InDescription, const FText& InLockedReason)
{
	const bool bLocked = !InLockedReason.IsEmpty();

	if (Txt_Title)
	{
		Txt_Title->SetText(InTitle);
	}
	if (Txt_Description)
	{
		Txt_Description->SetText(InDescription);
	}
	if (Txt_Locked)
	{
		// Игрок должен видеть ПРИЧИНУ отказа, а не факт («Недоступно: сначала
		// пройдите «Полигон „Купол“»»), поэтому текст приходит от миссии.
		Txt_Locked->SetText(InLockedReason);
		Txt_Locked->SetVisibility(bLocked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	OnPOIDataChanged(bLocked);
}
