#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TacticsDebug.generated.h"

class AActor;

/**
 * Единый реестр отладочных переключателей проекта.
 *
 * Раньше cvar объявлялись локально в четырёх .cpp и нигде не были перечислены:
 * чтобы вспомнить имя, приходилось грепать исходники. Здесь они собраны в один
 * список, и `xru1.Debug.List` печатает его прямо в консоль во время теста.
 *
 * Правило: cvar включает ДИАГНОСТИКУ, а не меняет правила игры. Ни один
 * переключатель отсюда не должен влиять на исход боя — иначе отладочный прогон
 * перестанет воспроизводить обычный.
 */
namespace TacticsDebug
{
	/** Подробный разбор решений AI: варианты, скор, выбор и причина. */
	XRU1_API bool IsAILogEnabled();

	/** Отрисовка решений AI в мире: цель, маршрут, угрозы, итоговый выбор. */
	XRU1_API bool IsAIDebugDrawEnabled();

	/** Лог всех опубликованных quest-событий с источником и целью. */
	XRU1_API bool IsQuestLogEnabled();

	/** Лог применения/снятия политик Action Gate и причин отказов. */
	XRU1_API bool IsGateLogEnabled();

	/** Лог звуковых событий и незаполненных реплик. */
	XRU1_API bool IsAudioLogEnabled();

	/** Сколько секунд держать отладочные надписи над юнитами. */
	XRU1_API float GetDebugDrawDuration();
}

/** Blueprint-доступ к тем же переключателям — для отладочных виджетов. */
UCLASS()
class XRU1_API UTacticsDebugLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Tactics|Debug")
	static bool IsAIDebugDrawEnabled();

	UFUNCTION(BlueprintPure, Category = "Tactics|Debug")
	static bool IsQuestLogEnabled();

	/**
	 * Печатает текст над актором, если включён AI debug draw. Обёртка нужна,
	 * чтобы места вызова не тащили DrawDebugHelpers и не проверяли cvar вручную.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Debug")
	static void DrawUnitDebugText(const AActor* Unit, const FString& Text,
		FLinearColor Color = FLinearColor::White, float ZOffset = 140.f);
};
