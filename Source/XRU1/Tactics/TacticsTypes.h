#pragma once

#include "CoreMinimal.h"
#include "TacticsTypes.generated.h"

/** Уровень сложности, выбирается при старте новой игры. Хранится в UTacticsSaveGame. */
UENUM(BlueprintType)
enum class EDifficultyLevel : uint8
{
	Easy   UMETA(DisplayName = "Easy"),
	Medium UMETA(DisplayName = "Medium"),
	Hard   UMETA(DisplayName = "Hard")
};

/** Роль юнита в фиксированном ростере из 4 классов. */
UENUM(BlueprintType)
enum class EUnitRole : uint8
{
	Assault UMETA(DisplayName = "Assault"),
	Sniper  UMETA(DisplayName = "Sniper"),
	Healer  UMETA(DisplayName = "Healer"),
	Tank    UMETA(DisplayName = "Tank")
};

/** Чей сейчас ход в пошаговом бою. */
UENUM(BlueprintType)
enum class ETurnPhase : uint8
{
	None    UMETA(DisplayName = "None"),
	Player  UMETA(DisplayName = "Player Turn"),
	Enemy   UMETA(DisplayName = "Enemy Turn")
};

/** Какая контекстная интеракция (клавиша F) доступна выбранному юниту. Для кнопки HUD. */
UENUM(BlueprintType)
enum class EInteractionKind : uint8
{
	None       UMETA(DisplayName = "Недоступна"),
	DefuseBomb UMETA(DisplayName = "Обезвредить заряд"),
	Evacuate   UMETA(DisplayName = "Эвакуация")
};
