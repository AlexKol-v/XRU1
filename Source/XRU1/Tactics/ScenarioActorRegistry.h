#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScenarioActorRegistry.generated.h"

class UBillboardComponent;

/**
 * Стабильный идентификатор актора сценария. Имена в World Outliner меняются при
 * копировании и переименовании, поэтому StateTree, Action Gate и scripted-шаги
 * обращаются к акторам ТОЛЬКО через AnchorId; имя остаётся диагностикой.
 *
 * Компонент вешается на юнита, голограмму, зону, якорь камеры или точку маршрута
 * внутри scenario sublevel.
 */
UCLASS(ClassGroup = (Tactics), meta = (BlueprintSpawnableComponent),
	HideCategories = (Activation, Cooking, AssetUserData, Replication))
class XRU1_API UScenarioActorIdComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScenarioActorIdComponent();

	/** Уникальный в пределах scenario sublevel идентификатор (например, Holo_A_OpenField). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	FName AnchorId;

	/**
	 * Актор физически размещён, но выключен до своего шага: скрыт, без коллизии,
	 * не тикает и не попадает в стороны боя. Одного HiddenInGame недостаточно —
	 * collision, perception и AI продолжали бы работать.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	bool bStartDeactivated = false;

	/**
	 * Боец выходит в бой сразу «тяжело раненым» — урок подъёма (шаг A9).
	 * Применяется один раз, при первом включении актора, поэтому подъём медиком
	 * не отменяется повторной активацией.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	bool bStartDowned = false;

	/** Участвует ли актор в сценарии прямо сейчас. */
	UFUNCTION(BlueprintPure, Category = "Scenario")
	bool IsScenarioActive() const { return bScenarioActive; }

	/** Вызывается после смены состояния — для BP-косметики (VFX включения голограммы). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Scenario")
	void OnScenarioActiveChanged(bool bNowActive);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool bScenarioActive = true;
	bool bDownedApplied = false;

	friend class UTacticalScenarioSubsystem;
};

/**
 * Реестр акторов текущего запуска сценария и единая точка их включения.
 *
 * WorldSubsystem: он рождается и умирает вместе с World, поэтому после travel и
 * retry реестр гарантированно пуст и не содержит указателей прошлого запуска.
 */
UCLASS()
class XRU1_API UTacticalScenarioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Первый актор с этим AnchorId (nullptr — не найден). */
	UFUNCTION(BlueprintPure, Category = "Scenario",
		meta = (WorldContext = "WorldContextObject"))
	static AActor* FindScenarioActorInWorld(const UObject* WorldContextObject, FName AnchorId);

	UFUNCTION(BlueprintPure, Category = "Scenario")
	AActor* FindScenarioActor(FName AnchorId) const;

	/** Все акторы с этим AnchorId — для групп (набор голограмм, набор якорей). */
	UFUNCTION(BlueprintPure, Category = "Scenario")
	TArray<AActor*> FindScenarioActors(FName AnchorId) const;

	/** AnchorId актора; NAME_None, если компонента нет. */
	UFUNCTION(BlueprintPure, Category = "Scenario")
	static FName GetScenarioAnchorId(const AActor* Actor);

	/** Участвует ли актор в сценарии сейчас. Актор без компонента — всегда да. */
	UFUNCTION(BlueprintPure, Category = "Scenario")
	static bool IsActorScenarioActive(const AActor* Actor);

	/**
	 * Единая точка включения/выключения staged-актора: presentation, collision,
	 * tick и участие в сторонах боя меняются вместе. Возвращает число изменённых
	 * акторов.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scenario")
	int32 SetScenarioActorActive(FName AnchorId, bool bActive);

	/** То же для конкретного актора. */
	UFUNCTION(BlueprintCallable, Category = "Scenario")
	bool SetActorScenarioActive(AActor* Actor, bool bActive);

	void RegisterScenarioActor(UScenarioActorIdComponent* Component);
	void UnregisterScenarioActor(UScenarioActorIdComponent* Component);

	/** Одноразовый перевод бойца в Downed по флагу компонента (урок подъёма). */
	void ApplyStartDowned(UScenarioActorIdComponent* Component);

private:
	/** MultiMap: один AnchorId может обозначать группу (Move_C2, Spawn_Enemy_Center). */
	TMultiMap<FName, TWeakObjectPtr<UScenarioActorIdComponent>> Registry;
};

/**
 * Пустой якорь сценария: точка камеры, разрешённая точка перемещения, вершина
 * маршрута врага. Ничего не делает сам — существует ради стабильного AnchorId и
 * трансформа, на которые ссылаются StateTree-задачи и Action Gate.
 */
UCLASS(Blueprintable)
class XRU1_API AScenarioAnchorPoint : public AActor
{
	GENERATED_BODY()

public:
	AScenarioAnchorPoint();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scenario")
	TObjectPtr<UScenarioActorIdComponent> ScenarioId;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif
};
