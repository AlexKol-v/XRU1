#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Widgets/SLeafWidget.h"
#include "ObjectivePointerSubsystem.generated.h"

class SObjectivePointerOverlay;
class UTacticalHUDStyleData;

/**
 * Когда указатель показывается. Правило — ДАННЫЕ записи, а не событие снаружи:
 * подписка на «цель выполнена» означала бы, что забытая отписка оставит стрелку
 * висеть на снятом заряде. Здесь состояние спрашивается у самой цели каждый
 * кадр, и рассинхрон невозможен по построению.
 */
UENUM(BlueprintType)
enum class EObjectivePointerRule : uint8
{
	/** Пока цель существует и сценарно активна. */
	Always,
	/** Пока заряд НЕ обезврежен (`ABombObjective::IsDisarmed`). */
	WhileBombArmed,
	/** Пока зона эвакуации включена (`AEvacZone::IsActive`). */
	WhileEvacActive
};

/** Тон указателя: обычная цель или срочная (под таймером). */
UENUM(BlueprintType)
enum class EObjectivePointerTone : uint8
{
	Normal,
	Urgent
};

/** Одна зарегистрированная цель. */
struct FObjectivePointerEntry
{
	/** Ключ регистрации: повторная регистрация с тем же Id заменяет запись. */
	FName Id;

	/** Актор-цель. Запись живёт, пока он жив. */
	TWeakObjectPtr<AActor> Anchor;

	FText Label;
	EObjectivePointerRule Rule = EObjectivePointerRule::Always;
	EObjectivePointerTone Tone = EObjectivePointerTone::Normal;

	/**
	 * Показывать остаток ходов рядом со стрелкой. Прямой аналог `CounterValue`
	 * у `XComGameState_IndicatorArrow` — «сколько ходов до взрыва» читается прямо
	 * на указателе, а не только в отдельном блоке HUD.
	 */
	bool bShowTurnsRemaining = false;
};

/**
 * УКАЗАТЕЛЬ НА ЦЕЛЬ МИССИИ: куда идти, когда цель не в кадре.
 *
 * Перенос механики XCOM 2 (`UISpecialMissionHUD_Arrows` +
 * `XComGameState_IndicatorArrow`): стрелка к актору или точке, прижатая к краю
 * экрана с отступом `ScreenEdgePadding`, с необязательным счётчиком и цветом по
 * состоянию. Скрывается, пока поднято меню прицеливания — там кадр занят
 * выстрелом, и посторонние указатели мешают.
 *
 * ⚠️ Это НЕ часть тумана войны и не спрашивает у него разрешения. Цели миссии в
 * XCOM 2 не прячутся вообще (`XComGameState_InteractiveObject::ForceModelVisible()`
 * безусловно `eForceVisible`, а зона эвакуации вовсе вне системы видимости), и у
 * нас так же: туман скрывает противника, а не задачу. Связь односторонняя —
 * туман про эти акторы ничего не знает, потому что у них нет
 * `UFogRevealableComponent`.
 */
UCLASS()
class XRU1_API UObjectivePointerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UObjectivePointerSubsystem* Get(const UObject* WorldContext);

	/**
	 * Зарегистрировать цель. Повторный вызов с тем же `Id` обновляет запись —
	 * так постановка может менять подпись или тон, не заботясь о дублях.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Objectives")
	void RegisterObjective(FName Id, AActor* Anchor, const FText& Label,
		EObjectivePointerRule Rule, EObjectivePointerTone Tone, bool bShowTurnsRemaining = false);

	/** Снять указатель. Безопасно вызывать для незарегистрированного Id. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Objectives")
	void UnregisterObjective(FName Id);

	/** Снять все указатели: новый запуск сценария не наследует чужие цели. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Objectives")
	void ClearAllObjectives();

	/** Записи для отрисовки (читает Slate-оверлей в Paint). */
	const TArray<FObjectivePointerEntry>& GetEntries() const { return Entries; }

	/** Показывать ли указатели прямо сейчас (не занят ли кадр прицеливанием). */
	bool ShouldDrawNow() const;

	/** Остаток ходов до взрыва (−1 — таймера нет). Для счётчика у стрелки. */
	int32 GetTurnsRemaining() const;

	/** Итоговая точка цели в мире (с подъёмом над актором) и её актуальность. */
	bool ResolveWorldPoint(const FObjectivePointerEntry& Entry, FVector& OutPoint) const;

	// --- UTickableWorldSubsystem ---
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	/** Тема оформления; nullptr допустим — работают дефолты полей темы. */
	const UTacticalHUDStyleData* ResolveTheme() const;

private:
	/** Ленивая установка оверлея в viewport. */
	void EnsureOverlay();

	/** Выполнено ли правило показа записи (и жив ли ещё её актор). */
	bool IsEntryActive(const FObjectivePointerEntry& Entry) const;

	TSharedPtr<SObjectivePointerOverlay> Overlay;
	TArray<FObjectivePointerEntry> Entries;
};

/**
 * Slate-оверлей указателей: проецирует точку цели в экран; если она в кадре —
 * рисует метку над целью, если нет — прижимает стрелку к краю и поворачивает её
 * в сторону цели. Один виджет на World, без детей — всё в OnPaint, как у
 * `SCombatFloatingTextOverlay`.
 */
class XRU1_API SObjectivePointerOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SObjectivePointerOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObjectivePointerSubsystem>, OwnerSubsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakObjectPtr<UObjectivePointerSubsystem> OwnerSubsystem;
};
