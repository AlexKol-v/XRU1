#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StreamableManager.h"
#include "MissionPointOfInterest.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UWidgetComponent;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPOIHoverChanged, bool, bHovered);

/**
 * Точка интереса на 3D-карте хаба (обучение / боевая миссия). При наведении
 * курсора показывает попап с информацией о миссии; по клику — грузит связанный
 * уровень. Упрощённая mission-select точка на замену AQuestWaypoint из донора.
 *
 * Каркас: визуал + hover-попап + метаданные миссии заложены; фактическая загрузка
 * уровня и интеграция с прогрессом кампании (UTacticsSaveGame) — точки расширения.
 */
UCLASS(Blueprintable)
class XRU1_API AMissionPointOfInterest : public AActor
{
	GENERATED_BODY()

public:
	AMissionPointOfInterest();

	/** Идентификатор миссии (для прогресса кампании), напр. "Tutorial" / "Mission01". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	FName MissionId = NAME_None;

	/**
	 * Гейт: POI доступна только после прохождения указанной миссии
	 * (у «Станции Узел-7» = "Tutorial"). NAME_None — доступна всегда.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	FName RequiredCompletedMission = NAME_None;

	/** Заголовок для попапа. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	FText Title;

	/** Описание для попапа (условия, брифинг). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI", meta = (MultiLine = true))
	FText Description;

	/** Уровень, загружаемый по выбору точки (soft-ссылка). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI", meta = (AllowedTypes = "World"))
	TSoftObjectPtr<UWorld> LevelToLoad;

	/** Класс виджета-попапа (CommonUI/UserWidget), задаётся в BP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	TSubclassOf<UUserWidget> PopupWidgetClass;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|POI")
	FOnPOIHoverChanged OnHoverChanged;

	/** Заблокирована ли точка (требуемая миссия ещё не пройдена). Для серого маркера в BP. */
	UFUNCTION(BlueprintPure, Category = "Tactics|POI")
	bool IsLocked() const;

	/** Загружает связанный уровень; у заблокированной точки шлёт OnSelectionDenied. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|POI")
	void SelectPointOfInterest();

	/** BP-хук отказа (звук/подсказка «сначала пройдите обучение»). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|POI")
	void OnSelectionDenied();

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleBeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void HandleEndCursorOver(UPrimitiveComponent* TouchedComponent);

	void SetHovered(bool bHovered);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	TObjectPtr<UStaticMeshComponent> Marker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	TObjectPtr<USphereComponent> HoverBounds;

	/** Носитель попап-виджета (скрыт, пока нет наведения). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	TObjectPtr<UWidgetComponent> PopupWidget;

	bool bIsHovered = false;
};
