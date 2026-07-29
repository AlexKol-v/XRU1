#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FogOfWarSubsystem.generated.h"

class AActor;

/**
 * Gameplay-источник истины для видимости со стороны игрока.
 *
 * Это слой правил, а не рендер тумана: материал/RenderTarget должен только отображать
 * его результат. Все HUD-подсказки, выбор цели и камера обязаны задавать вопрос здесь,
 * иначе скрытый враг может утечь через счётчик, preview или автофокус.
 */
UCLASS()
class XRU1_API UFogOfWarSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Видит ли хотя бы один живой боец игрока актор сейчас (радиус + общий LOS боя). */
	UFUNCTION(BlueprintPure, Category = "Tactics|FogOfWar")
	bool IsActorCurrentlyVisible(const AActor* Actor) const;

	/** Живые враги, видимые отряду прямо сейчас. */
	UFUNCTION(BlueprintPure, Category = "Tactics|FogOfWar")
	TArray<AActor*> GetCurrentlyVisibleEnemies() const;

	/** Количество обнаруженных сейчас врагов — безопасно для HUD при включённом тумане. */
	UFUNCTION(BlueprintPure, Category = "Tactics|FogOfWar")
	int32 GetCurrentlyVisibleEnemyCount() const;
};
