#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StreamableManager.h"
#include "MissionPointOfInterest.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UWidgetComponent;
class UUserWidget;
class UTacticalScenarioDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPOIHoverChanged, bool, bHovered);

/**
 * Точка интереса на 3D-карте хаба (обучение / боевая миссия). При наведении
 * курсора показывает попап с информацией о миссии; по клику запускает Scenario
 * общей боевой карты (либо legacy level fallback) и сохраняет выбор в кампании.
 * Упрощённая mission-select точка на замену AQuestWaypoint из донора.
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

	/**
	 * Сценарий общей карты. Если назначен, POI вызывает
	 * UTacticsGameInstance::StartCombatScenario и не открывает LevelToLoad напрямую.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	TObjectPtr<UTacticalScenarioDataAsset> Scenario;

	/** Legacy fallback для старых POI без Scenario Data Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI", meta = (AllowedTypes = "World"))
	TSoftObjectPtr<UWorld> LevelToLoad;

	/** Класс виджета-попапа (CommonUI/UserWidget), задаётся в BP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|POI")
	TSubclassOf<UUserWidget> PopupWidgetClass;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|POI")
	FOnPOIHoverChanged OnHoverChanged;

	/**
	 * Заблокирована ли точка. Источник правды — требования назначенной миссии
	 * (`UTacticalScenarioDataAsset::RequiredMissions`); `RequiredCompletedMission`
	 * ниже остаётся legacy-путём для точек без Scenario.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|POI")
	bool IsLocked() const;

	/** Причина недоступности для игрока; пусто — точка доступна. */
	UFUNCTION(BlueprintPure, Category = "Tactics|POI")
	FText GetLockedReason() const;

	/** Название: своё поле Title, иначе DisplayName миссии. */
	UFUNCTION(BlueprintPure, Category = "Tactics|POI")
	FText GetDisplayTitle() const;

	/** Описание: своё поле Description, иначе BriefingText миссии. */
	UFUNCTION(BlueprintPure, Category = "Tactics|POI")
	FText GetDisplayDescription() const;

	/** Загружает связанный уровень; у заблокированной точки шлёт OnSelectionDenied. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|POI")
	void SelectPointOfInterest();

	/** BP-хук отказа (звук/подсказка «сначала пройдите обучение»). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|POI")
	void OnSelectionDenied();

	/** Миссия этой точки уже пройдена (по слоту кампании). */
	UFUNCTION(BlueprintPure, Category = "Tactics|POI")
	bool IsCompleted() const;

	/** Точка выбрана игроком. Ставит контроллер хаба; влияет только на вид. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|POI")
	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintPure, Category = "Tactics|POI")
	bool IsSelected() const { return bIsSelected; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleBeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void HandleEndCursorOver(UPrimitiveComponent* TouchedComponent);

	void SetHovered(bool bHovered);

	/** Вид изменился (наведение/выбор/прогресс) — наследник перекрашивает себя. */
	virtual void OnVisualStateChanged() {}

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

	/** Выбрана ли точка сейчас (одна на весь хаб). */
	bool bIsSelected = false;
};
