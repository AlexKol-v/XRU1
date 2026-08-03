#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TacticalScenarioDataAsset.generated.h"

class UQuestDefinition;
class USoundBase;
class UTacticsSaveGame;
class UWorld;

UENUM(BlueprintType)
enum class ETacticalScenarioKind : uint8
{
	Tutorial UMETA(DisplayName = "Tutorial"),
	Mission  UMETA(DisplayName = "Mission")
};

/**
 * Конфигурация логического сценария на общей физической карте.
 * Tutorial и Mission01 отличаются данными/наборами акторов, а не копиями World.
 */
UCLASS(BlueprintType)
class XRU1_API UTacticalScenarioDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Стабильный ID для save/result routing: Tutorial, Mission01 и т.п. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	FName ScenarioId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	ETacticalScenarioKind Kind = ETacticalScenarioKind::Tutorial;

	// --- Витрина миссии (хаб, брифинг, экран результата) -----------------------
	// Название и описание принадлежат МИССИИ, а не маркеру на карте: один и тот
	// же текст нужен попапу POI, HUD хаба и брифингу, и он не должен разъезжаться
	// по трём местам.

	/** Название миссии для игрока («Полигон „Купол“»). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Витрина")
	FText DisplayName;

	/** Краткое описание/брифинг для попапа и HUD хаба. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Витрина", meta = (MultiLine = true))
	FText BriefingText;

	// --- Доступность -----------------------------------------------------------

	/**
	 * Миссии, которые нужно пройти до этой. Ссылки, а не строки: имя требования
	 * берётся прямо из ассета, поэтому переименование миссии не ломает подсказку
	 * и не требует отдельного реестра.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Доступность")
	TArray<TSoftObjectPtr<UTacticalScenarioDataAsset>> RequiredMissions;

	/**
	 * Разрешить запуск без кампании (прямой PIE хаба). Для боевых миссий обычно
	 * false: иначе прогрессия проверяется только в полном прохождении.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Доступность")
	bool bAvailableWithoutCampaign = true;

	/** Своя формулировка запрета; пусто — текст собирается из RequiredMissions. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Доступность")
	FText LockedHintOverride;

	/** Выполнены ли требования к запуску при данном слоте кампании (Save может быть null). */
	UFUNCTION(BlueprintPure, Category = "Scenario|Доступность")
	bool ArePrerequisitesMet(const UTacticsSaveGame* Save) const;

	/** Причина недоступности для игрока; пустой текст — миссия доступна. */
	UFUNCTION(BlueprintPure, Category = "Scenario|Доступность")
	FText GetLockedReason(const UTacticsSaveGame* Save) const;

	/** Название для игрока; при пустом DisplayName — ScenarioId (диагностика). */
	UFUNCTION(BlueprintPure, Category = "Scenario|Витрина")
	FText GetDisplayNameSafe() const;

	/** StateTree-квест, который управляет целями этого запуска общей карты. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	TSoftObjectPtr<UQuestDefinition> QuestDefinition;

	/**
	 * Единственный scenario-specific streaming sublevel. Persistent-карта хранит
	 * общий арт/nav/light; Tutorial и Mission01 не смешиваются и не дублируют его.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	TSoftObjectPtr<UWorld> ScenarioSublevel;

	/** -1 — взять правило GameMode/сложности; 0 — без таймера; >0 — явный лимит. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario", meta = (ClampMin = "-1"))
	int32 TurnLimit = -1;

	/**
	 * AnchorId якоря стартовой позиции камеры в scenario sublevel
	 * (AScenarioAnchorPoint). Камера ставится на него при старте сценария —
	 * PlayerStart persistent-карты общий и не знает, где начинается конкретный
	 * сценарий. None — камера остаётся где заспавнилась.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	FName InitialCameraAnchorId;

	/**
	 * Карта стартует РАЗВЕДАННОЙ: состояние `Unknown` (почти чёрная местность,
	 * GDD §5.9) не включается, туман только приглушает то, что сейчас никто не
	 * видит. У XCOM это штатный режим, а не отказ от механики —
	 * `XComWorldData.bShowNeverSeenAsHaveSeen` («Setting for certain maps to make
	 * it so that never seen fog is shown as have seen») и kismet-функция
	 * `InitializeAllViewersToHaveSeenFog(bool)`.
	 *
	 * Дефолт false — обе миссии демо идут с чёрной картой (решение 2026-08-03).
	 * Включать имеет смысл сценарию, чья режиссура ведёт камеру по секторам,
	 * которых отряд ещё не разведал: показывать игроку чёрный кадр хуже, чем
	 * потерять «неизвестное». Точечная альтернатива — сценарное раскрытие
	 * области (`UFogGridSubsystem::AddScriptedReveal`), она и используется
	 * тактами обучения.
	 *
	 * ⚠️ На ПРАВИЛА не влияет вовсе: живые враги скрыты в любом случае, это
	 * решает `UFogOfWarSubsystem`.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Fog")
	bool bStartFullyExplored = false;

	/**
	 * Завершать ход автоматически, когда у отряда кончились очки действия.
	 *
	 * В боевой миссии это удобство (XCOM так и делает). В ОБУЧЕНИИ — вред:
	 * шаги «передай ход» (A3, B1, C1) требуют, чтобы игрок нажал кнопку сам, а
	 * автопереход делает это за него, пока «Купол» ещё договаривает фразу.
	 * Поэтому это свойство СЦЕНАРИЯ, а не глобальная настройка контроллера:
	 * туториал ставит false, боевые миссии — true.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario")
	bool bAutoEndTurnWhenSquadExhausted = true;

	// --- Реплики исхода (экран результата) -------------------------------------
	// Живут в СЦЕНАРИИ, а не в виджете: экран результата один на все миссии, а
	// «Зачёт, отряд к вылету готов» и «…Узел-7 потерян» — тексты конкретного
	// сценария. Пусто — экран молчит.

	/**
	 * Реплика брифинга: звучит при открытии экрана брифинга этой миссии.
	 * Пусто — экран молчит, это штатная настройка, а не ошибка.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Озвучка")
	TSoftObjectPtr<USoundBase> BriefingVoice;

	/** Реплика при победе. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Озвучка")
	TSoftObjectPtr<USoundBase> VictoryVoice;

	/** Реплика при поражении (отряд погиб). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Озвучка")
	TSoftObjectPtr<USoundBase> DefeatVoice;

	/** Реплика при поражении по таймеру; пусто — играет DefeatVoice. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scenario|Озвучка")
	TSoftObjectPtr<USoundBase> DefeatByTimeoutVoice;
};
