#pragma once

#include "CoreMinimal.h"
#include "CoverTypes.h"
#include "UnitVisualState.generated.h"

/**
 * ПОЗА юнита — то, что он делает ПОСТОЯННО, пока не сменится состояние.
 * Читается стейт-машиной Anim Blueprint.
 *
 * ⚠️ Поза ≠ действие. Действие («выстрелил», «получил попадание») длится один
 * раз и играется МОНТАЖОМ через Default Slot; смешивать их в одну машину —
 * классический способ получить залипшие состояния.
 */
UENUM(BlueprintType)
enum class EUnitPose : uint8
{
	/** Стоит открыто. */
	Stand      UMETA(DisplayName = "Stand"),
	/** Бежит (locomotion управляется скоростью). */
	Moving     UMETA(DisplayName = "Moving"),
	/** Сидит за низким укрытием. */
	CrouchCover UMETA(DisplayName = "Crouch (half cover)"),
	/** Вжат в высокое укрытие. */
	HighCover  UMETA(DisplayName = "Pressed (full cover)"),
	/** Наблюдение: оружие вскинуто. */
	Overwatch  UMETA(DisplayName = "Overwatch"),
	/** Глухая оборона: максимально закрылся. */
	Hunkered   UMETA(DisplayName = "Hunkered down"),
	/** Тяжело ранен, лежит. */
	Downed     UMETA(DisplayName = "Downed"),
	/** Мёртв. */
	Dead       UMETA(DisplayName = "Dead")
};

/**
 * ЕДИНАЯ ТОЧКА СОСТОЯНИЯ ЮНИТА ДЛЯ АНИМАЦИЙ (фаза S2).
 *
 * ⚠️ Зачем структура, а не «пусть ABP сам всё опросит». До неё состояние было
 * размазано по шести местам: теги ASC (`State.Overwatch`/`HunkeredDown`/
 * `Taunting`), `bIsDead`/`bIsDowned` на `AUnitBase`, `BestCoverAround` и
 * `BestCoverDirection` на компоненте укрытий, статус path following у
 * AI-контроллера, `EFiringStance` из боевых статиков. Anim Blueprint, который
 * опрашивает шесть источников, неизбежно рассинхронизируется с игрой: часть
 * данных обновляется по событию, часть — каждый кадр.
 *
 * Теперь источник ОДИН: `AUnitBase::GetVisualState()`. Он собирается в момент
 * `NotifyUnitStateChanged` (то есть ровно тогда, когда что-то изменилось) и
 * читается ABP одним узлом.
 */
USTRUCT(BlueprintType)
struct FUnitVisualState
{
	GENERATED_BODY()

	/** Поза для стейт-машины. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	EUnitPose Pose = EUnitPose::Stand;

	/** Локальное укрытие юнита (для выбора варианта позы и подсказок). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	ECoverType Cover = ECoverType::None;

	/**
	 * Направление ОТ юнита К стене, за которой он прячется (мировое, XY).
	 * Нулевой — укрытия нет. Нужно, чтобы боец прижимался к стене НУЖНОЙ
	 * стороной, а не «в среднем присел».
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	FVector CoverDirection = FVector::ZeroVector;

	/**
	 * Та же стена в ЛОКАЛЬНЫХ координатах юнита: X — вперёд, Y — вправо.
	 * ABP удобнее работать с ней: «стена справа» — это `CoverDirectionLocal.Y > 0`
	 * независимо от того, куда повёрнут юнит и куда смотрит камера.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	FVector CoverDirectionLocal = FVector::ZeroVector;

	/** Юнит сейчас перемещается по приказу (не по velocity — по path following). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	bool bMoving = false;

	/** Юнит — сторона игрока (для разных наборов анимаций/скинов). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	bool bPlayerSide = false;
};
