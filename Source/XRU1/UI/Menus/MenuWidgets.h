#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "TacticsTypes.h"
#include "MenuWidgets.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDifficultyChosen, EDifficultyLevel, Difficulty);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMenuAction);

/**
 * Общий предок экранов меню на CommonUI activatable-стеке. Даёт единый хук
 * «назад» и место для общей навигационной логики. Дизайн виджетов — в BP-наследниках.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UMenuScreenBase : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Просьба закрыть экран и вернуться на предыдущий в стеке. */
	UPROPERTY(BlueprintAssignable, Category = "Menu")
	FOnMenuAction OnBackRequested;

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void RequestBack() { OnBackRequested.Broadcast(); }
};

/**
 * Главное меню: Продолжить / Новая игра / Настройки / Об авторе / Выйти.
 * BP вешает кнопки на эти BlueprintCallable-методы; логику перехода экранов
 * реализует владелец через GameUIManagerSubsystem / PrimaryGameLayout.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UMainMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Доступна ли кнопка «Продолжить» (есть ли сохранение). Опрашивает UTacticsGameInstance. */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool CanContinue() const;

	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnContinueClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnNewGameClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnSettingsClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnAboutClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnQuitClicked;

	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestContinue() { OnContinueClicked.Broadcast(); }
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestNewGame()  { OnNewGameClicked.Broadcast(); }
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestSettings() { OnSettingsClicked.Broadcast(); }
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestAbout()    { OnAboutClicked.Broadcast(); }
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestQuit()     { OnQuitClicked.Broadcast(); }
};

/** Экран настроек (звук/графика/управление). Каркас — поля дозаполняются позже. */
UCLASS(Abstract, Blueprintable)
class XRU1_API USettingsMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()
};

/** Экран «Об авторе». Текстовые поля задаёт дизайнер в BP. */
UCLASS(Abstract, Blueprintable)
class XRU1_API UAboutMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "About")
	FText AuthorName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "About", meta = (MultiLine = true))
	FText ProjectInfo;
};

/** Экран выбора сложности при старте новой игры. */
UCLASS(Abstract, Blueprintable)
class XRU1_API UDifficultySelectWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Menu")
	FOnDifficultyChosen OnDifficultyChosen;

	/** BP вызывает по нажатию кнопки конкретной сложности. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void ChooseDifficulty(EDifficultyLevel Difficulty) { OnDifficultyChosen.Broadcast(Difficulty); }
};

/** Экран паузы во время миссии. */
UCLASS(Abstract, Blueprintable)
class XRU1_API UPauseMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnResumeClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnReturnToMenuClicked;

	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestResume()       { OnResumeClicked.Broadcast(); }
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestReturnToMenu() { OnReturnToMenuClicked.Broadcast(); }
};
