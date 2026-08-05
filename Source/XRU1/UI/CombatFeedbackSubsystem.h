#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Widgets/SLeafWidget.h"
#include "CombatFeedbackSubsystem.generated.h"

class UTacticalHUDStyleData;
class SCombatFloatingTextOverlay;

/** Тип всплывающей надписи (цвет/размер берутся из темы по типу). */
UENUM(BlueprintType)
enum class ECombatFeedbackKind : uint8
{
	Damage,
	Heal,
	Miss,
	Status
};

/** Одна живущая надпись. Якорь слежения — актор; после его смерти позиция замирает. */
struct FCombatFloatingTextEntry
{
	FText Text;
	FLinearColor Color = FLinearColor::White;
	int32 FontSize = 16;
	double SpawnTime = 0.0;
	float Duration = 1.4f;
	TWeakObjectPtr<AActor> Anchor;
	FVector LastWorldPos = FVector::ZeroVector;
	/** Небольшой сдвиг по X, чтобы одновременные надписи не сливались. */
	float JitterX = 0.f;
};

/**
 * Всплывающий боевой фидбек над юнитами: урон, лечение, «ПРОМАХ», статусы
 * (09_UI_HUD §4). Чистый Slate поверх viewport по образцу STutorialHintOverlay —
 * WBP не требуется, настройки — в UTacticalHUDStyleData («08. Боевой фидбек»).
 *
 * Вызывается из подтверждённых точек механики (ResolveShotMechanics, heal,
 * overwatch), а не из montage/OnClicked — отменённое действие не всплывёт.
 */
UCLASS()
class XRU1_API UCombatFeedbackSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Хелпер для статиков боевой механики; nullptr вне игрового World. */
	static UCombatFeedbackSubsystem* Get(const UObject* WorldContext);

	/** Фактически снятые HP после всех mitigation/clamp (округляются до целого). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Feedback")
	void ShowDamage(AActor* Target, float Amount);

	/** Число лечения над целью (зелёное, со знаком «+»). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Feedback")
	void ShowHeal(AActor* Target, float Amount);

	/** «ПРОМАХ» над целью, по которой не попали. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Feedback")
	void ShowMiss(AActor* Target);

	/** Произвольная статусная надпись («НАБЛЮДЕНИЕ», «НЕДОСТУПНО»…). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Feedback")
	void ShowStatusText(AActor* Target, const FText& Text);

	/** Записи для отрисовки (читает Slate-оверлей в Paint). */
	const TArray<FCombatFloatingTextEntry>& GetEntries() const { return Entries; }

	// --- UTickableWorldSubsystem ---
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	void PushEntry(AActor* Target, const FText& Text, ECombatFeedbackKind Kind);

	/** Ленивая установка оверлея в viewport (ZOrder над трекером обучения). */
	void EnsureOverlay();

	/** Тема из GameInstance; nullptr допустим — работают дефолты полей темы. */
	const UTacticalHUDStyleData* ResolveTheme() const;

	TSharedPtr<SCombatFloatingTextOverlay> Overlay;
	TArray<FCombatFloatingTextEntry> Entries;

	/** Счётчик для чередования джиттера одновременных надписей. */
	int32 SpawnCounter = 0;
};

/**
 * Slate-оверлей отрисовки всплывающих надписей: проецирует мировую позицию
 * якоря в экран и рисует текст с подъёмом и затуханием. Один виджет на World,
 * без детей — всё в OnPaint.
 */
class XRU1_API SCombatFloatingTextOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatFloatingTextOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCombatFeedbackSubsystem>, OwnerSubsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakObjectPtr<UCombatFeedbackSubsystem> OwnerSubsystem;
};
