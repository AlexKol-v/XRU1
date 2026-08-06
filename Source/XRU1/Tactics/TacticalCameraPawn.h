#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TacticalCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UPostProcessComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Камера тактического боя (XCOM-стиль). Пешка игрока в боевых уровнях; ввод шлёт
 * `ATacticalPlayerController`.
 *
 * Управление собрано по образцу самого популярного камера-мода XCOM 2 (Free
 * Camera Rotation, wghost81): короткое нажатие Q/E поворачивает на шаг 45°,
 * удержание крутит непрерывно, Alt+мышь даёт свободный обзор (поворот и наклон),
 * колесо приближает и заодно опускает угол, PageUp/PageDown переключают этаж
 * обзора. Все скорости и пределы — параметры ниже; чувствительность, инверсия и
 * обзор приходят из `UTacticsUserSettings`.
 *
 * Владение камерой (кто важнее) НЕ здесь: приоритет «кадр выстрела > режиссура
 * такта > фоновые интенты» описан у `FrameShotForDuration` и
 * `FocusOnLocationDirected`, а ручной ввод рвёт любое удержание.
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

	/**
	 * СВОБОДНОЕ вращение на произвольный угол (удержание Q/E, Alt+мышь).
	 *
	 * Отдельный вход, а не «шаг с маленьким RotationStep»: у шага и у свободного
	 * вращения разные источники величины (константа против времени/пикселей), и
	 * склеивать их в один метод значило бы, что вызывающий каждый раз считает
	 * чужую арифметику. Владение камерой оба рвут одинаково — это ручной ввод.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddRotationDelta(float Degrees);

	/**
	 * РУЧНОЙ НАКЛОН (Alt+мышь по вертикали). Хранится как ОТКЛОНЕНИЕ от базового
	 * наклона, который камера сама выводит из зума (`PitchAtMinZoom/MaxZoom`):
	 * иначе после наезда колесом ручной угол пришлось бы поправлять заново.
	 * Один механизм: итоговый наклон = база(зум) + отклонение игрока, зажатое в
	 * `MinManualPitch..MaxManualPitch` (XCOM держит камеру в −90…−10).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddPitchDelta(float Degrees);

	/** Зум: положительное значение — приблизить. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddZoomInput(float Input);

	/**
	 * ПЛОСКОСТЬ ОБЗОРА на многоуровневой карте: ±1 этаж за нажатие (PageUp/Down).
	 * Смещение живёт поверх высоты, которую задают focus/follow, и переживает их —
	 * это отдельный выбор игрока «смотрю на второй этаж», а не свойство цели
	 * фокуса. Сбрасывается `ResetViewAdjustments` и центрированием на бойце.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void AddViewFloorStep(int32 Steps);

	/** Вернуть ракурс к дефолтному (наклон по зуму, нулевой этаж) — как Ctrl+F1 в XCOM-модах. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void ResetViewAdjustments();

	/**
	 * Применить пользовательские настройки камеры (обзор, чувствительность,
	 * инверсия). Зовётся из `UTacticsUserSettings` при загрузке и при сохранении
	 * экрана настроек — камера не читает настройки сама каждый кадр.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void ApplyUserCameraSettings(float InFieldOfView, float InRotationSensitivity,
		float InPitchSensitivity, bool bInInvertPitch);

	/** Чувствительность мыши в свободном обзоре (град/пиксель), с учётом настроек. */
	float GetMouseYawSensitivity() const { return MouseYawSensitivity * UserRotationSensitivity; }
	float GetMousePitchSensitivity() const
	{
		return MousePitchSensitivity * UserPitchSensitivity * (bUserInvertPitch ? -1.f : 1.f);
	}

	/** Скорость свободного вращения при удержании Q/E (град/сек). */
	float GetFreeRotationSpeed() const { return FreeRotationSpeed * UserRotationSensitivity; }

	/** Перелететь к актору: плавный полёт (XCOM) или мгновенно (bInstant). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void FocusOnActor(const AActor* Target, bool bInstant = false);

	/**
	 * Перевзвод шага A1: сбрасывает one-shot и накопители панорамы/поворота/зума.
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
	 * За кем камера следует сейчас (nullptr — ни за кем). Нужен туману войны:
	 * враг, скрывшийся посреди своего хода, обязан ПЕРЕСТАТЬ вести за собой
	 * камеру — иначе его позицию выдаёт движение кадра.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Camera")
	const AActor* GetFollowTarget() const { return FollowTarget.Get(); }

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
	 * Камерой сейчас распоряжается режиссура такта. Спрашивают те, кто хочет
	 * навести камеру «от себя» (акцент первого обнаружения): постановка обучения
	 * главнее любого автоматического кадра.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Camera")
	bool IsDirectorHolding() const { return bDirectorHold; }

	/**
	 * Игрок взял камеру сам (панорама/поворот/зум): удержание снимается, а
	 * накопленный фоновый интент ВЫБРАСЫВАЕТСЯ — иначе камера прыгнула бы из-под
	 * руки игрока к отложенной цели.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Camera")
	void BreakDirectorHold();

	/**
	 * Последнее режиссёрское удержание кончилось РУКОЙ ИГРОКА, а не по таймеру.
	 * Спрашивает возврат камеры после акцента первого обнаружения: если игрок
	 * уже увёл камеру сам, дёргать её обратно к отряду — значит вырывать
	 * управление. Сбрасывается следующим `FocusOnLocationDirected`.
	 */
	bool WasDirectorHoldBrokenByPlayer() const { return bDirectorHoldBrokenByPlayer; }

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

	/**
	 * Пределы длины пружины (зум), см — заданы для опорного обзора `ReferenceFov`.
	 *
	 * ⚠️ Числа выросли вместе с сужением обзора (было 800/2600 при FOV 90). Угол и
	 * дистанция задают охват вместе: если сузить обзор, не отодвинув камеру, поле
	 * боя видно хуже при тех же сантиметрах. Пересчёт под текущий обзор игрока —
	 * `GetZoomFovScale`, поэтому выбор FOV в настройках меняет ПЕРСПЕКТИВУ, а не
	 * «сколько влезает в экран».
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float MinZoom = 1250.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float MaxZoom = 4100.f;

	/** Обзор, под который заданы MinZoom/MaxZoom (см. GetZoomFovScale). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "40", ClampMax = "110"))
	float ReferenceFov = 65.f;

	/** Шаг зума за тик колеса, см. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera")
	float ZoomStep = 300.f;

	// --- Обзор (FOV) ----------------------------------------------------------
	//
	// XCOM 2 играет тактику на `DefaultFOV=50`, а кадр выстрела строит внутри
	// 0.75 этого угла (`X2Camera_Midpoint.FramingFOVPercentage`). Наши прежние 90°
	// — это широкоугольник: он растягивает периферию, съедает фигуры к центру и в
	// упор показывает больше стены, чем бойцов. Отсюда «вблизи не видно ни своего,
	// ни врага, оружия тем более». Тактический угол — настройка игрока
	// (`UTacticsUserSettings`), кадр выстрела — авторский и не настраивается.

	/** Тактический обзор по умолчанию (град). Настройка игрока переопределяет. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "40", ClampMax = "110"))
	float TacticalFov = 65.f;

	/** Обзор в кадре выстрела (град) — «телевик» XCOM. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot",
		meta = (ClampMin = "30", ClampMax = "90"))
	float ShotFrameFov = 50.f;

	/** Скорость доводки обзора между тактическим и кадровым (град/сек). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "1"))
	float FovInterpSpeed = 45.f;

	// --- Наклон, связанный с зумом --------------------------------------------
	//
	// Фиксированные −55° означали «всегда смотрим сверху»: при наезде фигуры
	// видны с макушки, ноги и оружие уходят. Теперь наклон — функция зума
	// (ближе → положе, как в любой тактике с приличной камерой), а игрок может
	// добавить своё отклонение (`PlayerPitchOffset`).

	/** Наклон на максимальном приближении (град, отрицательный — вниз). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "-89", ClampMax = "-5"))
	float PitchAtMinZoom = -34.f;

	/** Наклон на максимальном отдалении. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "-89", ClampMax = "-5"))
	float PitchAtMaxZoom = -58.f;

	/** Границы наклона с ручным отклонением (XCOM: −90…−10). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "-89", ClampMax = "-5"))
	float MinManualPitch = -85.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "-89", ClampMax = "-5"))
	float MaxManualPitch = -12.f;

	// --- Свободное вращение (мод Free Camera Rotation) -------------------------

	/** Скорость непрерывного вращения при удержании Q/E (град/сек). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "10"))
	float FreeRotationSpeed = 110.f;

	/** Чувствительность мыши в свободном обзоре: град на пиксель (ваниль XCOM: 0.3/0.2). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "0.01"))
	float MouseYawSensitivity = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "0.01"))
	float MousePitchSensitivity = 0.2f;

	// --- Панорама, зависящая от зума ------------------------------------------
	//
	// На максимальном приближении прежняя скорость 2000 см/с проносила экран
	// мимо бойца: величина сдвига «на глаз» пропорциональна не сантиметрам, а
	// доле экрана, а она растёт при наезде. Множители приводят ощущение к одному.

	/** Множитель скорости панорамы на минимальной дистанции (приближено). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "0.1", ClampMax = "3"))
	float PanSpeedScaleNear = 0.5f;

	/** Множитель скорости панорамы на максимальной дистанции (отдалено). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "0.1", ClampMax = "3"))
	float PanSpeedScaleFar = 1.4f;

	// --- Плоскость обзора по этажам -------------------------------------------

	/** Высота одного шага плоскости обзора (см). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "50"))
	float ViewFloorStepHeight = 300.f;

	/** Сколько шагов вверх/вниз разрешено от плоскости фокуса. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera",
		meta = (ClampMin = "0", ClampMax = "6"))
	int32 MaxViewFloorSteps = 3;

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

	/**
	 * Насколько далеко живому кадру разрешено ехать за участниками (см). Кадр
	 * следит за выходом из укрытия и падением цели, но должен остаться на месте
	 * сцены, даже если актора отбросило или телепортировало.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0"))
	float ShotFrameMaxTrackDrift = 400.f;

	/**
	 * ПЕРЕПАД ВЫСОТ: насколько высота камеры следует за точкой взгляда, а не за
	 * стрелком (0 — камера строго на высоте стрелка, как было; 1 — на высоте
	 * композиции). Боец на крыше и цель под ним иначе давали почти вертикальный
	 * кадр, где обе фигуры вырождаются в пятна.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0", ClampMax = "1"))
	float ShotFrameHeightBlend = 0.6f;

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
	 * Штраф ракурсу «со стороны стрелка» в ДАЛЬНЕМ кадре. Иначе говоря — цена
	 * отказа от обратного ракурса из-за цели, который на squadsight-дистанции и
	 * читается как снайперский выстрел: видно и цель, и откуда прилетело.
	 * Обратный побеждает при прочих равных, но уступает, если из-за цели её
	 * саму не видно (штраф 64 перебивает эти 16).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score")
	float LongShotFrontAnglePenalty = 16.f;

	/**
	 * Доля полу-FOV, в которую ОБЯЗАНЫ поместиться фигуры целиком (голова и ноги
	 * обоих участников, см. `GetSubjectPoints`). < 1 — запас на поля кадра и HUD.
	 * Не помещаются — камера отъезжает сама: композиция следует из геометрии, а
	 * не из подобранного вручную зума. Значение из XCOM
	 * (`X2Camera_Midpoint.FramingFOVPercentage = 0.75`).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0.3", ClampMax = "1"))
	float ShotFrameFovSafety = 0.75f;

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

	/**
	 * ОБХОД ПО ДУГЕ: на сколько градусов вокруг точки взгляда разрешено сместить
	 * камеру, если с «правильного» плеча всё закрыто мешами.
	 *
	 * XCOM перебирает не два кандидата, а весь набор авторских matinee-камер
	 * (десятки), и потому почти всегда находит чистый ракурс. У нас кандидаты
	 * считаются, поэтому набор задаётся дугой: `ShotFrameArcSteps` шагов по
	 * `ShotFrameArcSweep` градусов в каждую сторону. Именно этого не хватало в
	 * сценах, где ВСЕ кандидаты оказывались за геометрией.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "5", ClampMax = "60"))
	float ShotFrameArcSweep = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot", meta = (ClampMin = "0", ClampMax = "4"))
	int32 ShotFrameArcSteps = 2;

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

	/**
	 * СТОРОНА ВЫГЛЯДЫВАНИЯ. Камера должна стоять с той стороны, куда боец
	 * высунется из укрытия, иначе весь пик и выстрел происходят за его спиной —
	 * ровно жалоба «анимации пика и выстрела вообще не видно».
	 *
	 * В XCOM это `GetUnitFacing`: сторона берётся из `GetExitCoverPosition` (куда
	 * будет step-out), и матини с другой стороны отсеиваются. У нас сторона
	 * читается из точки выстрела `GetFiringStance` — она уже смещена к краю
	 * укрытия. Штраф мягкий: если с «правильной» стороны цель закрыта наглухо,
	 * лучше показать выстрел с другой, чем стену.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Shot|Score")
	float PenaltyOffPeekSide = 12.f;

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

	/**
	 * Post-process материал тумана войны (M_PP_FogOfWar): затемнение местности по
	 * сетке `UFogGridSubsystem`. Вешается тем же блендаблом, что и обводка, но
	 * ПОСЛЕ неё.
	 *
	 * ⚠️ Порядок блендаблов — не защита. Скрытый враг не проступит обводкой сквозь
	 * туман потому, что у него принудительно выключен Custom Depth
	 * (`UFogRevealableComponent`), а не потому, что туман нарисован сверху.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics|Camera")
	TObjectPtr<UMaterialInterface> FogOfWarMaterial;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Глобальный (unbound) пост-процесс пешки: несёт обводку юнитов и туман. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|Camera")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/**
	 * Живой инстанс материала тумана: параметры (текстуру сетки, границы, цвета
	 * состояний) пишет в него `UFogGridSubsystem`. Держим ссылкой, иначе GC заберёт
	 * инстанс, на который смотрит блендабл.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FogMaterialInstance;

	/**
	 * Постоянный пользовательский ракурс тактической камеры. Он меняется только
	 * ручным Q/E и колесом и не перезаписывается временным кадром выстрела.
	 */
	float TacticalYaw = 45.f;
	float TacticalZoom = 2800.f;

	// --- Шаг A1 обучения: подтверждённая настройка ракурса ---------------------
	//
	// Событие `Camera.Adjusted` публикуется не по raw WASD/Q/E/колесу, а после
	// того, как игрок реально изменил И положение, И поворот, И приближение сверх
	// порога. Иначе шаг закрывался бы от случайного касания колеса, а игрок так и
	// не узнал бы про панораму.

	/** На сколько градусов суммарно нужно повернуть камеру, чтобы засчитать шаг. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Tutorial",
		meta = (ClampMin = "5"))
	float AdjustedYawThreshold = 60.f;

	/** На сколько сантиметров суммарно нужно изменить дистанцию камеры. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Tutorial",
		meta = (ClampMin = "50"))
	float AdjustedZoomThreshold = 400.f;

	/** Сколько сантиметров суммарно нужно проехать панорамой (WASD / край экрана). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Camera|Tutorial",
		meta = (ClampMin = "50"))
	float AdjustedPanThreshold = 600.f;

	/** Публикует `Camera.Adjusted` один раз, когда пройдены все три порога. */
	void ReportCameraAdjustment(float YawDelta, float ZoomDelta, float PanDelta);

	float AccumulatedYawAdjustment = 0.f;
	float AccumulatedZoomAdjustment = 0.f;
	float AccumulatedPanAdjustment = 0.f;
	bool bCameraAdjustmentReported = false;

	/** Текущие цели интерполяции: обычный ракурс либо временный action-camera. */
	float TargetYaw = 45.f;
	float TargetZoom = 2800.f;

	/** Наклон камеры: вне кадра — база от зума + отклонение игрока, в кадре — из геометрии. */
	float TargetPitch = -55.f;

	/** Целевой обзор: тактический вне кадра, `ShotFrameFov` — в кадре выстрела. */
	float TargetFov = 65.f;

	/**
	 * Отклонение наклона, заданное игроком (град). Живёт поверх базы от зума,
	 * поэтому колесо не «съедает» выбранный угол, а сдвигает его вместе с базой.
	 */
	float PlayerPitchOffset = 0.f;

	/** Текущий сдвиг плоскости обзора в этажах (см. AddViewFloorStep). */
	int32 ViewFloorSteps = 0;

	// --- Пользовательские настройки (см. ApplyUserCameraSettings) --------------
	float UserRotationSensitivity = 1.f;
	float UserPitchSensitivity = 1.f;
	bool bUserInvertPitch = false;

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

	/**
	 * Раскрытие местности вокруг участников на время кадра
	 * (`UFogGridSubsystem::AddScriptedReveal`). Кадр показывает бой с чужого
	 * ракурса и крупно, поэтому неразведанной местности в нём куда больше
	 * обычного — без раскрытия выстрел играется в темноте. 0 — не взято.
	 */
	int32 ShotFrameRevealHandle = 0;

	// --- Живой кадр (аналог `TetherTPOV` в XCOM) ------------------------------
	//
	// Кадр XCOM не замирает: `X2Camera_OverTheShoulder::UpdateTether` каждый кадр
	// пересчитывает целевую точку по ТЕКУЩИМ позициям стрелка и цели и лениво
	// тянется к ней. Без этого выход из укрытия (step-out на полтора метра вбок),
	// отдача, падение цели происходят вне кадра, который построен по позициям
	// «до». Мы храним смещение камеры относительно точки взгляда и пересчитываем
	// точку взгляда каждый тик; ленивость дают уже существующие интерполяции —
	// отдельного tether-таймера не заводим, чтобы у плавности был один владелец.

	TWeakObjectPtr<const AActor> ShotFrameShooter;
	TWeakObjectPtr<const AActor> ShotFrameTarget;

	/** Позиции участников на момент построения — база для относительного сдвига. */
	FVector ShotFrameShooterStart = FVector::ZeroVector;
	FVector ShotFrameTargetStart = FVector::ZeroVector;

	/** Точка взгляда на момент построения (к ней прибавляется сдвиг участников). */
	FVector ShotFrameLookPoint = FVector::ZeroVector;

	/** Смещение камеры от точки взгляда, выбранное лестницей кандидатов. */
	FVector ShotFrameCamOffset = FVector::ZeroVector;

	/** Смесь «стрелок↔цель» для точки взгляда, выбранная по дистанции. */
	float ShotFrameLookBias = 0.5f;

	/** Кадр строится вокруг цели (дальний/squadsight выстрел). */
	bool bShotFrameLongShot = false;

	/** Камеру держит режиссура такта (см. FocusOnLocationDirected). */
	bool bDirectorHold = false;

	/** Остаток страховочного удержания, сек (< 0 — до явного снятия). */
	float DirectorHoldTimeLeft = -1.f;

	/** Последнее удержание прервал игрок (см. WasDirectorHoldBrokenByPlayer). */
	bool bDirectorHoldBrokenByPlayer = false;

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
	 * Позиция ДО кадра — камера возвращается ровно туда, откуда её забрал кадр.
	 * Наклон здесь НЕ хранится: вне кадра он однозначно выводится из зума и
	 * отклонения игрока (`GetDesiredTacticalPitch`), поэтому запоминать его
	 * значило бы завести второй источник правды для одной величины.
	 */
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
	 * Отодвигает камеру назад по оси взгляда, пока ВСЕ контрольные точки фигур не
	 * поместятся в безопасную долю FOV (`ShotFrameFovSafety`). Это заменяет ручной
	 * подбор «зум ближний/дальний»: композиция теперь следствие геометрии.
	 */
	FVector FitSubjectsInFrame(const FVector& CamPos, const FVector& LookPoint,
		const TArray<FVector, TInlineAllocator<6>>& Subjects) const;

	/**
	 * Контрольные точки фигуры для кадра: НОГИ, центр и ГОЛОВА по капсуле актора.
	 *
	 * XCOM вписывает юнита в кадр именно двумя точками (`X2Camera_Midpoint`:
	 * `GetHeadLocation` + `GetFeetLocation`). Одной точки груди мало: камера
	 * честно «влезала» серединой корпуса, а голова и оружие уходили за рамку —
	 * это и читалось как «оружия не видно».
	 *
	 * ⚠️ Применяется к ОБЕИМ фигурам. Требование к стрелку дорогое (он рядом с
	 * камерой), но без него его просто не видно в кадре — проверено прогоном
	 * 2026-08-03, см. EnterShotFraming.
	 */
	void GetSubjectPoints(const AActor* Subject, const FVector& AimOverride,
		TArray<FVector, TInlineAllocator<6>>& OutPoints) const;

	/**
	 * Множитель дистанций зума под текущий обзор игрока: во сколько раз надо
	 * отодвинуть камеру, чтобы охват остался таким же, как на `ReferenceFov`.
	 * Ширина видимого куска земли пропорциональна `Arm × tan(FOV/2)`, отсюда и
	 * формула — отношение тангенсов полууглов.
	 */
	float GetZoomFovScale() const;

	/** Границы зума с учётом текущего обзора (см. GetZoomFovScale). */
	float GetEffectiveMinZoom() const { return MinZoom * GetZoomFovScale(); }
	float GetEffectiveMaxZoom() const { return MaxZoom * GetZoomFovScale(); }

	/** Положение зума в своём диапазоне, 0 — вплотную, 1 — максимально далеко. */
	float GetZoomAlpha() const;

	/** Базовый наклон для текущего зума (ближе — положе). */
	float GetBasePitchForZoom() const;

	/** Итоговый наклон вне кадра: база от зума + отклонение игрока, зажатое пределами. */
	float GetDesiredTacticalPitch() const;

	/**
	 * Пересчитать позу камеры по живому кадру: точка взгляда берётся из ТЕКУЩИХ
	 * позиций участников, камера — из сохранённого смещения (см. ShotFrameCamOffset).
	 */
	void UpdateShotFrameTracking();

	/** Перевести пару «позиция камеры + точка взгляда» в цели пружины (yaw/pitch/arm/pivot). */
	void ApplyShotFramePose(const FVector& CamPos, const FVector& LookPoint);

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
		float PreferredSideSign, bool bIgnoreShooter) const;

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
