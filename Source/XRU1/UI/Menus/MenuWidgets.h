#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "TacticsTypes.h"
#include "MenuWidgets.generated.h"

class UTacticsGameInstance;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDifficultyChosen, EDifficultyLevel, Difficulty);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMenuAction);

/**
 * Общий предок экранов меню на CommonUI activatable-стеке. Навигация каноном
 * CommonUI: следующий экран проталкивается на слой Menu корневого
 * UPrimaryGameLayout (через UGameUIManagerSubsystem), «назад» — деактивация
 * себя, стек сам показывает предыдущий экран. Дизайн виджетов — в BP-наследниках.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UMenuScreenBase : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Просьба закрыть экран и вернуться на предыдущий в стеке. */
	UPROPERTY(BlueprintAssignable, Category = "Menu")
	FOnMenuAction OnBackRequested;

	/** Закрывает экран (деактивация = снятие со стека) и шлёт OnBackRequested. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void RequestBack();

	/** Проталкивает экран ScreenClass на слой Menu корневого лейаута. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	UCommonActivatableWidget* PushScreen(TSubclassOf<UCommonActivatableWidget> ScreenClass);

protected:
	/** GameInstance проекта (nullptr, если проект настроен на другой класс). */
	UTacticsGameInstance* GetTacticsGameInstance() const;
};

/**
 * Главное меню: Продолжить / Новая игра / Настройки / Об авторе / Выйти.
 * BP вешает кнопки на Request*-методы; переходы экранов реализованы здесь
 * (пуш на слой Menu), делегаты остаются для дополнительной BP-логики.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UMainMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Доступна ли кнопка «Продолжить» (есть ли сохранение). Опрашивает UTacticsGameInstance. */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool CanContinue() const;

	/** Экран настроек, открываемый кнопкой Settings. */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Screens")
	TSubclassOf<UMenuScreenBase> SettingsScreenClass;

	/** Экран «Об авторе». */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Screens")
	TSubclassOf<UMenuScreenBase> AboutScreenClass;

	/** Экран выбора сложности (открывается кнопкой «Новая игра»). */
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Screens")
	TSubclassOf<UMenuScreenBase> DifficultyScreenClass;

	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnContinueClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnNewGameClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnSettingsClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnAboutClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnQuitClicked;

	/** Загружает кампанию и отправляет игрока в хаб. */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestContinue();

	/** Открывает экран выбора сложности. */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestNewGame();

	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestSettings();
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestAbout();

	/** Завершает игру. */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestQuit();
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

	/**
	 * BP вызывает по нажатию кнопки конкретной сложности: создаёт новую кампанию
	 * (UTacticsGameInstance::StartNewCampaign) и отправляет игрока в хаб.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void ChooseDifficulty(EDifficultyLevel Difficulty);
};

/** Экран паузы во время миссии. */
UCLASS(Abstract, Blueprintable)
class XRU1_API UPauseMenuWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnResumeClicked;
	UPROPERTY(BlueprintAssignable, Category = "Menu") FOnMenuAction OnReturnToMenuClicked;

	/** Снимает паузу и закрывает экран. */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestResume();

	/** Снимает паузу и возвращает в главное меню (уровень MainMenuLevel из GameInstance). */
	UFUNCTION(BlueprintCallable, Category = "Menu") void RequestReturnToMenu();
};
