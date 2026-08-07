#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticsTypes.h"
#include "Templates/SubclassOf.h"
#include "TacticalEncounter.generated.h"

class AUnitBase;
class UBillboardComponent;

/**
 * ГРУППА ВРАГОВ, ОПИСАННАЯ ДАННЫМИ, А НЕ ЭКЗЕМПЛЯРАМИ НА КАРТЕ.
 *
 * Модель взята у XCOM 2 (`XComAISpawnManager`, `PodSpawnInfo`): группа там
 * описывается энкаунтером — идентификатором, составом, зоной появления и
 * правилами патрулирования, — а не конкретными акторами, расставленными
 * руками. Есть и ручной режим: `SpawnLocationActorTag` в оригинале означает
 * «поставить группу на позицию тегированного актора». У нас это `SpawnPoints`.
 *
 * Зачем это здесь: состав миссии зависит от сложности (GDD §10 — 4/5/6 врагов),
 * а копия карты на каждую сложность недопустима. Плюс под, маршрут патруля и
 * позиции перестают жить в трёх разных местах: они лежат в одном акторе, и
 * дизайнер правит их, не открывая свойства шести юнитов по очереди.
 *
 * Расстановка руками при этом остаётся полностью рабочей: заспавненные юниты
 * получают ровно те же `PodId`/`PatrolPoints`, что ставятся на экземпляре, и
 * оба способа сосуществуют на одной карте.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API ATacticalSpawnGroupBase : public AActor
{
	GENERATED_BODY()

public:
	ATacticalSpawnGroupBase();

	/** Кого спавнить (BP_Unit_Marauder и будущие архетипы). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Группа")
	TSubclassOf<AUnitBase> EnemyClass;

	/**
	 * Сколько бойцов на каждой сложности (GDD §10). Отсутствующий ключ = 0:
	 * так энкаунтер можно включить только на Hard, не заводя второй актор.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Группа")
	TMap<EDifficultyLevel, int32> CountByDifficulty;

	/**
	 * Точки появления (обычно `AScenarioAnchorPoint`). Берутся по порядку.
	 * Если бойцов больше, чем точек, остальные ищутся на навмеше вокруг
	 * последней использованной точки — см. `SpawnScatterRadius`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Группа")
	TArray<TObjectPtr<AActor>> SpawnPoints;

	/**
	 * Радиус разлёта, когда точек меньше, чем бойцов (см). Аналог
	 * `IdealPodMemberSpawnDistance` XCOM: члены группы стоят рядом, но не в
	 * одной клетке.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Группа", meta = (ClampMin = "100"))
	float SpawnScatterRadius = 500.f;

	/**
	 * Под группы. Пусто — каждый боец сам себе под (одиночки): вскрытие одного
	 * не поднимает остальных. Заполнено — вся группа вступает в бой вместе.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Группа")
	FName PodId;

	/**
	 * Маршрут патруля всей группы (циклический обход, две точки и больше).
	 * Одна точка — зона удержания, пусто — зона вокруг места появления.
	 * Полная таблица режимов — у `AUnitBase::PatrolPoints`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Патруль")
	TArray<TObjectPtr<AActor>> PatrolPoints;

	/**
	 * Радиус свободного обхода зоны (см). 0 — не бродить: без маршрута группа
	 * встанет постом, с одной точкой — будет держать её.
	 *
	 * Это `BaseRoamRange` XCOM 2 нашими средствами: невскрытая группа не стоит
	 * столбом и не марширует по рельсам, а живёт в своей зоне. Сторожа
	 * (`SentryCount`) радиус не получают — они охраняют объект, а не гуляют.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Патруль", meta = (ClampMin = "0"))
	float RoamRadius = 0.f;

	/**
	 * Как обходить маршрут. Точки вдоль дороги или стены — это НЕЗАМКНУТЫЙ
	 * маршрут, ему нужен `PingPong`: при кольцевом обходе боец, дойдя до конца,
	 * побежит к первой точке через всю линию.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Патруль")
	EPatrolRouteMode PatrolRouteMode = EPatrolRouteMode::Loop;

	/**
	 * Входить в маршрут с ближайшей к точке появления вершины, а не с первой.
	 * Группу ставят там, где она должна быть, поэтому по умолчанию включено:
	 * иначе бойцы, заспавненные в середине маршрута, побегут к его началу.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Патруль")
	bool bStartFromNearestPoint = true;

	/**
	 * Сколько бойцов группы остаются БЕЗ маршрута («сапёр-сторож» у заряда).
	 * Сторожа выбираются первыми, поэтому первая точка `SpawnPoints` — это
	 * позиция сторожа.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Патруль", meta = (ClampMin = "0"))
	int32 SentryCount = 0;

	/**
	 * Смещать стартовый индекс маршрута у каждого следующего бойца. Группа с
	 * общим маршрутом иначе выходит колонной из одной точки; со смещением она
	 * растягивается по контуру и читается как обход территории.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Патруль")
	bool bStaggerPatrolStart = true;

	/**
	 * Минимальная дистанция до ближайшего бойца отряда (см). Verbatim
	 * `XComExclusionDistance`: группа не может появиться игроку в упор.
	 * 0 — не проверять (осмысленно только для стартовой расстановки).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Правила", meta = (ClampMin = "0"))
	float MinDistanceToSquad = 0.f;

	/** Заспавненные этой группой бойцы (для логов, скриптов и валидатора). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Группа")
	TArray<AUnitBase*> GetSpawnedUnits() const;

	/** Сколько бойцов положено на этой сложности. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Группа")
	int32 GetCountForDifficulty(EDifficultyLevel Difficulty) const;

protected:
	/**
	 * Общая механика: подобрать точки, заспавнить, настроить под/маршрут,
	 * применить сложность и профиль AI. Возвращает число созданных бойцов.
	 * `bRegisterInCombat` — вводить ли их в уже идущий бой (подкрепления).
	 */
	int32 SpawnUnits(int32 Count, bool bRegisterInCombat, TArray<AUnitBase*>& OutUnits);

	/** Точка появления для i-го бойца с проекцией на навмеш и правилами дистанции. */
	bool ResolveSpawnLocation(int32 Index, FVector& OutLocation, FRotator& OutRotation) const;

	/** Ближе ли точка к отряду, чем `MinDistanceToSquad`. */
	bool IsTooCloseToSquad(const FVector& Location) const;

	/**
	 * Пригодна ли точка по правилам группы. База проверяет только дистанцию до
	 * отряда; маяк подкрепления добавляет «вне поля зрения отряда».
	 */
	virtual bool IsSpawnLocationAllowed(const FVector& Location) const;

	/** Проставляет юниту под, маршрут и роль сторожа. */
	void ConfigureSpawnedUnit(AUnitBase* Unit, int32 IndexInGroup, int32 TotalCount) const;

	/** BP-хук: группа появилась (VFX высадки, звук). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Группа")
	void OnUnitsSpawned(const TArray<AUnitBase*>& Units);

	UPROPERTY(Transient)
	TArray<TObjectPtr<AUnitBase>> SpawnedUnits;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Группа")
	TObjectPtr<USceneComponent> Root;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif
};

/**
 * Стартовая группа миссии. Спавнится сценарным bootstrap'ом ДО `StartCombat`,
 * поэтому её бойцы попадают в стороны боя обычным сбором и ничем не
 * отличаются от расставленных руками.
 *
 * Отсев по сложности — это «не создать лишнего», а не «удалить актора посреди
 * первого хода»: лишние бойцы не создаются вовсе (docs/03_ARCHITECTURE.md §10).
 */
UCLASS(Blueprintable)
class XRU1_API ATacticalEncounter : public ATacticalSpawnGroupBase
{
	GENERATED_BODY()

public:
	ATacticalEncounter();

	/** Имя группы для логов, валидатора и скриптов («Yard_Center», «Bomb_Guard»). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Группа")
	FName EncounterId;

	/**
	 * Выключить энкаунтер, не удаляя его с карты. Нужно, когда расстановка
	 * временно ведётся руками, а актор хочется сохранить.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Группа")
	bool bEnabled = true;

	/** Создаёт группу по сложности. Возвращает число бойцов. Идемпотентно. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Группа")
	int32 SpawnForDifficulty(EDifficultyLevel Difficulty);

private:
	bool bSpawned = false;
};

/** Что заставляет маяк вызвать волну. */
UENUM(BlueprintType)
enum class EReinforcementTrigger : uint8
{
	/** Только явный вызов: StateTree-задача или Blueprint. */
	Manual UMETA(DisplayName = "Только вручную (скрипт)"),

	/** Живых врагов на карте стало не больше порога. */
	WhenEnemiesLow UMETA(DisplayName = "Когда врагов осталось мало")
};

/**
 * МАЯК ПОДКРЕПЛЕНИЯ.
 *
 * Доставка воспроизводит `XComGameState_AIReinforcementSpawner` XCOM 2:
 *  1) маяк виден игроку СРАЗУ при запросе волны;
 *  2) бойцы появляются через `CountdownEnemyTurns` ходов ВРАГА — у игрока есть
 *     ход на реакцию;
 *  3) прибывшая группа сразу вскрыта и знает последнюю позицию отряда
 *     (в оригинале — `eAC_MapwideAlert_Hostile`), она не «просыпается» заново;
 *  4) точка высадки не ближе `MinDistanceToSquad` и по возможности вне поля
 *     зрения отряда.
 *
 * Триггер, в отличие от XCOM, не расписание, а просадка вражеской стороны:
 * у нас уже есть таймер заряда, и вторая система давления «по часам» ломала бы
 * гонку. Из Long War 2 взято правило роста: каждая следующая волна сильнее.
 */
UCLASS(Blueprintable)
class XRU1_API ATacticalReinforcementBeacon : public ATacticalSpawnGroupBase
{
	GENERATED_BODY()

public:
	ATacticalReinforcementBeacon();

	/** Идентификатор для StateTree-задачи «Call Reinforcements» и логов. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление")
	FName BeaconId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление")
	EReinforcementTrigger TriggerMode = EReinforcementTrigger::WhenEnemiesLow;

	/** Порог для `WhenEnemiesLow`: волна запрашивается при живых врагах <= N. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление", meta = (ClampMin = "0"))
	int32 EnemyCountThreshold = 2;

	/** Раньше этого хода волна не запрашивается (защита от «подкрепления на первом ходу»). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление", meta = (ClampMin = "1"))
	int32 EarliestTurn = 3;

	/** Сколько ходов врага проходит между маяком и высадкой. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление", meta = (ClampMin = "0"))
	int32 CountdownEnemyTurns = 1;

	/** Максимум волн с этого маяка. Бесконечные подкрепления ломают миссию с таймером. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление", meta = (ClampMin = "1"))
	int32 MaxWaves = 2;

	/** Пауза между волнами в ходах игрока. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление", meta = (ClampMin = "0"))
	int32 CooldownTurns = 2;

	/** На сколько бойцов каждая следующая волна больше предыдущей (LW2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление", meta = (ClampMin = "0"))
	int32 WaveSizeGrowth = 1;

	/**
	 * Не высаживать волну в точке, которую отряд сейчас видит. Выключать стоит
	 * только осознанно: «противник появился на глазах из воздуха» читается как
	 * дефект, а не как давление.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление")
	bool bAvoidSquadVision = true;

	/** Прибывшие сразу знают позицию отряда и вступают в бой (модель XCOM 2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Подкрепление")
	bool bAlertedOnArrival = true;

	/** Запросить волну немедленно (StateTree/Blueprint). true — запрос принят. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Подкрепление")
	bool RequestWave();

	UFUNCTION(BlueprintPure, Category = "Tactics|Подкрепление")
	int32 GetWavesSpawned() const { return WavesSpawned; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Подкрепление")
	bool IsWavePending() const { return bWavePending; }

	/** Находит маяк по `BeaconId` (для StateTree-задачи). */
	static ATacticalReinforcementBeacon* FindBeacon(const UObject* WorldContextObject, FName BeaconId);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Дополнительно к дистанции: точка не должна просматриваться отрядом. */
	virtual bool IsSpawnLocationAllowed(const FVector& Location) const override;

	UFUNCTION()
	void HandleTurnStarted(ETurnPhase Phase);

	/** Высаживает волну и вводит её в бой. */
	void SpawnWave();

	/** Выполнены ли условия автотриггера (порог, ход, кулдаун, лимит волн). */
	bool ShouldAutoRequest() const;

	/** BP-хук: маяк зажёгся (дым/сигнальный огонь на точке высадки). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Подкрепление")
	void OnBeaconSignal();

	/** BP-хук: волна высадилась. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Подкрепление")
	void OnWaveArrived(const TArray<AUnitBase*>& Units);

private:
	int32 WavesSpawned = 0;
	int32 CountdownLeft = 0;
	int32 LastWaveTurn = -100;
	bool bWavePending = false;
};
