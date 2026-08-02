#include "GamePauseSubsystem.h"

#include "TacticsAudioSubsystem.h"
#include "XRU1Log.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"

namespace XRU1PauseReasons
{
	const FName WindowFocus(TEXT("WindowFocus"));
}

namespace
{
	/**
	 * Пауза при потере фокуса окна. Отдельный переключатель нужен для отладки:
	 * при работе с редактором/скриптами фокус уходит постоянно.
	 */
	static TAutoConsoleVariable<int32> CVarPauseOnFocusLoss(
		TEXT("xru1.PauseOnFocusLoss"),
		1,
		TEXT("1 — ставить игру на паузу, когда окно теряет фокус (по умолчанию); 0 — не ставить."),
		ECVF_Default);
}

bool UGamePauseSubsystem::OwnsGameWorld() const
{
	// В редакторе живут ДВА GameInstance (редакторский и PIE), и делегат
	// активации приходит обоим. Реагировать должен только тот, у кого есть
	// игровой мир: иначе в логе появлялось «МИР ОСТАНОВЛЕН | world=<нет>»,
	// а состояние паузы расходилось с реальностью.
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return World && World->IsGameWorld();
}

void UGamePauseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Единственная точка, которая знает про фокус окна: раскидывать эту подписку
	// по контроллерам — значит получить разное поведение в бою и в хабе.
	if (FSlateApplication::IsInitialized())
	{
		ActivationChangedHandle = FSlateApplication::Get().OnApplicationActivationStateChanged()
			.AddUObject(this, &UGamePauseSubsystem::HandleApplicationActivationChanged);
	}
}

void UGamePauseSubsystem::Deinitialize()
{
	if (ActivationChangedHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationChangedHandle);
	}
	ActivationChangedHandle.Reset();

	ActiveReasons.Reset();
	Super::Deinitialize();
}

void UGamePauseSubsystem::HandleApplicationActivationChanged(bool bIsActive)
{
	// Только фиксируем намерение: применит его Tick после паузы устаканивания.
	if (PendingFocusState != bIsActive)
	{
		PendingFocusState = bIsActive;
		PendingFocusTimer = 0.f;
	}
}

void UGamePauseSubsystem::SetWindowFocused(bool bFocused)
{
	if (bWindowFocused == bFocused)
	{
		return;
	}
	bWindowFocused = bFocused;

	if (bFocused)
	{
		bEverFocused = true;
	}
	else if (!bEverFocused)
	{
		// Старт PIE: окно игры создаётся ненадолго БЕЗ фокуса, и наивная реакция
		// вешала паузу на только что запущенный уровень — звук глушился, ввод
		// хаба блокировался, карта «не крутилась». Пауза по фокусу имеет смысл
		// только после того, как окно хоть раз было активным.
		UE_LOG(LogXRU1UI, Display,
			TEXT("[Pause] окно ещё ни разу не было в фокусе — пауза по фокусу пропущена"));
		return;
	}

	if (CVarPauseOnFocusLoss.GetValueOnGameThread() == 0)
	{
		return;
	}

	UE_LOG(LogXRU1UI, Display, TEXT("[Pause] фокус окна: %s"),
		bFocused ? TEXT("получен") : TEXT("потерян"));

	// СНЯТИЕ выполняется всегда, а постановка — только при своём игровом мире.
	// Асимметрия намеренная: причина, поставленная в прошлом мире, обязана
	// сниматься в любом случае, иначе она «залипает» и замораживает игру.
	if (bFocused)
	{
		PopPauseReason(XRU1PauseReasons::WindowFocus);
	}
	else if (OwnsGameWorld())
	{
		PushPauseReason(XRU1PauseReasons::WindowFocus);
	}
}

void UGamePauseSubsystem::Tick(float DeltaTime)
{
	// Дебаунс: делегат активации приходит парами «получен→потерян» при
	// переключении окон, и без задержки это давало мигание паузы и звука.
	// Реагируем только на состояние, продержавшееся FocusSettleTime.
	if (PendingFocusState == bWindowFocused)
	{
		return;
	}
	PendingFocusTimer += DeltaTime;
	if (PendingFocusTimer < FocusSettleTime)
	{
		return;
	}
	PendingFocusTimer = 0.f;
	SetWindowFocused(PendingFocusState);
}

TStatId UGamePauseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGamePauseSubsystem, STATGROUP_Tickables);
}

FString UGamePauseSubsystem::DescribeReasons() const
{
	if (ActiveReasons.IsEmpty())
	{
		return TEXT("<нет>");
	}
	TArray<FString> Names;
	for (const FName& Name : ActiveReasons)
	{
		Names.Add(Name.ToString());
	}
	Names.Sort();
	return FString::Join(Names, TEXT(", "));
}

void UGamePauseSubsystem::PushPauseReason(FName InReason)
{
	if (InReason.IsNone())
	{
		return;
	}
	bool bAlready = false;
	ActiveReasons.Add(InReason, &bAlready);

	UE_LOG(LogXRU1UI, Display, TEXT("[Pause] + '%s'%s | причины: %s"),
		*InReason.ToString(), bAlready ? TEXT(" (уже была)") : TEXT(""), *DescribeReasons());

	if (!bAlready)
	{
		ApplyPauseState();
	}
}

void UGamePauseSubsystem::PopPauseReason(FName InReason)
{
	const bool bRemoved = ActiveReasons.Remove(InReason) > 0;

	if (bRemoved)
	{
		UE_LOG(LogXRU1UI, Display, TEXT("[Pause] - '%s' | причины: %s"),
			*InReason.ToString(), *DescribeReasons());
		ApplyPauseState();
	}
	else
	{
		// Штатная ситуация: экран закрылся после ClearAllPauseReasons (travel).
		// В Display это выглядело как ошибка, хотя снятие идемпотентно.
		UE_LOG(LogXRU1UI, Verbose, TEXT("[Pause] - '%s' (уже снята)"), *InReason.ToString());
	}
}

void UGamePauseSubsystem::ClearAllPauseReasons()
{
	if (ActiveReasons.Num() > 0)
	{
		UE_LOG(LogXRU1UI, Display, TEXT("[Pause] сброс всех причин (были: %s)"), *DescribeReasons());
		ActiveReasons.Reset();
		ApplyPauseState();
	}
}

void UGamePauseSubsystem::ApplyPauseState()
{
	const bool bShouldPause = IsPaused();
	if (bShouldPause == bAppliedPaused)
	{
		return;
	}
	bAppliedPaused = bShouldPause;

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld())
	{
		// Применять нечего: без игрового мира состояние всё равно разойдётся
		// с реальностью, а флаг bAppliedPaused соврёт при следующей проверке.
		bAppliedPaused = false;
		UE_LOG(LogXRU1UI, Verbose, TEXT("[Pause] пропуск: нет игрового мира"));
		return;
	}

	// Останавливает тик акторов, таймеры, AI и анимации разом — ручная
	// «заморозка» по системам всегда что-нибудь забывает.
	UGameplayStatics::SetGamePaused(World, bShouldPause);

	// Режим ввода подсистема НЕ трогает намеренно. Первая версия ставила
	// UIOnly/GameAndUI и делала хуже: возвращала главному меню игровой режим и
	// отбирала управление у хаба, если причина паузы почему-то залипала.
	// Игнорировать ввод на паузе — обязанность контроллера (IsPaused()).

	// Звук паузу мира не замечает: его надо гасить отдельно.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UTacticsAudioSubsystem* Audio = GameInstance->GetSubsystem<UTacticsAudioSubsystem>())
		{
			Audio->SetGameplayAudioPaused(bShouldPause);
		}
	}

	UE_LOG(LogXRU1UI, Display, TEXT("[Pause] МИР %s | причины: %s | world=%s"),
		bShouldPause ? TEXT("ОСТАНОВЛЕН") : TEXT("ПРОДОЛЖЕН"),
		*DescribeReasons(), World ? *World->GetName() : TEXT("<нет>"));

	OnPauseChanged.Broadcast(bShouldPause);
}
