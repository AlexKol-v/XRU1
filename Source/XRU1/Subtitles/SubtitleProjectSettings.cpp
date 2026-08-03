#include "SubtitleProjectSettings.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"

FText UXRU1SubtitleSettings::GetCultureDisplayName(const FString& Culture)
{
	// Родное имя языка («русский», «English»), а не перевод на текущий язык:
	// игрок, случайно выбравший незнакомый язык, обязан найти свой обратно.
	if (const FCulturePtr Found = FInternationalization::Get().GetCulture(Culture))
	{
		const FString NativeName = Found->GetNativeName();
		if (!NativeName.IsEmpty())
		{
			return FText::FromString(NativeName);
		}
	}
	return FText::FromString(Culture);
}
