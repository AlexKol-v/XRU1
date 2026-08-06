#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TacticsTypes.h"
#include "TacticsGameInstance.generated.h"

class UTacticsSaveGame;
class UTacticalHUDStyleData;
class UCoverTuningDataAsset;
class UMissionVfxDataAsset;
class UFogOfWarConfigDataAsset;
class UTacticalScenarioDataAsset;
class UTutorialStyleData;

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
	 * Применяет пользовательские настройки при запуске игры, а не только при
	 * загрузке кампании: громкость и качество не зависят от того, начал ли
	 * игрок прохождение.
	 */
	virtual void Init() override;

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
	 * Презентация обучения (DA_Tutorial_Style): подсказки-цели и мировые декали
	 * шага. Отдельно от UITheme — это другой слой с другими читателями
	 * (`UTutorialStyleData::Get`). Не назначен — используется CDO.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|UI")
	TObjectPtr<UTutorialStyleData> TutorialStyle;

	/**
	 * Единый тюнинг укрытий/LOS/выглядывания/высоты (DA_CoverTuning). Назначается
	 * один раз в BP-наследнике GameInstance. Если не назначен —
	 * UTacticsCombatStatics::GetCoverTuning отдаёт CDO (дефолты = прежние числа),
	 * и поведение игры не меняется.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	TObjectPtr<UCoverTuningDataAsset> CoverTuning;

	/**
	 * Niagara-эффекты событий миссии (DA_MissionVfx): лечение, обезвреживание,
	 * эвакуация. Не назначен — `UMissionVfxDataAsset::Get` отдаёт пустой CDO,
	 * события идут без эффектов (штатная деградация, не ошибка).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|VFX")
	TObjectPtr<UMissionVfxDataAsset> MissionVfx;

	/**
	 * Тюнинг ВИЗУАЛЬНОГО слоя тумана — сетки затемнения местности (DA_Fog_Showreel).
	 * Только вид и стоимость картинки: правила видимости общие с боевой LOS и
	 * настройке отсюда не подлежат. Не назначен — `UFogOfWarConfigDataAsset::Get`
	 * отдаёт CDO с рабочими дефолтами.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|FogOfWar")
	TObjectPtr<UFogOfWarConfigDataAsset> FogConfig;

	/**
	 * Микшер проекта (DA_TacticsAudio): SoundMix пользовательских громкостей,
	 * SoundClass категорий и общие звуки интерфейса. Без него ползунки громкости
	 * ни на что не влияют — подсистема звука об этом предупреждает в лог.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Audio")
	TObjectPtr<class UTacticsAudioSettingsDataAsset> AudioSettings;

	/**
	 * Профили поведения AI по уровням сложности. Сложность обязана менять СТИЛЬ
	 * врага, а не только его меткость: профиль задаёт агрессию, готовность
	 * фланкировать, частоту Overwatch/Hunker и дальность обзора.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|AI")
	TMap<EDifficultyLevel, TObjectPtr<class UAIBehaviorProfileDataAsset>> AIProfilesByDifficulty;

	/** Имя слота на диске. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Save")
	FString SaveSlotName = TEXT("TacticsCampaign");

	/** Уровень хаба (3D-карта выбора миссий). Задаётся в BP-наследнике GameInstance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Levels", meta = (AllowedTypes = "World"))
	TSoftObjectPtr<UWorld> HubLevel;

	/** Уровень главного меню. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Levels", meta = (AllowedTypes = "World"))
	TSoftObjectPtr<UWorld> MainMenuLevel;

	/**
	 * Единственная физическая карта для Tutorial и Mission01. Различия запуска
	 * находятся в UTacticalScenarioDataAsset, а не в дубликатах уровня.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Levels", meta = (AllowedTypes = "World"))
	TSoftObjectPtr<UWorld> SharedCombatLevel;

	/** Конфигурация текущего запуска общей боевой карты; живёт между OpenLevel. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tactics|Scenario")
	TObjectPtr<UTacticalScenarioDataAsset> ActiveScenario;

	/** Монотонное поколение запуска: late callback старого World не относится к новому run. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tactics|Scenario")
	int32 ActiveScenarioRunId = 0;

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

	/**
	 * Зачитывает ВСЕ обучающие сценарии (Kind == Tutorial) в текущем сейве —
	 * галочка «Пропустить обучение» на экране сложности. Сценарии находятся по
	 * классу через AssetRegistry: жёсткого списка ID нет, новый туториал
	 * подхватится сам. Прогрессию по-прежнему решает единственный механизм —
	 * RequiredMissions ↔ CompletedMissions, здесь только запись зачёта.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Save")
	void MarkTutorialScenariosCompleted();

	/** Загружает слот в CurrentSave. Возвращает загруженный объект или nullptr. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Save")
	UTacticsSaveGame* LoadCampaign();

	/**
	 * Применяет сохранённые громкости и настройки изображения к движку.
	 * Вызывается сразу после создания/загрузки слота: без этого ползунки в меню
	 * показывали бы сохранённые значения, а звучала бы игра на дефолтах.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Save")
	void ApplySavedUserSettings();

	/** Открывает уровень хаба (после «Продолжить» / выбора сложности новой игры). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Levels")
	void TravelToHub();

	/** Возвращает игрока на уровень главного меню (из паузы / после миссии). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Levels")
	void TravelToMainMenu();

	/** Выбирает Tutorial/Mission01 и открывает одну SharedCombatLevel. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Scenario")
	bool StartCombatScenario(UTacticalScenarioDataAsset* Scenario);

	/** Чистый retry текущего Scenario через тот же bootstrap и новый RunId. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Scenario")
	bool RestartActiveScenario();

	/**
	 * Принять сценарий БЕЗ travel: общая карта уже открыта. Нужен прямому запуску
	 * Main_Map_Showreel из редактора, когда Hub/POI ещё не пройден. Если сценарий
	 * уже выбран настоящим bootstrap'ом, ничего не меняет.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Scenario")
	bool AdoptScenarioInPlace(UTacticalScenarioDataAsset* Scenario);

	UFUNCTION(BlueprintPure, Category = "Tactics|Scenario")
	UTacticalScenarioDataAsset* GetActiveScenario() const { return ActiveScenario; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Scenario")
	int32 GetActiveScenarioRunId() const { return ActiveScenarioRunId; }

private:
	/** Валидация сценария, сброс quest runtime и новый RunId — общее для travel и in-place. */
	bool PrepareScenarioRun(UTacticalScenarioDataAsset* Scenario);

	/**
	 * Снимает все причины паузы перед сменой уровня. Причина, взятая экраном
	 * старого мира, иначе переживёт travel и заморозит новый.
	 */
	void ClearPauseBeforeTravel();
};
