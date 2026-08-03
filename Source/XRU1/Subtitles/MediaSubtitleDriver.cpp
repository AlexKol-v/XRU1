#include "MediaSubtitleDriver.h"

#include "MediaPlayer.h"
#include "SubtitleSubsystem.h"
#include "SubtitleTrackDataAsset.h"
#include "XRU1Log.h"

void UMediaSubtitleDriver::Start(UMediaPlayer* InPlayer, USubtitleTrackDataAsset* InTrack, FName InSourceId)
{
	Stop();

	Player = InPlayer;
	Track = InTrack;
	SourceId = InSourceId;

	if (!IsValid(InPlayer) || !IsValid(InTrack) || InTrack->Cues.Num() == 0)
	{
		UE_LOG(LogXRU1UI, Display, TEXT("[Субтитры] титры ролика '%s' не ведутся: плеер=%s трек=%s"),
			*SourceId.ToString(), *GetNameSafe(InPlayer), *GetNameSafe(InTrack));
		return;
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMediaSubtitleDriver::TickDriver), 0.f);

	UE_LOG(LogXRU1UI, Display, TEXT("[Субтитры] титры ролика '%s': %d реплик"),
		*SourceId.ToString(), InTrack->Cues.Num());
}

void UMediaSubtitleDriver::Stop()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (ActiveHandle.IsValid())
	{
		if (UXRU1SubtitleSubsystem* Subtitles = UXRU1SubtitleSubsystem::Get(this))
		{
			Subtitles->HideLine(ActiveHandle);
		}
		ActiveHandle = FXRU1SubtitleHandle();
	}

	CurrentCueIndex = INDEX_NONE;
	Player.Reset();
	Track = nullptr;
}

void UMediaSubtitleDriver::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}

bool UMediaSubtitleDriver::TickDriver(float /*DeltaTime*/)
{
	UMediaPlayer* MediaPlayer = Player.Get();
	if (!IsValid(MediaPlayer) || !IsValid(Track))
	{
		return true; // ведение прекращает Stop(), а не пропавший кадр
	}

	const float TimeSeconds = static_cast<float>(MediaPlayer->GetTime().GetTotalSeconds());
	const int32 CueIndex = Track->FindCueIndex(TimeSeconds);
	if (CueIndex == CurrentCueIndex)
	{
		return true;
	}
	CurrentCueIndex = CueIndex;

	UXRU1SubtitleSubsystem* Subtitles = UXRU1SubtitleSubsystem::Get(this);
	if (!Subtitles)
	{
		return true;
	}

	// Свою прошлую строку снимаем явно: между репликами таймлайна бывает тишина,
	// и без этого последняя реплика висела бы до самого конца ролика.
	Subtitles->HideLine(ActiveHandle);
	ActiveHandle = FXRU1SubtitleHandle();

	if (CueIndex != INDEX_NONE)
	{
		ActiveHandle = Subtitles->ShowLine(Track->MakeLine(CueIndex, SourceId));
	}

	return true;
}
