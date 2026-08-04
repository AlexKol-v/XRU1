#include "MissionVoiceDirector.h"

#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "QuestGameplayTags.h"
#include "QuestTypes.h"
#include "ScenarioActorRegistry.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "SubtitleSubsystem.h"
#include "TacticalCameraPawn.h"
#include "TacticalPlayerController.h"
#include "TurnManagerSubsystem.h"
#include "XRU1Log.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"


bool UMissionVoiceDataAsset::AddLine(FName LineId, const FString& TriggerChannel,
	const FText& Speaker, const FText& Subtitle, USoundBase* Voice,
	bool bOncePerMission, int32 Priority, FName FocusAnchorId, int32 EarliestTurn)
{
	// Тег обязан существовать: опечатка в канале означала бы вечно молчащую
	// реплику, и заметить это можно было бы только по отсутствию звука в бою.
	const FGameplayTag Channel = FGameplayTag::RequestGameplayTag(
		FName(*TriggerChannel), /*ErrorIfNotFound=*/false);
	if (!Channel.IsValid())
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[Voice] Канал %s не зарегистрирован — строка %s не добавлена"),
			*TriggerChannel, *LineId.ToString());
		return false;
	}

	FMissionVoiceLine& Line = Lines.AddDefaulted_GetRef();
	Line.LineId = LineId;
	Line.TriggerChannel = Channel;
	Line.Speaker = Speaker;
	Line.Subtitle = Subtitle;
	Line.Voice = Voice;
	Line.bOncePerMission = bOncePerMission;
	Line.Priority = Priority;
	Line.FocusAnchorId = FocusAnchorId;
	Line.EarliestTurn = EarliestTurn;
	return true;
}

void UMissionVoiceDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMissionVoiceDirectorSubsystem::Deinitialize()
{
	StopMission();
	Super::Deinitialize();
}

void UMissionVoiceDirectorSubsystem::StartMission(UMissionVoiceDataAsset* Table)
{
	StopMission();

	ActiveTable = Table;
	PlayedCount.Reset();
	LastPlayedTurn.Reset();

	if (!ActiveTable || ActiveTable->Lines.Num() == 0)
	{
		return; // сценарий без реплик — штатная настройка, а не ошибка
	}

	if (!UGameplayMessageSubsystem::HasInstance(this))
	{
		UE_LOG(LogXRU1Quest, Error, TEXT("[Voice] Нет GameplayMessageSubsystem — реплики боя молчат"));
		return;
	}

	// Один листенер на родительский канал: реплики висят на разных leaf-каналах,
	// и заводить подписку на каждый — лишний реестр, который разъедется с
	// таблицей при первой же правке.
	UGameplayMessageSubsystem& Messages = UGameplayMessageSubsystem::Get(this);
	EventListenerHandle = Messages.RegisterListener<FQuestEventData>(
		QuestGameplayTags::Quest_Event,
		[this](FGameplayTag Channel, const FQuestEventData& Data)
		{
			HandleQuestEvent(Channel, Data);
		},
		EGameplayMessageMatch::PartialMatch);

	bActive = true;
	UE_LOG(LogXRU1Quest, Display, TEXT("[Voice] Директор реплик запущен: строк %d"),
		ActiveTable->Lines.Num());
}

void UMissionVoiceDirectorSubsystem::StopMission()
{
	if (EventListenerHandle.IsValid())
	{
		EventListenerHandle.Unregister();
	}
	bActive = false;
	ActiveTable = nullptr;
}

int32 UMissionVoiceDirectorSubsystem::GetCurrentTurn() const
{
	const UTurnManagerSubsystem* TurnManager = GetWorld()
		? GetWorld()->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	return TurnManager ? TurnManager->GetTurnNumber() : 0;
}

bool UMissionVoiceDirectorSubsystem::CanPlayLine(const FMissionVoiceLine& Line) const
{
	const int32 Played = PlayedCount.FindRef(Line.LineId);
	if (Line.bOncePerMission && Played > 0)
	{
		return false;
	}

	const int32 Turn = GetCurrentTurn();
	if (Line.EarliestTurn > 0 && Turn < Line.EarliestTurn)
	{
		return false;
	}

	if (!Line.bOncePerMission && Played > 0 && Line.CooldownTurns > 0)
	{
		const int32 LastTurn = LastPlayedTurn.FindRef(Line.LineId);
		if (Turn - LastTurn < Line.CooldownTurns)
		{
			return false;
		}
	}
	return true;
}

void UMissionVoiceDirectorSubsystem::HandleQuestEvent(FGameplayTag Channel, const FQuestEventData& Data)
{
	if (!bActive || !ActiveTable)
	{
		return;
	}

	// В один кадр может совпасть несколько фактов (выстрел убил врага и закрыл
	// цель). Играем ОДНУ — самую важную: очередь из реплик через пять секунд
	// рассказывает уже не про то, что на экране.
	const FMissionVoiceLine* Best = nullptr;
	for (const FMissionVoiceLine& Line : ActiveTable->Lines)
	{
		if (Line.TriggerChannel != Channel || !CanPlayLine(Line))
		{
			continue;
		}
		if (!Best || Line.Priority > Best->Priority)
		{
			Best = &Line;
		}
	}

	if (Best)
	{
		PlayLine(*Best);
	}
}

void UMissionVoiceDirectorSubsystem::PlayLine(const FMissionVoiceLine& Line)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	PlayedCount.FindOrAdd(Line.LineId)++;
	LastPlayedTurn.Add(Line.LineId, GetCurrentTurn());

	// Звук ведёт субтитр: строка живёт ровно столько, сколько звучит голос —
	// штатный режим общего слоя субтитров (`ShowLineForSound`). Собственный
	// таймер понадобился бы только реплике без озвучки.
	UAudioComponent* VoiceComponent = nullptr;
	float Duration = Line.Duration;
	if (USoundBase* Voice = Line.Voice.LoadSynchronous())
	{
		VoiceComponent = UGameplayStatics::SpawnSound2D(World, Voice);
		if (Duration <= 0.f)
		{
			Duration = Voice->GetDuration();
		}
	}
	Duration = FMath::Clamp(Duration > 0.f ? Duration : 3.f, 1.f, 20.f);

	// Субтитр отдаётся общему слою — тому же, что показывает реплики обучения и
	// интро: строка обязана выглядеть одинаково во всей игре.
	if (UXRU1SubtitleSubsystem* Subtitles = UXRU1SubtitleSubsystem::Get(this))
	{
		FXRU1SubtitleLine SubtitleLine;
		SubtitleLine.Speaker = Line.Speaker;
		SubtitleLine.Text = Line.Subtitle;
		SubtitleLine.bSkippable = true;
		SubtitleLine.SourceId = Line.LineId;
		if (VoiceComponent)
		{
			Subtitles->ShowLineForSound(SubtitleLine, VoiceComponent, Duration);
		}
		else
		{
			Subtitles->ShowLineForDuration(SubtitleLine, Duration);
		}
	}

	// Короткий акцент камеры — необязательный. Управление у игрока не забирается:
	// он в любой момент может увести камеру сам.
	if (!Line.FocusAnchorId.IsNone())
	{
		if (const AActor* Anchor = UTacticalScenarioSubsystem::FindScenarioActorInWorld(
				World, Line.FocusAnchorId))
		{
			if (ATacticalPlayerController* PlayerController =
				Cast<ATacticalPlayerController>(World->GetFirstPlayerController()))
			{
				if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PlayerController->GetPawn()))
				{
					Camera->FocusOnLocation(Anchor->GetActorLocation());
				}
			}
		}
	}

	UE_LOG(LogXRU1Quest, Display, TEXT("[Voice] %s | %s | %.1f с | канал %s"),
		*Line.LineId.ToString(), *Line.Speaker.ToString(), Duration,
		*Line.TriggerChannel.ToString());
}
