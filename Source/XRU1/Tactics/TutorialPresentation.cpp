#include "TutorialPresentation.h"

#include "Engine/World.h"
#include "FogGridSubsystem.h"       // беат раскрывает местность вокруг точки фокуса
#include "FogRevealableComponent.h" // и показывает самого актора, на которого смотрит камера
#include "Misc/Paths.h" // имя файла озвучки в логе такта
#include "GameFramework/PlayerController.h"
#include "ScenarioActorRegistry.h"
#include "Sound/SoundBase.h"
#include "TacticalCameraPawn.h"
#include "Components/AudioComponent.h"
#include "TacticsAudioSubsystem.h"
#include "SubtitleSubsystem.h"
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

			// Раз камера показывает точку — местность вокруг неё обязана быть
			// видна. Карта стартует чёрной, и беат, наводящий на ещё не
			// разведанный сектор (зона эвакуации в D1, соседняя секция в B),
			// показал бы игроку пустоту. Это тот же приём, что у XCOM для
			// скриптовых показов — `XComWorldData::CreateFOWViewer`.
			//
			// Длительность здесь — СТРАХОВКА, как и у камеры: штатно раскрытие
			// снимается в `FinishBeat`, но такт может оборваться (пропуск реплики,
			// смена сценария), и повисшее раскрытие разведало бы сектор навсегда.
			if (UFogGridSubsystem* FogGrid = UFogGridSubsystem::Get(this))
			{
				FogGrid->RemoveScriptedReveal(BeatRevealHandle);
				BeatRevealHandle = FogGrid->AddScriptedReveal(FocusActor, FVector::ZeroVector,
					-1.f, FMath::Max(0.1f, Beat.Duration) + BeatRevealGraceSeconds);
			}

			// ⚠️ Раскрыть местность мало — надо показать и САМОГО актора, иначе
			// камера наводится на пустое место: местность вокруг врага открыта,
			// а он сам скрыт туманом (например, беат с фокусом на мародёре).
			// Такты `Scripted Move` берут
			// оба удержания парно — беат обязан вести себя так же.
			if (UFogRevealableComponent* Reveal =
				const_cast<AActor*>(FocusActor)->FindComponentByClass<UFogRevealableComponent>())
			{
				Reveal->AddScriptedRevealHold();
				BeatFogRevealHold = Reveal;
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
				// bAutoSubtitle=false: субтитр такта ведёт сам такт (ниже), и
				// автосубтитр по данным ассета озвучки перебил бы его текстом
				// той же реплики, но со своими часами.
				ActiveVoiceComponent = Audio->PlayVoice2D(Voice, 1.f, /*bAutoSubtitle=*/false);
			}
		}
		else
		{
			UE_LOG(LogXRU1Audio, Warning, TEXT("Такт %s: не загрузилась озвучка %s"),
				*Beat.BeatId.ToString(), *Beat.Voice.ToString());
		}
	}

	// Субтитр отдаётся общему слою: он один на всю игру и рисуется поверх любых
	// экранов. В оверлее подсказок боевого контроллера строке не место — вне
	// боя она не существовала бы физически.
	if (UXRU1SubtitleSubsystem* Subtitles = UXRU1SubtitleSubsystem::Get(this))
	{
		FXRU1SubtitleLine Line;
		Line.Speaker = ActiveBeat.Speaker;
		Line.Text = ActiveBeat.Subtitle;
		Line.bSkippable = true;
		Line.SourceId = ActiveBeat.BeatId;
		SubtitleHandle = Subtitles->ShowLine(Line);
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

	// Снимаем ИМЕННО свою строку: если такт уже сменился (обмен репликами),
	// дескриптор не совпадёт и слой проигнорирует запоздалый вызов.
	if (UXRU1SubtitleSubsystem* Subtitles = UXRU1SubtitleSubsystem::Get(this))
	{
		Subtitles->HideLine(SubtitleHandle);
	}
	SubtitleHandle = FXRU1SubtitleHandle();

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

	// Раскрытие местности снимается парно фокусу камеры: разведанным сектор
	// остаётся ровно в той мере, в какой отряд его действительно увидел.
	if (UFogGridSubsystem* FogGrid = UFogGridSubsystem::Get(this))
	{
		FogGrid->RemoveScriptedReveal(BeatRevealHandle);
	}
	BeatRevealHandle = 0;

	// Удержание показа снимается СТРОГО по своей ссылке: к концу такта поиск по
	// AnchorId может уже не найти актора (голограмма выключена следующим шагом),
	// и удержание утекло бы, оставив врага видимым навсегда.
	if (UFogRevealableComponent* Reveal = BeatFogRevealHold.Get())
	{
		Reveal->RemoveScriptedRevealHold();
	}
	BeatFogRevealHold = nullptr;

	OnBeatFinished.Broadcast(FinishedBeat);
}
