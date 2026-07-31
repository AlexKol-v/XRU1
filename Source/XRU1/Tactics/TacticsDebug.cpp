#include "TacticsDebug.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "XRU1.h"
#include "XRU1Log.h"

namespace TacticsDebug
{
	static TAutoConsoleVariable<int32> CVarAILog(
		TEXT("xru1.AI.LogCombat"), 0,
		TEXT("1 — печатать варианты, скор и выбранное действие тактического AI."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarAIDebugDraw(
		TEXT("xru1.AI.DebugDraw"), 0,
		TEXT("1 — рисовать в мире решение AI: цель, точку манёвра и угрозы."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarQuestLog(
		TEXT("xru1.Quest.LogEvents"), 0,
		TEXT("1 — печатать каждое опубликованное quest-событие с источником и целью."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarGateLog(
		TEXT("xru1.Tutorial.LogGate"), 0,
		TEXT("1 — печатать применение политик Action Gate и причины отказов."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarAudioLog(
		TEXT("xru1.Audio.LogEvents"), 0,
		TEXT("1 — печатать звуковые события и предупреждать о незаполненных репликах."),
		ECVF_Cheat);

	static TAutoConsoleVariable<float> CVarDebugDrawDuration(
		TEXT("xru1.Debug.DrawDuration"), 3.f,
		TEXT("Сколько секунд держатся отладочные надписи над юнитами."),
		ECVF_Cheat);

	bool IsAILogEnabled() { return CVarAILog.GetValueOnAnyThread() != 0; }
	bool IsAIDebugDrawEnabled() { return CVarAIDebugDraw.GetValueOnAnyThread() != 0; }
	bool IsQuestLogEnabled() { return CVarQuestLog.GetValueOnAnyThread() != 0; }
	bool IsGateLogEnabled() { return CVarGateLog.GetValueOnAnyThread() != 0; }
	bool IsAudioLogEnabled() { return CVarAudioLog.GetValueOnAnyThread() != 0; }
	float GetDebugDrawDuration() { return FMath::Max(0.1f, CVarDebugDrawDuration.GetValueOnAnyThread()); }

	/**
	 * `xru1.Debug.List` — печатает весь набор переключателей с текущими
	 * значениями. Без этого на финальном тесте приходится помнить имена наизусть.
	 */
	static FAutoConsoleCommand ListDebugCVars(
		TEXT("xru1.Debug.List"),
		TEXT("Показать все отладочные переключатели XRU1 и их текущие значения."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			UE_LOG(LogXRU1, Display, TEXT("--- Отладочные переключатели XRU1 ---"));
			UE_LOG(LogXRU1, Display, TEXT("xru1.AI.LogCombat        = %d  решения AI"), CVarAILog.GetValueOnGameThread());
			UE_LOG(LogXRU1, Display, TEXT("xru1.AI.DebugDraw        = %d  отрисовка решений AI"), CVarAIDebugDraw.GetValueOnGameThread());
			UE_LOG(LogXRU1, Display, TEXT("xru1.Quest.LogEvents     = %d  quest-события"), CVarQuestLog.GetValueOnGameThread());
			UE_LOG(LogXRU1, Display, TEXT("xru1.Tutorial.LogGate    = %d  Action Gate обучения"), CVarGateLog.GetValueOnGameThread());
			UE_LOG(LogXRU1, Display, TEXT("xru1.Audio.LogEvents     = %d  звук"), CVarAudioLog.GetValueOnGameThread());
			UE_LOG(LogXRU1, Display, TEXT("xru1.LOS.Debug           — линия огня и огневые позиции"));
			UE_LOG(LogXRU1, Display, TEXT("xru1.Cover.Debug         — геометрия укрытий"));
			UE_LOG(LogXRU1, Display, TEXT("xru1.MoveRange.LogBuildTime — стоимость поля хода"));
			UE_LOG(LogXRU1, Display, TEXT("Категории логов: LogXRU1AI, LogXRU1Combat, LogXRU1Turns,"));
			UE_LOG(LogXRU1, Display, TEXT("                 LogXRU1Scenario, LogXRU1Quest, LogXRU1Audio, LogXRU1UI"));
			UE_LOG(LogXRU1, Display, TEXT("Пример: Log LogXRU1AI Verbose"));
		}));
}

bool UTacticsDebugLibrary::IsAIDebugDrawEnabled()
{
	return TacticsDebug::IsAIDebugDrawEnabled();
}

bool UTacticsDebugLibrary::IsQuestLogEnabled()
{
	return TacticsDebug::IsQuestLogEnabled();
}

void UTacticsDebugLibrary::DrawUnitDebugText(const AActor* Unit, const FString& Text,
	FLinearColor Color, float ZOffset)
{
#if ENABLE_DRAW_DEBUG
	if (!Unit || !TacticsDebug::IsAIDebugDrawEnabled())
	{
		return;
	}
	const UWorld* World = Unit->GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugString(const_cast<UWorld*>(World),
		Unit->GetActorLocation() + FVector(0.f, 0.f, ZOffset), Text,
		/*TestBaseActor=*/nullptr, Color.ToFColor(true),
		TacticsDebug::GetDebugDrawDuration(), /*bDrawShadow=*/true);
#endif
}
