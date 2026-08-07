#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Templates/SubclassOf.h"
#include "HubPlayerController.generated.h"

class AHologramMapActor;
class AHubCameraPawn;
class AMissionPointOfInterest;
class UHubHUDWidget;
class UMissionBriefingWidget;
class UPrimaryGameLayout;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHubPOISelected, AMissionPointOfInterest*, POI);

/**
 * Контроллер хаба. Отдельный от тактического: боевой ввод здесь только мешал бы.
 *
 * Схема управления (выбрана, чтобы клик по маркеру никогда не проворачивал карту —
 * типичная проблема схемы «ЛКМ и вращает, и выбирает»):
 *   ПКМ + движение — вращение карты (гориз.) и наклон камеры (верт.);
 *   ЛКМ           — выбор маркера под курсором;
 *   двойной ЛКМ   — запуск миссии выбранного маркера;
 *   колесо        — зум;
 *   Q / E         — вращение карты с клавиатуры.
 *
 * Ввод намеренно на прямых key bindings, а не на Enhanced Input: хабу не нужен
 * ни один дополнительный ассет Input Action/Mapping Context.
 */
// Не Abstract: AHubGameMode ставит этот класс по умолчанию, а BP-наследник
// нужен лишь для назначения UI-классов.
UCLASS(Blueprintable)
class XRU1_API AHubPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHubPlayerController();

	/** Выбранный маркер (данные для HUD); nullptr — выбора нет. */
	UFUNCTION(BlueprintPure, Category = "Hub")
	AMissionPointOfInterest* GetSelectedPOI() const { return SelectedPOI; }

	/** Выбор маркера: обновляет HUD и подсветку. Публичен — HUD тоже может выбирать. */
	UFUNCTION(BlueprintCallable, Category = "Hub")
	void SelectPOI(AMissionPointOfInterest* POI);

	/** Запуск миссии выбранного маркера (кнопка «Начать операцию» в HUD). */
	UFUNCTION(BlueprintCallable, Category = "Hub")
	void LaunchSelectedPOI();

	/** Карта хаба, найденная в мире при старте. */
	UFUNCTION(BlueprintPure, Category = "Hub")
	AHologramMapActor* GetHologramMap() const;

	UPROPERTY(BlueprintAssignable, Category = "Hub")
	FOnHubPOISelected OnPOISelected;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Класс корневого UI-слоя (тот же WBP_PrimaryGameLayout, что и в меню). */
	UPROPERTY(EditDefaultsOnly, Category = "Hub|UI")
	TSubclassOf<UPrimaryGameLayout> RootLayoutClass;

	/** HUD хаба, пушится на слой Game. */
	UPROPERTY(EditDefaultsOnly, Category = "Hub|UI")
	TSubclassOf<UHubHUDWidget> HubHUDClass;

	/**
	 * Брифинг между выбором точки и боем. Не назначен — точка запускает миссию
	 * сразу, поэтому хаб остаётся рабочим и без экрана.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hub|UI")
	TSubclassOf<UMissionBriefingWidget> BriefingScreenClass;

	/**
	 * Град поворота карты на пиксель горизонтального движения мыши.
	 * EditAnywhere: подбирается на слух и на глаз, менять должно быть можно и на
	 * инстансе контроллера, не пересобирая проект.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Input", meta = (ClampMin = "0.05"))
	float MouseYawSensitivity = 1.2f;

	/** Град наклона камеры на пиксель вертикального движения мыши. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Input", meta = (ClampMin = "0.05"))
	float MousePitchSensitivity = 0.7f;

	/** Скорость вращения картой с клавиатуры (Q/E), град/сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hub|Input", meta = (ClampMin = "1"))
	float KeyboardYawSpeed = 90.f;

private:
	UFUNCTION() void HandleRotatePressed();
	UFUNCTION() void HandleRotateReleased();
	UFUNCTION() void HandleSelectPressed();
	UFUNCTION() void HandleSelectDoubleClick();
	UFUNCTION() void HandleZoomIn();
	UFUNCTION() void HandleZoomOut();

	/** Маркер под курсором (nullptr, если курсор не на маркере). */
	AMissionPointOfInterest* TracePOIUnderCursor() const;

	/** Игра на паузе (открыто меню, окно без фокуса) — ввод хаба игнорируется. */
	bool IsInputBlockedByPause() const;

	AHubCameraPawn* GetHubCamera() const;

	/** Найденная в мире карта; кэшируется в BeginPlay. */
	UPROPERTY(Transient)
	TObjectPtr<AHologramMapActor> HologramMap;

	UPROPERTY(Transient)
	TObjectPtr<AMissionPointOfInterest> SelectedPOI;

	UPROPERTY(Transient)
	TObjectPtr<UHubHUDWidget> HubHUD;

	/** Зажата ПКМ — идёт вращение. */
	bool bRotating = false;

	/** Последнее залогированное состояние блокировки ввода (лог только на смене). */
	mutable bool bLoggedInputBlocked = false;
};
