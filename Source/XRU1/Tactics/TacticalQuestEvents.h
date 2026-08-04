#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NativeGameplayTags.h"
#include "TacticalQuestEvents.generated.h"

class AActor;

/**
 * Нативные каналы событий обучения/миссии. Они описывают уже подтверждённый
 * результат механики, а не нажатие UI-кнопки, поэтому квест не рассинхронизируется
 * с AP, анимацией или отклонённой способностью.
 */
namespace TacticalQuestTags
{
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Quest_Tutorial);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Quest_Mission01);

	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Camera_Adjusted);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Unit_Selected);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Zone_Entered);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Movement_Settled_Open);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Movement_Settled_InCover);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Turn_Ended);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Turn_Player_Started);

	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Combat_Attack_Normal);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Combat_Attack_Squadsight);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Combat_Attack_Overwatch);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Combat_Enemy_Eliminated);

	/**
	 * Отряд ВПЕРВЫЕ увидел этого врага (переход в видимость по правилам тумана).
	 *
	 * Реплика «Нас увидели. Работаем.» обязана звучать по факту обнаружения, а
	 * не по первому выстрелу: между контактом и стрельбой проходит ход, и
	 * привязка к атаке ставила бы её задним числом. Канал публикует
	 * `UFogOfWarSubsystem` — единственный источник правды о видимости; Source —
	 * увиденный враг.
	 */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Combat_Enemy_Spotted);

	/**
	 * Боец отряда получил подтверждённый урон и остался жив.
	 *
	 * Публикуется из точки применения урона (правило 11 §7: событие только из
	 * подтверждённого результата механики), Source — раненый боец. Смерть и
	 * тяжёлое ранение — другие факты и другие каналы.
	 */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Combat_Squad_Wounded);

	/**
	 * Боец отряда УПАЛ (тяжёлое ранение).
	 *
	 * Отдельный факт от `Squad.Wounded`: реплика медика «Держись! Иду» уместна
	 * над лежащим бойцом и нелепа на царапине. Публикуется из подтверждённой
	 * смены состояния в `AUnitBase::SetDowned`.
	 */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Combat_Squad_Downed);

	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Heal_Normal);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Heal_Revive);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Taunt_Activated);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_RunAndGun_Activated);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Overwatch_Activated);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Hunker_Activated);

	/**
	 * Сценарный выстрел ЗАКОНЧИЛСЯ (презентация и урон доведены).
	 *
	 * Обычные выстрелы ВРАГА намеренно не публикуют quest-событий: они не
	 * должны закрывать шаги обучения. Но реплики-реакции («Ай! За что?!»,
	 * «Щекотно», «Слышал свист?») обязаны звучать ПОСЛЕ попадания, а не на
	 * входе в шаг — поэтому о завершении объявляет сам оркестратор выстрела,
	 * своим событием и только для своего сценарного действия.
	 * Source — стрелок, Target — цель.
	 */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Scripted_ShotFinished);

	/**
	 * Сценарная перебежка ЗАКОНЧИЛАСЬ: боец добежал, осел в укрытие и довернул.
	 * Source — боец, Target — точка назначения. Нужна репликам, которые звучат
	 * ПОСЛЕ прибытия («Не стреляйте, свои!» Кадета), — обычное движение по
	 * приказу задачи боевых quest-событий не публикует.
	 */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Scripted_MoveFinished);

	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Defuse_Progressed);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Defuse_Completed);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Evac_Unit);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Evac_Squad);

	/**
	 * Пройдена половина исходного лимита ходов (02 §6: «Половина времени вышла»).
	 * One-shot на запуск: канал публикует `UTurnManagerSubsystem` в начале хода
	 * игрока, потому что только он знает и лимит, и номер хода. Реплика не может
	 * висеть на StateTree-счётчике: лимит зависит от сложности.
	 */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Bomb_HalfTime);

	/**
	 * Остались последние ходы до подрыва (`BombTickWarningTurns`, по умолчанию 3).
	 * One-shot; публикуется вместе с тикающим звуком заряда.
	 */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Bomb_FinalTurns);

	/** Подкрепление запрошено: маяк виден, юниты придут через Countdown ходов врага. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Reinforcements_Signaled);

	/** Подкрепление высадилось и введено в бой. */
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Reinforcements_Arrived);

	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Scenario_Ready);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Scenario_Succeeded);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Scenario_Failed);
}

/** Единая точка публикации событий в STQuestSystem из C++ и Blueprint. */
UCLASS()
class XRU1_API UTacticalQuestEvents : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Зарегистрирован ли actor как боец стороны игрока в текущем бою. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Quest",
		meta = (WorldContext = "WorldContextObject"))
	static bool IsPlayerSideUnit(const UObject* WorldContextObject, const AActor* Unit);

	/**
	 * Публикует FQuestEventData на одном точном leaf-канале Quest.Event.*.
	 * Не отправляйте одновременно generic и specific tag одного результата:
	 * StateTree-цель на родительском канале и так поймает дочерний через MatchesTag.
	 * Возвращает false для неверного тега или недоступной шины сообщений.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Quest",
		meta = (WorldContext = "WorldContextObject", DefaultToSelf = "Source"))
	static bool BroadcastQuestEvent(
		const UObject* WorldContextObject,
		FGameplayTag EventChannel,
		UObject* Source,
		int32 Amount = 1);

	/**
	 * То же, но с объектом действия: цель атаки/лечения, зона, бомба. Payload
	 * доезжает до StateTree, поэтому шаг может требовать конкретного бойца И
	 * конкретную цель, а не «любое событие этого канала».
	 * ScenarioRunId подставляется автоматически из GameInstance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Quest",
		meta = (WorldContext = "WorldContextObject"))
	static bool BroadcastQuestEventEx(
		const UObject* WorldContextObject,
		FGameplayTag EventChannel,
		UObject* Source,
		UObject* Target,
		int32 Amount = 1);

	/** Поколение текущего запуска сценария; 0 — сценарий не активен. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Quest",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetScenarioRunId(const UObject* WorldContextObject);
};
