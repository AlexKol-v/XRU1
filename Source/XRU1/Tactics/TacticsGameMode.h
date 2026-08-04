#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TacticsTypes.h"
#include "TacticsGameMode.generated.h"

class AUnitBase;
class ABombObjective;
class AEvacZone;
class ATacticalScenarioDirector;
class UMissionResultWidget;
class UCommonActivatableWidget;

/** Параметры вражеской стороны для одного уровня сложности (GDD §10). */
USTRUCT(BlueprintType)
struct FTacticsDifficultyParams
{
	GENERATED_BODY()

	/** HP врага (переопределяет атрибут при старте). 0 = не менять. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0"))
	float EnemyHealth = 100.f;

	/** Точность врага (BaseAim юнита). 0 = не менять. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0", ClampMax = "100"))
	float EnemyAim = 65.f;

	/** Лимит ходов игрока до взрыва бомбы (0 = таймера нет). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "0"))
	int32 TurnLimit = 10;

	/**
	 * СКОЛЬКО ВРАГОВ МАКСИМУМ АТАКУЕТ ЗА ОДИН ХОД (A8). −1 — без лимита.
	 *
	 * Verbatim XCOM 2 (`XComAI.ini`, `MaxEngagedEnemies`): Rookie 4, Veteran 6,
	 * Commander 6, Legend −1. У нас три уровня, поэтому 4 / 6 / −1.
	 *
	 * ⚠️ Это САМЫЙ дешёвый и самый честный регулятор сложности: он не подкручивает
	 * врагу точность и не отнимает у игрока информацию — просто ограничивает,
	 * сколько стволов смотрит на отряд одновременно. Превысившие лимит не «стоят
	 * столбом»: они уходят в занятую ветку — наблюдение или перемещение.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty", meta = (ClampMin = "-1"))
	int32 MaxAttackersPerTurn = 6;
};

/**
 * GameMode боевого уровня (туториал/миссия). Обязанности:
 *  - собрать юнитов карты по TacticsTeamIds и запустить бой;
 *  - применить сложность из сейва к врагам (HP/aim, таймер ходов при наличии
 *    бомбы) — GDD §10 (отсев части врагов по сложности НЕ реализован —
 *    состав врагов на карте фиксирован, сложность правит только их статы);
 *  - следить за целями: бомба обезврежена → снять таймер, включить зоны
 *    эвакуации; все живые эвакуированы → победа (GDD §5.7);
 *  - по концу боя показать экран результата и записать прогресс кампании.
 */
UCLASS(Blueprintable)
class XRU1_API ATacticsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATacticsGameMode();

	/** Id миссии для прогресса кампании («Tutorial» / «Mission01»). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission")
	FName MissionId = NAME_None;

	/**
	 * Победа при уничтожении всех врагов. Для миссий «цель + эвакуация»
	 * выключить: бой закончится только полной эвакуацией отряда.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission")
	bool bWinWhenAllEnemiesDead = true;

	/** Параметры сложности (заполняются в BP; ключи Easy/Medium/Hard). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission")
	TMap<EDifficultyLevel, FTacticsDifficultyParams> DifficultyParams;

	/**
	 * Какую сложность считать текущей, если кампании нет (прямой PIE карты).
	 * Настройка боевого стенда: миссия собирается и балансируется на сложном
	 * режиме, и запуск карты «как есть» должен воспроизводить именно его.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission")
	EDifficultyLevel DifficultyWithoutCampaign = EDifficultyLevel::Hard;

	/** Экран результата (WBP от UMissionResultWidget). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|UI")
	TSubclassOf<UMissionResultWidget> MissionResultWidgetClass;

	/** Боевой HUD (WBP от UTacticalHUDWidget), пушится на слой Game. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|UI")
	TSubclassOf<UCommonActivatableWidget> TacticalHUDClass;

	/** Задержка перед StartCombat, чтобы BeginPlay юнитов/навмеша завершился. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission", meta = (ClampMin = "0"))
	float CombatStartDelay = 0.3f;

	/**
	 * СКОЛЬКО ЖДАТЬ ГОТОВНОСТИ НАВИГАЦИИ перед стартом боя (с). 0 — не ждать.
	 *
	 * Навмеш стоит с `RuntimeGeneration = Dynamic` и `bForceRebuildOnLoad`, то
	 * есть на загрузке карты строится заново и асинхронно (по логу редактора —
	 * 12.3 с на полный проход тайлов). Бой же стартовал по фиксированной
	 * задержке, и `ATacticalSpawnGroupBase::ResolveSpawnLocation` получал отказ
	 * от ВСЕХ навигационных запросов подряд: `GetRandomReachablePointInRadius`,
	 * затем две проекции. Оставался последний шаг — поставить бойца у базы со
	 * смещением по индексу и написать `[Encounter] ... без подтверждения
	 * навмешем`.
	 *
	 * То есть расстановка групп молча подменялась фолбэком не из-за плохих точек
	 * (они все лежат на навмеше — проверено проекцией в редакторе), а из-за
	 * гонки со стартом. Ждём.
	 *
	 * ⚠️ Таймаут обязателен: на карте без навмеша ожидание не должно превращаться
	 * в вечную загрузку. По его истечении бой стартует как есть, с явным
	 * предупреждением в лог.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Mission", meta = (ClampMin = "0"))
	float NavigationReadyTimeout = 20.f;

	/** Старт боя отложен и произойдёт сам (ждём навигацию). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Mission")
	bool IsCombatStartPending() const;

	/** Активирует все зоны эвакуации уровня (зовёт и скрипт туториала). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Mission")
	void ActivateEvacuation();

	/**
	 * Запускает бой после того, как ScenarioDirector подтвердил загрузку
	 * scenario sublevel. Идемпотентно: повторный callback стриминга не создаёт
	 * второй бой, HUD или подписки на objectives.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Scenario")
	bool StartScenarioCombat();

	/** Проиграна ли миссия по таймеру (для текста экрана поражения). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Mission")
	bool WasDefeatByTimeout() const { return bDefeatByTimeout; }

	/** Сложность текущего боя (из сейва; без кампании — `DifficultyWithoutCampaign`). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Mission")
	EDifficultyLevel GetActiveDifficulty() const { return ResolveDifficulty(); }

	/**
	 * Приводит созданного в рантайме врага к правилам текущего боя: статы
	 * сложности и профиль поведения AI. Зовут `ATacticalEncounter` и
	 * `ATacticalReinforcementBeacon` — иначе заспавненный боец играл бы по
	 * дефолтам своего BP, а не по правилам уровня сложности.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Mission")
	void ApplySpawnedEnemyDefaults(AUnitBase* Enemy);

protected:
	virtual void BeginPlay() override;

	/** Сбор сторон, сложность, запуск боя. */
	void StartMissionCombat();

	/**
	 * Навигация достроена и отвечает на запросы. Навигации на карте нет вовсе —
	 * считаем готовой: ждать нечего, а бой без навмеша это отдельный дефект.
	 */
	bool IsNavigationReadyForCombat();

	/** Время первого запроса на старт боя — отсчёт таймаута ожидания навигации. */
	float CombatStartRequestedTime = -1.f;

	/** Применяет параметры сложности к вражескому юниту. */
	void ApplyDifficultyToEnemy(AUnitBase* Enemy, const FTacticsDifficultyParams& Params);

	/** Назначает врагу профиль поведения текущей сложности (если он заведён). */
	void ApplyBehaviorProfileToEnemy(AUnitBase* Enemy, EDifficultyLevel Difficulty) const;

	/**
	 * Создаёт стартовые группы (`ATacticalEncounter`) ДО сбора сторон, чтобы их
	 * бойцы попали в бой обычным путём. Возвращает число созданных бойцов.
	 */
	int32 SpawnConfiguredEncounters(EDifficultyLevel Difficulty);

	UFUNCTION()
	void HandleBombDisarmed();

	UFUNCTION()
	void HandleUnitEvacuated(AUnitBase* Unit);

	/** Разносит последний Evac.Unit и Evac.Squad по разным StateTree ticks. */
	void CompleteSquadEvacuation();

	UFUNCTION()
	void HandleTurnLimitExpired();

	UFUNCTION()
	void HandleCombatEnded(bool bPlayerWon);

	/** Ждёт next-tick обработки terminal quest event перед save/result. */
	void PollScenarioFinalization();

	/** Единственное место записи campaign result и открытия result screen. */
	void CompleteCombatResult(bool bPlayerWon);

	/** Все живые юниты отряда эвакуированы? (Downed не блокируют победу — GDD §5.7.) */
	bool AreAllLivingPlayersEvacuated() const;

	/** Текущая сложность из сейва (Medium, если сейва нет — прямой запуск PIE). */
	EDifficultyLevel ResolveDifficulty() const;

	FTimerHandle StartCombatTimerHandle;
	FTimerHandle SquadEvacuationTimerHandle;
	FTimerHandle ScenarioFinalizationTimerHandle;
	TWeakObjectPtr<ATacticalScenarioDirector> PendingScenarioDirector;
	double ScenarioFinalizationDeadline = 0.0;
	bool bCombatStarted = false;
	bool bDefeatByTimeout = false;
	bool bSquadEvacuationPending = false;
	bool bCombatResultPending = false;
	bool bPendingPlayerWon = false;

	/** Отряд игрока на карте (кэш для проверки эвакуации). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AUnitBase>> PlayerUnits;
};
