#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "POIPopupWidget.generated.h"

class UTextBlock;

/**
 * Попап точки интереса хаба (world-space WidgetComponent на
 * AMissionPointOfInterest). Показывает заголовок, брифинг-описание и строку
 * блокировки. Данные приходят из актора через SetupFromPOI при каждом
 * наведении — состояние гейта могло измениться после пройденного туториала.
 */
UCLASS(Abstract, Blueprintable)
class XRU1_API UPOIPopupWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Заполняет попап данными POI; зовёт AMissionPointOfInterest. */
	UFUNCTION(BlueprintCallable, Category = "Menu|POI")
	void SetupFromPOI(const FText& InTitle, const FText& InDescription, bool bInLocked);

protected:
	/** BP-хук после заполнения (доп. оформление; вёрстка уже обновлена в C++). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|POI")
	void OnPOIDataChanged(bool bInLocked);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Title;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Description;

	/** Строка «Сначала пройдите обучение»; видна только у заблокированной POI. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Locked;
};
