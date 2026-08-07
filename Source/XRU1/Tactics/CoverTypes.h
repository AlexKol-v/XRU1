#pragma once

#include "CoreMinimal.h"
#include "CoverTypes.generated.h"

/**
 * Тип укрытия юнита относительно КОНКРЕТНОГО источника угрозы (врага).
 * Определяется UCoverDetectionComponent трейсами в сторону врага.
 * None  — открыт, укрытия нет.
 * Half  — половинчатое укрытие (низкая стена): частичный бонус к защите.
 * Full  — полное укрытие (высокая стена): максимальный бонус к защите.
 */
UENUM(BlueprintType)
enum class ECoverType : uint8
{
	None UMETA(DisplayName = "No Cover"),
	Half UMETA(DisplayName = "Half Cover"),
	Full UMETA(DisplayName = "Full Cover")
};

/**
 * ОДНА СТОРОНА УКРЫТИЯ: конкретная стена, за которой юнит прячется, и её
 * ориентация. До S1 система хранила только ТИП укрытия и теряла направление —
 * из-за чего «фланг» приходилось определять одиночным лучом «есть ли стена
 * ровно в сторону стрелка».
 *
 * Модель XCOM: юнит занимает позицию у одной-двух стен, и каждая защищает
 * ПОЛУПЛОСКОСТЬ со своей стороны. Атака засчитывается как фланг, если она не
 * попадает ни в одну защитную дугу. Именно поэтому нужно направление, а не
 * только тип.
 */
USTRUCT(BlueprintType)
struct FCoverSide
{
	GENERATED_BODY()

	/** Направление ОТ юнита К стене (XY, нормализовано). Ось защитной дуги. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Cover")
	FVector Direction = FVector::ZeroVector;

	/** Что это за стена — Half или Full. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Cover")
	ECoverType Type = ECoverType::None;

	/** Дистанция до стены (см) — ближняя стена важнее дальней. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Cover")
	float Distance = 0.f;
};

/**
 * Щит цели ПРОТИВ КОНКРЕТНОГО СТРЕЛКА — то, что игрок видит над головой и в
 * панели цели. Ровно три состояния XCOM:
 *  None    — цель в чистом поле, щита нет;
 *  Covered — укрытие работает против этого стрелка (синий щит, шанс снижен);
 *  Flanked — цель В УКРЫТИИ, но стрелок зашёл сбоку, и укрытие не работает
 *            (жёлтый щит, шанс НЕ снижен).
 *
 * Важно, что состояний три, а не два: «нет синего щита» само по себе не
 * различает «открыт» и «флангирован», а игроку это различие нужно —
 * флангирование он заработал манёвром и должен его видеть.
 * Считается ЕДИНОЙ функцией UTacticsCombatStatics::GetCoverShieldAgainst.
 */
UENUM(BlueprintType)
enum class ECoverShield : uint8
{
	None    UMETA(DisplayName = "No Shield"),
	Covered UMETA(DisplayName = "Covered (blue)"),
	Flanked UMETA(DisplayName = "Flanked (yellow)")
};

/**
 * ПРЕВЬЮ ТОЧКИ ПЕРЕМЕЩЕНИЯ (XCOM: при наведении видно, во что ты вбегаешь).
 *
 * Игрок обязан знать ДО клика: сколько стволов будет смотреть на эту точку и
 * какое укрытие он там получит. Без этого выбор позиции — угадайка, а вся
 * механика укрытий работает вхолостую: её просто не видно в момент решения.
 *
 * Считается тем же кодом, что и настоящий бой (`EvaluateCoverAtLocation` +
 * `HasLineOfSightFromLocation`), поэтому превью не может соврать.
 */
USTRUCT(BlueprintType)
struct FTacticalMovePreview
{
	GENERATED_BODY()

	/** Сколько живых врагов простреливает эту точку (есть линия огня оттуда/туда). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Preview")
	int32 EnemiesSeeing = 0;

	/** От скольких из них укрытие в этой точке РАБОТАЕТ. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Preview")
	int32 EnemiesCovered = 0;

	/** От скольких точка ОТКРЫТА — именно они и будут стрелять без штрафа. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Preview")
	int32 EnemiesExposed = 0;

	/** Лучшее укрытие в точке против простреливающих (для иконки щита). */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Preview")
	ECoverType BestCover = ECoverType::None;

	/** Скольких врагов юнит будет ФЛАНКИРОВАТЬ из этой точки. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|Preview")
	int32 EnemiesFlanked = 0;
};

/**
 * Как юнит стреляет из своей текущей позиции (для анимации и превью).
 * Определяется UTacticsCombatStatics::GetFiringStance по тому, какая огневая
 * позиция дала линию огня:
 * Open      — укрытия нет, стреляет с места стоя;
 * OverCover — half: LOS из центра, привстать и выстрелить ПОВЕРХ, с места;
 * StepOut   — full: LOS только из выглядывания у края, выйти за угол и выстрелить.
 * Геймплейно юнит НЕ перемещается: стойка — это выбор монтажа/визуальный сдвиг.
 */
UENUM(BlueprintType)
enum class EFiringStance : uint8
{
	Open      UMETA(DisplayName = "Open"),
	OverCover UMETA(DisplayName = "Over Cover"),
	StepOut   UMETA(DisplayName = "Step Out")
};
