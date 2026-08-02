#include "MenuWidgets.h"
#include "TacticsAudioSubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticsSaveGame.h"
#include "TacticsUserSettings.h"
#include "GamePauseSubsystem.h"
#include "GameFramework/GameUserSettings.h"
#include "XRU1Log.h"
#include "TacticalHUDStyleData.h"
#include "GameUIManagerSubsystem.h"
#include "PrimaryGameLayout.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaSource.h"
#include "Engine/World.h"
#include "TimerManager.h"

// --- UMenuScreenBase --------------------------------------------------------

void UMenuScreenBase::RequestBack()
{
	OnBackRequested.Broadcast();
	// Канон CommonUI: деактивация снимает виджет со стека, предыдущий экран
	// активируется автоматически.
	DeactivateWidget();
}

UPrimaryGameLayout* UMenuScreenBase::GetRootLayout() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	UGameUIManagerSubsystem* UIManager = GameInstance
		? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
	return UIManager ? UIManager->GetRootLayout() : nullptr;
}

UCommonActivatableWidget* UMenuScreenBase::PushScreen(TSubclassOf<UCommonActivatableWidget> ScreenClass)
{
	if (!ScreenClass)
	{
		return nullptr;
	}

	UPrimaryGameLayout* RootLayout = GetRootLayout();
	if (!RootLayout)
	{
		return nullptr;
	}

	return RootLayout->PushWidgetToLayer(EUILayer::Menu, ScreenClass);
}

UTacticsGameInstance* UMenuScreenBase::GetTacticsGameInstance() const
{
	return GetGameInstance<UTacticsGameInstance>();
}

UTacticalHUDStyleData* UMenuScreenBase::GetUITheme() const
{
	const UTacticsGameInstance* GameInstance = GetTacticsGameInstance();
	return GameInstance ? GameInstance->GetUITheme() : nullptr;
}

UTacticsAudioSubsystem* UMenuScreenBase::GetAudioSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UTacticsAudioSubsystem>() : nullptr;
}

void UMenuScreenBase::RegisterButtonSounds(UButton* Button)
{
	if (!Button)
	{
		return;
	}
	Button->OnHovered.AddUniqueDynamic(this, &UMenuScreenBase::HandleButtonHovered);
	Button->OnClicked.AddUniqueDynamic(this, &UMenuScreenBase::HandleButtonClicked);
}

void UMenuScreenBase::NativeConstruct()
{
	Super::NativeConstruct();

	// Пауза держится, пока экран СУЩЕСТВУЕТ в стеке, а не пока он активен.
	// При открытии настроек поверх паузы CommonUI деактивирует нижний экран,
	// и привязка к активации давала «окно без паузы» на кадр между
	// деактивацией паузы и активацией настроек — игра успевала тикнуть.
	if (!bPauseGameWhileActive)
	{
		return;
	}
	if (UGamePauseSubsystem* Pause = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UGamePauseSubsystem>() : nullptr)
	{
		if (PauseReasonId.IsNone())
		{
			PauseReasonId = FName(*FString::Printf(TEXT("Menu.%s"), *GetName()));
		}
		UE_LOG(LogXRU1UI, Display, TEXT("[Menu] экран '%s' открыт — держит паузу"), *GetName());
		Pause->PushPauseReason(PauseReasonId);
	}
}

void UMenuScreenBase::NativeOnActivated()
{
	Super::NativeOnActivated();
	ApplyScreenArt();
}

void UMenuScreenBase::ApplyScreenArt()
{
	// Собственный фон экрана больше не используется — прячем, чтобы картинка не
	// шла в два слоя (в рукотворных экранах он остался с прежней схемы).
	if (WidgetTree)
	{
		static const TCHAR* BackgroundNames[] = {
			TEXT("Img_Background"), TEXT("PreviewBackground"), TEXT("Background"), TEXT("Img_Bg")
		};
		for (const TCHAR* Name : BackgroundNames)
		{
			if (UImage* Local = Cast<UImage>(WidgetTree->FindWidget(FName(Name))))
			{
				Local->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	UPrimaryGameLayout* RootLayout = GetRootLayout();
	if (!RootLayout)
	{
		return; // Designer preview или экран вне лейаута — просто без фона
	}

	// Полноэкранный экран закрывает игровой слой: HUD под ним всё равно не
	// используется, а видно его быть не должно.
	RootLayout->SetGameLayerHidden(this, bHidesGameLayer);

	const UTacticalHUDStyleData* Theme = GetUITheme();
	if (ScreenArtKind == EXRU1UIScreenArt::None || !Theme)
	{
		RootLayout->SetScreenBackdrop(this, nullptr, FLinearColor::White);
		return;
	}

	const FXRU1UIScreenArtwork Artwork = Theme->GetScreenArtwork(ScreenArtKind);
	RootLayout->SetScreenBackdrop(this, Artwork.Texture.LoadSynchronous(), Artwork.Tint);
}

void UMenuScreenBase::NativeOnDeactivated()
{
	// Здесь НИЧЕГО не снимаем — ни паузу, ни фон.
	//
	// Пауза: экран мог просто уйти под другой экран стека.
	// Фон: `SCommonAnimatedSwitcher` сначала полностью убирает уходящий экран и
	// только потом активирует следующий («the next widget is not activated until
	// the previous widget has transitioned fully out of view»). Снятие фона на
	// деактивации попадало ровно в этот промежуток — экран проваливался в
	// чёрное и лишь потом получал фон нового экрана. Снимаем в NativeDestruct,
	// когда экран действительно ушёл со стека.
	Super::NativeOnDeactivated();
}

void UMenuScreenBase::NativeDestruct()
{
	// Единственная точка снятия: экран уходит со стека (закрыт, travel, смена
	// уровня). Ни пауза, ни фон, ни спрятанный HUD не должны пережить свой экран.
	ReleasePauseHold();
	if (UPrimaryGameLayout* RootLayout = GetRootLayout())
	{
		RootLayout->ClearScreenBackdrop(this);
		RootLayout->SetGameLayerHidden(this, false);
	}
	Super::NativeDestruct();
}

void UMenuScreenBase::ReleasePauseHold()
{
	if (PauseReasonId.IsNone())
	{
		return;
	}
	if (UGamePauseSubsystem* Pause = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UGamePauseSubsystem>() : nullptr)
	{
		UE_LOG(LogXRU1UI, Display, TEXT("[Menu] экран '%s' закрыт — отпускает паузу"), *GetName());
		Pause->PopPauseReason(PauseReasonId);
	}
	// Имя очищается: следующий показ этого же экрана возьмёт причину заново,
	// а «висящей» причины от уничтоженного виджета не останется.
	PauseReasonId = NAME_None;
}

void UMenuScreenBase::HandleBackClicked()
{
	RequestBack();
}

void UMenuScreenBase::HandleButtonHovered()
{
	if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->PlayUIHover();
	}
}

void UMenuScreenBase::HandleButtonClicked()
{
	if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->PlayUIClick();
	}
}

// --- UMainMenuWidget --------------------------------------------------------

void UMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ScreenArtKind = EXRU1UIScreenArt::MainMenu;

	// Авто-биндинг: вёрстке достаточно каноничных имён, граф WBP пуст.
	if (Btn_Continue) { Btn_Continue->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleContinueClicked); RegisterButtonSounds(Btn_Continue); }
	if (Btn_NewGame)  { Btn_NewGame->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleNewGameClicked);   RegisterButtonSounds(Btn_NewGame); }
	if (Btn_Settings) { Btn_Settings->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleSettingsClicked); RegisterButtonSounds(Btn_Settings); }
	if (Btn_About)    { Btn_About->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleAboutClicked);       RegisterButtonSounds(Btn_About); }
	if (Btn_Quit)     { Btn_Quit->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleQuitClicked);         RegisterButtonSounds(Btn_Quit); }
}

void UMainMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// «Продолжить» активна только при существующем сохранении; перечитывается
	// при каждом возврате на экран (кампания могла появиться за это время).
	if (Btn_Continue)
	{
		Btn_Continue->SetIsEnabled(CanContinue());
	}

	// Музыка меню — отсюда, а не из GameMode: GM_MainMenu собран в Blueprint от
	// движкового класса, а этот экран точно существует в каждом заходе в меню.
	// Повторная активация (возврат из настроек) трек не перезапускает.
	if (UTacticsAudioSubsystem* Audio = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UTacticsAudioSubsystem>() : nullptr)
	{
		Audio->PlayMenuMusic();
	}
}

void UMainMenuWidget::HandleContinueClicked() { RequestContinue(); }
void UMainMenuWidget::HandleNewGameClicked()  { RequestNewGame(); }
void UMainMenuWidget::HandleSettingsClicked() { RequestSettings(); }
void UMainMenuWidget::HandleAboutClicked()    { RequestAbout(); }
void UMainMenuWidget::HandleQuitClicked()     { RequestQuit(); }

bool UMainMenuWidget::CanContinue() const
{
	if (const UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		return GI->HasSaveGame();
	}
	return false;
}

void UMainMenuWidget::RequestContinue()
{
	OnContinueClicked.Broadcast();

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		if (GI->LoadCampaign())
		{
			GI->TravelToHub();
		}
	}
}

void UMainMenuWidget::RequestNewGame()
{
	OnNewGameClicked.Broadcast();
	PushScreen(DifficultyScreenClass);
}

void UMainMenuWidget::RequestSettings()
{
	OnSettingsClicked.Broadcast();
	PushScreen(SettingsScreenClass);
}

void UMainMenuWidget::RequestAbout()
{
	OnAboutClicked.Broadcast();
	PushScreen(AboutScreenClass);
}

void UMainMenuWidget::RequestQuit()
{
	OnQuitClicked.Broadcast();
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

// --- UIntroPlayerWidget -----------------------------------------------------

void UIntroPlayerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ScreenArtKind = EXRU1UIScreenArt::IntroFallback;

	// Пауза остановила бы и само видео: интро — единственный экран, который
	// обязан жить своим временем.
	bPauseGameWhileActive = false;

	// Пропуск — по УДЕРЖАНИЮ, поэтому кнопка слушает Pressed/Released, а не
	// Clicked. Звук клика ей тоже не вешаем: короткий щелчок здесь ничего не
	// делает, и «клик прозвучал, но ничего не произошло» читается как баг.
	if (Btn_Skip)
	{
		Btn_Skip->OnPressed.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleSkipPressed);
		Btn_Skip->OnReleased.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleSkipReleased);
	}

	// Без фокуса клавиатуры Space/Enter до виджета не доходят.
	SetIsFocusable(true);

	ResetSkipHoldUI();
}

bool UIntroPlayerWidget::IsSkipKey(const FKey& Key)
{
	// Только клавиатура: геймпада у проекта нет, а EKeys::Virtual_Accept в 5.7
	// помечен устаревшим и роняет сборку с -WarningsAsErrors на следующей версии.
	return Key == EKeys::SpaceBar || Key == EKeys::Enter;
}

FReply UIntroPlayerWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsSkipKey(InKeyEvent.GetKey()))
	{
		// IsRepeat: автоповтор клавиши не должен перезапускать отсчёт с нуля.
		if (!InKeyEvent.IsRepeat())
		{
			HandleSkipPressed();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UIntroPlayerWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsSkipKey(InKeyEvent.GetKey()))
	{
		HandleSkipReleased();
		return FReply::Handled();
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

void UIntroPlayerWidget::HandleSkipPressed()
{
	UWorld* World = GetWorld();
	if (!World || bIntroFinished || SkipHoldStartTime >= 0.0)
	{
		return;
	}

	SkipHoldStartTime = World->GetTimeSeconds();
	if (Bar_SkipHold)
	{
		Bar_SkipHold->SetPercent(0.f);
		Bar_SkipHold->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (Txt_SkipHint)
	{
		Txt_SkipHint->SetText(NSLOCTEXT("XRU1.Menu", "SkipHolding", "Отпустите, чтобы продолжить просмотр"));
	}
	// Прогресс обновляется таймером: у виджета TickFrequency по умолчанию Auto,
	// и чисто нативный NativeTick без BP-графа может не вызываться вовсе.
	World->GetTimerManager().SetTimer(SkipHoldTimer, FTimerDelegate::CreateUObject(
		this, &UIntroPlayerWidget::TickSkipHold), 0.03f, /*bLoop=*/true);
}

void UIntroPlayerWidget::TickSkipHold()
{
	const UWorld* World = GetWorld();
	if (!World || SkipHoldStartTime < 0.0)
	{
		return;
	}
	const float Held = static_cast<float>(World->GetTimeSeconds() - SkipHoldStartTime);
	const float Ratio = SkipHoldDuration > 0.f ? FMath::Clamp(Held / SkipHoldDuration, 0.f, 1.f) : 1.f;
	if (Bar_SkipHold)
	{
		Bar_SkipHold->SetPercent(Ratio);
	}
	if (Ratio >= 1.f)
	{
		CompleteSkipHold();
	}
}

void UIntroPlayerWidget::CompleteSkipHold()
{
	UE_LOG(LogXRU1UI, Display, TEXT("[Intro] пропуск удержанием (%.2f с)"), SkipHoldDuration);
	ResetSkipHoldUI();
	FinishIntro();
}

void UIntroPlayerWidget::HandleSkipReleased()
{
	if (SkipHoldStartTime < 0.0)
	{
		return;
	}
	ResetSkipHoldUI();
}

void UIntroPlayerWidget::ResetSkipHoldUI()
{
	SkipHoldStartTime = -1.0;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SkipHoldTimer);
	}
	if (Bar_SkipHold)
	{
		Bar_SkipHold->SetPercent(0.f);
		Bar_SkipHold->SetVisibility(ESlateVisibility::Hidden);
	}
	if (Txt_SkipHint)
	{
		Txt_SkipHint->SetText(NSLOCTEXT("XRU1.Menu", "SkipHint",
			"Удерживайте ЛКМ, ПРОБЕЛ или ENTER, чтобы пропустить"));
	}
}

void UIntroPlayerWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// Фокус берём при каждом показе: экран мог открыться поверх меню, у
	// которого фокус остался на кнопке «Новая игра».
	SetKeyboardFocus();
	ResetSkipHoldUI();

	const UTacticalHUDStyleData* Theme = GetUITheme();
	if (!Theme)
	{
		return;
	}

	// Материал с MediaTexture — единственный способ показать видео в UMG:
	// Image рисует кадры плеера только через него.
	UMaterialInterface* VideoMaterial = Theme->IntroVideoMaterial.LoadSynchronous();
	UMediaSource* Source = Theme->IntroMediaSource.LoadSynchronous();
	IntroPlayer = Theme->IntroMediaPlayer.LoadSynchronous();

	// Чёрный экран интро может означать четыре разные поломки; без этой строки
	// они неотличимы друг от друга.
	UE_LOG(LogXRU1UI, Display,
		TEXT("[Intro] цепочка: Img_Intro=%s материал='%s' плеер='%s' источник='%s'"),
		Img_Intro ? TEXT("есть") : TEXT("НЕТ (в вёрстке нет виджета Img_Intro)"),
		*GetNameSafe(VideoMaterial), *GetNameSafe(IntroPlayer), *GetNameSafe(Source));

	if (Img_Intro && VideoMaterial && IntroPlayer && Source)
	{
		Img_Intro->SetBrushFromMaterial(VideoMaterial);
		Img_Intro->SetVisibility(ESlateVisibility::HitTestInvisible);

		IntroPlayer->OnMediaOpened.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleMediaOpened);
		IntroPlayer->OnEndReached.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleMediaEndReached);
		IntroPlayer->OnMediaOpenFailed.AddUniqueDynamic(this, &UIntroPlayerWidget::HandleMediaOpenFailed);
		IntroPlayer->SetLooping(false);

		const FString SourceUrl = Source->GetUrl();
		const bool bCanPlay = Source->Validate();
		if (!IntroPlayer->OpenSource(Source))
		{
			// OpenSource false — отказ ДО открытия файла (нет плагина плеера,
			// пустой/некорректный URL). OnMediaOpenFailed приходит позже и
			// означает уже другое: файл найден, но не читается.
			UE_LOG(LogXRU1UI, Error,
				TEXT("[Intro] OpenSource отказал сразу: url='%s' валиден=%d — проверь путь файла и плагины Media"),
				*SourceUrl, bCanPlay ? 1 : 0);
			HandleMediaOpenFailed(SourceUrl);
			return;
		}
		UE_LOG(LogXRU1UI, Display, TEXT("[Intro] OpenSource принят: url='%s' валиден=%d"),
			*SourceUrl, bCanPlay ? 1 : 0);

		// Музыка меню обязана уйти: играть заставку под меню-трек — это не
		// «слоями», а «две дорожки разом».
		if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
		{
			Audio->StopMusic();
		}
		CreateIntroSound();
		// Этот Play() почти всегда холостой: OpenSource открывает файл АСИНХРОННО,
		// и играть ещё нечего. Настоящий запуск — в HandleMediaOpened. Оставлен
		// на случай уже открытого плеера (повторный показ экрана).
		IntroPlayer->Play();
	}
	else
	{
		// Ролика нет — показываем статичный арт, экран остаётся проходимым.
		if (Img_Intro)
		{
			const FXRU1UIScreenArtwork Artwork = Theme->GetScreenArtwork(EXRU1UIScreenArt::IntroFallback);
			if (UTexture2D* Texture = Artwork.Texture.LoadSynchronous())
			{
				Img_Intro->SetBrushFromTexture(Texture);
				Img_Intro->SetColorAndOpacity(Artwork.Tint);
			}
		}
		UE_LOG(LogXRU1UI, Display,
			TEXT("[Intro] Видео не настроено (Media Player/Material/Source в теме) — показан статичный экран"));
	}

	// Страховка: без неё сбой воспроизведения оставил бы игрока на чёрном экране.
	if (MaxIntroDuration > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(IntroTimeoutTimer, this,
				&UIntroPlayerWidget::HandleMediaEndReached, MaxIntroDuration, /*bLoop=*/false);
		}
	}
}

void UIntroPlayerWidget::HandleMediaOpened(FString OpenedUrl)
{
	// Файл открыт — дальше «чёрный экран» может быть только на стороне
	// материала/текстуры, и в лог попадает именно это разграничение.
	const int32 VideoTracks = IntroPlayer ? IntroPlayer->GetNumTracks(EMediaPlayerTrack::Video) : 0;
	const FTimespan Duration = IntroPlayer ? IntroPlayer->GetDuration() : FTimespan::Zero();
	UE_LOG(LogXRU1UI, Display,
		TEXT("[Intro] медиа открыто: url='%s' видео-дорожек=%d длительность=%.1f с играет=%d. ")
		TEXT("Если экран чёрный при видео-дорожках > 0 — виноват материал (MediaTexture в Final Color, домен UI)"),
		*OpenedUrl, VideoTracks, Duration.GetTotalSeconds(),
		IntroPlayer && IntroPlayer->IsPlaying() ? 1 : 0);

	if (VideoTracks <= 0)
	{
		UE_LOG(LogXRU1UI, Warning,
			TEXT("[Intro] в открытом медиа нет видео-дорожек — кодек файла не поддержан плеером"));
	}

	// ⚠️ Запуск ровно здесь, а не сразу после OpenSource. Открытие медиа
	// асинхронное: Play() до готовности плеера возвращает true и не делает
	// ничего. Из-за этого интро показывало чёрный экран при полностью верной
	// цепочке ассетов — ролик был открыт (ready=1, ошибок нет), но rate=0.
	// На флаг PlayOnOpen самого MediaPlayer не полагаемся: он выключен по
	// умолчанию и его легко потерять при пересоздании ассета.
	if (IntroPlayer && !IntroPlayer->IsPlaying())
	{
		const bool bStarted = IntroPlayer->Play();
		UE_LOG(LogXRU1UI, Display, TEXT("[Intro] запуск воспроизведения: %s"),
			bStarted ? TEXT("принят") : TEXT("ОТКАЗ"));
	}
}

void UIntroPlayerWidget::HandleMediaEndReached()
{
	UE_LOG(LogXRU1UI, Display, TEXT("[Intro] ролик доигран до конца"));
	FinishIntro();
}

void UIntroPlayerWidget::HandleMediaOpenFailed(FString FailedUrl)
{
	// Не держим игрока на пустом экране из-за отсутствующего файла/кодека.
	UE_LOG(LogXRU1UI, Error, TEXT("[Intro] медиа НЕ открылось: url='%s' — ухожу в хаб"), *FailedUrl);
	FinishIntro();
}

void UIntroPlayerWidget::CreateIntroSound()
{
	if (IntroSound || !IntroPlayer)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayer();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}

	// MediaPlayer отдаёт только КАДРЫ. Звуковую дорожку забирает отдельный
	// UMediaSoundComponent — без него ролик молчит, а причина «немого интро»
	// нигде не видна: ошибок нет, воспроизведение идёт.
	IntroSound = NewObject<UMediaSoundComponent>(PC, TEXT("IntroMediaSound"));
	IntroSound->SetMediaPlayer(IntroPlayer);
	// Категория «голос»: в ролике закадровый текст, и им должен управлять
	// соответствующий ползунок, а не музыкальный.
	if (const UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
	{
		if (const UTacticsAudioSettingsDataAsset* Asset = Audio->GetAudioSettingsAsset())
		{
			IntroSound->SoundClass = Asset->VoiceClass;
		}
	}
	IntroSound->RegisterComponentWithWorld(World);
	IntroSound->Start();

	UE_LOG(LogXRU1UI, Display, TEXT("[Intro] звуковая дорожка подключена (SoundClass '%s')"),
		*GetNameSafe(IntroSound->SoundClass));
}

void UIntroPlayerWidget::StopIntroPlayback()
{
	if (IntroSound)
	{
		IntroSound->Stop();
		IntroSound->DestroyComponent();
		IntroSound = nullptr;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IntroTimeoutTimer);
		World->GetTimerManager().ClearTimer(SkipHoldTimer);
	}
	if (IntroPlayer)
	{
		IntroPlayer->OnMediaOpened.RemoveDynamic(this, &UIntroPlayerWidget::HandleMediaOpened);
		IntroPlayer->OnEndReached.RemoveDynamic(this, &UIntroPlayerWidget::HandleMediaEndReached);
		IntroPlayer->OnMediaOpenFailed.RemoveDynamic(this, &UIntroPlayerWidget::HandleMediaOpenFailed);
		IntroPlayer->Close();
		IntroPlayer = nullptr;
	}
}

void UIntroPlayerWidget::NativeDestruct()
{
	StopIntroPlayback();
	Super::NativeDestruct();
}

void UIntroPlayerWidget::FinishIntro()
{
	// Кнопка «Пропустить», конец ролика и таймаут могут прийти почти одновременно —
	// travel должен произойти ровно один раз.
	if (bIntroFinished)
	{
		return;
	}
	bIntroFinished = true;
	StopIntroPlayback();

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->TravelToHub();
	}
}

// --- UDifficultySelectWidget ------------------------------------------------

void UDifficultySelectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ScreenArtKind = EXRU1UIScreenArt::Difficulty;

	if (Btn_Easy)   { Btn_Easy->OnClicked.AddUniqueDynamic(this, &UDifficultySelectWidget::HandleEasyClicked);     RegisterButtonSounds(Btn_Easy); }
	if (Btn_Medium) { Btn_Medium->OnClicked.AddUniqueDynamic(this, &UDifficultySelectWidget::HandleMediumClicked); RegisterButtonSounds(Btn_Medium); }
	if (Btn_Hard)   { Btn_Hard->OnClicked.AddUniqueDynamic(this, &UDifficultySelectWidget::HandleHardClicked);     RegisterButtonSounds(Btn_Hard); }
	if (Btn_Back)   { Btn_Back->OnClicked.AddUniqueDynamic(this, &UDifficultySelectWidget::HandleBackClicked);     RegisterButtonSounds(Btn_Back); }
}

void UDifficultySelectWidget::HandleEasyClicked()   { ChooseDifficulty(EDifficultyLevel::Easy); }
void UDifficultySelectWidget::HandleMediumClicked() { ChooseDifficulty(EDifficultyLevel::Medium); }
void UDifficultySelectWidget::HandleHardClicked()   { ChooseDifficulty(EDifficultyLevel::Hard); }

void UDifficultySelectWidget::ChooseDifficulty(EDifficultyLevel Difficulty)
{
	OnDifficultyChosen.Broadcast(Difficulty);

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->StartNewCampaign(Difficulty);
		if (IntroScreenClass)
		{
			PushScreen(IntroScreenClass);
		}
		else
		{
			GI->TravelToHub();
		}
	}
}

// --- UAboutMenuWidget -------------------------------------------------------

void UAboutMenuWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// Тексты из Class Defaults видны и в Designer-превью.
	if (Txt_Author)
	{
		Txt_Author->SetText(AuthorName);
	}
	if (Txt_ProjectInfo)
	{
		Txt_ProjectInfo->SetText(ProjectInfo);
	}
}

void UAboutMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ScreenArtKind = EXRU1UIScreenArt::About;
	if (Btn_Back)
	{
		Btn_Back->OnClicked.AddUniqueDynamic(this, &UAboutMenuWidget::HandleBackClicked);
		RegisterButtonSounds(Btn_Back);
	}
}

// --- UPauseMenuWidget -------------------------------------------------------

void UPauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ScreenArtKind = EXRU1UIScreenArt::Pause;

	bPauseGameWhileActive = true;

	if (Btn_Resume)       { Btn_Resume->OnClicked.AddUniqueDynamic(this, &UPauseMenuWidget::HandleResumeClicked);             RegisterButtonSounds(Btn_Resume); }
	if (Btn_Settings)     { Btn_Settings->OnClicked.AddUniqueDynamic(this, &UPauseMenuWidget::HandleSettingsClicked);         RegisterButtonSounds(Btn_Settings); }
	if (Btn_ReturnToMenu) { Btn_ReturnToMenu->OnClicked.AddUniqueDynamic(this, &UPauseMenuWidget::HandleReturnToMenuClicked); RegisterButtonSounds(Btn_ReturnToMenu); }
}

void UPauseMenuWidget::HandleResumeClicked() { RequestResume(); }

void UPauseMenuWidget::HandleSettingsClicked()
{
	PushScreen(SettingsScreenClass);
}

void UPauseMenuWidget::HandleReturnToMenuClicked() { RequestReturnToMenu(); }

void UPauseMenuWidget::RequestResume()
{
	OnResumeClicked.Broadcast();
	// Паузу снимет деактивация экрана (NativeOnDeactivated → PopPauseReason):
	// прямой SetGamePaused здесь снял бы и чужие причины, например «нет фокуса».
	DeactivateWidget();
}

void UPauseMenuWidget::RequestReturnToMenu()
{
	OnReturnToMenuClicked.Broadcast();
	// Уход с уровня: снимаем ВСЕ причины, иначе пауза, взятая другим экраном
	// или потерей фокуса, переживёт смену мира и заморозит меню.
	if (UGamePauseSubsystem* Pause = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UGamePauseSubsystem>() : nullptr)
	{
		Pause->ClearAllPauseReasons();
	}

	if (UTacticsGameInstance* GI = GetTacticsGameInstance())
	{
		GI->TravelToMainMenu();
	}
}

// --- USettingsMenuWidget ----------------------------------------------------

void USettingsMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ScreenArtKind = EXRU1UIScreenArt::Settings;

	// Настройки открываются и поверх боя, и поверх хаба: пока экран открыт,
	// мир под ним стоит.
	bPauseGameWhileActive = true;

	// Пять слайдеров звука: один обработчик на всех — структура всё равно
	// собирается целиком из текущих значений (STATUS §2.3).
	USlider* const AudioSliders[] = { Sld_Master, Sld_Music, Sld_Sfx, Sld_UI, Sld_Voice };
	for (USlider* Slider : AudioSliders)
	{
		if (Slider)
		{
			Slider->OnValueChanged.AddUniqueDynamic(this, &USettingsMenuWidget::HandleAudioSliderValue);
			Slider->OnMouseCaptureEnd.AddUniqueDynamic(this, &USettingsMenuWidget::HandleAudioCaptureEnd);
		}
	}

	// Выпадающий список качества: опции создаются здесь, а не в Designer,
	// чтобы порядок 0..3 всегда совпадал с ScalabilityLevel.
	if (Cmb_Quality && Cmb_Quality->GetOptionCount() == 0)
	{
		Cmb_Quality->AddOption(TEXT("Низкое"));
		Cmb_Quality->AddOption(TEXT("Среднее"));
		Cmb_Quality->AddOption(TEXT("Высокое"));
		Cmb_Quality->AddOption(TEXT("Эпическое"));
	}

	// Камера: те же два события, что и у звука — «тащат» применяем сразу, «отпустил»
	// пишем на диск. Игрок подбирает обзор и чувствительность глазами, поэтому
	// откладывать применение до кнопки «Применить» нельзя.
	USlider* const CameraSliders[] = { Sld_CameraFov, Sld_CameraSensitivity, Sld_CameraPitchSensitivity };
	for (USlider* Slider : CameraSliders)
	{
		if (Slider)
		{
			Slider->OnValueChanged.AddUniqueDynamic(this, &USettingsMenuWidget::HandleCameraSliderValue);
			Slider->OnMouseCaptureEnd.AddUniqueDynamic(this, &USettingsMenuWidget::HandleCameraCaptureEnd);
		}
	}
	if (Chk_InvertPitch) { Chk_InvertPitch->OnCheckStateChanged.AddUniqueDynamic(this, &USettingsMenuWidget::HandleCameraCheckChanged); }
	if (Chk_EdgeScroll)  { Chk_EdgeScroll->OnCheckStateChanged.AddUniqueDynamic(this, &USettingsMenuWidget::HandleCameraCheckChanged); }

	if (Btn_Apply) { Btn_Apply->OnClicked.AddUniqueDynamic(this, &USettingsMenuWidget::HandleApplyClicked); RegisterButtonSounds(Btn_Apply); }
	if (Btn_Reset) { Btn_Reset->OnClicked.AddUniqueDynamic(this, &USettingsMenuWidget::HandleResetClicked); RegisterButtonSounds(Btn_Reset); }
	if (Btn_Back)  { Btn_Back->OnClicked.AddUniqueDynamic(this, &USettingsMenuWidget::HandleBackClicked);   RegisterButtonSounds(Btn_Back); }
}

void USettingsMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	RefreshControlsFromSettings();
}

void USettingsMenuWidget::RefreshControlsFromSettings()
{
	const FTacticsAudioSettings Audio = GetAudioSettings();
	{
		const FTacticsVideoSettings Video = GetVideoSettings();
		// «Настройки открылись не с теми значениями» проверяется только логом:
		// видно, что именно прочитано и из какого источника.
		UE_LOG(LogXRU1UI, Display,
			TEXT("[Settings] прочитано: Master=%.2f Music=%.2f Sfx=%.2f UI=%.2f Voice=%.2f | ")
			TEXT("Quality=%d Scale=%.2f Fullscreen=%d VSync=%d (изображение — из GameUserSettings)"),
			Audio.MasterVolume, Audio.MusicVolume, Audio.SfxVolume, Audio.UIVolume, Audio.VoiceVolume,
			Video.ScalabilityLevel, Video.ResolutionScale, Video.bFullscreen ? 1 : 0, Video.bVSync ? 1 : 0);
	}
	// USlider::SetValue ВЫЗЫВАЕТ OnValueChanged (в логе это видно как лишнее
	// «Громкости применены» сразу после чтения). Пока значения совпадают, вреда
	// нет, но обработчик не должен принимать программную установку за действие
	// игрока — иначе первое же усложнение обработчика станет багом.
	TGuardValue<bool> RefreshGuard(bUpdatingControls, true);

	if (Sld_Master) { Sld_Master->SetValue(Audio.MasterVolume); }
	if (Sld_Music)  { Sld_Music->SetValue(Audio.MusicVolume); }
	if (Sld_Sfx)    { Sld_Sfx->SetValue(Audio.SfxVolume); }
	if (Sld_UI)     { Sld_UI->SetValue(Audio.UIVolume); }
	if (Sld_Voice)  { Sld_Voice->SetValue(Audio.VoiceVolume); }

	const FTacticsVideoSettings Video = GetVideoSettings();
	if (Cmb_Quality)
	{
		Cmb_Quality->SetSelectedIndex(FMath::Clamp(Video.ScalabilityLevel, 0, 3));
	}
	if (Sld_ResolutionScale) { Sld_ResolutionScale->SetValue(Video.ResolutionScale); }
	if (Chk_Fullscreen)      { Chk_Fullscreen->SetIsChecked(Video.bFullscreen); }
	if (Chk_VSync)           { Chk_VSync->SetIsChecked(Video.bVSync); }

	// Камера: слайдеры работают в 0..1, реальные величины — на границах
	// CameraFovMin/Max и CameraSensitivityMin/Max (обратная операция — в Collect).
	const FTacticsCameraSettings CameraSettings = GetCameraSettings();
	if (Sld_CameraFov)
	{
		Sld_CameraFov->SetValue(FMath::GetMappedRangeValueClamped(
			FVector2f(CameraFovMin, CameraFovMax), FVector2f(0.f, 1.f), CameraSettings.FieldOfView));
	}
	if (Sld_CameraSensitivity)
	{
		Sld_CameraSensitivity->SetValue(FMath::GetMappedRangeValueClamped(
			FVector2f(CameraSensitivityMin, CameraSensitivityMax), FVector2f(0.f, 1.f),
			CameraSettings.RotationSensitivity));
	}
	if (Sld_CameraPitchSensitivity)
	{
		Sld_CameraPitchSensitivity->SetValue(FMath::GetMappedRangeValueClamped(
			FVector2f(CameraSensitivityMin, CameraSensitivityMax), FVector2f(0.f, 1.f),
			CameraSettings.PitchSensitivity));
	}
	if (Chk_InvertPitch) { Chk_InvertPitch->SetIsChecked(CameraSettings.bInvertPitch); }
	if (Chk_EdgeScroll)  { Chk_EdgeScroll->SetIsChecked(CameraSettings.bEdgeScroll); }
}

FTacticsAudioSettings USettingsMenuWidget::CollectAudioSettings() const
{
	// База — текущий слот: отсутствующий в вёрстке слайдер не занулит громкость.
	FTacticsAudioSettings Settings = GetAudioSettings();
	if (Sld_Master) { Settings.MasterVolume = Sld_Master->GetValue(); }
	if (Sld_Music)  { Settings.MusicVolume = Sld_Music->GetValue(); }
	if (Sld_Sfx)    { Settings.SfxVolume = Sld_Sfx->GetValue(); }
	if (Sld_UI)     { Settings.UIVolume = Sld_UI->GetValue(); }
	if (Sld_Voice)  { Settings.VoiceVolume = Sld_Voice->GetValue(); }
	return Settings;
}

FTacticsVideoSettings USettingsMenuWidget::CollectVideoSettings() const
{
	FTacticsVideoSettings Settings = GetVideoSettings();
	if (Cmb_Quality && Cmb_Quality->GetSelectedIndex() != INDEX_NONE)
	{
		Settings.ScalabilityLevel = Cmb_Quality->GetSelectedIndex();
	}
	if (Sld_ResolutionScale) { Settings.ResolutionScale = Sld_ResolutionScale->GetValue(); }
	if (Chk_Fullscreen)      { Settings.bFullscreen = Chk_Fullscreen->IsChecked(); }
	if (Chk_VSync)           { Settings.bVSync = Chk_VSync->IsChecked(); }
	return Settings;
}

void USettingsMenuWidget::HandleAudioSliderValue(float /*NewValue*/)
{
	if (bUpdatingControls)
	{
		return; // это мы сами расставили контролы, а не игрок двинул ползунок
	}
	// Во время перетаскивания результат слышен сразу, но на диск не пишем.
	ApplyAudioSettings(CollectAudioSettings(), /*bSaveToSlot=*/false);
}

void USettingsMenuWidget::HandleAudioCaptureEnd()
{
	if (bUpdatingControls)
	{
		return;
	}
	ApplyAudioSettings(CollectAudioSettings(), /*bSaveToSlot=*/true);
}

FTacticsCameraSettings USettingsMenuWidget::CollectCameraSettings() const
{
	// База — текущие настройки: контрол, которого нет в вёрстке, не должен
	// обнулять чужое значение (то же правило, что у звука и изображения).
	FTacticsCameraSettings Settings = GetCameraSettings();
	if (Sld_CameraFov)
	{
		Settings.FieldOfView = FMath::GetMappedRangeValueClamped(
			FVector2f(0.f, 1.f), FVector2f(CameraFovMin, CameraFovMax), Sld_CameraFov->GetValue());
	}
	if (Sld_CameraSensitivity)
	{
		Settings.RotationSensitivity = FMath::GetMappedRangeValueClamped(
			FVector2f(0.f, 1.f), FVector2f(CameraSensitivityMin, CameraSensitivityMax),
			Sld_CameraSensitivity->GetValue());
	}
	if (Sld_CameraPitchSensitivity)
	{
		Settings.PitchSensitivity = FMath::GetMappedRangeValueClamped(
			FVector2f(0.f, 1.f), FVector2f(CameraSensitivityMin, CameraSensitivityMax),
			Sld_CameraPitchSensitivity->GetValue());
	}
	if (Chk_InvertPitch) { Settings.bInvertPitch = Chk_InvertPitch->IsChecked(); }
	if (Chk_EdgeScroll)  { Settings.bEdgeScroll = Chk_EdgeScroll->IsChecked(); }
	return Settings;
}

void USettingsMenuWidget::HandleCameraSliderValue(float /*NewValue*/)
{
	if (bUpdatingControls)
	{
		return; // программная расстановка контролов, а не действие игрока
	}
	ApplyCameraSettings(CollectCameraSettings(), /*bSaveToSlot=*/false);
}

void USettingsMenuWidget::HandleCameraCaptureEnd()
{
	if (bUpdatingControls)
	{
		return;
	}
	ApplyCameraSettings(CollectCameraSettings(), /*bSaveToSlot=*/true);
}

void USettingsMenuWidget::HandleCameraCheckChanged(bool /*bIsChecked*/)
{
	if (bUpdatingControls)
	{
		return;
	}
	// Галочка — законченное действие, промежуточного состояния у неё нет:
	// применяем и сразу сохраняем.
	ApplyCameraSettings(CollectCameraSettings(), /*bSaveToSlot=*/true);
}

void USettingsMenuWidget::HandleApplyClicked()
{
	ApplyVideoSettings(CollectVideoSettings(), /*bSaveToSlot=*/true);
	ApplyCameraSettings(CollectCameraSettings(), /*bSaveToSlot=*/true);
}

void USettingsMenuWidget::HandleResetClicked()
{
	ResetToDefaults();
	RefreshControlsFromSettings();
}

FTacticsAudioSettings USettingsMenuWidget::GetAudioSettings() const
{
	// Единственный источник правды — UTacticsUserSettings (docs/09_UI_HUD §5.5).
	if (const UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		return UserSettings->GetAudioSettings();
	}
	return FTacticsAudioSettings();
}

FTacticsVideoSettings USettingsMenuWidget::GetVideoSettings() const
{
	if (const UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		return UserSettings->GetVideoSettings();
	}
	return FTacticsVideoSettings();
}

FTacticsCameraSettings USettingsMenuWidget::GetCameraSettings() const
{
	if (const UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		return UserSettings->GetCameraSettings();
	}
	return FTacticsCameraSettings();
}

void USettingsMenuWidget::ApplyCameraSettings(const FTacticsCameraSettings& NewSettings, bool bSaveToSlot)
{
	UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get();
	if (!UserSettings)
	{
		return;
	}

	// Запись + применение к живой камере одним вызовом (см. SetCameraSettings):
	// экран настроек открывается поверх боя, и обзор обязан меняться на глазах.
	UserSettings->SetCameraSettings(NewSettings, this);
	if (bSaveToSlot)
	{
		UserSettings->SaveSettings();
		UE_LOG(LogXRU1UI, Display,
			TEXT("[Settings] камера: обзор=%.0f° чувствительность=%.2f/%.2f инверсия=%d край=%d"),
			NewSettings.FieldOfView, NewSettings.RotationSensitivity, NewSettings.PitchSensitivity,
			NewSettings.bInvertPitch ? 1 : 0, NewSettings.bEdgeScroll ? 1 : 0);
	}
}

void USettingsMenuWidget::ApplyAudioSettings(const FTacticsAudioSettings& NewSettings, bool bSaveToSlot)
{
	// Сначала применяем: игрок должен слышать результат прямо во время
	// перетаскивания ползунка, а не после нажатия «Сохранить».
	if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->ApplyAudioSettings(NewSettings);
	}

	// Значение живёт в настройках приложения; на диск пишем по отпусканию
	// ползунка (bSaveToSlot), а не на каждый кадр перетаскивания.
	if (UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		UserSettings->SetAudioSettings(NewSettings);
		if (bSaveToSlot)
		{
			UserSettings->SaveSettings();
		}
	}
}

void USettingsMenuWidget::ApplyVideoSettings(const FTacticsVideoSettings& NewSettings, bool /*bSaveToSlot*/)
{
	UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get();
	if (!UserSettings)
	{
		UE_LOG(LogXRU1UI, Warning,
			TEXT("[Settings] UTacticsUserSettings недоступны — проверь GameUserSettingsClassName в DefaultEngine.ini"));
		return;
	}

	UserSettings->SetVideoSettings(NewSettings);
	// ApplySettings сам пишет GameUserSettings.ini, поэтому отдельного
	// сохранения (и параметра bSaveToSlot) здесь больше нет.
	// bCheckForCommandLineOverrides=false: параметры запуска не должны молча
	// отменять осознанный выбор игрока в меню.
	UserSettings->ApplySettings(/*bCheckForCommandLineOverrides=*/false);
}

void USettingsMenuWidget::ResetToDefaults()
{
	if (UTacticsUserSettings* UserSettings = UTacticsUserSettings::Get())
	{
		// Дефолты звука задаёт дизайнер в DA_TacticsAudio, а не константы кода.
		UserSettings->ResetToProjectDefaults(this);
		UserSettings->ApplySettings(/*bCheckForCommandLineOverrides=*/false);
		if (UTacticsAudioSubsystem* Audio = GetAudioSubsystem())
		{
			Audio->ApplyAudioSettings(UserSettings->GetAudioSettings());
		}
	}
}
