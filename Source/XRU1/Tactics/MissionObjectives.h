#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MissionObjectives.generated.h"

class AUnitBase;
class UStaticMeshComponent;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDefuseProgress, int32, Current, int32, Required);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBombDisarmed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnitEvacuated, AUnitBase*, Unit);

/**
 * Цель миссии «бомба» (GDD §5.7): юнит вплотную тратит 1 AP на действие
 * «Обезвредить»; нужно RequiredActions таких действий (одним юнитом за два
 * хода или двумя за один). По завершении — OnDisarmed: GameMode снимает
 * таймер ходов и активирует зоны эвакуации.
 */
UCLASS(Blueprintable)
class XRU1_API ABombObjective : public AActor
{
	GENERATED_BODY()

public:
	ABombObjective();

	/** Радиус, с которого юнит может работать с зарядом (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Objective", meta = (ClampMin = "0"))
	float InteractRadius = 200.f;

	/** Сколько действий «Обезвредить» нужно суммарно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Objective", meta = (ClampMin = "1"))
	int32 RequiredActions = 2;

	UFUNCTION(BlueprintPure, Category = "Tactics|Objective")
	bool IsDisarmed() const { return bDisarmed; }

	UFUNCTION(BlueprintPure, Category = "Tactics|Objective")
	int32 GetDefuseProgress() const { return DefuseProgress; }

	/** Может ли юнит обезвреживать прямо сейчас (жив, рядом, есть AP, не обезврежено). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Objective")
	bool CanDefuse(const AUnitBase* Unit) const;

	/**
	 * Действие «Обезвредить»: списывает 1 AP, двигает прогресс; при завершении
	 * шлёт OnDisarmed. Возвращает true, если действие выполнено.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Objective")
	bool TryDefuse(AUnitBase* Unit);

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Objective")
	FOnDefuseProgress OnDefuseProgress;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Objective")
	FOnBombDisarmed OnDisarmed;

protected:
	/** BP-хук шага обезвреживания (звук/анимация; bComplete — заряд снят). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Objective")
	void OnDefuseStep(AUnitBase* Unit, bool bComplete);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Objective")
	TObjectPtr<USceneComponent> Root;

	/** Меш заряда (назначается в BP). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Objective")
	TObjectPtr<UStaticMeshComponent> Mesh;

	int32 DefuseProgress = 0;
	bool bDisarmed = false;
};

/**
 * Зона эвакуации (GDD §5.7): неактивна до выполнения цели миссии; после
 * активации юнит внутри зоны тратит 1 AP на действие «Эвакуация» и покидает
 * поле. GameMode следит, когда эвакуированы все живые.
 */
UCLASS(Blueprintable)
class XRU1_API AEvacZone : public AActor
{
	GENERATED_BODY()

public:
	AEvacZone();

	/** Радиус зоны (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Evac", meta = (ClampMin = "0"))
	float ZoneRadius = 400.f;

	/** Активна ли зона с самого старта (туториал включает скриптом позже). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|Evac")
	bool bActiveFromStart = false;

	UFUNCTION(BlueprintPure, Category = "Tactics|Evac")
	bool IsActive() const { return bActive; }

	/** Включает зону (дым-маркер и доступность действия «Эвакуация»). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Evac")
	void ActivateZone();

	/** Находится ли юнит в зоне. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Evac")
	bool IsUnitInside(const AUnitBase* Unit) const;

	/** Может ли юнит эвакуироваться прямо сейчас (зона активна, внутри, есть AP). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Evac")
	bool CanEvacuate(const AUnitBase* Unit) const;

	/** Действие «Эвакуация»: 1 AP, юнит покидает поле. true — успех. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Evac")
	bool TryEvacuate(AUnitBase* Unit);

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Evac")
	FOnUnitEvacuated OnUnitEvacuated;

protected:
	virtual void BeginPlay() override;

	/** BP-хук активации зоны (включить дым/декаль/звук сирены). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactics|Evac")
	void OnZoneActivated();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Evac")
	TObjectPtr<USceneComponent> Root;

	bool bActive = false;
};
