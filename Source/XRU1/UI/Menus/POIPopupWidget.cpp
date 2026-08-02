#include "POIPopupWidget.h"
#include "Components/TextBlock.h"

void UPOIPopupWidget::SetupFromPOI(const FText& InTitle, const FText& InDescription, bool bInLocked)
{
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
		Txt_Locked->SetVisibility(bInLocked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	OnPOIDataChanged(bInLocked);
}
