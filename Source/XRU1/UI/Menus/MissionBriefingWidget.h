#pragma once

#include "CoreMinimal.h"
#include "MenuWidgets.h"
#include "MissionBriefingWidget.generated.h"

class AMissionPointOfInterest;
class UTacticalScenarioDataAsset;

/**
 * Брифинг миссии между выбором точки в хабе и боем (docs/03_ARCHITECTURE.md §11).
 *
 * Свой текст экран не сочиняет: название и описание принадлежат миссии
 * (`UTacticalScenarioDataAsset::DisplayName` / `BriefingText`), а роль
 * крупного арта выбирается по виду сценария — учебный полигон и боевая
 * операция выглядят по-разному. Запуск миссии тоже не свой: экран вызывает
 * `AMissionPointOfInterest::SelectPointOfInterest()`, где живёт гейт
 * доступности и переход к `StartCombatScenario`.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UMissionBriefingWidget : public UMenuScreenBase
{
	GENERATED_BODY()

public:
	/** Заполняет экран данными точки; вызывает контроллер хаба сразу после пуша. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Briefing")
	void SetupFromPOI(AMissionPointOfInterest* POI);

	UFUNCTION(BlueprintPure, Category = "Menu|Briefing")
	AMissionPointOfInterest* GetPointOfInterest() const { return PointOfInterest; }

	/** «Начать операцию»: отдаёт намерение точке интереса. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Briefing")
	void StartOperation();

protected:
	virtual void NativeOnInitialized() override;

	/** BP-хук после заполнения (анимация появления, доп. статистика). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Briefing")
	void OnBriefingReady(AMissionPointOfInterest* POI);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_BriefTitle;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_BriefText;

	/** Строка доступности; у доступной миссии остаётся пустой. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_BriefStatus;

	/** Крупный арт миссии; заполняется из темы по виду сценария. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UImage> Img_BriefArt;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Start;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UButton> Btn_Back;

private:
	/** Перечитывает тексты, арт и доступность кнопки по текущей точке. */
	void RefreshFromPOI();

	/** Реплика брифинга выбранной миссии (пусто — экран молчит). */
	void PlayBriefingVoice();

	UFUNCTION() void HandleStartClicked();

	UPROPERTY(Transient)
	TObjectPtr<AMissionPointOfInterest> PointOfInterest;
};
