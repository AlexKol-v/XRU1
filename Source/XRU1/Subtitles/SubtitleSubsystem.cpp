#include "SubtitleSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

#include "GamePauseSubsystem.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "SoundSubtitleData.h"
#include "SubtitleOverlay.h"
#include "SubtitleProjectSettings.h"
#include "TacticalHUDStyleData.h"
#include "TacticsGameInstance.h"
#include "TacticsUserSettings.h"
#include "XRU1Log.h"

UXRU1SubtitleSubsystem* UXRU1SubtitleSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}

	// GameInstance может прийти напрямую (её Init идёт раньше мира) либо через
	// любой объект мира — поддерживаем оба пути, как это делает слой звука.
	UGameInstance* GameInstance = const_cast<UGameInstance*>(Cast<UGameInstance>(WorldContext));
	if (!GameInstance)
	{
		const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		GameInstance = World ? World->GetGameInstance() : nullptr;
	}
	return GameInstance ? GameInstance->GetSubsystem<UXRU1SubtitleSubsystem>() : nullptr;
}

void UXRU1SubtitleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Пауза — владелец состояния «игра идёт»; слой субтитров у неё только
	// СПРАШИВАЕТ (IsDisplaySuppressed, живой запрос без кеша). Подписка нужна
	// лишь затем, чтобы внешний дисплей (WBP) узнал о смене видимости через
	// OnLineChanged. Зависимость объявлена явно: подписываться на подсистему,
	// которую коллекция ещё не создала, — это молча пропущенная подписка.
	Collection.InitializeDependency(UGamePauseSubsystem::StaticClass());
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UGamePauseSubsystem* Pause = GameInstance->GetSubsystem<UGamePauseSubsystem>())
		{
			Pause->OnPauseChanged.AddDynamic(this, &UXRU1SubtitleSubsystem::HandlePauseChanged);
		}
	}

	// Смена уровня чистит виджеты viewport (`UWorld::CleanupWorld`), поэтому
	// оверлей после travel обязан ставиться заново, а строка прошлого мира —
	// сниматься: её владелец остался в мире, которого больше нет.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UXRU1SubtitleSubsystem::HandlePostLoadMap);

	// Смена языка: активная строка обязана перерисоваться на новом языке.
	TextRevisionHandle = FTextLocalizationManager::Get().OnTextRevisionChangedEvent.AddUObject(
		this, &UXRU1SubtitleSubsystem::HandleTextRevisionChanged);
}

void UXRU1SubtitleSubsystem::Deinitialize()
{
	StopTracking();
	ClearActiveLine();
	RemoveDisplay();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UGamePauseSubsystem* Pause = GameInstance->GetSubsystem<UGamePauseSubsystem>())
		{
			Pause->OnPauseChanged.RemoveDynamic(this, &UXRU1SubtitleSubsystem::HandlePauseChanged);
		}
	}

	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	if (TextRevisionHandle.IsValid())
	{
		FTextLocalizationManager::Get().OnTextRevisionChangedEvent.Remove(TextRevisionHandle);
		TextRevisionHandle.Reset();
	}

	Super::Deinitialize();
}

// --- Показ -------------------------------------------------------------------

FXRU1SubtitleHandle UXRU1SubtitleSubsystem::ShowLine(const FXRU1SubtitleLine& Line)
{
	return BeginLine(Line);
}

FXRU1SubtitleHandle UXRU1SubtitleSubsystem::ShowLineForSound(const FXRU1SubtitleLine& Line,
	UAudioComponent* Voice, float HoldAfterSound)
{
	if (!IsValid(Voice))
	{
		// Компонента нет (звук не запустился) — показывать нечему следовать.
		return FXRU1SubtitleHandle();
	}

	const FXRU1SubtitleHandle Handle = BeginLine(Line);
	if (!Handle.IsValid())
	{
		return Handle;
	}

	TrackedVoice = Voice;
	HoldAfterSoundSeconds = FMath::Max(0.f, HoldAfterSound);
	VoiceFinishedHandle = Voice->OnAudioFinishedNative.AddUObject(
		this, &UXRU1SubtitleSubsystem::HandleVoiceFinished);

	// Страховка. Компонент спаунится с bAutoDestroy и может умереть, не успев
	// оповестить (смена уровня, выгрузка звука) — без неё строка зависла бы на
	// экране навсегда. Таймер идёт по игровому времени, то есть замирает на
	// паузе вместе с самим голосом.
	const USoundBase* Sound = Voice->Sound;
	const float SoundDuration = Sound ? Sound->GetDuration() : 0.f;
	if (SoundDuration > 0.f && SoundDuration < INDEFINITELY_LOOPING_DURATION)
	{
		StartDurationTimer(Handle.Id, SoundDuration + HoldAfterSoundSeconds + 0.5f);
	}

	return Handle;
}

FXRU1SubtitleHandle UXRU1SubtitleSubsystem::ShowLineForDuration(const FXRU1SubtitleLine& Line, float Seconds)
{
	const FXRU1SubtitleHandle Handle = BeginLine(Line);
	if (Handle.IsValid())
	{
		StartDurationTimer(Handle.Id, Seconds);
	}
	return Handle;
}

FXRU1SubtitleHandle UXRU1SubtitleSubsystem::ShowVoiceSubtitle(USoundBase* Sound, UAudioComponent* Voice)
{
	if (!IsValid(Sound))
	{
		return FXRU1SubtitleHandle();
	}

	const USoundSubtitleData* Data = Cast<USoundSubtitleData>(
		Sound->GetAssetUserDataOfClass(USoundSubtitleData::StaticClass()));
	if (!Data || !Data->HasSubtitle())
	{
		// Не ошибка: текст реплики может вести другой источник (такт обучения).
		// Но пока ассеты озвучки заполняются вручную, «реплика прозвучала без
		// субтитра» — самая частая причина обращения, и её нужно видеть в логе.
		UE_LOG(LogXRU1UI, Display,
			TEXT("[Субтитры] у озвучки '%s' нет данных субтитра (Asset User Data → «Субтитр озвучки»)"),
			*Sound->GetName());
		return FXRU1SubtitleHandle();
	}

	const FXRU1SubtitleLine Line = Data->MakeLine(FName(*Sound->GetName()));
	if (IsValid(Voice))
	{
		return ShowLineForSound(Line, Voice, Data->HoldAfterSound);
	}

	// Компонента нет — держим по длительности самого ассета.
	const float Duration = Sound->GetDuration();
	const float Fallback = (Duration > 0.f && Duration < INDEFINITELY_LOOPING_DURATION)
		? Duration + Data->HoldAfterSound : 3.f;
	return ShowLineForDuration(Line, Fallback);
}

void UXRU1SubtitleSubsystem::HideLine(const FXRU1SubtitleHandle& Handle)
{
	// Снять строку имеет право только её владелец. Запоздалый Hide прошлой
	// реплики обязан быть проигнорирован — иначе конец предыдущей гасит уже
	// начавшуюся следующую (типовой случай: обмен репликами в такте обучения).
	if (!Handle.IsValid() || Handle != ActiveHandle)
	{
		return;
	}
	StopTracking();
	ClearActiveLine();
}

void UXRU1SubtitleSubsystem::HideAll()
{
	StopTracking();
	ClearActiveLine();
}

FXRU1SubtitleHandle UXRU1SubtitleSubsystem::ShowSubtitle(FText Text, FText Speaker, float Seconds)
{
	FXRU1SubtitleLine Line;
	Line.Text = MoveTemp(Text);
	Line.Speaker = MoveTemp(Speaker);
	Line.SourceId = TEXT("Blueprint");
	return ShowLineForDuration(Line, Seconds);
}

void UXRU1SubtitleSubsystem::HideSubtitle(FXRU1SubtitleHandle Handle)
{
	HideLine(Handle);
}

// --- Внутреннее ---------------------------------------------------------------

FXRU1SubtitleHandle UXRU1SubtitleSubsystem::BeginLine(const FXRU1SubtitleLine& Line)
{
	StopTracking();

	if (Line.IsEmpty() || !GetUserSettings().bEnabled)
	{
		ClearActiveLine();
		return FXRU1SubtitleHandle();
	}

	ActiveLine = Line;
	LineWorld = GetTimerWorld();
	ActiveHandle.Id = NextHandleId++;
	if (NextHandleId <= 0)
	{
		NextHandleId = 1; // переполнение: 0 зарезервирован под «нет строки»
	}

	EnsureDisplay();
	OnLineChanged.Broadcast(GetVisibleLine());

	// Display, а не Verbose: строк мало (одна на реплику), а вопрос «показался
	// ли субтитр и от какого источника» возникает постоянно.
	UE_LOG(LogXRU1UI, Display, TEXT("[Субтитры] показ #%d (источник %s): «%s»"),
		ActiveHandle.Id, *ActiveLine.SourceId.ToString(), *ActiveLine.Text.ToString());

	return ActiveHandle;
}

void UXRU1SubtitleSubsystem::StopTracking()
{
	if (UWorld* World = GetTimerWorld())
	{
		World->GetTimerManager().ClearTimer(DurationTimer);
	}
	DurationTimer.Invalidate();

	if (VoiceFinishedHandle.IsValid())
	{
		if (UAudioComponent* Voice = TrackedVoice.Get())
		{
			Voice->OnAudioFinishedNative.Remove(VoiceFinishedHandle);
		}
		VoiceFinishedHandle.Reset();
	}
	TrackedVoice.Reset();
	HoldAfterSoundSeconds = 0.f;
}

void UXRU1SubtitleSubsystem::ClearActiveLine()
{
	if (!ActiveHandle.IsValid())
	{
		return;
	}

	UE_LOG(LogXRU1UI, Verbose, TEXT("[Субтитры] снята #%d (источник %s)"),
		ActiveHandle.Id, *ActiveLine.SourceId.ToString());

	ActiveLine = FXRU1SubtitleLine();
	ActiveHandle = FXRU1SubtitleHandle();
	OnLineChanged.Broadcast(GetVisibleLine());
}

void UXRU1SubtitleSubsystem::StartDurationTimer(int32 HandleId, float Seconds)
{
	UWorld* World = GetTimerWorld();
	if (!World)
	{
		UE_LOG(LogXRU1UI, Warning,
			TEXT("[Субтитры] нет мира для таймера строки #%d — она останется до следующей реплики"),
			HandleId);
		return;
	}

	World->GetTimerManager().SetTimer(DurationTimer,
		FTimerDelegate::CreateUObject(this, &UXRU1SubtitleSubsystem::HandleDurationElapsed, HandleId),
		FMath::Max(0.05f, Seconds), /*bLoop=*/false);
}

void UXRU1SubtitleSubsystem::HandleVoiceFinished(UAudioComponent* Component)
{
	// Событие могло прийти от ЧУЖОГО (предыдущего) звука — тогда оно не наше.
	if (!ActiveHandle.IsValid() || Component != TrackedVoice.Get())
	{
		return;
	}

	if (HoldAfterSoundSeconds > 0.f)
	{
		const int32 HandleId = ActiveHandle.Id;
		const float Hold = HoldAfterSoundSeconds;
		StopTracking();          // дальше строкой владеет таймер, а не звук
		StartDurationTimer(HandleId, Hold);
		return;
	}

	StopTracking();
	ClearActiveLine();
}

void UXRU1SubtitleSubsystem::HandleDurationElapsed(int32 HandleId)
{
	if (ActiveHandle.Id != HandleId)
	{
		return; // строку уже сменили — таймер опоздал
	}
	StopTracking();
	ClearActiveLine();
}

bool UXRU1SubtitleSubsystem::IsDisplaySuppressed() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UGamePauseSubsystem* Pause = GameInstance
		? GameInstance->GetSubsystem<UGamePauseSubsystem>() : nullptr;
	return Pause && Pause->IsPaused();
}

FXRU1SubtitleLine UXRU1SubtitleSubsystem::GetVisibleLine() const
{
	return IsDisplaySuppressed() ? FXRU1SubtitleLine() : ActiveLine;
}

void UXRU1SubtitleSubsystem::HandlePauseChanged(bool bPaused)
{
	// Встроенный Slate-оверлей спрашивает GetVisibleLine сам каждый кадр;
	// внешнему дисплею (WBP) смена видимости приходит тем же событием, что и
	// смена строки, — второго канала оповещения у него нет.
	OnLineChanged.Broadcast(GetVisibleLine());

	UE_LOG(LogXRU1UI, Verbose, TEXT("[Субтитры] показ %s паузой (строка %s)"),
		bPaused ? TEXT("подавлен") : TEXT("возвращён"),
		ActiveHandle.IsValid() ? TEXT("жива") : TEXT("отсутствует"));
}

void UXRU1SubtitleSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{

	// ⚠️ Событие приходит ПОЗЖЕ, чем BeginPlay акторов нового мира. Реплика,
	// начатая в BeginPlay (вводная хаба — `AHubGameMode::BeginPlay`), к этому
	// моменту уже показана, и слепая уборка гасила её на месте: субтитр не
	// появлялся вовсе. Поэтому здесь всё решается по принадлежности МИРУ.

	// Оверлей, поставленный до загрузки, снят вместе со старым миром — ссылка
	// протухла. Оверлей нового мира трогать нельзя: иначе появится второй.
	if (DisplayWorld.Get() != LoadedWorld)
	{
		DisplayWidget.Reset();
		DisplayViewport.Reset();
		DisplayWorld.Reset();
	}

	// Строку прошлого мира снимаем: её владелец остался там и снять её не сможет.
	if (ActiveHandle.IsValid() && LineWorld.Get() != LoadedWorld)
	{
		StopTracking();
		ClearActiveLine();
		return;
	}

	// Строка нового мира жива — вернуть ей оверлей, если он был потерян.
	if (ActiveHandle.IsValid())
	{
		EnsureDisplay();
	}
}

void UXRU1SubtitleSubsystem::HandleTextRevisionChanged()
{
	RefreshDisplay();
}

void UXRU1SubtitleSubsystem::RefreshDisplay()
{
	if (ActiveHandle.IsValid())
	{
		EnsureDisplay();
	}
	OnLineChanged.Broadcast(GetVisibleLine());
}

// --- Дисплей -------------------------------------------------------------------

void UXRU1SubtitleSubsystem::EnsureDisplay()
{
	if (!UXRU1SubtitleSettings::Get().bUseBuiltInDisplay)
	{
		return; // рисует внешний дисплей, подписанный на OnLineChanged
	}
	if (DisplayWidget.IsValid() && DisplayViewport.IsValid())
	{
		return;
	}

	UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (!Viewport)
	{
		return; // ещё нет вьюпорта (ранний старт) — поставим при следующей строке
	}

	DisplayWidget = SNew(SXRU1SubtitleOverlay).Owner(this);
	Viewport->AddViewportWidgetContent(DisplayWidget.ToSharedRef(),
		UXRU1SubtitleSettings::Get().DisplayZOrder);
	DisplayViewport = Viewport;
	DisplayWorld = GetTimerWorld();

	UE_LOG(LogXRU1UI, Display, TEXT("[Субтитры] оверлей поставлен в viewport (ZOrder=%d)"),
		UXRU1SubtitleSettings::Get().DisplayZOrder);
}

void UXRU1SubtitleSubsystem::RemoveDisplay()
{
	if (DisplayWidget.IsValid())
	{
		if (UGameViewportClient* Viewport = DisplayViewport.Get())
		{
			Viewport->RemoveViewportWidgetContent(DisplayWidget.ToSharedRef());
		}
	}
	DisplayWidget.Reset();
	DisplayViewport.Reset();
	DisplayWorld.Reset();
}

// --- Стиль и контекст ----------------------------------------------------------

FTacticsSubtitleSettings UXRU1SubtitleSubsystem::GetUserSettings() const
{
	if (const UTacticsUserSettings* Settings = UTacticsUserSettings::Get())
	{
		return Settings->GetSubtitleSettings();
	}
	return FTacticsSubtitleSettings();
}

const UTacticalHUDStyleData* UXRU1SubtitleSubsystem::ResolveTheme() const
{
	const UTacticsGameInstance* GameInstance = Cast<UTacticsGameInstance>(GetGameInstance());
	if (const UTacticalHUDStyleData* Theme = GameInstance ? GameInstance->GetUITheme() : nullptr)
	{
		return Theme;
	}
	// Тема не назначена — берём CDO: дефолты её полей И ЕСТЬ дефолты вида
	// субтитра. Так числа не дублируются в коде слоя (конвенция §3).
	return GetDefault<UTacticalHUDStyleData>();
}

bool UXRU1SubtitleSubsystem::IsGameplayAnchor() const
{
	const UGameUIManagerSubsystem* UIManager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
	const UPrimaryGameLayout* Layout = UIManager ? UIManager->GetRootLayout() : nullptr;
	return Layout && Layout->IsGameLayerVisible();
}

FXRU1SubtitleStyle UXRU1SubtitleSubsystem::GetResolvedStyle() const
{
	FXRU1SubtitleStyle Style;

	// ResolveTheme() никогда не отдаёт nullptr: без назначенного ассета
	// возвращается CDO с теми же дефолтами.
	if (const UTacticalHUDStyleData* Theme = ResolveTheme())
	{
		Style.LineFontSize = Theme->SubtitleFontSize;
		Style.SpeakerFontSize = Theme->SubtitleSpeakerFontSize;
		Style.HintFontSize = Theme->SubtitleHintFontSize;
		Style.LineColor = Theme->SubtitleTextColor;
		Style.SpeakerColor = Theme->SubtitleSpeakerColor;
		Style.HintColor = Theme->SubtitleHintColor;
		Style.BackdropColor = Theme->SubtitleBackdropColor;
		Style.BackdropPadding = Theme->SubtitleBackdropPadding;
		Style.WrapWidth = Theme->SubtitleWrapWidth;
		Style.bShowSkipHint = Theme->bSubtitleShowSkipHint;
		Style.BottomOffset = IsGameplayAnchor()
			? Theme->SubtitleGameplayBottomOffset : Theme->SubtitleCinematicBottomOffset;
	}

	const FTacticsSubtitleSettings User = GetUserSettings();
	Style.bShowSpeaker = User.bShowSpeakerNames;

	// Размер — множителем к дизайнерским кеглям: тема остаётся единственным
	// местом, где живут абсолютные значения.
	float SizeScale = 1.f;
	switch (User.TextSize)
	{
	case EXRU1SubtitleTextSize::Large:      SizeScale = 1.25f; break;
	case EXRU1SubtitleTextSize::ExtraLarge: SizeScale = 1.55f; break;
	default: break;
	}
	Style.LineFontSize = FMath::Max(8, FMath::RoundToInt(Style.LineFontSize * SizeScale));
	Style.SpeakerFontSize = FMath::Max(7, FMath::RoundToInt(Style.SpeakerFontSize * SizeScale));
	Style.HintFontSize = FMath::Max(7, FMath::RoundToInt(Style.HintFontSize * SizeScale));

	switch (User.Backdrop)
	{
	case EXRU1SubtitleBackdrop::None:
		Style.BackdropColor.A = 0.f;
		break;
	case EXRU1SubtitleBackdrop::Solid:
		Style.BackdropColor.A = 1.f;
		break;
	default:
		break; // Soft — как задано темой
	}

	return Style;
}

UWorld* UXRU1SubtitleSubsystem::GetTimerWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}
