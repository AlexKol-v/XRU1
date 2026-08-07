#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "MissionVoiceDirector.generated.h"

class USoundBase;
struct FQuestEventData;

/**
 * Одна реплика боя: что говорится и КОГДА.
 *
 * Почему это таблица, а не задачи StateTree. Реплики боя — реакции на факты,
 * которые случаются в произвольном порядке и могут не случиться вовсе. В дереве
 * им пришлось бы жить фоновыми задачами родительского состояния, а состояние с
 * одними фоновыми задачами завершается сразу, и дерево уходит в бесконечный
 * перезапуск: реплики звучат по кругу. StateTree остаётся для
 * ЦЕЛЕЙ (последовательность шагов), реплики — здесь.
 */
USTRUCT(BlueprintType)
struct XRU1_API FMissionVoiceLine
{
	GENERATED_BODY()

	/** Идентификатор для логов и диагностики. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика")
	FName LineId;

	/** Канал события, по которому реплика звучит (точное совпадение leaf). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика")
	FGameplayTag TriggerChannel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика")
	FText Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика", meta = (MultiLine = true))
	FText Subtitle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика")
	TSoftObjectPtr<USoundBase> Voice;

	/** Сколько держать субтитр, с. 0 — взять длительность озвучки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика", meta = (ClampMin = "0"))
	float Duration = 0.f;

	/**
	 * Прозвучать не больше одного раза за бой.
	 *
	 * По умолчанию включено: почти все боевые реплики описывают ПЕРВОЕ событие
	 * своего рода («Нас увидели», «Минус один»). Повтор на каждом контакте
	 * превращает их в шум.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика|Правила")
	bool bOncePerMission = true;

	/**
	 * Минимальная пауза между повторами, в ходах игрока. Работает только у
	 * повторяемых реплик (`bOncePerMission = false`).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика|Правила", meta = (ClampMin = "0"))
	int32 CooldownTurns = 2;

	/** Раньше этого хода реплика молчит (0 — без ограничения). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика|Правила", meta = (ClampMin = "0"))
	int32 EarliestTurn = 0;

	/**
	 * Приоритет при совпадении в один кадр: выше — важнее. Пока играет реплика,
	 * менее важная в тот же момент отбрасывается, а не встаёт в очередь: через
	 * пять секунд она уже не про то, что происходит на экране.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика|Правила")
	int32 Priority = 0;

	/**
	 * AnchorId, на который навести камеру (пусто — не трогать). Реплика боя
	 * камеру НЕ отбирает надолго: это короткий акцент, а не катсцена.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Реплика|Правила")
	FName FocusAnchorId;
};

/** Таблица реплик одного сценария. Ссылка живёт в `UTacticalScenarioDataAsset`. */
UCLASS(BlueprintType)
class XRU1_API UMissionVoiceDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Реплики")
	TArray<FMissionVoiceLine> Lines;

	/**
	 * Добавить строку, задав канал ТЕКСТОМ.
	 *
	 * Существует ради скриптовой сборки таблицы: `FGameplayTag` из Python не
	 * создаётся (`TagName` — read-only), а таблица из девяти реплик по восемь
	 * полей — ровно та работа, где ручной ввод даёт опечатку в теге, невидимую
	 * до прогона. Тег запрашивается у менеджера, поэтому несуществующий канал
	 * отвергается сразу, а не молчит в бою.
	 */
	UFUNCTION(BlueprintCallable, Category = "Реплики")
	bool AddLine(FName LineId, const FString& TriggerChannel, const FText& Speaker,
		const FText& Subtitle, USoundBase* Voice, bool bOncePerMission = true,
		int32 Priority = 0, FName FocusAnchorId = NAME_None, int32 EarliestTurn = 0);

	UFUNCTION(BlueprintCallable, Category = "Реплики")
	void ClearLines() { Lines.Reset(); }
};

/**
 * Директор реплик боя: слушает доменные каналы и проигрывает реплику по таблице.
 *
 * Ответственность строго одна — ПРЕЗЕНТАЦИЯ. Он ничего не решает про механику,
 * не блокирует ввод и не может помешать игроку: боевая реплика комментирует
 * происходящее, а не ведёт игрока за руку (этим занимается обучение через
 * StateTree и Action Gate).
 */
UCLASS()
class XRU1_API UMissionVoiceDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Ставит таблицу и сбрасывает историю. Зовёт GameMode на старте боя. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Реплики")
	void StartMission(UMissionVoiceDataAsset* Table);

	/** Прекращает реагировать (конец боя, выход из сценария). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Реплики")
	void StopMission();

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Разбор события: найти подходящую реплику и проиграть, если правила позволяют. */
	void HandleQuestEvent(FGameplayTag Channel, const FQuestEventData& Data);

	/** Проверка правил реплики (один раз за бой, кулдаун, самый ранний ход). */
	bool CanPlayLine(const FMissionVoiceLine& Line) const;

	void PlayLine(const FMissionVoiceLine& Line);

	int32 GetCurrentTurn() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMissionVoiceDataAsset> ActiveTable;

	/** Сколько раз реплика уже звучала и на каком ходу — правила живут здесь. */
	TMap<FName, int32> PlayedCount;
	TMap<FName, int32> LastPlayedTurn;

	FGameplayMessageListenerHandle EventListenerHandle;
	bool bActive = false;
};
