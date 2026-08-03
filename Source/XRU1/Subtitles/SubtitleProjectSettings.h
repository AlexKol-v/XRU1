#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SubtitleProjectSettings.generated.h"

/**
 * Проектные настройки слоя субтитров и языка (Project Settings → Game →
 * «Субтитры и язык (XRU1)», файл `DefaultGame.ini`).
 *
 * Здесь лежит то, что выбирает НЕ игрок, а проект: список поддерживаемых
 * языков и способ отрисовки. Пользовательские настройки (включены ли субтитры,
 * размер, подложка, выбранный язык) живут в `UTacticsUserSettings` —
 * `GameUserSettings.ini`, там же, где движок хранит культуру.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Субтитры и язык (XRU1)"))
class XRU1_API UXRU1SubtitleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	/** Настройки проекта; никогда не nullptr. */
	static const UXRU1SubtitleSettings& Get() { return *GetDefault<UXRU1SubtitleSettings>(); }

	// --- Отрисовка ------------------------------------------------------------

	/**
	 * Рисовать субтитры встроенным Slate-оверлеем.
	 *
	 * Это точка замены дисплея: выключив флаг, проект получает работающую
	 * подсистему без своего виджета, а рисовать берётся любой другой (WBP,
	 * подписанный на `OnLineChanged`). Источники реплик при этом не меняются —
	 * они вообще не знают, кто рисует.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Отрисовка")
	bool bUseBuiltInDisplay = true;

	/**
	 * Порядок отрисовки оверлея в viewport.
	 *
	 * Должен быть ВЫШЕ корневого лейаута CommonUI (он добавляется с ZOrder 0) и
	 * выше оверлея подсказок обучения (ZOrder 8): интро, брифинг и экран
	 * результата — это экраны CommonUI, и субтитры к ним идут поверх.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Отрисовка", meta = (ClampMin = "0"))
	int32 DisplayZOrder = 10;

	// --- Язык -----------------------------------------------------------------

	/**
	 * Поддерживаемые культуры (коды ICU) в порядке показа в меню.
	 * Первая — язык-источник проекта: тексты написаны на нём.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Язык")
	TArray<FString> AvailableCultures = { TEXT("ru"), TEXT("en") };

	/**
	 * Культура, которую слой предлагает как «по умолчанию» при сбросе настроек.
	 * Пусто — брать культуру операционной системы.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Язык")
	FString DefaultCulture = TEXT("ru");

	/** Отображаемое имя языка для меню; неизвестный код показывается как есть. */
	UFUNCTION(BlueprintPure, Category = "Язык")
	static FText GetCultureDisplayName(const FString& Culture);
};
