#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "XRU1WidgetAuthoringLibrary.generated.h"

/**
 * Editor-библиотека программной сборки вёрстки Widget Blueprint экранов меню.
 *
 * Зачем: MCP-мост UnrealClaude не умеет редактировать WidgetTree (см.
 * docs/agents/AGENT_UNREAL_TOOLING.md §4), а Python API движка не экспонирует
 * ни WidgetTree на запись, ни классы K2Node*. Эта библиотека закрывает пробел:
 * агент вызывает её функции из редактора через execute_script (Python:
 * unreal.XRU1WidgetAuthoringLibrary.build_*), вёрстка появляется в обычных
 * WBP-ассетах, и пользователь дальше правит их в Designer как рукотворные.
 *
 * Логика экранов сюда не входит: обработчики уже привязываются в C++
 * (NativeOnInitialized виджетов меню) по каноничным именам Btn_* / Sld_* / Txt_*
 * (пробелы вокруг слэшей обязательны: «звёздочка+слэш» подряд закрывает
 * блочный комментарий и ломает UHT).
 *
 * Все функции работают только в editor-сборке; в runtime возвращают false.
 * Функция НЕ сохраняет ассет: после успешной сборки его сохраняет вызывающая
 * сторона (unreal.EditorAssetLibrary.save_asset) — так проще откатиться.
 */
UCLASS()
class XRU1_API UXRU1WidgetAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Экран настроек: 5 слайдеров звука, качество/масштаб/чекбоксы, Apply/Reset/Back. */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool BuildSettingsMenuLayout(const FString& AssetPath, bool bOverwriteExisting);

	/** Экран «Об авторе»: заголовок, Txt_Author, Txt_ProjectInfo, Btn_Back. */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool BuildAboutMenuLayout(const FString& AssetPath, bool bOverwriteExisting);

	/** Экран паузы: тёмная подложка, Resume/Settings/ReturnToMenu. */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool BuildPauseMenuLayout(const FString& AssetPath, bool bOverwriteExisting);

	/** Интро: фон, полноэкранная прозрачная Btn_Skip и подсказка «Пропустить». */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool BuildIntroPlayerLayout(const FString& AssetPath, bool bOverwriteExisting);

	/** Экран результата: арт, заголовок, подзаголовок, Retry/ToHub/ToMenu. */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool BuildMissionResultLayout(const FString& AssetPath, bool bOverwriteExisting);

	/** Попап POI хаба: Txt_Title, Txt_Description, Txt_Locked. */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool BuildPOIPopupLayout(const FString& AssetPath, bool bOverwriteExisting);

	/**
	 * HUD хаба: карточка точки слева внизу (Txt_POITitle/Description/Status,
	 * Btn_Start) и служебные кнопки справа вверху (Btn_Settings, Btn_ToMenu).
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool BuildHubHUDLayout(const FString& AssetPath, bool bOverwriteExisting);

	/**
	 * Брифинг миссии: фон-арт, Txt_BriefTitle, Txt_BriefText, Txt_BriefStatus,
	 * Btn_Start и Btn_Back.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool BuildMissionBriefingLayout(const FString& AssetPath, bool bOverwriteExisting);

	/**
	 * Вставляет в СУЩЕСТВУЮЩУЮ вёрстку полноэкранный фон `Img_Background` под
	 * весь остальной контент, ничего не разрушая.
	 *
	 * Нужен рукотворным экранам: `UMenuScreenBase::ApplyScreenArt()` ищет фон по
	 * имени и молча уходит, если виджета нет, — так главное меню и оказалось
	 * чёрным при полностью заполненной теме. Картинку функция НЕ ставит: арт
	 * приходит из темы в рантайме, поэтому виджет создаётся скрытым.
	 *
	 * Идемпотентна: если `Img_Background` уже есть, ничего не меняет.
	 *
	 * ⚠️ Фон живёт ОДИН на весь лейаут
	 * (`AddLayoutBackdrop`), а локальные фоны экранов прячутся в рантайме.
	 * Функция оставлена для экранов, которым нужен собственный фон.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool AddScreenBackground(const FString& AssetPath);

	/**
	 * Точечная правка WBP_PauseMenuWidget: кнопка `Btn_ToHub` («На базу») перед
	 * `Btn_ReturnToMenu`. Отдельная функция, а не пересборка BuildPauseMenuLayout:
	 * полная перезапись стёрла бы фон и любые рукотворные правки экрана.
	 * Идемпотентна — существующая кнопка означает успех без изменений.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool AddPauseMenuHubButton(const FString& AssetPath);

	/**
	 * Точечная правка WBP_DifficultySelect: строка «Пропустить обучение» с
	 * чекбоксом `Chk_SkipTutorial` между кнопками сложности и «Назад».
	 * Идемпотентна — существующий чекбокс означает успех без изменений.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool AddDifficultySkipTutorialToggle(const FString& AssetPath);

	/**
	 * Точечная правка WBP_TacticalHUD: баннер подкреплений врага верх-центр —
	 * Border `ReinforcementPanel` с текстом `ReinforcementText`. Имена читает
	 * BindWidgetOptional базы HUD; создаётся скрытым, показом и текстом
	 * управляет UTacticalHUDWidget. Идемпотентна.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool AddTacticalHUDReinforcementBanner(const FString& AssetPath);

	/**
	 * Вставляет в корневой лейаут (`WBP_PrimaryGameLayout`) общий фон
	 * `Img_ScreenBackdrop` — под все стеки-слои.
	 *
	 * Так фон переживает переходы между экранами: `UCommonActivatableWidgetStack`
	 * показывает только верхний виджет, поэтому фон внутри экрана неизбежно
	 * исчезает и появляется заново на каждом переходе. Идемпотентна.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Widget Authoring")
	static bool AddLayoutBackdrop(const FString& AssetPath);
};
