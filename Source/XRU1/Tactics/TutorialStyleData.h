#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TutorialStyleData.generated.h"

class UMaterialInterface;

/**
 * Презентация слоя обучения: подсказки-цели поверх экрана и мировые декали шага.
 *
 * Выделен из `UTacticalHUDStyleData` (2026-08-03): тема интерфейса отвечает за
 * HUD, экраны и палитру, а здесь лежит то, что рисует обучение — в том числе
 * ДЕКАЛИ В МИРЕ, которым в «визуальной теме интерфейса» не место. Читатели
 * разные: `STutorialHintOverlay`, `ATacticalPlayerController` (маркеры
 * разрешённых точек шага) и `ATacticalQuestZone`.
 *
 * Ассет назначается один раз в BP-наследнике `UTacticsGameInstance`
 * (поле `TutorialStyle`); не назначен — `Get()` отдаёт CDO, и обучение
 * рисуется дефолтами, а не молча пропадает.
 */
UCLASS(BlueprintType)
class XRU1_API UTutorialStyleData : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Оверлей подсказок (STutorialHintOverlay) ----------------------------

	/** Отступ блока подсказок от левого верхнего угла экрана, px. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Подсказки")
	FVector2D HintOffset = FVector2D(28.f, 110.f);

	/** Размер шрифта названия квеста. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Подсказки", meta = (ClampMin = "6"))
	int32 HintTitleFontSize = 12;

	/** Размер шрифта строк целей. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Подсказки", meta = (ClampMin = "6"))
	int32 HintTextFontSize = 17;

	/** Размер шрифта причины отказа. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Подсказки", meta = (ClampMin = "6"))
	int32 HintDenialFontSize = 14;

	/** Ширина переноса строк, px. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Подсказки", meta = (ClampMin = "200"))
	float HintWrapWidth = 560.f;

	// --- Мировые декали шага -------------------------------------------------

	/**
	 * Декаль-маркер разрешённых точек перемещения шага (AllowedDestinationAnchors
	 * активной политики). Радиус кольца ВСЕГДА равен DestinationTolerance
	 * политики: картинка и проверка клика — один источник правды. Размер кольца
	 * меняется через DestinationTolerance в задаче Apply Action Gate шага.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Мир")
	TSoftObjectPtr<UMaterialInterface> DestinationMarkerMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
			TEXT("/Game/XRU1Game/Materials/M_SelectionRing.M_SelectionRing")));

	/**
	 * Декаль подсветки прямоугольной зоны шага (ATacticalQuestZone::SetHighlighted):
	 * рамка со скруглёнными углами по габаритам бокса зоны.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Мир")
	TSoftObjectPtr<UMaterialInterface> ZoneMarkerMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
			TEXT("/Game/XRU1Game/Materials/M_ZoneFrame.M_ZoneFrame")));

	/**
	 * Назначенный в GameInstance ассет либо CDO. Никогда не возвращает nullptr:
	 * отсутствие ассета не должно выключать подсказки обучения.
	 */
	static const UTutorialStyleData* Get(const UObject* WorldContext);
};
