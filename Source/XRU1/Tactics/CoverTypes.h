#pragma once

#include "CoreMinimal.h"
#include "CoverTypes.generated.h"

/**
 * Тип укрытия юнита относительно КОНКРЕТНОГО источника угрозы (врага).
 * Определяется UCoverDetectionComponent трейсами в сторону врага.
 * None  — открыт, укрытия нет.
 * Half  — половинчатое укрытие (низкая стена): частичный бонус к защите.
 * Full  — полное укрытие (высокая стена): максимальный бонус к защите.
 */
UENUM(BlueprintType)
enum class ECoverType : uint8
{
	None UMETA(DisplayName = "No Cover"),
	Half UMETA(DisplayName = "Half Cover"),
	Full UMETA(DisplayName = "Full Cover")
};
