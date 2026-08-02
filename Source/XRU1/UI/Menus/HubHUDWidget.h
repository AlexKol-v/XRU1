#pragma once

#include "CoreMinimal.h"
#include "MenuWidgets.h"
#include "HubHUDWidget.generated.h"

class AMissionPointOfInterest;

/**
 * HUD хаба: карточка выбранной точки интереса и запуск операции
 * (BRIEF_HubHologram §4.4). Живёт на слое Game.
 *
 * Наследник UMenuScreenBase ради готовой инфраструктуры экранов: тема,
 * PushScreen для настроек и звуки кнопок. Механика запуска целиком остаётся
 * в AMissionPointOfInterest — HUD только показывает данные и передаёт намерение.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UHubHUDWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Контроллер хаба сообщает о смене выбора; nullptr — выбор снят. */
	UFUNCTION(BlueprintCallable, Category = "Hub|UI")
	void SetSelectedPOI(AMissionPointOfInterest* POI);

	UFUNCTION(BlueprintPure, Category = "Hub|UI")
	AMissionPointOfInterest* GetSelectedPOI() const { return SelectedPOI; }

	/** Экран настроек, открываемый из хаба. */
	UPROPERTY(EditDefaultsOnly, Category = "Hub|UI")
	TSubclassOf<UMenuScreenBase> SettingsScreenClass;

protected:
	virtual void NativeOnInitialized() override;

	/** BP-хук после смены выбора (анимация появления карточки и т.п.). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Hub|UI")
	void OnSelectedPOIChanged(AMissionPointOfInterest* POI, bool bLocked);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_POITitle;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_POIDescription;

	/** Строка «Обучение пройдено» / «Требуется пройти обучение». */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Status;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Start;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Settings;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_ToMenu;

private:
	/** Перерисовывает карточку и доступность «Начать операцию» по текущему выбору. */
	void RefreshFromSelection();

	UFUNCTION() void HandleStartClicked();
	UFUNCTION() void HandleSettingsClicked();
	UFUNCTION() void HandleToMenuClicked();

	UPROPERTY(Transient)
	TObjectPtr<AMissionPointOfInterest> SelectedPOI;
};
