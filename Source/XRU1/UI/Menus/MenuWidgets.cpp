#include "MenuWidgets.h"
#include "TacticsGameInstance.h"

bool UMainMenuWidget::CanContinue() const
{
	if (const UTacticsGameInstance* GI = GetGameInstance<UTacticsGameInstance>())
	{
		return GI->HasSaveGame();
	}
	return false;
}
