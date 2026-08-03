#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FogRevealableComponent.generated.h"

/**
 * Дизайнерский форс видимости — перевод `EForceVisibilitySetting` из XCOM 2
 * (`X2GameRulesetVisibilityInterface`). Нужен, потому что «показывать по LOS» —
 * правило по умолчанию, а не закон: сценарный такт обучения обязан показать
 * актора, которого отряд ещё не видит, а голограмма следующей секции обязана
 * оставаться скрытой, даже стоя в чистом поле.
 */
UENUM(BlueprintType)
enum class EFogVisibilityOverride : uint8
{
	/** Решает туман войны (норма). */
	Auto          UMETA(DisplayName = "Auto (по видимости отряда)"),
	/** Всегда виден: цель миссии, «известный» objective. */
	AlwaysVisible UMETA(DisplayName = "Always visible"),
	/** Всегда скрыт: актор, которым владеет чужой механизм. */
	AlwaysHidden  UMETA(DisplayName = "Always hidden")
};

/**
 * ЕДИНСТВЕННЫЙ владелец «показан ли актор игроку с точки зрения тумана войны».
 *
 * ⚠️ Почему компонент, а не вызов `SetActorHiddenInGame` по месту. В UE нет
 * одного вызова, который гасит презентацию целиком (в XCOM 2 он есть —
 * `Visualizer.SetVisibleToTeams`). Проверено по исходникам движка 5.7:
 *
 *  - `USceneComponent::ShouldRender()` поднимается по `Owner->GetParentComponent()`,
 *    поэтому `SetActorHiddenInGame` НАКРЫВАЕТ вложенные Child Actor'ы —
 *    оружие юнита (`Gun` → `BP_AssaultRifle_*`, ~40 примитивов) гасится само;
 *  - но оверхед-худ создан как `EWidgetSpace::Screen`, а экранные виджеты рисует
 *    `SWorldWidgetScreenLayer`, который на `Owner->IsHidden()` не смотрит вообще;
 *  - обводка `Custom Depth` — состояние компонента меша, её надо снимать явно,
 *    иначе скрытый враг проступит сквозь `M_OutlinePP`.
 *
 * Собрать это в одном месте — единственный способ не забыть половину при
 * следующем добавлении визуального слоя.
 *
 * Компонент НЕ трогает коллизию, тик и участие в бою. Скрытый враг обязан
 * оставаться препятствием (правило XCOM: неразведанный под блокирует тайлы),
 * иначе приказ поставит бойца внутрь противника. Полное выключение актора — это
 * другой механизм и другой владелец (`UTacticalScenarioSubsystem::SetActorScenarioActive`).
 */
UCLASS(ClassGroup = (Tactics), meta = (BlueprintSpawnableComponent), DisplayName = "Fog Revealable")
class XRU1_API UFogRevealableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFogRevealableComponent();

	/** Дизайнерский форс. `Auto` — обычный режим, решает туман. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactics|FogOfWar")
	EFogVisibilityOverride Override = EFogVisibilityOverride::Auto;

	/**
	 * Скрыт ли актор туманом ПРЯМО СЕЙЧАС. Дешёвый предикат для гейтов
	 * (ховер, звук, всплывающий текст): читает флаг, ничего не пересчитывает.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|FogOfWar")
	bool IsPresentationHidden() const { return bPresentationHidden; }

	/**
	 * То же для произвольного актора. Актор без компонента считается видимым:
	 * туман скрывает только то, что кто-то явно поручил ему скрывать.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|FogOfWar")
	static bool IsActorPresentationHidden(const AActor* Actor);

	/**
	 * Сценарное удержание «показывать»: такт обучения ведёт камеру за юнитом и
	 * обязан его показать независимо от LOS (у Firaxis это ветка `m_bInMatinee`
	 * в `ForceModelVisible`). Счётчик, а не bool: тактов может быть два внахлёст,
	 * и снятие одного не должно гасить актора под вторым.
	 */
	void AddScriptedRevealHold();
	void RemoveScriptedRevealHold();

	/**
	 * Снять ВСЕ удержания разом — только для сброса сессии тумана. Оборванный
	 * StateTree может не дойти до `ExitState`, и повисший счётчик оставил бы
	 * актора видимым в следующем запуске сценария.
	 */
	void ClearScriptedRevealHolds();

	/**
	 * Итог всех правил: что компонент ХОЧЕТ показывать при расчётной видимости
	 * `bComputedVisible`. Порядок приоритетов зафиксирован здесь и больше нигде.
	 */
	bool ResolveDesiredVisibility(bool bComputedVisible) const;

	/**
	 * Применяет решение к презентации. Возвращает true, если состояние
	 * изменилось. Скрытие может быть ОТЛОЖЕНО (см. `IsHideDeferred`).
	 */
	bool ApplyVisibility(bool bVisible);

	/**
	 * Скрытие отложено, потому что актор доигрывает действие. XCOM делает так же:
	 * `bRemovedFromPlay` прячет юнита только когда его текущий action завершён —
	 * иначе боец исчезает посреди монтажа выстрела или эвакуации.
	 */
	bool IsHideDeferred() const { return bHideDeferred; }

	/** Короткая причина последнего решения — для `LogXRU1Fog` и `xru1.Fog.Explain`. */
	const TCHAR* GetLastDecisionReason() const { return LastDecisionReason; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Можно ли прятать прямо сейчас (не играет ли актор монтаж). */
	bool CanHideNow() const;

	/** Гасит/возвращает то, что не покрывает `SetActorHiddenInGame`. */
	void ApplyExtraPresentation(bool bVisible);

	/**
	 * Один раз на актора: предупредить, если настройка тика анимации BP такова,
	 * что скрытый юнит перестанет проигрывать монтажи (а на их нотифаях висит
	 * `FireCommit` и завершение хода).
	 */
	void WarnIfHiddenBreaksAnimation();

	bool bPresentationHidden = false;
	bool bHideDeferred = false;
	bool bAnimationTickChecked = false;
	int32 ScriptedRevealHolds = 0;

	/**
	 * Когда актор впервые попросился скрыться (игровое время); −1 — не просился.
	 * По нему отсчитывается выдержка перед скрытием (`xru1.Fog.HideGrace`),
	 * гасящая мигание на границе линии обзора.
	 */
	double HideRequestedTime = -1.0;

	/**
	 * Литерал, не выделяет память; живёт в статической строке. `mutable`, потому
	 * что заполняется внутри const-решателя `ResolveDesiredVisibility`: это чисто
	 * диагностический след, а не состояние компонента.
	 */
	mutable const TCHAR* LastDecisionReason = TEXT("init");
};
