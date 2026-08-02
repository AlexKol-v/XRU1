#pragma once

#include "CoreMinimal.h"
#include "MenuWidgets.h"
#include "MissionResultWidget.generated.h"

/**
 * Экран результата миссии (победа/поражение) — GDD §4. Живёт в стеке Menu.
 * Визуал (заголовок, статистика, кнопки) — в WBP-наследнике; GameMode зовёт
 * SetupResult, кнопки зовут Retry/GoToHub/GoToMainMenu.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UMissionResultWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Заполняет экран результатом (зовёт ATacticsGameMode при конце боя). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void SetupResult(bool bInVictory, bool bInDefeatByTimeout);

	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsVictory() const { return bVictory; }

	/** Поражение из-за таймера бомбы (другой текст на экране). */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsDefeatByTimeout() const { return bDefeatByTimeout; }

	/** Кнопка «Повторить»: перезапускает текущий Scenario через чистый bootstrap. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void RetryMission();

	/** Кнопка «На базу»: в хаб. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void GoToHub();

	/** Кнопка «В главное меню». */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void GoToMainMenu();

	/**
	 * Победа в боевой миссии (не туториале) = демо пройдено: экран показывает
	 * DemoComplete-арт и текст вместо обычного результата (GDD §4).
	 */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsDemoComplete() const;

	/** Текущий сценарий — учебный полигон (у него свой текст и арт результата). */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsTutorialScenario() const;

protected:
	/** BP-хук: результат готов — обновить тексты/показать статистику. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu")
	void OnResultReady(bool bInVictory, bool bInDefeatByTimeout);

	virtual void NativeOnInitialized() override;

	/** C++-заполнение текстов/арта по результату; работает без BP-графа. */
	void UpdateResultVisuals();

	/** Реплика исхода из активного сценария (VictoryVoice / DefeatVoice). */
	void PlayOutcomeVoice();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_ResultTitle;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_ResultSubtitle;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UImage> Img_ResultArt;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Retry;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_ToHub;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_ToMenu;

	bool bVictory = false;
	bool bDefeatByTimeout = false;

private:
	UFUNCTION() void HandleRetryClicked();
	UFUNCTION() void HandleToHubClicked();
	UFUNCTION() void HandleToMenuClicked();
};
