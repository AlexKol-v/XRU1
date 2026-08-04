#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h" // ключи кэша без удержания ссылок на погибших
#include "FogOfWarSubsystem.generated.h"

class AActor;
class UFogRevealableComponent;

/** Актор стал видим/скрыт для отряда. Один канал на всех потребителей. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFogActorVisibilityChanged,
	AActor*, Actor, bool, bVisible);

/**
 * Пересчёт видимости ЗАВЕРШЁН — независимо от того, изменилось ли что-нибудь.
 *
 * Отдельный канал от `OnActorVisibilityChanged`, потому что вопросы разные:
 * «изменилась видимость актора» и «наблюдатели могли сдвинуться». Сетке
 * затемнения местности нужен второй: боец, прошедший вдоль стены, меняет картину
 * местности, не меняя видимости ни одного врага.
 */
DECLARE_MULTICAST_DELEGATE(FOnFogVisibilityRecomputed);

/**
 * ЕДИНСТВЕННЫЙ player-facing источник правды «видит ли отряд этого актора».
 *
 * Это слой ПРАВИЛ, а не рендера: материал/RenderTarget обязаны только отображать
 * его результат и никогда ничего не разрешать. Любая поверхность, показывающая
 * игроку информацию о противнике — HUD, ховер, панель цели, камера, звук,
 * всплывающий текст — спрашивает здесь. Заводить второй предикат нельзя: именно
 * так расходятся картинка и правила стрельбы.
 *
 * ⚠️ Разделение полномочий (важно, легко нарушить):
 *  - ГЕОМЕТРИЮ «сторона видит цель» считает `UTacticsCombatStatics::AnyUnitSees` —
 *    один статик на всех, им же пользуется Squadsight снайпера и AI;
 *  - эта подсистема добавляет к геометрии ровно две вещи: КЭШ и правило
 *    «сторона игрока», а также владеет презентацией через
 *    `UFogRevealableComponent`;
 *  - AI НИКОГДА не читает эту подсистему. Знания врага — его перцепция.
 *
 * Модель обновления взята у XCOM 2 (`X2GameRulesetVisibilityManager` +
 * `X2VisibilityObserver`): пересчёт по СОБЫТИЮ, отдельный дешёвый путь пока
 * кто-то бежит, и ноль работы в кадре, где ничего не произошло. Наивный
 * «пересчитать при каждом запросе» у Firaxis измерен в ~20 мс на вызов — ровно
 * так работала первая редакция этого класса, и вызывал её биндинг HUD.
 */
UCLASS()
class XRU1_API UFogOfWarSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Подсистема тумана мира (может вернуть nullptr вне игрового мира). */
	static UFogOfWarSubsystem* Get(const UObject* WorldContextObject);

	// --- Запрос (дёшево: читает кэш) -----------------------------------------

	/**
	 * Видит ли отряд игрока этого актора сейчас — значение из кэша.
	 *
	 * Актор, которым туман не управляет (нет `UFogRevealableComponent`), считается
	 * ВИДИМЫМ: скрывается только то, что поручили скрывать. Так же ведёт себя
	 * XCOM 2 — `XComGameState_InteractiveObject::ForceModelVisible()` безусловно
	 * `eForceVisible`, поэтому цель миссии и зона эвакуации не прячутся никогда.
	 *
	 * ⚠️ Есть второй способ спросить то же самое —
	 * `UFogRevealableComponent::IsActorPresentationHidden`. Это НЕ второй источник
	 * правды: решение принимается здесь, компонент лишь хранит его применённым к
	 * презентации, и кэш заполняется из него же. Правило выбора:
	 *  - «видит ли отряд» для правил, HUD и камеры — спрашивать ЗДЕСЬ;
	 *  - гейт презентации в горячей точке (звук, ховер, всплывающий текст), где
	 *    нужен ответ по одному актору и без обращения к миру, — у компонента.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|FogOfWar")
	bool IsActorCurrentlyVisible(const AActor* Actor) const;

	/** Живые враги, видимые отряду прямо сейчас. */
	UFUNCTION(BlueprintPure, Category = "Tactics|FogOfWar")
	TArray<AActor*> GetCurrentlyVisibleEnemies() const;

	/** Количество обнаруженных сейчас врагов — безопасно для HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactics|FogOfWar")
	int32 GetCurrentlyVisibleEnemyCount() const;

	/** Видимость актора изменилась. HUD и камера подписываются сюда, а не опрашивают. */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|FogOfWar")
	FOnFogActorVisibilityChanged OnActorVisibilityChanged;

	/**
	 * Пересчёт прошёл (см. `FOnFogVisibilityRecomputed`). Сюда подписан визуальный
	 * слой — `UFogGridSubsystem`.
	 *
	 * ⚠️ Направление зависимости одностороннее: сетка подписывается на правила,
	 * правила о сетке не знают вовсе и никогда её не спрашивают. Обратная стрелка
	 * немедленно сделала бы картинку источником правды.
	 */
	FOnFogVisibilityRecomputed OnVisibilityRecomputed;

	// --- Жизненный цикл -------------------------------------------------------

	/**
	 * Пометить видимость устаревшей: пересчёт произойдёт ОДИН раз в этом кадре,
	 * сколько бы событий ни пришло. `Reason` попадает в `LogXRU1Fog` — по логу
	 * должно восстанавливаться, что именно спровоцировало пересчёт.
	 */
	void MarkVisibilityDirty(const UObject* Reason);

	/**
	 * Новый запуск сценария: кэш, подписки и состояние презентации не имеют права
	 * пережить `ScenarioRunId`. Иначе Tutorial раскроет Mission01 на той же карте,
	 * а retry покажет врагов прошлого прогона на один кадр.
	 */
	void ResetForScenario(FName ScenarioId, int32 RunId);

	// --- Регистрация скрываемых ----------------------------------------------

	void RegisterRevealable(UFogRevealableComponent* Component);
	void UnregisterRevealable(UFogRevealableComponent* Component);

	/**
	 * Сторона игрока. Такие акторы туман НЕ скрывает и их презентацию НЕ трогает:
	 * видимостью оверхедов отряда владеет
	 * `ATacticalPlayerController::UpdateSquadOverheadVisibility` (кадр выстрела,
	 * прицеливание, реакция), и второй хозяин там уже приводил к худам,
	 * выскакивающим посреди кадра. Публичный, потому что то же правило обязан
	 * применять сам компонент: показать своего его может попросить и сценарный
	 * такт, минуя пересчёт.
	 */
	static bool IsPlayerSideActor(const AActor* Actor);

	/**
	 * Включён ли подробный разбор (`xru1.Fog.Explain`). Нужен компоненту: часть
	 * его диагностики интересна только при разборе, а второй cvar на то же самое
	 * означал бы два выключателя у одного инструмента.
	 */
	static bool IsExplainEnabled();

	// --- UTickableWorldSubsystem ---------------------------------------------

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/**
	 * Полный пересчёт: расчёт видимости, применение презентации, рассылка событий.
	 * Единственное место, где меняется кэш.
	 *
	 * `bRoutine` — пересчёт «по расписанию» (кто-то бежит, ждём отложенного
	 * скрытия). Такой пересчёт без изменений в журнал не пишется даже при
	 * включённом разборе: их сотни за бой, и они топят значимые строки.
	 */
	void RecomputeNow(const TCHAR* Reason, bool bRoutine = false);

	/** Чистая геометрия + правило стороны, без кэша и без override компонента. */
	bool ComputeActorVisible(const AActor* Actor, const TArray<AActor*>& Viewers) const;

	/** Бежит ли сейчас хоть кто-то: во время движения видимость переоценивается. */
	bool IsAnyUnitInTransit() const;

	/**
	 * Зарегистрированные скрываемые. Слабые ссылки: враг может погибнуть или
	 * уехать вместе с выгруженным sublevel, и реестр не должен его удерживать.
	 */
	TArray<TWeakObjectPtr<UFogRevealableComponent>> Revealables;

	/** Кэш «актор → виден отряду». Ключ не удерживает объект. */
	TMap<TObjectKey<AActor>, bool> VisibilityCache;

	/**
	 * Враги, о встрече с которыми уже объявлено каналом `Combat.Enemy.Spotted`.
	 * Событие описывает ПЕРВЫЙ контакт: враг, скрывшийся за стену и вышедший
	 * обратно, не должен заново объявлять «нас увидели». Живёт один запуск —
	 * очищается в `ResetForScenario` вместе с кэшем.
	 */
	TSet<TObjectKey<AActor>> SpottedEnemies;

	/**
	 * Живые враги, видимые отряду на момент последнего пересчёта. Поддерживается
	 * вместе с кэшем: счётчик врагов в HUD спрашивается каждый кадр и не имеет
	 * права ни строить массив, ни обходить кэш заново.
	 */
	TArray<TWeakObjectPtr<AActor>> VisibleEnemies;

	/** Событие пришло — пересчитать в ближайшем тике (коалесинг за кадр). */
	bool bVisibilityDirty = true;

	/**
	 * Имя источника ближайшего пересчёта — для журнала. Именно строка, а не
	 * указатель: источником обычно оказывается юнит, который в этот самый момент
	 * погибает, и к моменту пересчёта объекта может уже не быть.
	 */
	FString PendingReasonName = TEXT("старт");

	/** Кто-то бежал в прошлом тике: по остановке нужен финальный точный пересчёт. */
	bool bWasAnyoneInTransit = false;

	/** Значение `xru1.Fog.Disable` в прошлом тике — переключение считается событием. */
	bool bFogDisabledLastTick = false;

	/** Есть отложенные скрытия (актор доигрывает действие) — переспрашивать. */
	bool bHasDeferredHides = false;

	/**
	 * Время последнего пересчёта по ИГРОВОМУ времени мира (с учётом замедления).
	 * Так и надо: в slow-mo реакции всё движется медленнее, и переоценивать
	 * видимость чаще, чем меняется сцена, незачем. На паузе тик не идёт вовсе,
	 * поэтому «застрять» интервал не может.
	 */
	double LastRecomputeTime = 0.0;

	/** Текущий запуск сценария — для журнала и защиты от состояния прошлого run. */
	FName ActiveScenarioId;
	int32 ActiveRunId = 0;
};
