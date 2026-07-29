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

	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Heal_Normal);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Heal_Revive);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Taunt_Activated);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_RunAndGun_Activated);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Overwatch_Activated);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Ability_Hunker_Activated);

	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Defuse_Progressed);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Defuse_Completed);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Evac_Unit);
	XRU1_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Tactical_Objective_Evac_Squad);

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
};
