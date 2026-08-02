#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "GamePauseSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGamePauseChanged, bool, bPaused);

/**
 * Причины паузы, принадлежащие системам. Экраны меню используют не эти
 * константы, а собственное уникальное имя `Menu.<ИмяВиджета>` — иначе два
 * открытых экрана делили бы одну причину и закрытие первого снимало бы паузу.
 */
namespace XRU1PauseReasons
{
	/** Окно игры потеряло фокус (свернули/переключились). */
	XRU1_API extern const FName WindowFocus;
}

/**
 * Единственный владелец паузы в проекте.
 *
 * Пауза почти всегда нужна нескольким системам одновременно (открыт экран
 * настроек И свёрнуто окно), поэтому это не флаг, а **стек причин**: пауза
 * держится, пока есть хоть одна причина. Флаг в такой ситуации неизбежно
 * ломается — закрытие одного окна снимало бы паузу, нужную другому.
 *
 * Что делает пауза:
 *  - останавливает мир (`SetGamePaused`) — тик акторов, таймеры, AI, анимации;
 *  - глушит боевой звук и ставит на паузу текущую реплику (голос продолжится
 *    с того же места, а не начнётся заново);
 *  - контроллеры игнорируют ввод, спрашивая `IsPaused()` — иначе камера/карта
 *    продолжали бы крутиться за открытым меню.
 *
 * Потеря фокуса окном — такая же причина паузы (`Reason::WindowFocus`),
 * подписка на `FSlateApplication::OnApplicationActivationStateChanged`.
 */
UCLASS()
class XRU1_API UGamePauseSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- FTickableGameObject: опрос фокуса игрового вьюпорта (нужен в PIE) ---
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }

	/** Текстовый список активных причин — для логов и отладки. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Pause")
	FString DescribeReasons() const;

	/** Добавляет причину паузы. Повторный вызов с той же причиной безопасен. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Pause")
	void PushPauseReason(FName Reason);

	/** Снимает причину; пауза уходит, когда снята последняя. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Pause")
	void PopPauseReason(FName Reason);

	UFUNCTION(BlueprintPure, Category = "Tactics|Pause")
	bool IsPaused() const { return ActiveReasons.Num() > 0; }

	/** Снимает все причины — аварийный выход (смена уровня, конец миссии). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Pause")
	void ClearAllPauseReasons();

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Pause")
	FOnGamePauseChanged OnPauseChanged;

private:
	/** Приводит состояние мира и звука к текущему набору причин. */
	void ApplyPauseState();

	/** Окно активировано/деактивировано — пауза по фокусу. */
	void HandleApplicationActivationChanged(bool bIsActive);

	/** Единая точка смены «окно в фокусе» (вызывается после дебаунса). */
	void SetWindowFocused(bool bFocused);

	/** Есть ли у этого GameInstance игровой мир (в редакторе их два). */
	bool OwnsGameWorld() const;

	/** Последнее применённое состояние фокуса. */
	bool bWindowFocused = true;

	/** Окно хоть раз было активным: до этого терять фокус «нечему» (старт PIE). */
	bool bEverFocused = false;

	/** Состояние фокуса, ожидающее подтверждения дебаунсом. */
	bool PendingFocusState = true;

	/** Сколько уже держится PendingFocusState, сек. */
	float PendingFocusTimer = 0.f;

	/**
	 * Сколько состояние фокуса должно продержаться, прежде чем ему поверят.
	 * Переключение окон даёт серию событий «получен→потерян» за один кадр.
	 */
	static constexpr float FocusSettleTime = 0.2f;

	/** Причины, удерживающие паузу прямо сейчас. */
	TSet<FName> ActiveReasons;

	/** Последнее применённое состояние (чтобы не дёргать мир каждый кадр). */
	bool bAppliedPaused = false;

	FDelegateHandle ActivationChangedHandle;
};
