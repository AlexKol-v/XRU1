#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TacticsAudioTypes.h"
#include "TacticsTypes.h"
#include "TacticsSaveGame.generated.h"

/**
 * Слот сохранения кампании. Хранит минимум, нужный для пункта «Продолжить» в
 * главном меню: выбранную сложность, пройденные миссии и состав отряда.
 */
UCLASS(BlueprintType)
class XRU1_API UTacticsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Выбранная при старте новой игры сложность. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Tactics|Save")
	EDifficultyLevel Difficulty = EDifficultyLevel::Medium;

	/** Идентификаторы пройденных миссий/уровней (напр. "Tutorial", "Mission01"). */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Tactics|Save")
	TArray<FName> CompletedMissions;

	/** Последняя открытая точка интереса в хабе (куда возвращаемся при загрузке). */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Tactics|Save")
	FName LastHubPointOfInterest = NAME_None;

	/** Роли отряда (фиксированный ростер). По умолчанию — 4 класса из ТЗ. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Tactics|Save")
	TArray<EUnitRole> SquadRoles;

	/**
	 * LEGACY, не использовать. Настройки переехали в `UTacticsUserSettings`
	 * (2026-08-02): громкость и качество — параметры приложения, а не прогресса,
	 * и не должны сбрасываться при «Новой игре». Пока настройки жили здесь,
	 * экран читал слот, движок применял своё, и меню предлагало применить
	 * полноэкранный режим, который игрок не выбирал.
	 *
	 * Поля оставлены только для чтения старых слотов; новый код в них не пишет.
	 */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Tactics|Save|Legacy")
	FTacticsAudioSettings AudioSettings;

	/** LEGACY: см. комментарий выше. Источник правды — `UTacticsUserSettings`. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Tactics|Save|Legacy")
	FTacticsVideoSettings VideoSettings;

	UFUNCTION(BlueprintPure, Category = "Tactics|Save")
	bool IsMissionCompleted(FName MissionId) const { return CompletedMissions.Contains(MissionId); }
};
