#include "TacticalQuestEvents.h"

#include "GameFramework/GameplayMessageSubsystem.h"
#include "Engine/World.h"
#include "QuestGameplayTags.h"
#include "QuestTypes.h"
#include "TacticsDebug.h"
#include "TacticsGameInstance.h"
#include "XRU1Log.h"
#include "TurnManagerSubsystem.h"

namespace TacticalQuestTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Quest_Tutorial, "Quest.Tutorial", "Предвылетная аттестация на общей боевой карте.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Quest_Mission01, "Quest.Mission01", "Операция на Узле-7 на общей боевой карте.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Camera_Adjusted, "Quest.Event.Tactical.Camera.Adjusted", "Игрок осознанно изменил ракурс камеры.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Unit_Selected, "Quest.Event.Tactical.Unit.Selected", "Игрок выбрал требуемого бойца.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Zone_Entered, "Quest.Event.Tactical.Zone.Entered", "Боец игрока впервые вошёл в требуемую тактическую зону.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Movement_Settled_Open, "Quest.Event.Tactical.Movement.Settled.Open", "Открытое перемещение и финальный доворот полностью завершены.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Movement_Settled_InCover, "Quest.Event.Tactical.Movement.Settled.InCover", "Юнит полностью закончил перемещение в рабочем укрытии.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Turn_Ended, "Quest.Event.Tactical.Turn.Ended", "Фаза игрока действительно завершена.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Turn_Player_Started, "Quest.Event.Tactical.Turn.Player.Started", "Подтверждённо началась новая фаза игрока.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Combat_Attack_Normal, "Quest.Event.Tactical.Combat.Attack.Normal", "Обычная атака дошла до подтверждённого результата.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Combat_Attack_Squadsight, "Quest.Event.Tactical.Combat.Attack.Squadsight", "Выстрел через зрение союзника разрешён.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Combat_Attack_Overwatch, "Quest.Event.Tactical.Combat.Attack.Overwatch", "Реакционная атака наблюдения разрешилась.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Combat_Enemy_Eliminated, "Quest.Event.Tactical.Combat.Enemy.Eliminated", "Вражеский юнит подтверждённо уничтожен.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Ability_Heal_Normal, "Quest.Event.Tactical.Ability.Heal.Normal", "Лечение подтверждено изменением здоровья.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Ability_Heal_Revive, "Quest.Event.Tactical.Ability.Heal.Revive", "Тяжело раненый юнит действительно поднят.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Ability_Taunt_Activated, "Quest.Event.Tactical.Ability.Taunt.Activated", "Провокация успешно активирована.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Ability_RunAndGun_Activated, "Quest.Event.Tactical.Ability.RunAndGun.Activated", "Рывок и удар успешно выдал дополнительные AP.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Ability_Overwatch_Activated, "Quest.Event.Tactical.Ability.Overwatch.Activated", "Наблюдение успешно активировано.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Ability_Hunker_Activated, "Quest.Event.Tactical.Ability.Hunker.Activated", "Глухая оборона успешно активирована.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Scripted_ShotFinished, "Quest.Event.Tactical.Scripted.ShotFinished", "Сценарный выстрел закончился: презентация и урон доведены.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Scripted_MoveFinished, "Quest.Event.Tactical.Scripted.MoveFinished", "Сценарная перебежка закончилась: боец добежал и осел.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Objective_Defuse_Progressed, "Quest.Event.Tactical.Objective.Defuse.Progressed", "Один промежуточный шаг обезвреживания подтверждён.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Objective_Defuse_Completed, "Quest.Event.Tactical.Objective.Defuse.Completed", "Обезвреживание полностью завершено.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Objective_Evac_Unit, "Quest.Event.Tactical.Objective.Evac.Unit", "Один боец успешно эвакуирован.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Objective_Evac_Squad, "Quest.Event.Tactical.Objective.Evac.Squad", "Все живые бойцы успешно эвакуированы.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Scenario_Ready, "Quest.Event.Tactical.Scenario.Ready", "Сценарный sublevel зарегистрирован и бой готов к управлению.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Scenario_Succeeded, "Quest.Event.Tactical.Scenario.Succeeded", "Сценарий завершён подтверждённой победой.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Tactical_Scenario_Failed, "Quest.Event.Tactical.Scenario.Failed", "Сценарий завершён подтверждённым поражением.");
}

bool UTacticalQuestEvents::IsPlayerSideUnit(
	const UObject* WorldContextObject,
	const AActor* Unit)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UTurnManagerSubsystem* TurnManager = World
		? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!Unit || !TurnManager)
	{
		return false;
	}

	for (const AActor* PlayerUnit : TurnManager->GetPlayerSideUnits())
	{
		if (PlayerUnit == Unit)
		{
			return true;
		}
	}
	return false;
}

int32 UTacticalQuestEvents::GetScenarioRunId(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UTacticsGameInstance* GameInstance = World
		? World->GetGameInstance<UTacticsGameInstance>() : nullptr;
	return GameInstance ? GameInstance->GetActiveScenarioRunId() : 0;
}

bool UTacticalQuestEvents::BroadcastQuestEvent(
	const UObject* WorldContextObject,
	FGameplayTag EventChannel,
	UObject* Source,
	int32 Amount)
{
	return BroadcastQuestEventEx(WorldContextObject, EventChannel, Source, nullptr, Amount);
}

bool UTacticalQuestEvents::BroadcastQuestEventEx(
	const UObject* WorldContextObject,
	FGameplayTag EventChannel,
	UObject* Source,
	UObject* Target,
	int32 Amount)
{
	if (!WorldContextObject || !EventChannel.IsValid() || Amount <= 0 ||
		!EventChannel.MatchesTag(QuestGameplayTags::Quest_Event) ||
		!UGameplayMessageSubsystem::HasInstance(WorldContextObject))
	{
		return false;
	}

	FQuestEventData EventData;
	EventData.EventTag = EventChannel;
	EventData.Amount = Amount;
	EventData.Source = Source;
	EventData.Target = Target;
	// Поколение проставляем здесь, а не в каждом вызывающем: иначе один забытый
	// emitter снова начнёт засчитываться после retry.
	EventData.ScenarioRunId = GetScenarioRunId(WorldContextObject);

	if (TacticsDebug::IsQuestLogEnabled())
	{
		// Источник и цель — ровно то, чего не хватало для разбора «почему шаг не
		// закрылся»: канал совпал, а боец оказался не тот.
		UE_LOG(LogXRU1Quest, Display,
			TEXT("Событие %s | source=%s | target=%s | amount=%d | run=%d"),
			*EventChannel.ToString(), *GetNameSafe(Source), *GetNameSafe(Target),
			Amount, EventData.ScenarioRunId);
	}

	UGameplayMessageSubsystem::Get(WorldContextObject).BroadcastMessage(EventChannel, EventData);
	return true;
}
