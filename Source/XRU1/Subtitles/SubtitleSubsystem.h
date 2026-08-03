#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SubtitleTypes.h"
#include "SubtitleSubsystem.generated.h"

class SXRU1SubtitleOverlay;
class UAudioComponent;
class UGameViewportClient;
class USoundBase;
class USubtitleTrackDataAsset;
class UTacticalHUDStyleData;

/** Активная строка сменилась (пустая — субтитр снят). Для внешнего дисплея. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubtitleLineChanged, const FXRU1SubtitleLine&, Line);

/**
 * Единый слой субтитров проекта.
 *
 * ── Принцип ──────────────────────────────────────────────────────────────────
 * Слой — ДИСПЛЕЙ, а не планировщик. Он не решает, сколько живёт реплика: у
 * источников уже есть свои часы, и второй счётчик неизбежно с ними разойдётся.
 * Поэтому режимов показа три, и режим выбирает источник:
 *
 *   1. `ShowLine` + `HideLine`   — временем владеет источник.
 *      Такт обучения (`StartBeat`/`FinishBeat` вызывает задача StateTree, она же
 *      считает `Duration`, обмен репликами и пропуск игроком) и титры ролика
 *      (время даёт медиаплеер). Тайминги совпадают ПО ПОСТРОЕНИЮ.
 *   2. `ShowLineForSound`        — строка живёт ровно столько, сколько звучит
 *      переданный компонент. Пауза, обрыв следующей репликой и остановка звука
 *      снимают субтитр сами: отдельного учёта нет вообще.
 *   3. `ShowLineForDuration`     — единственный случай собственного тайминга,
 *      по таймеру ИГРОВОГО мира (замирает вместе с миром и голосом).
 *
 * ── Одна строка на экране ────────────────────────────────────────────────────
 * Слой повторяет правило звукового слоя: голос в проекте один
 * (`UTacticsAudioSubsystem::VoiceComponent` — одиночный указатель, новая реплика
 * останавливает предыдущую), значит и субтитр один, а новая строка вытесняет
 * предыдущую. Очереди приоритетов нет намеренно: в движковом плагине она даёт
 * класс багов «строка молча не показалась» (равный приоритет отбрасывает
 * новую), а выигрыша в нашем сценарии не даёт.
 *
 * ── Заменяемость ─────────────────────────────────────────────────────────────
 * Подсистема — стабильный API; рисующий виджет заменяем. Встроенный
 * Slate-оверлей отключается флагом `bUseBuiltInDisplay`, и любой другой дисплей
 * (WBP) подписывается на `OnLineChanged` и читает `GetResolvedStyle()`.
 * Источники при этом не меняются: они не знают, кто и где рисует.
 *
 * Уровень GameInstance, а не мира: субтитры нужны в меню, хабе и бою и обязаны
 * переживать travel.
 */
UCLASS()
class XRU1_API UXRU1SubtitleSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Подсистема по любому объекту мира/GameInstance; nullptr вне игры. */
	static UXRU1SubtitleSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Показ ---------------------------------------------------------------

	/** Режим 1: строку снимет владелец. Возвращает дескриптор для `HideLine`. */
	FXRU1SubtitleHandle ShowLine(const FXRU1SubtitleLine& Line);

	/**
	 * Режим 2: строка живёт, пока звучит `Voice`.
	 * `HoldAfterSound` — сколько держать после конца звука, с.
	 * `Voice == nullptr` — деградация до режима 3 по длительности звука.
	 */
	FXRU1SubtitleHandle ShowLineForSound(const FXRU1SubtitleLine& Line, UAudioComponent* Voice,
		float HoldAfterSound = 0.f);

	/** Режим 3: строка снимается таймером игрового мира. */
	FXRU1SubtitleHandle ShowLineForDuration(const FXRU1SubtitleLine& Line, float Seconds);

	/**
	 * Субтитр по данным САМОГО ассета озвучки (`USoundSubtitleData` в
	 * `Asset User Data`). Ничего не делает, если данных нет — поэтому вызов
	 * безопасно стоит в общей точке проигрывания голоса.
	 */
	FXRU1SubtitleHandle ShowVoiceSubtitle(USoundBase* Sound, UAudioComponent* Voice);

	/** Снимает строку, если дескриптор всё ещё принадлежит активной строке. */
	void HideLine(const FXRU1SubtitleHandle& Handle);

	/** Снимает строку кем угодно (смена уровня, экстренная уборка). */
	UFUNCTION(BlueprintCallable, Category = "Субтитры")
	void HideAll();

	// --- Фасад для Blueprint --------------------------------------------------

	/** Показать реплику на заданное время (режим 3). */
	UFUNCTION(BlueprintCallable, Category = "Субтитры")
	FXRU1SubtitleHandle ShowSubtitle(FText Text, FText Speaker, float Seconds = 3.f);

	UFUNCTION(BlueprintCallable, Category = "Субтитры")
	void HideSubtitle(FXRU1SubtitleHandle Handle);

	// --- Состояние (читает дисплей) -------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Субтитры")
	bool IsShowingLine() const { return ActiveHandle.IsValid(); }

	/** Копией, а не ссылкой: UFUNCTION не умеет возвращать ссылку. */
	UFUNCTION(BlueprintPure, Category = "Субтитры")
	FXRU1SubtitleLine GetActiveLine() const { return ActiveLine; }

	/** Тема + настройки игрока + выбранный якорь позиции, сведённые воедино. */
	UFUNCTION(BlueprintPure, Category = "Субтитры")
	FXRU1SubtitleStyle GetResolvedStyle() const;

	/** Активная строка сменилась; пустая строка — субтитр снят. */
	UPROPERTY(BlueprintAssignable, Category = "Субтитры")
	FOnSubtitleLineChanged OnLineChanged;

	/**
	 * Пересобрать показ: язык сменился либо дисплей родился заново.
	 *
	 * Без этого строка, отправленная до создания виджета (или до travel),
	 * исчезала бы молча — болезнь, уже описанная в `UDialogueSubsystem::ReplayCurrent`.
	 */
	void RefreshDisplay();

private:
	// --- Внутреннее ------------------------------------------------------------

	/** Общий вход всех режимов: гасит предыдущую строку и ставит новую. */
	FXRU1SubtitleHandle BeginLine(const FXRU1SubtitleLine& Line);

	/** Снимает подписки и таймеры текущей строки (идемпотентно). */
	void StopTracking();

	/** Гасит активную строку и оповещает дисплей. */
	void ClearActiveLine();

	/** Создаёт (при необходимости) встроенный оверлей и кладёт его в viewport. */
	void EnsureDisplay();

	/** Убирает встроенный оверлей из viewport. */
	void RemoveDisplay();

	/** Пользовательские настройки; при отсутствии настроек — дефолты структуры. */
	FTacticsSubtitleSettings GetUserSettings() const;

	/** Тема UI проекта; без назначенного ассета — её CDO (никогда не nullptr). */
	const UTacticalHUDStyleData* ResolveTheme() const;

	/** Виден ли боевой/хабовый HUD — от этого зависит высота строки. */
	bool IsGameplayAnchor() const;

	/** Конец звука, к которому привязана строка. */
	void HandleVoiceFinished(UAudioComponent* Component);

	/** Ставит таймер снятия строки по игровому времени. */
	void StartDurationTimer(int32 HandleId, float Seconds);

	/** Истекла собственная длительность строки. */
	void HandleDurationElapsed(int32 HandleId);

	/** Смена уровня: viewport очищен, оверлей нужно поставить заново. */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** Язык сменился: перерисовать активную строку. */
	void HandleTextRevisionChanged();

	/** Мир для таймеров; может быть nullptr на стыке уровней. */
	UWorld* GetTimerWorld() const;

	FXRU1SubtitleLine ActiveLine;
	FXRU1SubtitleHandle ActiveHandle;

	/**
	 * Мир, которому принадлежит активная строка.
	 *
	 * Нужен, потому что реплика может начаться ВНУТРИ загрузки уровня: вводная
	 * хаба идёт из `AHubGameMode::BeginPlay`, а `PostLoadMapWithWorld` приходит
	 * ПОЗЖЕ, уже после неё. Без этой отметки уборка «строка осталась от прошлого
	 * мира» гасила только что показанный субтитр нового мира.
	 */
	TWeakObjectPtr<UWorld> LineWorld;

	/** Мир, в котором был поставлен оверлей (viewport чистится вместе с миром). */
	TWeakObjectPtr<UWorld> DisplayWorld;

	/** Счётчик дескрипторов; 0 зарезервирован под «нет строки». */
	int32 NextHandleId = 1;

	/** Звук, к которому привязана текущая строка (режим 2). */
	TWeakObjectPtr<UAudioComponent> TrackedVoice;
	FDelegateHandle VoiceFinishedHandle;

	/** Сколько держать строку после конца звука (режим 2), с. */
	float HoldAfterSoundSeconds = 0.f;

	/** Таймер собственной длительности (режим 3) и удержания после звука. */
	FTimerHandle DurationTimer;

	/** Встроенный Slate-дисплей и viewport, в который он положен. */
	TSharedPtr<SXRU1SubtitleOverlay> DisplayWidget;
	TWeakObjectPtr<UGameViewportClient> DisplayViewport;

	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle TextRevisionHandle;
};
