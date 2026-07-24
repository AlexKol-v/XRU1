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

/**
 * Как юнит стреляет из своей текущей позиции (для анимации Ф10 и превью Ф11).
 * Определяется UTacticsCombatStatics::GetFiringStance по тому, какая огневая
 * позиция дала линию огня:
 * Open      — укрытия нет, стреляет с места стоя;
 * OverCover — half: LOS из центра, привстать и выстрелить ПОВЕРХ, с места;
 * StepOut   — full: LOS только из выглядывания у края, выйти за угол и выстрелить.
 * Геймплейно юнит НЕ перемещается: стойка — это выбор монтажа/визуальный сдвиг.
 */
UENUM(BlueprintType)
enum class EFiringStance : uint8
{
	Open      UMETA(DisplayName = "Open"),
	OverCover UMETA(DisplayName = "Over Cover"),
	StepOut   UMETA(DisplayName = "Step Out")
};
