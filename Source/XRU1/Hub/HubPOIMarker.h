#pragma once

#include "CoreMinimal.h"
#include "MissionPointOfInterest.h"
#include "HubPOIMarker.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * Визуальный маркер точки интереса на голографической карте: полупрозрачная
 * сфера и световой столб до поверхности карты (BRIEF_HubHologram §4.2).
 *
 * Логика запуска миссии и гейт целиком унаследованы от AMissionPointOfInterest —
 * здесь только внешний вид и его три состояния: доступна / под курсором /
 * заблокирована. Состояния красятся через Dynamic Material Instance, поэтому
 * BP-граф маркеру не нужен.
 */
UCLASS(Blueprintable)
class XRU1_API AHubPOIMarker : public AMissionPointOfInterest
{
	GENERATED_BODY()

public:
	AHubPOIMarker();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** Столб света от сферы к поверхности карты: видно, к какой точке маркер. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hub|POI")
	TObjectPtr<UStaticMeshComponent> Beam;

	/** Материал сферы и столба (наш M_HubPOI с параметрами Color/Glow/Opacity). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hub|POI")
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	// Цвет = состояние точки, и состояний четыре. Порядок приоритета в
	// RefreshVisualState: пройдена → заблокирована → выбрана → доступна;
	// наведение только подсвечивает текущий цвет, а не заменяет его, иначе
	// «выбрано» терялось бы под курсором.

	/** Доступна: жёлтый — маркер обязан читаться на бирюзовом столе карты. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI")
	FLinearColor AvailableColor = FLinearColor(1.f, 0.78f, 0.06f, 1.f);

	/** Выбрана игроком: красный. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI")
	FLinearColor SelectedColor = FLinearColor(0.95f, 0.12f, 0.08f, 1.f);

	/** Миссия пройдена: зелёный. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI")
	FLinearColor CompletedColor = FLinearColor(0.10f, 0.85f, 0.32f, 1.f);

	/** Заблокирована: серый, погашенный. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI")
	FLinearColor LockedColor = FLinearColor(0.16f, 0.18f, 0.2f, 1.f);

	/** Во сколько раз ярче становится цвет под курсором. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI", meta = (ClampMin = "1"))
	float HoverBrightness = 1.5f;

	/** Насколько увеличивается маркер под курсором. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI", meta = (ClampMin = "1"))
	float HoverScale = 1.15f;

	/** Амплитуда пульсации свечения доступной точки (0 — выключить). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI", meta = (ClampMin = "0"))
	float PulseAmplitude = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI", meta = (ClampMin = "0"))
	float PulseSpeed = 2.2f;

	/**
	 * Базовая яркость свечения. Больше ~1.2 у Unlit+Translucent даёт пересвет:
	 * оранжевый уходит в белёсый и перестаёт отличаться от серого «заблокировано».
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI", meta = (ClampMin = "0"))
	float BaseGlow = 1.f;

	/** Во сколько раз тусклее светится заблокированная точка. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI", meta = (ClampMin = "0"))
	float LockedGlowScale = 0.4f;

	/** Непрозрачность доступной точки (материал полупрозрачный). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI", meta = (ClampMin = "0", ClampMax = "1"))
	float AvailableOpacity = 0.85f;

	/** Непрозрачность заблокированной: она должна выглядеть погашенной. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|POI", meta = (ClampMin = "0", ClampMax = "1"))
	float LockedOpacity = 0.35f;

	/** Перекраска по смене наведения/выбора (база зовёт при любом изменении). */
	virtual void OnVisualStateChanged() override;

private:
	/** Применяет цвет/масштаб по текущим состоянию, выбору и наведению. */
	void RefreshVisualState();

	UFUNCTION()
	void HandleHoverChanged(bool bHovered);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SphereMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeamMaterial;

	/** Масштаб сферы из редактора — база для увеличения под курсором. */
	FVector BaseMarkerScale = FVector::OneVector;

	/**
	 * Кэш `IsLocked()`. Тик пульсации не должен опрашивать слот кампании и
	 * подгружать требования миссии каждый кадр; значение обновляется там, где
	 * состояние реально меняется (`RefreshVisualState`).
	 */
	bool bCachedLocked = false;
};
