#include "TutorialPresentation.h"

#include "Engine/World.h"
#include "Misc/Paths.h" // имя файла озвучки в логе такта
#include "GameFramework/PlayerController.h"
#include "ScenarioActorRegistry.h"
#include "Sound/SoundBase.h"
#include "TacticalCameraPawn.h"
#include "Components/AudioComponent.h"
#include "TacticsAudioSubsystem.h"
#include "XRU1Log.h"

void UTutorialPresentationSubsystem::RequestSkipBeat()
{
	if (!bBeatActive)
	{
		return;
	}
	bSkipRequested = true;

	// Пропуск ОБРЫВАЕТ голос. Иначе реплика продолжает звучать поверх уже
	// начавшегося следующего шага: игрок слышит инструкцию к тому, что сам
	// только что перескочил, и связь «реплика ↔ шаг» рвётся. Естественное
	// окончание такта голос по-прежнему не рубит — там фраза уже договорена.
	if (ActiveVoiceComponent.IsValid())
	{
		ActiveVoiceComponent->Stop();
		ActiveVoiceComponent = nullptr;
	}
	UE_LOG(LogXRU1Quest, Display, TEXT("[Beat] Игрок пропускает реплику %s — голос оборван"),
		*ActiveBeat.BeatId.ToString());
}

bool UTutorialPresentationSubsystem::ConsumeSkipRequest()
{
	const bool bSkip = bSkipRequested;
	bSkipRequested = false;
	return bSkip;
}

void UTutorialPresentationSubsystem::StartBeat(const FTacticalTutorialBeat& Beat)
{
	bSkipRequested = false; // новая реплика — новый запрос пропуска
	if (bBeatActive)
	{
		FinishBeat();
	}

	ActiveBeat = Beat;
	bBeatActive = true;

	UE_LOG(LogXRU1Quest, Display,
		TEXT("[Beat] СТАРТ %s | %s | %.1f с | голос=%s | фокус=%s | ответ=%s | ввод заблокирован"),
		*Beat.BeatId.ToString(),
		Beat.Speaker.IsEmpty() ? TEXT("<без имени>") : *Beat.Speaker.ToString(),
		Beat.Duration, *FPaths::GetBaseFilename(Beat.Voice.ToString()),
		Beat.FocusAnchorId.IsNone() ? TEXT("нет") : *Beat.FocusAnchorId.ToString(),
		Beat.HasFollowUp() ? TEXT("есть") : TEXT("нет"));

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
				// Новая реплика обрывает предыдущую: озвучен каждый шаг, и
				// быстрый игрок уходит вперёд раньше, чем «Купол» договорил.
				if (ActiveVoiceComponent.IsValid())
				{
					ActiveVoiceComponent->Stop();
				}
				ActiveVoiceComponent = Audio->PlayVoice2D(Voice);
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

	UE_LOG(LogXRU1Quest, Display, TEXT("[Beat] КОНЕЦ %s — ввод разблокирован"),
		*FinishedBeat.BeatId.ToString());

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
