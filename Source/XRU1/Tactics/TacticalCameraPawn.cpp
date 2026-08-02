#include "TacticalCameraPawn.h"
#include "XRU1Log.h"
#include "TacticalQuestEvents.h"
#include "TacticsCombatStatics.h" // GetShotGeometryObjects — единая геометрия мира
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h" // GetEffectiveTimeDilation — камера живёт в реальном времени
#include "Engine/HitResult.h"
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"

ATacticalCameraPawn::ATacticalCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Root);
	SpringArm->TargetArmLength = TargetZoom;
	SpringArm->SetRelativeRotation(FRotator(TargetPitch, TargetYaw, 0.f));
	SpringArm->bDoCollisionTest = false; // камера не должна «прилипать» к стенам укрытий

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	// Обзор задаём здесь, а не в BP: он участвует в расчёте кадра выстрела
	// (`FitSubjectsInFrame`), и разъезд «в BP одно, в расчёте другое» дал бы
	// композицию, которая не сходится с картинкой.
	Camera->SetFieldOfView(TacticalFov);

	// Глобальный пост-процесс: unbound (действует всегда, независимо от активной
	// камеры и позиции пешки) — надёжнее блендабла на самой камере.
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
}

void ATacticalCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	// Стартовая высота пивота — собственная высота пешки (смещение 0), дальше её
	// ведут focus/follow/кадр выстрела.
	PivotWorldZ = GetActorLocation().Z;
	PreShotPivotZ = PivotWorldZ;

	// Стартовый ракурс собираем из тех же правил, что действуют дальше: наклон —
	// функция зума, обзор — тактический. Иначе первый кадр игры показывал бы
	// значения из конструктора, а первый же поворот колеса их «чинил».
	TargetPitch = GetDesiredTacticalPitch();
	TargetFov = TacticalFov;
	if (SpringArm)
	{
		SpringArm->SetRelativeRotation(FRotator(TargetPitch, TargetYaw, 0.f));
	}
	if (Camera)
	{
		Camera->SetFieldOfView(TargetFov);
	}

	// Обводка юнитов при наведении: PP-материал блендаблом на unbound-компонент.
	if (OutlineMaterial && PostProcess)
	{
		PostProcess->Settings.AddBlendable(OutlineMaterial, 1.f);
	}
	else if (!OutlineMaterial)
	{
		UE_LOG(LogXRU1Combat, Warning, TEXT("[Highlight] OutlineMaterial не назначен в пешке-камере — ")
			TEXT("обводки юнитов не будет (проверь Default Pawn Class в GameMode = BP_TacticalCameraPawn)"));
	}
}

void ATacticalCameraPawn::AddPanInput(const FVector2D& Input)
{
	if (Input.IsNearlyZero())
	{
		return;
	}

	// Ручная панорама разрывает автофокус/следование, кадр выстрела и
	// режиссёрское удержание (как в XCOM: тронул камеру — она твоя).
	if (bShotFraming || FollowTarget.IsValid() || bDirectorHold)
	{
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Ручная панорама игрока — рвёт автокамеру"));
	}
	BreakDirectorHold();
	ClearFollowTarget();
	AbandonShotFraming();
	bHasFocusGoal = false;

	// Направления берём от текущего yaw камеры, движение — в плоскости земли.
	const FRotator YawRot(0.f, SpringArm->GetRelativeRotation().Yaw, 0.f);
	const FVector Forward = YawRot.RotateVector(FVector::ForwardVector);
	const FVector Right = YawRot.RotateVector(FVector::RightVector);

	// Скорость привязана к зуму: на приближении тот же сдвиг в сантиметрах
	// проносит по экрану куда больше, чем на отдалении. Игрок мерит панораму
	// долями экрана, поэтому и скорость приводим к ним (XCOM отдаёт этот же
	// множитель настройке `m_fScrollSpeed`, у нас он вдобавок зависит от зума).
	const float ZoomScale = FMath::Lerp(PanSpeedScaleNear, PanSpeedScaleFar, GetZoomAlpha());

	// Реальное время, как и весь Tick камеры: под slow-mo реакции панорама
	// игрока не должна становиться вязкой.
	const float Delta = GetWorld() ? GetWorld()->DeltaRealTimeSeconds : 0.016f;
	const FVector Offset = (Forward * Input.Y + Right * Input.X) * PanSpeed * ZoomScale * Delta;
	AddActorWorldOffset(Offset);

	// Считаем фактический сдвиг в мире, а не силу ввода: упёртая в границу карты
	// камера не должна накручивать «настройку», которой на экране не произошло.
	ReportCameraAdjustment(0.f, 0.f, Offset.Size2D());
}

void ATacticalCameraPawn::AddRotationStep(float Direction)
{
	if (!FMath::IsNearlyZero(Direction))
	{
		AddRotationDelta(RotationStep * FMath::Sign(Direction));
	}
}

void ATacticalCameraPawn::AddRotationDelta(float Degrees)
{
	if (FMath::IsNearlyZero(Degrees))
	{
		return;
	}

	// Любой ручной поворот — заявка на владение камерой (XCOM: тронул — твоя).
	if (bShotFraming || bDirectorHold)
	{
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Ручной поворот игрока — рвёт автокамеру"));
		BreakDirectorHold();
		AbandonShotFraming();
	}

	// Нормализуем сразу, иначе yaw копится без ограничений (см. Tick).
	// TacticalYaw — постоянный выбор игрока; action-camera меняет только
	// TargetYaw и после себя всегда возвращается к TacticalYaw.
	TacticalYaw = FRotator::NormalizeAxis(TacticalYaw + Degrees);
	if (!bShotFraming)
	{
		TargetYaw = TacticalYaw;
	}
	ReportCameraAdjustment(FMath::Abs(Degrees), 0.f, 0.f);
}

void ATacticalCameraPawn::AddPitchDelta(float Degrees)
{
	if (FMath::IsNearlyZero(Degrees))
	{
		return;
	}
	if (bShotFraming || bDirectorHold)
	{
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Ручной наклон игрока — рвёт автокамеру"));
		BreakDirectorHold();
		AbandonShotFraming();
	}

	// Отклонение зажимаем не само по себе, а по ИТОГОВОМУ наклону: предел
	// принадлежит углу камеры, а не «сколько игрок накрутил». Иначе на краю
	// диапазона зума ручное отклонение внезапно переставало работать.
	const float Base = GetBasePitchForZoom();
	const float Desired = FMath::Clamp(Base + PlayerPitchOffset + Degrees,
		MinManualPitch, MaxManualPitch);
	PlayerPitchOffset = Desired - Base;
	if (!bShotFraming)
	{
		TargetPitch = Desired;
	}
}

void ATacticalCameraPawn::AddViewFloorStep(int32 Steps)
{
	if (Steps == 0)
	{
		return;
	}
	const int32 NewSteps = FMath::Clamp(ViewFloorSteps + Steps, -MaxViewFloorSteps, MaxViewFloorSteps);
	if (NewSteps == ViewFloorSteps)
	{
		return; // упёрлись в предел — молча, но без ложного лога «этаж сменён»
	}
	ViewFloorSteps = NewSteps;
	UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Плоскость обзора: этаж %+d (%.0f см)"),
		ViewFloorSteps, ViewFloorSteps * ViewFloorStepHeight);

	// Смена этажа — тоже взятие камеры игроком: режиссёрское удержание уступает,
	// иначе выбранный этаж тут же перебивался бы фокусом такта.
	BreakDirectorHold();
	AbandonShotFraming();
}

void ATacticalCameraPawn::ResetViewAdjustments()
{
	if (FMath::IsNearlyZero(PlayerPitchOffset) && ViewFloorSteps == 0)
	{
		return;
	}
	UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Ракурс сброшен к дефолтному (наклон %.0f°, этаж %+d)"),
		PlayerPitchOffset, ViewFloorSteps);
	PlayerPitchOffset = 0.f;
	ViewFloorSteps = 0;
	if (!bShotFraming)
	{
		// Иначе сброс «сработал бы» только на следующее движение колеса: наклон
		// пересчитывается там, где меняется зум или отклонение игрока.
		TargetPitch = GetDesiredTacticalPitch();
	}
}

void ATacticalCameraPawn::ApplyUserCameraSettings(float InFieldOfView, float InRotationSensitivity,
	float InPitchSensitivity, bool bInInvertPitch)
{
	// Дистанцию тянем за обзором в ту же сторону: игрок настраивает перспективу,
	// а не «сколько видно». Без этого выбор 50° резал обзор поля втрое, и первое,
	// что делал бы игрок, — отъезжал колесом обратно.
	const float ScaleBefore = GetZoomFovScale();
	TacticalFov = FMath::Clamp(InFieldOfView, 40.f, 110.f);
	const float ScaleAfter = GetZoomFovScale();
	if (!FMath::IsNearlyEqual(ScaleBefore, ScaleAfter) && ScaleBefore > KINDA_SMALL_NUMBER)
	{
		TacticalZoom *= ScaleAfter / ScaleBefore;
	}
	TacticalZoom = FMath::Clamp(TacticalZoom, GetEffectiveMinZoom(), GetEffectiveMaxZoom());

	UserRotationSensitivity = FMath::Clamp(InRotationSensitivity, 0.1f, 4.f);
	UserPitchSensitivity = FMath::Clamp(InPitchSensitivity, 0.1f, 4.f);
	bUserInvertPitch = bInInvertPitch;
	if (!bShotFraming)
	{
		TargetFov = TacticalFov; // в кадре выстрела обзор авторский — не трогаем
		TargetZoom = TacticalZoom;
		TargetPitch = GetDesiredTacticalPitch();

		// Применяем НЕМЕДЛЕННО, не дожидаясь доводки в Tick. Причина конкретная:
		// экран настроек открывается поверх боя и держит паузу, а на паузе актор
		// не тикает — игрок двигал бы ползунок обзора и не видел ровно ничего.
		// Плавная доводка нужна переходу «тактический вид ↔ кадр выстрела», а не
		// настройке.
		if (Camera)
		{
			Camera->SetFieldOfView(TargetFov);
		}
		if (SpringArm)
		{
			SpringArm->TargetArmLength = TargetZoom;
			FRotator Rot = SpringArm->GetRelativeRotation();
			Rot.Pitch = TargetPitch;
			SpringArm->SetRelativeRotation(Rot);
		}
	}
	UE_LOG(LogXRU1Camera, Display,
		TEXT("[Camera] Настройки игрока: обзор %.0f°, чувствительность %.2f/%.2f, инверсия=%d"),
		TacticalFov, UserRotationSensitivity, UserPitchSensitivity, bUserInvertPitch ? 1 : 0);
}

float ATacticalCameraPawn::GetZoomFovScale() const
{
	// Ширина видимого куска земли ≈ Arm × tan(FOV/2). Хотим, чтобы при смене
	// обзора она не менялась, — значит дистанции масштабируются обратным
	// отношением тангенсов. Практический смысл: игрок, поставивший 50°, получает
	// «телевик» с тем же охватом, а не вид в замочную скважину.
	const float RefTan = FMath::Tan(FMath::DegreesToRadians(0.5f * FMath::Clamp(ReferenceFov, 20.f, 150.f)));
	const float NowTan = FMath::Tan(FMath::DegreesToRadians(0.5f * FMath::Clamp(TacticalFov, 20.f, 150.f)));
	return NowTan > KINDA_SMALL_NUMBER ? RefTan / NowTan : 1.f;
}

float ATacticalCameraPawn::GetZoomAlpha() const
{
	const float Min = GetEffectiveMinZoom();
	const float Max = GetEffectiveMaxZoom();
	return Max > Min ? FMath::Clamp((TacticalZoom - Min) / (Max - Min), 0.f, 1.f) : 1.f;
}

float ATacticalCameraPawn::GetBasePitchForZoom() const
{
	// Ближе к бойцам — положе (видно фигуры и оружие), дальше — вид сверху
	// (видно расстановку). Прежние фиксированные −55° были компромиссом, который
	// плох в обоих концах диапазона.
	return FMath::Lerp(PitchAtMinZoom, PitchAtMaxZoom, GetZoomAlpha());
}

float ATacticalCameraPawn::GetDesiredTacticalPitch() const
{
	return FMath::Clamp(GetBasePitchForZoom() + PlayerPitchOffset, MinManualPitch, MaxManualPitch);
}

void ATacticalCameraPawn::ReArmCameraAdjustedEvent()
{
	bCameraAdjustmentReported = false;
	AccumulatedYawAdjustment = 0.f;
	AccumulatedZoomAdjustment = 0.f;
	AccumulatedPanAdjustment = 0.f;
}

void ATacticalCameraPawn::ReportCameraAdjustment(float YawDelta, float ZoomDelta, float PanDelta)
{
	if (bCameraAdjustmentReported)
	{
		return;
	}

	const bool bYawWasDone = AccumulatedYawAdjustment >= AdjustedYawThreshold;
	const bool bZoomWasDone = AccumulatedZoomAdjustment >= AdjustedZoomThreshold;
	const bool bPanWasDone = AccumulatedPanAdjustment >= AdjustedPanThreshold;

	AccumulatedYawAdjustment += FMath::Abs(YawDelta);
	AccumulatedZoomAdjustment += FMath::Abs(ZoomDelta);
	AccumulatedPanAdjustment += FMath::Abs(PanDelta);

	const bool bYawDone = AccumulatedYawAdjustment >= AdjustedYawThreshold;
	const bool bZoomDone = AccumulatedZoomAdjustment >= AdjustedZoomThreshold;
	const bool bPanDone = AccumulatedPanAdjustment >= AdjustedPanThreshold;

	// Каждое закрытое требование отмечаем в логе один раз: по логу видно, какого
	// именно движения игроку не хватает, чтобы шаг A1 закрылся.
	if (bYawDone != bYawWasDone || bZoomDone != bZoomWasDone || bPanDone != bPanWasDone)
	{
		UE_LOG(LogXRU1Camera, Display,
			TEXT("[Camera] A1 «Осмотритесь»: поворот %.0f/%.0f%s, зум %.0f/%.0f%s, панорама %.0f/%.0f%s"),
			AccumulatedYawAdjustment, AdjustedYawThreshold, bYawDone ? TEXT(" (готово)") : TEXT(""),
			AccumulatedZoomAdjustment, AdjustedZoomThreshold, bZoomDone ? TEXT(" (готово)") : TEXT(""),
			AccumulatedPanAdjustment, AdjustedPanThreshold, bPanDone ? TEXT(" (готово)") : TEXT(""));
	}

	if (!bYawDone || !bZoomDone || !bPanDone)
	{
		return;
	}

	// One-shot на запуск: шаг A1 засчитывается один раз, повторные вращения уже
	// не создают событий и не могут накрутить чужой счётчик.
	bCameraAdjustmentReported = UTacticalQuestEvents::BroadcastQuestEvent(
		this, TacticalQuestTags::Event_Tactical_Camera_Adjusted, this);
}

void ATacticalCameraPawn::AddZoomInput(float Input)
{
	if (!FMath::IsNearlyZero(Input) && (bShotFraming || bDirectorHold))
	{
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Ручной зум игрока — рвёт автокамеру"));
		BreakDirectorHold();
		AbandonShotFraming();
	}

	const float ZoomBefore = TacticalZoom;
	TacticalZoom = FMath::Clamp(TacticalZoom - Input * ZoomStep, GetEffectiveMinZoom(), GetEffectiveMaxZoom());
	if (!bShotFraming)
	{
		TargetZoom = TacticalZoom;
		// Наклон следует за зумом: одно движение колеса меняет и дистанцию, и
		// угол — так наезд действительно «опускает» камеру к бойцам, а не просто
		// придвигает вид сверху.
		TargetPitch = GetDesiredTacticalPitch();
	}
	// Считаем фактическое изменение, а не ввод: упёртое в MinZoom колесо не должно
	// накапливать «настройку», которой на экране не произошло.
	ReportCameraAdjustment(0.f, TacticalZoom - ZoomBefore, 0.f);
}

void ATacticalCameraPawn::FocusOnActor(const AActor* Target, bool bInstant)
{
	if (Target)
	{
		FocusOnLocation(Target->GetActorLocation(), bInstant);
	}
}

void ATacticalCameraPawn::FocusOnLocation(const FVector& Location, bool bInstant)
{
	// Монополия кадра презентации / режиссёрского такта: фоновый фокус
	// (автовыбор бойца, новый шаг) откладывается до снятия удержания, а не
	// уводит камеру с выстрела или с показываемой игроку точки.
	if (ShouldDeferAmbientIntent())
	{
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Focus → %s ОТЛОЖЕН: камерой владеет %s"),
			*Location.ToCompactString(),
			IsPlayingPresentationFrame() ? TEXT("кадр выстрела") : TEXT("режиссура такта"));
		bHasPendingCameraIntent = true;
		bPendingIntentIsFollow = false;
		bPendingIntentIsDirected = bMarkingDirectedIntent;
		PendingFocusLocation = Location;
		bPendingFocusInstant = bInstant; // мгновенный фокус обязан остаться мгновенным
		PendingFollowTarget = nullptr;
		return;
	}

	UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Focus → %s instant=%d (кадр=%d follow=%s)"),
		*Location.ToCompactString(), bInstant ? 1 : 0, bShotFraming ? 1 : 0,
		*GetNameSafe(FollowTarget.Get()));

	// Новый интент камеры перечёркивает кадр выстрела: без этого истёкший таймер
	// кадра «вернул бы» камеру назад посреди уже начатого фокуса.
	AbandonShotFraming();

	// Явный фокус отменяет следование за актором.
	FollowTarget = nullptr;

	// Камера смотрит на ПЛОСКОСТЬ, где стоит цель фокуса, а не на плоскость
	// высоты своего спавна: на многоуровневой карте это разные вещи.
	PivotWorldZ = Location.Z;

	if (bInstant)
	{
		// Телепорт (старт боя): двигаем только в плоскости земли, высота пешки своя.
		bHasFocusGoal = false;
		SetActorLocation(FVector(Location.X, Location.Y, GetActorLocation().Z));
		if (SpringArm)
		{
			FVector Offset = SpringArm->TargetOffset;
			Offset.Z = FMath::Clamp(PivotWorldZ - GetActorLocation().Z, -MaxPivotZOffset, MaxPivotZOffset);
			SpringArm->TargetOffset = Offset; // телепорт — без доводки
		}
	}
	else
	{
		FocusGoal = Location;
		bHasFocusGoal = true; // полёт доводится в Tick
	}
}

void ATacticalCameraPawn::SetFollowTarget(const AActor* Target)
{
	// Монополия кадра презентации / такта: подхват врага «вышел из-за угла» и
	// follow чужого хода ждут конца выстрела. Именно этот вызов срывал кадр
	// реакции наблюдения сразу после его построения (лог 2026-08-02).
	if (ShouldDeferAmbientIntent())
	{
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Follow → %s ОТЛОЖЕН: камерой владеет %s"),
			*GetNameSafe(Target),
			IsPlayingPresentationFrame() ? TEXT("кадр выстрела") : TEXT("режиссура такта"));
		bHasPendingCameraIntent = true;
		bPendingIntentIsFollow = true;
		bPendingIntentIsDirected = false; // follow — всегда фон
		PendingFollowTarget = Target;
		return;
	}

	UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Follow → %s (был %s, кадр=%d)"),
		*GetNameSafe(Target), *GetNameSafe(FollowTarget.Get()), bShotFraming ? 1 : 0);

	// Следование — новый интент: кадр выстрела (например, предыдущего врага)
	// бросаем, иначе его таймер потом дёрнет камеру с сопровождаемого юнита.
	AbandonShotFraming();

	FollowTarget = Target;
	bHasFocusGoal = Target != nullptr; // цель обновляется каждый тик в Tick
	if (Target)
	{
		FocusGoal = Target->GetActorLocation();
	}
}

void ATacticalCameraPawn::ClearFollowTarget()
{
	// Отмена следования во время удержания отменяет и отложенный follow —
	// иначе снятие удержания вернуло бы отменённое сопровождение.
	if (ShouldDeferAmbientIntent() && bHasPendingCameraIntent && bPendingIntentIsFollow)
	{
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Отложенный follow отменён"));
		bHasPendingCameraIntent = false;
		PendingFollowTarget = nullptr;
	}

	if (FollowTarget.IsValid())
	{
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Follow снят (был %s)"),
			*GetNameSafe(FollowTarget.Get()));
		FollowTarget = nullptr;
		bHasFocusGoal = false;
	}
}

void ATacticalCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Камера — режиссура, а не мир: она обязана жить в РЕАЛЬНОМ времени.
	// Slow-mo реакции (SetGlobalTimeDilation) замедляет DeltaSeconds, и камера
	// летела к кадру в 4 раза медленнее, тогда как таймеры презентации
	// (умноженные на дилатацию) шли в реальном темпе — монтаж стартовал, пока
	// камера была на полпути. Раздилатированная дельта выравнивает оба мира.
	const AWorldSettings* WorldSettings = GetWorldSettings();
	const float Dilation = WorldSettings ? WorldSettings->GetEffectiveTimeDilation() : 1.f;
	if (Dilation > KINDA_SMALL_NUMBER)
	{
		DeltaSeconds /= Dilation;
	}

	// Живой кадр выстрела: точка взгляда пересчитывается по текущим позициям
	// участников (боец вышел из-за угла, цель падает) — см. UpdateShotFrameTracking.
	if (bShotFraming)
	{
		UpdateShotFrameTracking();
	}

	// Плавная доводка поворота к целевому значению по КРАТЧАЙШЕЙ дуге:
	// GetRelativeRotation() всегда возвращает Yaw, нормализованный в (-180,180],
	// поэтому обычный FInterpTo(Rot.Yaw, TargetYaw, ...) при накопленном TargetYaw
	// интерполировал «в лоб» по числам и после нескольких поворотов заставлял
	// камеру докручиваться на полный круг, чтобы догнать ушедшее далеко значение.
	FRotator Rot = SpringArm->GetRelativeRotation();
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(Rot.Yaw, TargetYaw);
	Rot.Yaw += FMath::FInterpTo(0.f, DeltaYaw, DeltaSeconds, InterpSpeed);
	// Наклон: тактические −55° ↔ пологий кадр выстрела (диапазон малый, простой FInterpTo).
	Rot.Pitch = FMath::FInterpTo(Rot.Pitch, TargetPitch, DeltaSeconds, InterpSpeed);
	SpringArm->SetRelativeRotation(Rot);

	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetZoom, DeltaSeconds, InterpSpeed);

	// Обзор доводится линейно (как `X2Camera_LookAt::InterpolateFOV`): переход
	// «тактический ↔ кадр выстрела» должен читаться как наезд объектива, а
	// экспоненциальная доводка на коротком кадре не успевает дойти до цели.
	if (Camera)
	{
		const float FovNow = Camera->FieldOfView;
		const float FovStep = FMath::Min(FMath::Abs(TargetFov - FovNow), FovInterpSpeed * DeltaSeconds);
		if (FovStep > KINDA_SMALL_NUMBER)
		{
			Camera->SetFieldOfView(FovNow + FMath::Sign(TargetFov - FovNow) * FovStep);
		}
	}

	// Высота точки вращения задана МИРОВОЙ координатой и пересчитывается в
	// смещение относительно ТЕКУЩЕЙ высоты пешки каждый кадр — иначе расчёт и
	// построение расходятся на любом перепаде высот (пешка по Z не двигается).
	// Ручной выбор этажа живёт ПОВЕРХ: он смещает плоскость обзора и переживает
	// focus/follow, которые задают саму плоскость (в кадре выстрела не действует —
	// там высоту диктует композиция).
	const float FloorOffset = bShotFraming ? 0.f : ViewFloorSteps * ViewFloorStepHeight;
	const float DesiredOffsetZ =
		FMath::Clamp(PivotWorldZ + FloorOffset - GetActorLocation().Z, -MaxPivotZOffset, MaxPivotZOffset);
	FVector Offset = SpringArm->TargetOffset;
	Offset.Z = FMath::FInterpTo(Offset.Z, DesiredOffsetZ, DeltaSeconds, InterpSpeed);
	SpringArm->TargetOffset = Offset;

	// Полёт к цели фокуса / следование за актором (XCOM-glide, только XY).
	if (FollowTarget.IsValid())
	{
		FocusGoal = FollowTarget->GetActorLocation();
		if (!bShotFraming)
		{
			PivotWorldZ = FocusGoal.Z; // сопровождаемый ушёл на уступ — камера за ним
		}
	}
	else if (FollowTarget.IsStale())
	{
		// Цель уничтожена (юнит умер в кадре) — долетаем до последней точки.
		FollowTarget = nullptr;
	}

	if (bHasFocusGoal)
	{
		const FVector Current = GetActorLocation();
		const FVector Goal(FocusGoal.X, FocusGoal.Y, Current.Z);
		SetActorLocation(FMath::VInterpTo(Current, Goal, DeltaSeconds, FocusInterpSpeed));

		// Долетели и никого не сопровождаем — фокус завершён. В кадре выстрела
		// фокус НЕ гасим: кадр держит точку смотрения до самого выхода.
		if (!FollowTarget.IsValid() && !bShotFraming && FVector::DistSquared2D(GetActorLocation(), Goal) < 25.f)
		{
			bHasFocusGoal = false;
		}
	}

	// Страховка режиссёрского удержания: владелец мог не отпустить (такт живёт
	// в состоянии, которое ждёт игрока). Камера не имеет права зависнуть.
	if (bDirectorHold && DirectorHoldTimeLeft > 0.f)
	{
		DirectorHoldTimeLeft -= DeltaSeconds;
		if (DirectorHoldTimeLeft <= 0.f)
		{
			UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] РЕЖИССУРА: удержание истекло по времени"));
			ReleaseDirectorHold();
		}
	}

	// Кадр выстрела с конечной длительностью сам себя снимает.
	if (bShotFraming && ShotFrameTimeLeft >= 0.f)
	{
		ShotFrameTimeLeft -= DeltaSeconds;
		if (ShotFrameTimeLeft <= 0.f)
		{
			UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Таймер кадра выстрела истёк → возврат ракурса"));
			ClearShotFraming();
		}
	}
}

// --- Кадр выстрела / прицеливания (XCOM) --------------------------------------------

void ATacticalCameraPawn::FrameShot(const AActor* Shooter, const AActor* Target)
{
	// Прицеливание: фоновые интенты такой кадр перебивают.
	EnterShotFraming(Shooter, Target, -1.f, /*bPresentation=*/false);
}

void ATacticalCameraPawn::FrameShotForDuration(const AActor* Shooter, const AActor* Target, float Duration)
{
	const float ResolvedDuration = Duration < 0.f
		? -1.f
		: (Duration > 0.f ? Duration : ShotFrameDuration);
	EnterShotFraming(Shooter, Target, ResolvedDuration, /*bPresentation=*/true);
}

void ATacticalCameraPawn::FocusOnLocationDirected(const FVector& Location, float HoldDuration)
{
	// Режиссура сильнее прежней режиссуры: новый такт — новый владелец взгляда.
	// Кадр выстрела при этом не рвём: если он ещё живёт, фокус такта исполнится
	// сразу после него (как отложенный, но с пометкой «режиссёрский»).
	bDirectorHold = false;
	UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] РЕЖИССУРА: взгляд удержан на %s (%.1f с)"),
		*Location.ToCompactString(), HoldDuration);
	{
		TGuardValue<bool> DirectedGuard(bMarkingDirectedIntent, true);
		FocusOnLocation(Location);
	}
	bDirectorHold = true;
	DirectorHoldTimeLeft = HoldDuration > 0.f ? HoldDuration : -1.f;
}

void ATacticalCameraPawn::ReleaseDirectorHold()
{
	if (!bDirectorHold)
	{
		return;
	}
	bDirectorHold = false;
	DirectorHoldTimeLeft = -1.f;
	UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] РЕЖИССУРА: удержание снято"));
	ApplyPendingCameraIntent();
}

void ATacticalCameraPawn::BreakDirectorHold()
{
	if (!bDirectorHold && !bHasPendingCameraIntent)
	{
		return;
	}
	UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] РЕЖИССУРА: прервана игроком"));
	bDirectorHold = false;
	DirectorHoldTimeLeft = -1.f;
	bHasPendingCameraIntent = false;
	PendingFollowTarget = nullptr;
}

bool ATacticalCameraPawn::ApplyPendingCameraIntent()
{
	if (!bHasPendingCameraIntent)
	{
		return false;
	}
	// ⚠️ Порядок владения: кадр выстрела > режиссура такта > фон. Пока живёт
	// кадр презентации, НИКАКОЙ отложенный интент не исполняется — иначе снятие
	// режиссёрского удержания посреди выстрела бросало кадр (лог: такт B5
	// закончился на Commit и увёл камеру до смерти цели). Интент остаётся
	// накопленным, его исполнит ClearShotFraming терминала.
	if (IsPlayingPresentationFrame())
	{
		return false;
	}
	// Кадр кончился, но такт ещё говорит — фоновый интент продолжает ждать.
	// Исполняем сразу только сам режиссёрский фокус.
	if (bDirectorHold && !bPendingIntentIsDirected)
	{
		return false;
	}
	bHasPendingCameraIntent = false;
	// Сам вызов не должен снова уйти в отложенные.
	TGuardValue<bool> ApplyGuard(bApplyingPendingIntent, true);

	// Кадра уже нет — вызовы отработают обычным путём (монополия снята).
	if (bPendingIntentIsFollow)
	{
		const AActor* Target = PendingFollowTarget.Get();
		PendingFollowTarget = nullptr;
		UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Исполняю отложенный follow → %s"),
			*GetNameSafe(Target));
		if (!Target)
		{
			return false; // цель погибла за время кадра — держим возврат кадра
		}
		SetFollowTarget(Target);
		return true;
	}

	UE_LOG(LogXRU1Camera, Display, TEXT("[Camera] Исполняю отложенный focus → %s (instant=%d)"),
		*PendingFocusLocation.ToCompactString(), bPendingFocusInstant ? 1 : 0);
	FocusOnLocation(PendingFocusLocation, bPendingFocusInstant);
	return true;
}

void ATacticalCameraPawn::EnterShotFraming(const AActor* Shooter, const AActor* Target,
	float Duration, bool bPresentation)
{
	// ⚠️ Признак монополии выставляется ТОЛЬКО после проверки участников: если
	// выставить его в FrameShot* до неё, вызов с пустой целью (кадр не
	// строится) молча понизил бы ЖИВОЙ кадр презентации до прицеливания, и
	// фоновый интент увёл бы камеру с выстрела.
	if (!Shooter || !Target)
	{
		return;
	}

	// Позицию/наклон до кадра запоминаем ОДИН раз за вход в кадр:
	// повторный FrameShot (переключение цели табом) не должен затирать его уже
	// кадровыми значениями — иначе после выхода игрок останется в наезде навсегда.
	if (!bShotFraming)
	{
		PreShotPivotZ = PivotWorldZ;
		// Если камера СЕЙЧАС летит к цели (только что выбрали бойца — glide ещё
		// идёт), «прежняя» позиция — это КОНЕЦ полёта, а не промежуточная точка,
		// иначе возврат после выстрела/отмены встанет посреди карты.
		PreShotLocation = bHasFocusGoal
			? FVector(FocusGoal.X, FocusGoal.Y, GetActorLocation().Z)
			: GetActorLocation();
	}
	bShotFraming = true;
	bPresentationFrame = bPresentation;
	ShotFrameTimeLeft = Duration;

	UE_LOG(LogXRU1Camera, Display,
		TEXT("[Camera] Кадр выстрела: %s → %s, duration=%.2f (−1 = до терминала), dist2D=%.0f"),
		*GetNameSafe(Shooter), *GetNameSafe(Target), Duration,
		FVector::Dist2D(Shooter->GetActorLocation(), Target->GetActorLocation()));

	// --- Что кадр ОБЯЗАН показать --------------------------------------------
	//
	// Точка стрелка — НЕ центр его капсулы, а точка, из которой он реально будет
	// стрелять: `GetFiringStance` отдаёт позицию глаз с учётом выхода за угол
	// (StepOut) или подъёма над укрытием (OverCover). Это наш аналог
	// `GetPredictedHeadLocation` из XCOM, и именно его отсутствие давало кадры,
	// где пик и выстрел происходят вне экрана: камера считалась по позе «до».
	FVector FiringEye = FVector::ZeroVector;
	UTacticsCombatStatics::GetFiringStance(Shooter, Target, FiringEye);
	const FVector ShooterCenterAim = Shooter->GetActorLocation() + FVector(0.f, 0.f, ShotFrameAimHeight);
	const FVector ShooterAim = FiringEye.IsNearlyZero() ? ShooterCenterAim : FiringEye;
	const FVector TargetAim = Target->GetActorLocation() + FVector(0.f, 0.f, ShotFrameAimHeight);

	FVector Axis = TargetAim - ShooterAim;
	Axis.Z = 0.f;
	const float Dist = Axis.Size();
	if (!Axis.Normalize())
	{
		// Вырожденный случай (цель ровно над стрелком) — держим текущий ракурс.
		Axis = FRotator(0.f, TargetYaw, 0.f).Vector();
	}
	const FVector Side = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();

	// --- Адаптив по дистанции (XCOM action-cam) --------------------------------
	const float FarAlpha = ShotFrameFarDistance > 1.f
		? FMath::Clamp(Dist / ShotFrameFarDistance, 0.f, 1.f) : 0.f;
	const float Back = FMath::Lerp(ShotFrameBackNear, ShotFrameBackFar, FarAlpha);
	const float Shoulder = FMath::Lerp(ShotFrameShoulderNear, ShotFrameShoulderFar, FarAlpha);
	const float Lift = FMath::Lerp(ShotFrameHeightNear, ShotFrameHeightFar, FarAlpha);
	const float Bias = FMath::Lerp(ShotFrameTargetBias, ShotFrameTargetBiasFar, FarAlpha);

	// --- Дальний (squadsight) выстрел: кадр строится ВОКРУГ ЦЕЛИ ---------------
	// На такой дистанции «из-за плеча» физически не может показать обоих: цель
	// вырождается в точку, а зритель должен увидеть попадание и урон. Камера
	// встаёт на линию выстрела рядом с целью, стрелок остаётся за камерой.
	const bool bLongShot = Dist > ShotFrameTargetOnlyDistance;
	const float BackDistance = bLongShot ? ShotFrameLongShotBack : Back;

	const FVector LookPoint = bLongShot ? TargetAim : FMath::Lerp(ShooterAim, TargetAim, Bias);

	// ПЕРЕПАД ВЫСОТ. Якорь по XY остаётся у стрелка (кадр всё-таки из-за его
	// плеча), но по высоте подтягивается к точке взгляда: боец на крыше и цель
	// внизу иначе давали кадр, где камера висит на уровне стрелка и смотрит в
	// пол почти вертикально — обе фигуры вырождались. `ShotFrameHeightBlend`
	// задаёт, насколько высота камеры следует за композицией, а не за стрелком.
	FVector CameraAnchor = bLongShot ? TargetAim : ShooterAim;
	if (!bLongShot)
	{
		CameraAnchor.Z = FMath::Lerp(ShooterAim.Z, LookPoint.Z, ShotFrameHeightBlend);
	}

	// СТОРОНА ВЫГЛЯДЫВАНИЯ: точка выстрела уже смещена к тому краю укрытия,
	// откуда боец высунется. Её проекция на боковую ось и говорит, с какой
	// стороны обязана стоять камера, чтобы пик и оружие были видны.
	float PreferredSideSign = 0.f;
	if (!bLongShot)
	{
		const float PeekOffset = FVector::DotProduct(ShooterAim - ShooterCenterAim, Side);
		if (FMath::Abs(PeekOffset) > 20.f) // меньше — это шум позы, а не выход за угол
		{
			PreferredSideSign = FMath::Sign(PeekOffset);
		}
	}

	// --- ПРАВИЛО 180°: пара сменилась — ось съёмки можно выбрать заново --------
	if (LastFramedShooter.Get() != Shooter || LastFramedTarget.Get() != Target)
	{
		LastShoulderSign = 0.f;
	}

	// --- ЛЕСТНИЦА КАНДИДАТОВ: плечо (±) × подъём (0/1/2 шага) ------------------
	// Порядок перебора задаёт предпочтение: сначала оба плеча на «своей» высоте,
	// и только если оттуда цель закрыта — подъём над укрытием. Так камера не
	// уезжает в вертикаль без нужды, но и не показывает игроку стену.
	//
	// Выбираем МИНИМАЛЬНЫЙ штраф (схема XCOM `X2Camera_OverTheShoulder`), а не
	// максимальный балл: у них так, и веса переносятся без пересчёта знаков.
	FVector BestCam = CameraAnchor - Axis * BackDistance + Side * Shoulder + FVector(0.f, 0.f, Lift);
	float BestPenalty = FLT_MAX;
	float BestSideSign = LastShoulderSign != 0.f ? LastShoulderSign : 1.f;

	// ЧТО ОБЯЗАНО ВЛЕЗТЬ В КАДР: ЦЕЛЬ целиком (голова, грудь, ноги) и точка
	// выстрела стрелка.
	//
	// ⚠️ Стрелок входит ОДНОЙ точкой, а не габаритами. Требовать его целиком
	// нельзя физически: камера стоит в полутора метрах за спиной, и чтобы в
	// кадр влезли его голова и ноги, ей пришлось бы отъехать на десятки метров
	// (в прогоне 2026-08-02 это дало `arm=3000` — потолок — и `pitch=-2` на
	// дистанции 20 м, то есть плоский дальний план вместо кадра из-за плеча).
	// В XCOM «голова + ступни» — правило обзорной `X2Camera_Midpoint`, а OTS
	// показывает стрелка частично: его спина работает рамкой кадра, а не
	// объектом съёмки.
	// ЧТО ОБЯЗАНО ВЛЕЗТЬ В КАДР: обе фигуры целиком — голова, грудь и ноги
	// стрелка и цели.
	//
	// ⚠️ Да, требование к стрелку отгоняет камеру назад: он стоит в паре метров
	// от неё, и его габариты стоят дороже всего остального. Это ОСОЗНАННАЯ цена.
	// Проверено прогоном 2026-08-03: попытка «оптимизировать» — вписывать только
	// цель, а стрелка держать в кадре расчётной геометрией выноса — даёт кадр,
	// в котором своего бойца не видно вообще (он уходит на 16° ниже оси при
	// нижнем крае 14.7°), а камера ложится почти на землю (`pitch=-1`), и склон
	// закрывает пол-экрана. Гарантия «обе фигуры в кадре» важнее компактного
	// отъезда: пусть камера стоит дальше, но показывает бой, а не пейзаж.
	TArray<FVector, TInlineAllocator<6>> SubjectPoints;
	if (!bLongShot)
	{
		GetSubjectPoints(Shooter, ShooterAim, SubjectPoints);
	}
	GetSubjectPoints(Target, TargetAim, SubjectPoints);

	// Стоимость выбора ракурса измеряем, а не предполагаем: в худшем случае
	// (чистого кандидата нет) перебор доходит до трёх кругов по ~60 позиций, и
	// каждая стоит нескольких сферо-свипов. Это разовое событие, но пошаговая
	// игра не должна ловить хич на выстреле — по логу видно, когда он появился.
	const double LadderStartSeconds = FPlatformTime::Seconds();

	auto RunLadder = [&]()
	{
		BestPenalty = FLT_MAX;
		// Перебор упорядочен по предпочтению (своя высота → подъём, авторский
		// ракурс → дуга), и каждый кандидат стоит нескольких сферо-свипов: до
		// 60 позиций × 5 трейсов на один выстрел. Нашли чистый — дальше не ищем.
		bool bFoundPerfect = false;
		for (int32 Step = 0; Step < 3 && !bFoundPerfect; ++Step)
		{
			for (const float SideSign : {1.f, -1.f})
			{
				if (bFoundPerfect) { break; }
				// Дальний кадр пробует ОБА конца оси: со стороны стрелка и
				// ОБРАТНЫЙ ракурс из-за цели. Стрелка в таком кадре нет, линия
				// 180° не читается, а цель за укрытием со «своей» стороны часто
				// не видна вообще (лог B5: penalty 96 = цель закрыта, камеру
				// вжало в геометрию на 2.6 м).
				//
				// ⚠️ Предпочтение — за ОБРАТНЫМ: на squadsight-дистанции кадр
				// «из-за цели» показывает и попадание, и то, откуда прилетело,
				// и именно он читается как снайперский выстрел (прямая оценка
				// игрока: «обратный ракурс от врага был крутой»). Ракурс со
				// стороны стрелка остаётся запасным и потому штрафуется.
				const int32 AxisVariants = bLongShot ? 2 : 1;
				for (int32 AxisIndex = 0; AxisIndex < AxisVariants; ++AxisIndex)
				{
					const float AxisSign = AxisIndex == 0 ? -1.f : 1.f;
					const FVector BaseCam = CameraAnchor
						+ Axis * (BackDistance * AxisSign)
						+ Side * (Shoulder * SideSign)
						+ FVector(0.f, 0.f, Lift + Step * ShotFrameClearanceLift);

					// ОБХОД ПО ДУГЕ вокруг точки взгляда. Без него набор
					// кандидатов — четыре точки, и сцена, где все они за мешом,
					// заканчивалась кадром в стену. XCOM решает это количеством
					// авторских камер; у нас — вращением базовой позиции.
					for (int32 ArcIndex = 0; ArcIndex <= 2 * ShotFrameArcSteps; ++ArcIndex)
					{
						// Порядок: 0, +sweep, −sweep, +2·sweep, −2·sweep… —
						// сначала пробуем ближайшие к авторской точке ракурсы.
						const int32 ArcMagnitude = (ArcIndex + 1) / 2;
						const float ArcAngle = ArcMagnitude * ShotFrameArcSweep *
							((ArcIndex % 2) == 1 ? 1.f : -1.f);

						FVector Cam = LookPoint + (BaseCam - LookPoint).RotateAngleAxis(ArcAngle, FVector::UpVector);

						// ФАКТИЧЕСКАЯ сторона съёмки — по готовой позиции, а не
						// по номинальному плечу: обход по дуге на 25–50° сам
						// способен перенести камеру через ось стрелок→цель, и
						// правило 180° считало бы сторону, которой уже нет.
						const float ActualSide = FMath::Sign(
							FVector::DotProduct(Cam - LookPoint, Side));

						// В дальнем кадре в поле зрения обязана поместиться
						// только цель: «влезь и стрелок» отгоняло камеру на
						// десятки метров.
						Cam = FitSubjectsInFrame(Cam, LookPoint, SubjectPoints);
						Cam = PullCameraOutOfGeometry(LookPoint, Cam);

						// Подъём и дуга — компромиссы одного порядка: чем выше
						// камера, тем меньше «из-за плеча»; чем дальше по дуге,
						// тем меньше кадр похож на задуманный. Оба штрафуются
						// весом закрытого корпуса, то есть уступают только
						// действительно испорченному кадру.
						const float Penalty = ScoreShotCandidate(Cam, LookPoint, ShooterAim, TargetAim,
							ActualSide, PreferredSideSign, /*bIgnoreShooter=*/bLongShot)
							+ Step * PenaltyShooterWaistBlocked
							+ ArcMagnitude * PenaltyShooterWaistBlocked
							// Только в дальнем кадре: в ближнем вариант оси один, и
							// постоянная добавка сдвинула бы абсолютный штраф, по
							// которому решается «можно ли нарушить правило 180°».
							+ ((bLongShot && AxisIndex == 0) ? LongShotFrontAnglePenalty : 0.f);
						if (Penalty < BestPenalty)
						{
							BestPenalty = Penalty;
							BestCam = Cam;
							BestSideSign = ActualSide;
						}
						if (BestPenalty <= KINDA_SMALL_NUMBER)
						{
							bFoundPerfect = true; // чище кадра уже не будет
							break;
						}
					}
					if (bFoundPerfect) { break; }
				}
				if (bFoundPerfect) { break; }
			}
		}
	};

	RunLadder();

	// ⚠️ ЕДИНСТВЕННОЕ основание нарушить правило 180°: со «своей» стороны ЦЕЛЬ не
	// видно ни с одной высоты. Бесполезный кадр хуже, чем скачок через ось —
	// но только он это и оправдывает. Проверяем по штрафу цели: он уникален по
	// величине среди «мягких», и его наличие означает «в кого стреляем, не видно».
	if (LastShoulderSign != 0.f && BestPenalty >= PenaltyTargetBlocked)
	{
		const float StickySide = LastShoulderSign;
		LastShoulderSign = 0.f; // временно снимаем ось — оба плеча равноправны
		RunLadder();
		if (BestPenalty >= PenaltyTargetBlocked)
		{
			// И с другой стороны не лучше — остаёмся на прежней оси, чтобы хотя
			// бы не дёргать зрителя зря.
			LastShoulderSign = StickySide;
			RunLadder();
		}
	}

	// Запоминаем ось съёмки: пока пара стрелок↔цель та же, камера обязана
	// оставаться со своей стороны (правило 180°, см. PenaltyCrosscut).
	LastShoulderSign = BestSideSign;
	LastFramedShooter = Shooter;
	LastFramedTarget = Target;

	// --- Живой кадр: запоминаем, ЧТО снимаем и ОТКУДА --------------------------
	// Смещение камеры относительно точки взгляда — и есть выбранный ракурс.
	// Дальше он держится, а точка взгляда едет за участниками (UpdateShotFrameTracking).
	ShotFrameShooter = Shooter;
	ShotFrameTarget = Target;
	ShotFrameShooterStart = Shooter->GetActorLocation();
	ShotFrameTargetStart = Target->GetActorLocation();
	ShotFrameLookPoint = LookPoint;
	ShotFrameCamOffset = BestCam - LookPoint;
	ShotFrameLookBias = Bias;
	bShotFrameLongShot = bLongShot;

	// Обзор кадра — авторский «телевик» (XCOM снимает выстрел на 50°, а не на
	// тактическом угле): фигуры крупнее, периферия не растягивается.
	TargetFov = ShotFrameFov;

	ApplyShotFramePose(BestCam, LookPoint);

	UE_LOG(LogXRU1Camera, Display,
		TEXT("[Camera] Кадр построен (%s, %s): cam=%s look=%s arm=%.0f yaw=%.0f pitch=%.0f fov=%.0f ")
		TEXT("penalty=%.1f плечо=%+.0f пик=%+.0f за %.2f мс"),
		bLongShot ? TEXT("дальний: вокруг цели") : TEXT("из-за плеча"),
		bPresentationFrame ? TEXT("презентация") : TEXT("прицеливание"),
		*BestCam.ToCompactString(), *LookPoint.ToCompactString(), TargetZoom,
		TargetYaw, TargetPitch, TargetFov, BestPenalty, BestSideSign, PreferredSideSign,
		(FPlatformTime::Seconds() - LadderStartSeconds) * 1000.0);
}

void ATacticalCameraPawn::ApplyShotFramePose(const FVector& CamPos, const FVector& LookPoint)
{
	// Камера пружины = Pivot − Rot.Vector() × Arm. Берём Rot как взгляд из
	// найденной позиции в точку взгляда, а Arm — как расстояние до неё: тогда
	// конец пружины попадает РОВНО в CamPos, и посчитанное совпадает с
	// показанным (схема до 2026-07-25 это свойство теряла).
	const FVector ToLook = LookPoint - CamPos;
	const FRotator ShotRot = ToLook.Rotation();

	TargetYaw = ShotRot.Yaw;
	TargetPitch = FMath::Clamp(ShotRot.Pitch, -85.f, 45.f);
	TargetZoom = FMath::Clamp(ToLook.Size(), ShotFrameMinArm, ShotFrameMaxArm);
	PivotWorldZ = LookPoint.Z;

	// Следование за бегущим на время кадра снимаем — иначе перетянет фокус.
	FollowTarget = nullptr;
	FocusGoal = LookPoint; // XY доводится полётом пешки, Z — через PivotWorldZ
	bHasFocusGoal = true;
}

void ATacticalCameraPawn::UpdateShotFrameTracking()
{
	const AActor* Shooter = ShotFrameShooter.Get();
	const AActor* Target = ShotFrameTarget.Get();
	if (!Shooter && !Target)
	{
		return; // оба исчезли (смерть в кадре) — держим последний ракурс
	}

	// Смещение участников ОТНОСИТЕЛЬНО позиций на момент построения. Именно
	// относительное, а не абсолютные позиции: кадр строился по предсказанной
	// точке выстрела (выход за угол), и переход на «текущий центр капсулы»
	// дёрнул бы камеру назад к позе «до пика», а потом снова вперёд.
	// Ограничение сдвига — страховка от отброса тела/телепорта: кадр обязан
	// остаться на месте сцены, даже если актор уехал (`ShotFrameMaxTrackDrift`).
	const FVector ShooterDrift = Shooter
		? (Shooter->GetActorLocation() - ShotFrameShooterStart).GetClampedToMaxSize(ShotFrameMaxTrackDrift)
		: FVector::ZeroVector;
	const FVector TargetDrift = Target
		? (Target->GetActorLocation() - ShotFrameTargetStart).GetClampedToMaxSize(ShotFrameMaxTrackDrift)
		: FVector::ZeroVector;

	const FVector Drift = bShotFrameLongShot
		? TargetDrift
		: FMath::Lerp(ShooterDrift, TargetDrift, ShotFrameLookBias);

	const FVector Look = ShotFrameLookPoint + Drift;
	ApplyShotFramePose(Look + ShotFrameCamOffset, Look);
}

bool ATacticalCameraPawn::IsSegmentClear(const FVector& From, const FVector& To, float* OutBlockedFraction) const
{
	if (OutBlockedFraction)
	{
		*OutBlockedFraction = 0.f;
	}
	const UWorld* World = GetWorld();
	const float Length = FVector::Dist(From, To);
	if (!World || Length < 1.f)
	{
		return true;
	}

	// Толстый свип, а не волосяной луч: XCOM проверяет ракурс лучом толщиной
	// `TraceWidth = 20`, и по той же причине — щель между мешами не считается
	// «видно», зритель её тоже не увидит.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShotFrameVis), /*bTraceComplex=*/false);
	FHitResult Hit;
	const bool bBlocked = ShotFrameTraceWidth > 0.f
		? World->SweepSingleByObjectType(Hit, From, To, FQuat::Identity,
			UTacticsCombatStatics::GetShotGeometryObjects(),
			FCollisionShape::MakeSphere(ShotFrameTraceWidth), Params)
		: World->LineTraceSingleByObjectType(Hit, From, To,
			UTacticsCombatStatics::GetShotGeometryObjects(), Params);
	if (!bBlocked)
	{
		return true;
	}
	if (OutBlockedFraction)
	{
		// 0 — стена у самого конца отрезка, 1 — прямо перед стартом.
		*OutBlockedFraction = FMath::Clamp(1.f - Hit.Distance / Length, 0.f, 1.f);
	}
	return false;
}

void ATacticalCameraPawn::GetSubjectPoints(const AActor* Subject, const FVector& AimOverride,
	TArray<FVector, TInlineAllocator<6>>& OutPoints) const
{
	if (!Subject)
	{
		return;
	}

	// XY берём из точки прицела (для стрелка это позиция ВЫХОДА из-за угла, а не
	// центр капсулы) — фигура в кадре окажется именно там.
	const FVector Center = Subject->GetActorLocation();
	const FVector Aim = AimOverride.IsNearlyZero()
		? Center + FVector(0.f, 0.f, ShotFrameAimHeight)
		: AimOverride;

	float Radius = 0.f;
	float HalfHeight = 0.f;
	Subject->GetSimpleCollisionCylinder(Radius, HalfHeight);
	if (HalfHeight < KINDA_SMALL_NUMBER)
	{
		HalfHeight = 90.f; // нет капсулы (маркер, объект) — обычный рост бойца
	}

	OutPoints.Add(Aim);                                            // линия груди / выстрела
	OutPoints.Add(FVector(Aim.X, Aim.Y, Center.Z + HalfHeight));   // голова
	OutPoints.Add(FVector(Aim.X, Aim.Y, Center.Z - HalfHeight));   // ноги
}

FVector ATacticalCameraPawn::FitSubjectsInFrame(const FVector& CamPos, const FVector& LookPoint,
	const TArray<FVector, TInlineAllocator<6>>& Subjects) const
{
	// Полууглы кадра: горизонтальный — из FOV кадра выстрела, вертикальный — с
	// учётом соотношения сторон. Вертикальный уже, поэтому именно он обычно и
	// решает. Берём именно кадровый угол, а не текущий: в момент расчёта обзор
	// ещё доводится к нему, и считать по промежуточному значению — значит
	// получить композицию, которая через полсекунды разъедется.
	const float TanH = FMath::Tan(FMath::DegreesToRadians(0.5f * ShotFrameFov)) * ShotFrameFovSafety;
	const float TanV = TanH / 1.7777f; // 16:9
	if (TanH <= KINDA_SMALL_NUMBER)
	{
		return CamPos;
	}

	FVector Result = CamPos;
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		const FRotator ViewRot = (LookPoint - Result).Rotation();
		float NeedScale = 1.f;
		for (const FVector& Subject : Subjects)
		{
			// В систему координат камеры: X — вперёд, Y — вправо, Z — вверх.
			const FVector Local = ViewRot.UnrotateVector(Subject - Result);
			if (Local.X <= 1.f)
			{
				NeedScale = FMath::Max(NeedScale, 2.f); // фигура позади камеры — отъезжаем
				continue;
			}
			NeedScale = FMath::Max(NeedScale, FMath::Abs(Local.Y) / (Local.X * TanH));
			NeedScale = FMath::Max(NeedScale, FMath::Abs(Local.Z) / (Local.X * TanV));
		}
		if (NeedScale <= 1.f)
		{
			break;
		}

		// Отъезд назад по оси взгляда: угловой размер сцены падает примерно
		// обратно пропорционально дистанции, поэтому пары итераций хватает.
		const float CurrentArm = FVector::Dist(Result, LookPoint);
		const float NewArm = FMath::Min(CurrentArm * FMath::Min(NeedScale, 2.f), ShotFrameMaxArm);
		if (NewArm <= CurrentArm + 1.f)
		{
			break; // упёрлись в потолок отъезда
		}
		Result = LookPoint + (Result - LookPoint).GetSafeNormal() * NewArm;
	}
	return Result;
}

FVector ATacticalCameraPawn::PullCameraOutOfGeometry(const FVector& LookPoint, const FVector& CamPos) const
{
	const UWorld* World = GetWorld();
	const FVector ToCam = CamPos - LookPoint;
	const float Arm = ToCam.Size();
	if (!World || Arm < 1.f)
	{
		return CamPos;
	}

	// Толстый свип, а не луч: волосяной трейс проскакивает в щель между мешами,
	// и камера оказывалась внутри стены целиком.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShotFrameArm), /*bTraceComplex=*/false);
	FHitResult Hit;
	if (World->SweepSingleByObjectType(Hit, LookPoint, CamPos, FQuat::Identity,
		UTacticsCombatStatics::GetShotGeometryObjects(),
		FCollisionShape::MakeSphere(20.f), Params))
	{
		const float SafeArm = FMath::Clamp(Hit.Distance - ShotFrameWallPadding, ShotFrameMinArm, Arm);
		return LookPoint + ToCam / Arm * SafeArm;
	}
	return CamPos;
}

float ATacticalCameraPawn::ScoreShotCandidate(const FVector& CamPos, const FVector& LookPoint,
	const FVector& ShooterAim, const FVector& TargetAim, float SideSign,
	float PreferredSideSign, bool bIgnoreShooter) const
{
	// ⚠️ ШТРАФ, а не балл: чем МЕНЬШЕ, тем лучше. Так устроен
	// `X2Camera_OverTheShoulder` в XCOM, и веса переносятся один в один.
	float Penalty = 0.f;

	// (1) ЦЕЛЬ — главное. Кадр, где не видно, в кого стреляешь, бесполезен.
	if (!IsSegmentClear(CamPos, TargetAim))
	{
		Penalty += PenaltyTargetBlocked;
	}

	// (2) СТРЕЛОК — раздельно голова и корпус, как в XCOM. Разделение не
	// формальность: «видно каску над укрытием» и «видно бойца целиком» — разные
	// кадры, и первый допустим, а раньше обе ситуации считались одинаково.
	// В дальнем кадре стрелок за камерой — его проверки исключены целиком.
	if (bIgnoreShooter)
	{
		if (!IsSegmentClear(CamPos, LookPoint))
		{
			Penalty += PenaltyBlockedStart;
		}
		if (LastShoulderSign != 0.f && SideSign * LastShoulderSign < 0.f)
		{
			Penalty += PenaltyCrosscut;
		}
		return Penalty;
	}

	// (2a) СТОРОНА ВЫГЛЯДЫВАНИЯ: камера обязана быть с той стороны, куда боец
	// высунется, иначе весь пик и выстрел уходят за его спину. Штраф мягкий —
	// закрытая цель (64) его перебивает, то есть ради видимого выстрела камера
	// перейдёт на «неправильную» сторону, но без нужды не станет.
	if (PreferredSideSign != 0.f && SideSign * PreferredSideSign < 0.f)
	{
		Penalty += PenaltyOffPeekSide;
	}

	const FVector ShooterHead = ShooterAim + FVector(0.f, 0.f, 55.f);
	const FVector ShooterWaist = ShooterAim - FVector(0.f, 0.f, 35.f);
	const bool bHeadClear = IsSegmentClear(CamPos, ShooterHead);
	const bool bWaistClear = IsSegmentClear(CamPos, ShooterWaist);
	if (!bHeadClear && !bWaistClear)
	{
		Penalty += PenaltyShooterNotVisible;
	}
	else
	{
		if (!bHeadClear)  { Penalty += PenaltyShooterHeadBlocked; }
		if (!bWaistClear) { Penalty += PenaltyShooterWaistBlocked; }
	}

	// (3) Камера отгорожена стеной от точки взгляда — она внутри геометрии.
	if (!IsSegmentClear(CamPos, LookPoint))
	{
		Penalty += PenaltyBlockedStart;
	}

	// (4) ПРАВИЛО 180°. Переход на другую сторону оси стрелок→цель меняет фигуры
	// местами в кадре и читается как «камеру развернуло». Штраф заведомо
	// перебивает всё остальное: сторона меняется, только если с прежней цель не
	// видно вообще (64 < 1000, значит один закрытый кадр смену НЕ оправдывает,
	// а вот безвыходная ситуация — оправдывает, потому что там штраф копится).
	if (LastShoulderSign != 0.f && SideSign * LastShoulderSign < 0.f)
	{
		Penalty += PenaltyCrosscut;
	}
	return Penalty;
}

void ATacticalCameraPawn::AbandonShotFraming()
{
	if (!bShotFraming)
	{
		return;
	}
	// Кто перечеркнул кадр — видно по предыдущей строке лога (Focus/Follow/ввод).
	UE_LOG(LogXRU1Camera, Display,
		TEXT("[Camera] Кадр выстрела БРОШЕН (timeLeft=%.2f, презентация=%d) — новый интент взгляда"),
		ShotFrameTimeLeft, bPresentationFrame ? 1 : 0);
	bShotFraming = false;
	bPresentationFrame = false;
	ShotFrameTimeLeft = -1.f;
	// Кадр перечёркнут явным интентом (ввод игрока/новый кадр) — отложенный
	// запрос устарел вместе с ним.
	bHasPendingCameraIntent = false;
	PendingFollowTarget = nullptr;

	ShotFrameShooter = nullptr;
	ShotFrameTarget = nullptr;

	// Новый focus/follow/pan может перечеркнуть ПОЗИЦИЮ старого кадра, но не имеет
	// права превращать временный yaw/zoom/обзор action-camera в глобальный ракурс.
	// Возвращаем постоянные пользовательские значения и наклон по правилам зума.
	TargetYaw = TacticalYaw;
	TargetZoom = TacticalZoom;
	TargetPitch = GetDesiredTacticalPitch();
	TargetFov = TacticalFov;
	// Высоту пивота тоже возвращаем: иначе панорама, начатая из прицеливания,
	// уехала бы по карте на линии груди вместо тактической плоскости. Вызовы
	// focus/follow перезапишут её сразу после Abandon — это правильный порядок.
	PivotWorldZ = PreShotPivotZ;
}

void ATacticalCameraPawn::ClearShotFraming()
{
	if (!bShotFraming)
	{
		return;
	}
	UE_LOG(LogXRU1Camera, Display,
		TEXT("[Camera] Кадр выстрела снят штатно → возврат к pre-shot %s (timeLeft=%.2f)"),
		*PreShotLocation.ToCompactString(), ShotFrameTimeLeft);
	bShotFraming = false;
	bPresentationFrame = false;
	ShotFrameTimeLeft = -1.f;
	ShotFrameShooter = nullptr;
	ShotFrameTarget = nullptr;

	// Полный возврат ракурса (XCOM): поворот, наклон, зум, обзор, высота и
	// позиция — как до кадра. Плавно, тем же glide-механизмом, что и фокус.
	TargetYaw = TacticalYaw;
	TargetZoom = TacticalZoom;
	TargetPitch = GetDesiredTacticalPitch();
	TargetFov = TacticalFov;
	PivotWorldZ = PreShotPivotZ;
	FocusGoal = PreShotLocation;
	bHasFocusGoal = true;

	// Интент, накопленный за время презентации, свежее возврата к pre-shot:
	// исполняем его ПОВЕРХ (новый шаг обучения уже выбрал, куда смотреть).
	ApplyPendingCameraIntent();
}
