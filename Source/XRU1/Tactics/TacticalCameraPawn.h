#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TacticalCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UPostProcessComponent;
class UMaterialInterface;

/**
 * Камера тактического боя (XCOM-стиль): панорамирование по земле, поворот
 * шагами по 45°, зум пружиной. Пешка игрока в боевых уровнях; ввод шлёт
 * ATacticalPlayerController.
 */
UCLASS(Blueprintable)
class XRU1_API ATacticalCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ATacticalCameraPawn();

	/** Панорамирование в плоскости земли (ось X — вправо, Y — вперёд относительно камеры). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddPanInput(const FVector2D& Input);

	/** Поворот на шаг: Direction > 0 — по часовой (E), < 0 — против (Q). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddRotationStep(float Direction);

	/** Зум: положительное значение — приблизить. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddZoomInput(float Input);

	/** Перелететь к актору: плавный полёт (XCOM) или мгновенно (bInstant). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FocusOnActor(const AActor* Target, bool bInstant = false);

	/** Перелететь к точке в плоскости земли (плавно; bInstant — телепорт). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FocusOnLocation(const FVector& Location, bool bInstant = false);

	/**
	 * Следовать за актором (бегущий свой юнит / действующий враг), пока не
	 * вызван ClearFollowTarget или игрок не двинул камеру сам (XCOM: ручная
	 * панорама разрывает follow).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void SetFollowTarget(const AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void ClearFollowTarget();

	/**
	 * КАДР ПРИЦЕЛИВАНИЯ (XCOM): камера разворачивается вдоль оси стрелок→цель и
	 * наезжает, чтобы читались обе фигуры. Держится, пока не позвали
	 * ClearShotFraming — им пользуется режим выбора цели.
	 *
	 * Точку смотрения смещаем к ЦЕЛИ (ShotFrameTargetBias): игроку важнее видеть,
	 * в кого он стреляет и в каком та укрытии, чем собственную спину.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FrameShot(const AActor* Shooter, const AActor* Target);

	/**
	 * То же, но на время (сек): кадр самого выстрела — держится, пока летит
	 * пуля и играет реакция, затем камера сама возвращает прежние поворот и
	 * зум. Зовётся и для выстрела игрока, и для выстрела врага — иначе игрок
	 * не видит, в кого враг стреляет.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FrameShotForDuration(const AActor* Shooter, const AActor* Target, float Duration);

	/** Снимает кадр прицеливания и возвращает прежние поворот/зум. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void ClearShotFraming();

	UFUNCTION(BlueprintPure, Category = "Tactics|Camera")
	bool IsFramingShot() const { return bShotFraming; }

	/**
	 * Бросить кадр БЕЗ возврата прежнего ракурса — на смену фазы/ручной ввод.
	 * ClearShotFraming вернул бы поворот/зум и тем самым перечеркнул новое
	 * состояние камеры, которое как раз и наступает.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AbandonShotFraming();

	// --- Дизайнерские параметры ----------------------------------------------

	/** Скорость панорамирования (см/сек на единицу ввода). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float PanSpeed = 2000.f;

	/** Шаг поворота (град). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float RotationStep = 45.f;

	/** Скорость интерполяции поворота/зума. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float InterpSpeed = 8.f;

	/** Скорость полёта камеры к цели фокуса/следования (VInterpTo). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float FocusInterpSpeed = 8.f;

	/** Пределы длины пружины (зум), см. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float MinZoom = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float MaxZoom = 2600.f;

	/** Шаг зума за тик колеса, см. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float ZoomStep = 300.f;

	// --- Кадр выстрела/прицеливания (XCOM) ------------------------------------

	/** Длина пружины в кадре выстрела (см) — наезд. Зажимается в [MinZoom, MaxZoom]. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot")
	float ShotFrameZoom = 900.f;

	/**
	 * Насколько точка смотрения смещена от стрелка к цели (0 — на стрелке,
	 * 1 — на цели). Чуть за середину: в кадре обе фигуры, но цель крупнее.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0", ClampMax = "1"))
	float ShotFrameTargetBias = 0.65f;

	/**
	 * Доворот камеры от оси стрелок→цель (град). Строго вдоль оси цель
	 * закрывается спиной стрелка — уводим вбок, получая ракурс «из-за плеча».
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot")
	float ShotFrameYawOffset = 35.f;

	/** Сколько держать кадр самого выстрела по умолчанию (сек). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameDuration = 1.6f;

	/**
	 * Post-process материал обводки юнитов (M_OutlinePP: edge-detect по Custom
	 * Stencil). Вешается блендаблом на unbound-PostProcessComponent пешки —
	 * PostProcessVolume на карте не нужен.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Camera")
	TObjectPtr<UMaterialInterface> OutlineMaterial;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Глобальный (unbound) пост-процесс пешки: несёт обводку юнитов. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Целевые значения для плавной интерполяции. */
	float TargetYaw = 45.f;
	float TargetZoom = 1800.f;

	// --- Фокус/следование (XCOM-полёт камеры) ---------------------------------

	/** Актор, за которым камера следует каждый тик (невалиден = не следуем). */
	TWeakObjectPtr<const AActor> FollowTarget;

	/** Точка, к которой камера летит (валидна при bHasFocusGoal). */
	FVector FocusGoal = FVector::ZeroVector;
	bool bHasFocusGoal = false;

	// --- Кадр выстрела --------------------------------------------------------

	/** Камера сейчас в кадре выстрела/прицеливания. */
	bool bShotFraming = false;

	/** Остаток времени кадра (< 0 — держать до ClearShotFraming). */
	float ShotFrameTimeLeft = -1.f;

	/** Поворот и зум ДО кадра — вернём их по выходу (игрок не теряет свой ракурс). */
	float PreShotYaw = 45.f;
	float PreShotZoom = 1800.f;

	/** Общая часть FrameShot/FrameShotForDuration. */
	void EnterShotFraming(const AActor* Shooter, const AActor* Target, float Duration);
};
