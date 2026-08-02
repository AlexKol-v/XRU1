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
	/**
	 * Заполняет попап данными POI; зовёт AMissionPointOfInterest.
	 * `InLockedReason` пустой означает «точка доступна» — отдельный флаг не нужен
	 * и не может разойтись с текстом.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|POI")
	void SetupFromPOI(const FText& InTitle, const FText& InDescription, const FText& InLockedReason);

protected:
	/**
	 * Попап обязан быть прозрачным для мыши. Он висит прямо над маркером, и в
	 * Screen space его hit-test идёт через Slate, а не через физику: перекрыв
	 * курсор, он отбирает наведение у маркера, попап гаснет, наведение
	 * возвращается — точка мигает с частотой кадров. Коллизия компонента на это
	 * не влияет, лечится только видимостью самого виджета.
	 */
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;

	/** BP-хук после заполнения (доп. оформление; вёрстка уже обновлена в C++). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|POI")
	void OnPOIDataChanged(bool bInLocked);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Title;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Description;

	/** Строка «Сначала пройдите обучение»; видна только у заблокированной POI. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Locked;
};
