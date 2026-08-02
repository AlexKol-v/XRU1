#include "TutorialPresentation.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ScenarioActorRegistry.h"
#include "Sound/SoundBase.h"
#include "TacticalCameraPawn.h"
#include "TacticsAudioSubsystem.h"
#include "XRU1Log.h"

void UTutorialPresentationSubsystem::StartBeat(const FTacticalTutorialBeat& Beat)
{
	if (bBeatActive)
	{
		FinishBeat();
	}

	ActiveBeat = Beat;
	bBeatActive = true;

	// Камера наводится здесь, а не в BP: точка задаётся стабильным AnchorId и
	// потому переживает переименование актора в Outliner.
	if (!Beat.FocusAnchorId.IsNone())
	{
		if (const AActor* FocusActor =
			UTacticalScenarioSubsystem::FindScenarioActorInWorld(this, Beat.FocusAnchorId))
		{
			const APlayerController* PlayerController = GetWorld()
				? GetWorld()->GetFirstPlayerController() : nullptr;
			if (ATacticalCameraPawn* Camera = PlayerController
				? Cast<ATacticalCameraPawn>(PlayerController->GetPawn()) : nullptr)
			{
				// Режиссёрский фокус: пока такт идёт, фоновые интенты (автовыбор
				// следующего бойца, подхват врага) камеру не уводят. Иначе
				// показать игроку точку невозможно — в логе D1 фокус на зоне
				// эвакуации жил один кадр и был перебит выбором Танка.
				// Длительность такта — она же страховка от «камера залипла».
				Camera->FocusOnLocationDirected(FocusActor->GetActorLocation(),
					FMath::Max(0.1f, Beat.Duration));
			}
		}
	}

	// Озвучка: реплика играется отсюда, а не из BP-слоя — субтитр и голос обязаны
	// стартовать одним кадром, иначе рассинхрон видно на коротких тактах.
	// Пустой Voice — штатная ситуация: остаётся только субтитр (GDD §13).
	if (!Beat.Voice.IsNull())
	{
		if (USoundBase* Voice = Beat.Voice.LoadSynchronous())
		{
			const UWorld* World = GetWorld();
			UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			if (UTacticsAudioSubsystem* Audio = GameInstance
				? GameInstance->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
			{
				Audio->PlayVoice2D(Voice);
			}
		}
		else
		{
			UE_LOG(LogXRU1Audio, Warning, TEXT("Такт %s: не загрузилась озвучка %s"),
				*Beat.BeatId.ToString(), *Beat.Voice.ToString());
		}
	}

	OnBeatStarted.Broadcast(ActiveBeat);
}

void UTutorialPresentationSubsystem::FinishBeat()
{
	if (!bBeatActive)
	{
		return;
	}

	bBeatActive = false;
	const FTacticalTutorialBeat FinishedBeat = ActiveBeat;
	ActiveBeat = FTacticalTutorialBeat();

	// Камеру отпускаем ровно на конце такта — накопленный фоновый интент
	// (выбор бойца, follow) исполнится сразу же и без потери.
	if (!FinishedBeat.FocusAnchorId.IsNone())
	{
		const APlayerController* PlayerController = GetWorld()
			? GetWorld()->GetFirstPlayerController() : nullptr;
		if (ATacticalCameraPawn* Camera = PlayerController
			? Cast<ATacticalCameraPawn>(PlayerController->GetPawn()) : nullptr)
		{
			Camera->ReleaseDirectorHold();
		}
	}

	OnBeatFinished.Broadcast(FinishedBeat);
}
