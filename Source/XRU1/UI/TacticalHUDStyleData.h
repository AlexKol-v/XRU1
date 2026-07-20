#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TacticalHUDStyleData.generated.h"

class UTexture2D;

/**
 * Единый стиль боевого HUD (GDD §9): все размеры и текстуры иконок в одном
 * DataAsset — дизайнер подгоняет их в редакторе, не трогая ни WBP, ни код.
 * Применяется в UTacticalHUDWidget::ApplyStyle() (NativePreConstruct — виден
 * и в превью Designer). Незаданная текстура (None) НЕ трогает браш из
 * Designer — ассет дополняет разметку, а не заменяет её.
 */
UCLASS(BlueprintType)
class XRU1_API UTacticalHUDStyleData : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Кнопки действий (ActionsPanel + EndTurn) ----------------------------

	/** Размер иконки внутри кнопок панели действий. */
	UPROPERTY(EditAnywhere, Category = "Кнопки действий")
	FVector2D ActionIconSize = FVector2D(40.f, 40.f);

	/**
	 * Приравнять Pressed-отступ кнопки к Normal: у стандартного стиля UButton
	 * контент при нажатии сдвигается (кнопка «прыгает») — это убирает сдвиг.
	 */
	UPROPERTY(EditAnywhere, Category = "Кнопки действий")
	bool bUnifyPressedPadding = true;

	/**
	 * Единый отступ (Normal/Pressed) кнопок ActionsPanel — кнопки создавались
	 * в Designer в разные заходы и получили разный NormalPadding, из-за чего
	 * итоговый размер кнопки (иконка + отступ) визуально «прыгал» между
	 * кнопками несмотря на одинаковый ActionIconSize. Принудительно
	 * выставляется в ApplyStyle() на все 6 кнопок ActionsPanel.
	 */
	UPROPERTY(EditAnywhere, Category = "Кнопки действий")
	FMargin ActionButtonPadding = FMargin(8.f);

	// --- Текстуры иконок кнопок ----------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Иконки кнопок")
	TObjectPtr<UTexture2D> AttackIcon;

	UPROPERTY(EditAnywhere, Category = "Иконки кнопок")
	TObjectPtr<UTexture2D> OverwatchIcon;

	UPROPERTY(EditAnywhere, Category = "Иконки кнопок")
	TObjectPtr<UTexture2D> HunkerIcon;

	UPROPERTY(EditAnywhere, Category = "Иконки кнопок")
	TObjectPtr<UTexture2D> AbilityIcon;

	/** Иконка кнопки F в режиме «Обезвредить» (и по умолчанию). */
	UPROPERTY(EditAnywhere, Category = "Иконки кнопок")
	TObjectPtr<UTexture2D> InteractDefuseIcon;

	/** Иконка кнопки F в режиме «Эвакуация». */
	UPROPERTY(EditAnywhere, Category = "Иконки кнопок")
	TObjectPtr<UTexture2D> InteractEvacIcon;

	UPROPERTY(EditAnywhere, Category = "Иконки кнопок")
	TObjectPtr<UTexture2D> SkipIcon;

	// --- Кнопка «Завершить ход» ----------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Завершить ход")
	TObjectPtr<UTexture2D> EndTurnIcon;

	UPROPERTY(EditAnywhere, Category = "Завершить ход")
	FVector2D EndTurnIconSize = FVector2D(32.f, 32.f);

	// --- Панель цели (щит укрытия, как в XCOM 2) ------------------------------

	UPROPERTY(EditAnywhere, Category = "Панель цели")
	TObjectPtr<UTexture2D> HalfCoverIcon;

	UPROPERTY(EditAnywhere, Category = "Панель цели")
	TObjectPtr<UTexture2D> FullCoverIcon;

	UPROPERTY(EditAnywhere, Category = "Панель цели")
	FVector2D TargetCoverIconSize = FVector2D(28.f, 28.f);
};
