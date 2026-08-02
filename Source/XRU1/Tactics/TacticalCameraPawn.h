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

	/**
	 * Перевзвод шага A1: сбрасывает one-shot и накопители поворота/зума.
	 * Вызывается при применении политики шага обучения — «осмотритесь»
	 * отсчитывается от начала шага, а не от загрузки карты, иначе вращение
	 * камеры во время стриминга закрыло бы one-shot до того, как его ждут.
	 */
	void ReArmCameraAdjustedEvent();

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
	 * в кого он стреляет и в каком та укрытии, чем собственную спину. Позиция
	 * камеры при этом ГАРАНТИРУЕТ видимость цели и то, что обе фигуры влезают в
	 * кадр — см. блок параметров `Tactics|Camera|Shot`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FrameShot(const AActor* Shooter, const AActor* Target);

	/**
	 * КАДР ПРЕЗЕНТАЦИИ выстрела (Duration в сек; −1 — держать до
	 * ClearShotFraming). Зовётся и для выстрела игрока, и для выстрела врага, и
	 * для реакции наблюдения — иначе игрок не видит, в кого стреляют.
	 *
	 * ⚠️ В отличие от кадра прицеливания этот кадр МОНОПОЛЕН: пока он живёт,
	 * фоновые интенты взгляда (фокус на выбранном бойце, подхват вышедшего из-за
	 * угла врага, follow чужого хода) НЕ выполняются сразу, а запоминаются и
	 * применяются в момент снятия кадра. Без этого произвольный тик уводил
	 * камеру посреди выстрела — «камера улетает раньше, чем напишет урон» и
	 * «реакция наблюдения без кадра» (оба пойманы в логе 2026-08-02).
	 * Ручной ввод игрока и новый кадр монополию перебивают.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FrameShotForDuration(const AActor* Shooter, const AActor* Target, float Duration);

	/** Живёт ли монопольный кадр презентации (см. FrameShotForDuration). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Camera")
	bool IsPlayingPresentationFrame() const { return bShotFraming && bPresentationFrame; }

	/**
	 * РЕЖИССЁРСКИЙ ФОКУС: навести камеру и удержать взгляд, пока такт не
	 * закончится (`ReleaseDirectorHold`). Пока удержание активно, фоновые
	 * интенты (автовыбор следующего бойца, подхват врага) откладываются — тем
	 * же механизмом, что и в кадре выстрела.
	 *
	 * Иначе показ «куда идти» не работает в принципе: в логе D1 такт навёл
	 * камеру на зону эвакуации, и в том же кадре дозревший автопереход выбора
	 * бойца увёл её обратно к Танку — игрок зоны не увидел.
	 *
	 * `HoldDuration` (сек) — СТРАХОВКА: удержание снимается само, даже если
	 * владелец забыл его отпустить. Без неё один незакрытый такт вешал камеру
	 * на весь шаг — «камера перестала фокусироваться на юнитах» (лог C0:
	 * ExitState задачи такта наступает только при выходе из СОСТОЯНИЯ, а шаг
	 * живёт до выполнения игроком всех целей). ≤ 0 — держать до явного снятия.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FocusOnLocationDirected(const FVector& Location, float HoldDuration = -1.f);

	/** Снять режиссёрское удержание и исполнить накопленный фоновый интент. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void ReleaseDirectorHold();

	/**
	 * Игрок взял камеру сам (панорама/поворот/зум): удержание снимается, а
	 * накопленный фоновый интент ВЫБРАСЫВАЕТСЯ — иначе камера прыгнула бы из-под
	 * руки игрока к отложенной цели.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void BreakDirectorHold();

	/** Снимает кадр и возвращает постоянный пользовательский ракурс и позицию до кадра. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void ClearShotFraming();

	UFUNCTION(BlueprintPure, Category = "Tactics|Camera")
	bool IsFramingShot() const { return bShotFraming; }

	/**
	 * Играется ли конечный по времени кадр самого выстрела. В отличие от
	 * IsFramingShot не считает бессрочное прицеливание: автопереход бойца/хода
	 * должен ждать кинематографичный выстрел, но не может зависнуть на aim-mode.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Camera")
	bool IsPlayingShotFrame() const { return bShotFraming && ShotFrameTimeLeft >= 0.f; }

	/**
	 * Камера ДЕРЖИТ кадр прицеливания (бессрочно), а не проигрывает кадр
	 * выстрела (с таймером). Нужно единому источнику правды режима: выход из
	 * прицеливания возвращает камеру, а выход из-за выстрела — нет (кадр
	 * выстрела сам вернёт по своему таймеру).
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Camera")
	bool IsHoldingAimFrame() const { return bShotFraming && ShotFrameTimeLeft < 0.f; }

	/**
	 * Бросить кадр перед новым focus/follow/pan: позицию до кадра не возвращает,
	 * но временные yaw/zoom/pitch всегда заменяет постоянным тактическим ракурсом.
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

	// --- Кадр выстрела/прицеливания «из-за плеча» (XCOM 2 action cam) ---------
	//
	// МОДЕЛЬ (переписана 2026-07-25). Камера строится НАПРЯМУЮ в мировых
	// координатах — позиция и точка взгляда, — а не подбором «yaw + длина
	// пружины + наклон». Пружина потом получает ровно те yaw/pitch/длину, при
	// которых её конец попадает в вычисленную точку, поэтому расчёт и
	// результат больше не могут разойтись.
	//
	// Почему прежняя схема разъезжалась (три доказуемых дефекта):
	//  1. Кадр СЧИТАЛСЯ от `ShooterLocation.Z`, а СТРОИЛСЯ от Z пешки-камеры
	//     (`TargetOffset.Z` прибавлялся к её собственной высоте, а она никогда
	//     не менялась). На перепаде высот безопасная длина пружины и наклон
	//     считались для одной точки, а камера смотрела из другой.
	//  2. Плечевой доворот задавался УГЛОМ. Боковое разведение фигур в кадре
	//     пропорционально дистанции: на 10 м 28° разводят, а в упор (1 м) цель
	//     остаётся ровно за спиной стрелка. Отсюда «врага вообще не видно».
	//     Теперь вынос задан в СМ и в упор даёт большой угол сам собой.
	//  3. Не проверялось, видна ли ЦЕЛЬ из выбранной позиции — только не стоит
	//     ли камера за стеной. Теперь перебирается лестница кандидатов
	//     (плечо × подъём), и берётся та, откуда читаются обе фигуры.

	/** Высота точки прицела над ActorLocation юнита (см) — линия груди. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot")
	float ShotFrameAimHeight = 15.f;

	/**
	 * Дальняя дистанция стрелок→цель (см), при которой параметры композиции
	 * достигают «дальних» значений. Между 0 и этой — линейная интерполяция:
	 * близкий выстрел показывается плечом крупно, дальний — отъездом.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "1"))
	float ShotFrameFarDistance = 1400.f;

	/**
	 * Вынос камеры НАЗАД за спину стрелка (см): ближний / дальний выстрел.
	 *
	 * ⚠️ Числа 2026-07-25 уменьшены втрое по замечанию игрока: «в XCOM ракурс
	 * буквально в 20–30 см позади плеча». Было 240/780 — это не «из-за плеча», а
	 * «сверху сзади», и стрелок в кадре превращался в точку. Настоящий OTS: спина
	 * стрелка занимает угол кадра и служит рамкой, цель — в фокусе. Отъезд на
	 * дальних выстрелах теперь делает не эта константа, а решение
	 * `FitSubjectsInFrame` — оно отъезжает ровно настолько, чтобы обе фигуры
	 * влезли, и ни на сантиметр больше.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameBackNear = 85.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameBackFar = 190.f;

	/**
	 * Боковой вынос камеры от оси стрелок→цель (СМ, не градусы) — «плечо».
	 * В упор именно он открывает цель из-за спины стрелка. Порядок величины —
	 * ширина плеча плюс немного (капсула ~34 см радиусом).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameShoulderNear = 75.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameShoulderFar = 95.f;

	/** Подъём камеры над линией прицела (см): ближний / дальний выстрел. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameHeightNear = 45.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameHeightFar = 110.f;

	/** Точка взгляда между стрелком и целью (0 — стрелок, 1 — цель): близко / далеко. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0", ClampMax = "1"))
	float ShotFrameTargetBias = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0", ClampMax = "1"))
	float ShotFrameTargetBiasFar = 0.5f;

	/**
	 * Дистанция (см), дальше которой кадр строится ВОКРУГ ЦЕЛИ, а не из-за
	 * плеча стрелка.
	 *
	 * Так делает XCOM на squadsight-выстрелах, и по той же причине: на 35 м
	 * «из-за плеча» показывает спину стрелка крупно и цель в несколько
	 * пикселей где-то в дымке — попадание, цифру урона и смерть цели зритель
	 * просто не видит (жалоба «снайпер стреляет странно», лог 2026-08-02:
	 * dist=3588, штраф 64 = цель вообще закрыта). Дальний кадр ставит камеру
	 * рядом с целью на линии выстрела: стрелок остаётся за камерой, зато виден
	 * тот, в кого прилетает. Значение по умолчанию — чуть ниже обзора отряда
	 * (2500 см): дальше него цель для стрелка и так «не своя».
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameTargetOnlyDistance = 2400.f;

	/** Отступ камеры от ЦЕЛИ по линии выстрела в дальнем кадре (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "100"))
	float ShotFrameLongShotBack = 700.f;

	/**
	 * Доля полу-FOV, в которую ОБЯЗАНЫ поместиться и стрелок, и цель. < 1 —
	 * запас на поля кадра и на HUD. Если не помещаются, камера автоматически
	 * отъезжает: композиция «обе фигуры читаются» гарантируется расчётом, а не
	 * подобранными вручную значениями зума.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0.3", ClampMax = "1"))
	float ShotFrameFovSafety = 0.7f;

	/** Границы длины пружины в кадре (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "100"))
	float ShotFrameMinArm = 260.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "200"))
	float ShotFrameMaxArm = 3000.f;

	/**
	 * Шаг подъёма камеры в лестнице «цель не видна» (см). Аналог того, как в
	 * XCOM камера прицеливания приподнимается над укрытием, вместо того чтобы
	 * показывать игроку стену.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameClearanceLift = 170.f;

	/** Отступ камеры от стены, в которую она упирается (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameWallPadding = 45.f;

	// --- Штрафы выбора ракурса: VERBATIM из `XComCamera.ini` -------------------
	//
	// `[XComGame.X2Camera_OverTheShoulder]`. XCOM выбирает ракурс не «правилом», а
	// перебором кандидатов со штрафами — ровно та же схема, что у нас; поэтому
	// числа переносим как есть, а не выдумываем свои.
	//
	//   TargetedLocationBlockedScorePenalty = 64   ← цель не видно: худшее из зол
	//   CantSeeFireingUnitAtAllPenalty      = 24   ← стрелка не видно вовсе
	//   FiringUnitHeadBlockedScorePenalty   = 16   ← видно, но голова закрыта
	//   FiringUnitWaistBlockedScorePenalty  = 8    ← закрыт корпус
	//   BlockedStartLocationPenalty         = 32   ← камера внутри геометрии
	//   CrosscutScorePenalty                = 1000 ← пересечение оси съёмки
	//   TraceWidth                          = 20   ← толщина луча проверки

	/** Цель не читается из позиции камеры (XCOM: 64). Главный штраф. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score")
	float PenaltyTargetBlocked = 64.f;

	/** Стрелка не видно совсем (XCOM: 24). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score")
	float PenaltyShooterNotVisible = 24.f;

	/** У стрелка закрыта голова (XCOM: 16). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score")
	float PenaltyShooterHeadBlocked = 16.f;

	/** У стрелка закрыт корпус (XCOM: 8). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score")
	float PenaltyShooterWaistBlocked = 8.f;

	/** Камера стоит внутри геометрии / отгорожена от точки взгляда (XCOM: 32). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score")
	float PenaltyBlockedStart = 32.f;

	/**
	 * ПРАВИЛО 180° (XCOM: `CrosscutScorePenalty` = 1000). Киношное «не пересекать
	 * ось»: если камера перепрыгивает на другую сторону линии стрелок→цель,
	 * зритель теряет ориентацию — фигуры меняются местами, и это читается как
	 * «камеру развернуло». Штраф заведомо перебивает любую комбинацию остальных.
	 *
	 * ⚠️ Это же и лечит жалобу «при смене юнита камера крутится на 360»: сторона
	 * съёмки теперь ЛИПКАЯ между перекадровками и меняется, только если с
	 * прежней стороны цель не видно вовсе.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score")
	float PenaltyCrosscut = 1000.f;

	/** Толщина луча проверки видимости в кадре (XCOM `TraceWidth` = 20). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score", meta = (ClampMin = "0"))
	float ShotFrameTraceWidth = 20.f;

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

	/**
	 * Постоянный пользовательский ракурс тактической камеры. Он меняется только
	 * ручным Q/E и колесом и не перезаписывается временным кадром выстрела.
	 */
	float TacticalYaw = 45.f;
	float TacticalZoom = 1800.f;

	// --- Шаг A1 обучения: подтверждённая настройка ракурса ---------------------
	//
	// Событие `Camera.Adjusted` публикуется не по raw Q/E/колесу, а после того,
	// как игрок реально изменил И поворот, И приближение сверх порога. Иначе шаг
	// закрывался бы от случайного касания колеса.

	/** На сколько градусов суммарно нужно повернуть камеру, чтобы засчитать шаг. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Tutorial",
		meta = (ClampMin = "5"))
	float AdjustedYawThreshold = 60.f;

	/** На сколько сантиметров суммарно нужно изменить дистанцию камеры. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Tutorial",
		meta = (ClampMin = "50"))
	float AdjustedZoomThreshold = 400.f;

	/** Публикует `Camera.Adjusted` один раз, когда оба порога пройдены. */
	void ReportCameraAdjustment(float YawDelta, float ZoomDelta);

	float AccumulatedYawAdjustment = 0.f;
	float AccumulatedZoomAdjustment = 0.f;
	bool bCameraAdjustmentReported = false;

	/** Текущие цели интерполяции: обычный ракурс либо временный action-camera. */
	float TargetYaw = 45.f;
	float TargetZoom = 1800.f;

	/** Наклон камеры: −55° тактический вид, вычисляется по геометрии в кадре. */
	float TargetPitch = -55.f;

	/**
	 * МИРОВАЯ высота точки вращения пружины. В `Tick` из неё каждый кадр
	 * пересчитывается `SpringArm->TargetOffset.Z` относительно ТЕКУЩЕЙ высоты
	 * пешки — только так пивот попадает туда, где его посчитали.
	 *
	 * ⚠️ Зачем вообще. Пешка-камеры по Z НЕ двигается никогда (весь `FocusOnLocation`
	 * и `Tick` сохраняют `Current.Z`), а высота её спавна — это высота
	 * `PlayerStart`. То есть до этой правки вся тактическая камера смотрела на
	 * плоскость высоты старта, а не на плоскость, где стоят бойцы, и кадр
	 * выстрела считался от Z стрелка, но строился от Z пешки. Теперь высота
	 * пивота — явное состояние: её задают focus/follow (пол юнита) и кадр
	 * выстрела (линия груди), а ручная панорама не трогает.
	 */
	float PivotWorldZ = 0.f;

	/** Высота пивота до кадра выстрела — возвращается вместе с ракурсом. */
	float PreShotPivotZ = 0.f;

	/** Предел смещения пивота от собственной высоты пешки (см) — страховка. */
	static constexpr float MaxPivotZOffset = 1200.f;

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

	/** Текущий кадр — монопольная презентация выстрела, а не прицеливание. */
	bool bPresentationFrame = false;

	/** Камеру держит режиссура такта (см. FocusOnLocationDirected). */
	bool bDirectorHold = false;

	/** Остаток страховочного удержания, сек (< 0 — до явного снятия). */
	float DirectorHoldTimeLeft = -1.f;

	// --- Отложенный интент взгляда (монополия кадра презентации) --------------
	// Фоновый запрос (фокус/следование), пришедший во время презентации, не
	// теряется и не рвёт кадр: он исполняется в момент снятия кадра. Это делает
	// владение камерой однозначным — арбитраж живёт в самой камере, а не в
	// десятке проверок `if (IsFramingShot())` по вызывающим.

	bool bHasPendingCameraIntent = false;
	bool bPendingIntentIsFollow = false;
	TWeakObjectPtr<const AActor> PendingFollowTarget;
	FVector PendingFocusLocation = FVector::ZeroVector;

	/** Отложенный фокус был мгновенным (телепорт камеры), а не полётом. */
	bool bPendingFocusInstant = false;

	/** Идёт исполнение отложенного интента — сам себя откладывать он не должен. */
	bool bApplyingPendingIntent = false;

	/** Отложенный интент поставила режиссура такта, а не фон. */
	bool bPendingIntentIsDirected = false;

	/** Текущий вызов Focus* — режиссёрский (см. FocusOnLocationDirected). */
	bool bMarkingDirectedIntent = false;

	/** Единственная проверка «камера сейчас занята и фоновый интент ждёт». */
	bool ShouldDeferAmbientIntent() const
	{
		return !bApplyingPendingIntent && (IsPlayingPresentationFrame() || bDirectorHold);
	}

	/** Применить отложенный интент (если есть) — зовётся при снятии удержания. */
	bool ApplyPendingCameraIntent();

	/**
	 * Ракурс ДО кадра для параметров, которыми игрок напрямую не управляет.
	 * Yaw/zoom хранятся отдельно в TacticalYaw/TacticalZoom и потому не могут
	 * случайно «унаследовать» временный action-camera ракурс.
	 */
	float PreShotPitch = -55.f;
	FVector PreShotLocation = FVector::ZeroVector;

	/**
	 * Общая часть FrameShot/FrameShotForDuration. `bPresentation` — монопольный
	 * кадр совершённого выстрела (см. FrameShotForDuration); выставляется здесь,
	 * ПОСЛЕ проверки участников, чтобы неудавшийся вызов не понизил живой кадр.
	 */
	void EnterShotFraming(const AActor* Shooter, const AActor* Target, float Duration,
		bool bPresentation);

	/**
	 * Свободен ли отрезок между двумя точками по геометрии мира. Юниты
	 * НАМЕРЕННО не учитываются (тот же object-query, что у линии огня): чужой
	 * боец, мелькнувший между камерой и целью, не повод перестраивать кадр.
	 */
	bool IsSegmentClear(const FVector& From, const FVector& To, float* OutBlockedFraction = nullptr) const;

	/**
	 * Отодвигает камеру назад по оси взгляда, пока ОБЕ фигуры не поместятся в
	 * безопасную долю FOV (`ShotFrameFovSafety`). Это заменяет ручной подбор
	 * «зум ближний/дальний»: композиция теперь следствие геометрии, а не таблицы.
	 */
	FVector FitSubjectsInFrame(const FVector& CamPos, const FVector& LookPoint,
		const FVector& SubjectA, const FVector& SubjectB) const;

	/** Подтягивает камеру вперёд, если между точкой взгляда и ею стена. */
	FVector PullCameraOutOfGeometry(const FVector& LookPoint, const FVector& CamPos) const;

	/**
	 * ШТРАФ кандидата по verbatim-весам XCOM (чем МЕНЬШЕ, тем лучше). Проверяет
	 * отдельно: цель, голову стрелка, его корпус, «камера в стене» и пересечение
	 * оси съёмки. Именно отсутствие раздельных проверок давало кадры, где вместо
	 * врага стена или вместо боя — спина.
	 *
	 * `bIgnoreShooter` — дальний (squadsight) кадр: стрелок в нём заведомо за
	 * камерой, и его «невидимость» не повод портить ракурс на цели.
	 */
	float ScoreShotCandidate(const FVector& CamPos, const FVector& LookPoint,
		const FVector& ShooterAim, const FVector& TargetAim, float SideSign,
		bool bIgnoreShooter) const;

	/**
	 * СТОРОНА СЪЁМКИ прошлого кадра (+1/−1) — ось правила 180°. 0 — кадра ещё не
	 * было, любая сторона допустима. Сбрасывается только сменой ПАРЫ
	 * стрелок↔цель: пока пара та же, камера обязана оставаться на своей стороне.
	 */
	float LastShoulderSign = 0.f;

	/** Пара, для которой запомнена сторона (смена пары разрешает переход через ось). */
	TWeakObjectPtr<const AActor> LastFramedShooter;
	TWeakObjectPtr<const AActor> LastFramedTarget;
};
