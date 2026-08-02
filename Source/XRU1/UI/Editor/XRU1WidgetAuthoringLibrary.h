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
};
