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

	/** Кнопка «Повторить»: перезапускает текущий уровень. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void RetryMission();

	/** Кнопка «На базу»: в хаб. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void GoToHub();

	/** Кнопка «В главное меню». */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void GoToMainMenu();

protected:
	/** BP-хук: результат готов — обновить тексты/показать статистику. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu")
	void OnResultReady(bool bInVictory, bool bInDefeatByTimeout);

	bool bVictory = false;
	bool bDefeatByTimeout = false;
};
