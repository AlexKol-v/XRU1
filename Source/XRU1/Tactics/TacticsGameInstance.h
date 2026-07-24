#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TacticsTypes.h"
#include "TacticsGameInstance.generated.h"

class UTacticsSaveGame;
class UTacticalHUDStyleData;
class UCoverTuningDataAsset;

/**
 * GameInstance проекта: владеет текущим слотом кампании (UTacticsSaveGame) и
 * даёт главному меню операции Save/Load/Continue. Устанавливается в настройках
 * проекта как Game Instance Class.
 */
UCLASS()
class XRU1_API UTacticsGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/**
	 * Единая UI-тема проекта (DA_TacticalHUDStyle): иконки, портреты, экранный
	 * арт, палитра, размеры и отступы. Назначается один раз в BP-наследнике.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|UI")
	TObjectPtr<UTacticalHUDStyleData> UITheme;

	/** Общая UI-тема для HUD и всех WBP-экранов. */
	UFUNCTION(BlueprintPure, Category = "Tactics|UI")
	UTacticalHUDStyleData* GetUITheme() const { return UITheme; }

	/**
	 * Единый тюнинг укрытий/LOS/выглядывания/высоты (DA_CoverTuning). Назначается
	 * один раз в BP-наследнике GameInstance. Если не назначен —
	 * UTacticsCombatStatics::GetCoverTuning отдаёт CDO (дефолты = прежние числа),
	 * и поведение игры не меняется.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	TObjectPtr<UCoverTuningDataAsset> CoverTuning;

	/** Имя слота на диске. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Save")
	FString SaveSlotName = TEXT("TacticsCampaign");

	/** Уровень хаба (3D-карта выбора миссий). Задаётся в BP-наследнике GameInstance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Levels", meta = (AllowedTypes = "World"))
	TSoftObjectPtr<UWorld> HubLevel;

	/** Уровень главного меню. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Levels", meta = (AllowedTypes = "World"))
	TSoftObjectPtr<UWorld> MainMenuLevel;

	/** Текущий загруженный/активный слот кампании (в памяти). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Save")
	TObjectPtr<UTacticsSaveGame> CurrentSave;

	/** Есть ли на диске сохранение — управляет доступностью кнопки «Продолжить». */
	UFUNCTION(BlueprintPure, Category = "Tactics|Save")
	bool HasSaveGame() const;

	/** Создаёт новую кампанию с выбранной сложностью и ростером по умолчанию. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Save")
	UTacticsSaveGame* StartNewCampaign(EDifficultyLevel Difficulty);

	/** Пишет CurrentSave в слот. Возвращает успех. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Save")
	bool SaveCampaign();

	/** Загружает слот в CurrentSave. Возвращает загруженный объект или nullptr. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Save")
	UTacticsSaveGame* LoadCampaign();

	/** Открывает уровень хаба (после «Продолжить» / выбора сложности новой игры). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Levels")
	void TravelToHub();

	/** Возвращает игрока на уровень главного меню (из паузы / после миссии). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Levels")
	void TravelToMainMenu();
};
