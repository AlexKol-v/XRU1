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

	/**
	 * СТОРОНА УКРЫТИЯ: −1 — стена слева, +1 — справа, 0 — края нет. Именно по
	 * ней ABP берёт `Left`/`Right`-вариант cover-клипа.
	 *
	 * Источник — `UCoverDetectionComponent::PeekSideSign`: сторона считается
	 * из геометрии стены и края В ПРОЕКТНОЙ СТОЙКЕ (боец вдоль стены лицом к
	 * краю) и НЕ зависит от фактического поворота актора. Вывод из
	 * `CoverDirectionLocal.Y` вырождается в 0, если после settlement юнит стоит
	 * не вдоль стены, — и выглядывание блокируется навсегда.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	float PeekSideLocal = 0.f;

	/**
	 * ДОВОРОТ НА МЕСТЕ: на сколько градусов юнит начал разворачиваться, не сходя
	 * с клетки (знак: `+` вправо, `−` влево; 0 — не разворачивается). Снимок в
	 * момент СТАРТА доворота — по нему ABP выбирает клип `Turn_045/090/135/180`
	 * и сторону; сам поворот дальше идёт плавно со скоростью `TurnInPlaceRate`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	float PendingTurnYaw = 0.f;

	/**
	 * ПОРА ВЫГЛЯНУТЬ из укрытия (короткое озирание в ожидании). Держится
	 * `CoverPeekDuration` секунд, потом гаснет до следующего раза.
	 *
	 * ⚠️ Таймер живёт в C++ намеренно. В ABP его пришлось бы строить на узле
	 * `Current State Time`, привязанном к КОНКРЕТНОЙ стейт-машине: ошибиться
	 * машиной там легко (время чужого состояния растёт и не сбрасывается), и
	 * тогда условие перехода залипает истинным — юнит уходит в выглядывание
	 * каждый кадр и мигает между позами.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	bool bShouldPeek = false;

	/**
	 * Юнит сейчас перемещается: приказ по навмешу (path following) ИЛИ короткий
	 * подшаг к стене (`HugCover`). Подшаг включён сюда намеренно — иначе юнит
	 * ехал бы к стене в статичной позе укрытия вместо шага.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	bool bMoving = false;

	/** Юнит — сторона игрока (для разных наборов анимаций/скинов). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Visual")
	bool bPlayerSide = false;
};
