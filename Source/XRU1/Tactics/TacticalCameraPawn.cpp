#include "TacticalCameraPawn.h"
#include "TacticsCombatStatics.h" // GetShotGeometryObjects — единая геометрия мира
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
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

	// Обводка юнитов при наведении: PP-материал блендаблом на unbound-компонент.
	if (OutlineMaterial && PostProcess)
	{
		PostProcess->Settings.AddBlendable(OutlineMaterial, 1.f);
	}
	else if (!OutlineMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Highlight] OutlineMaterial не назначен в пешке-камере — ")
			TEXT("обводки юнитов не будет (проверь Default Pawn Class в GameMode = BP_TacticalCameraPawn)"));
	}
}

void ATacticalCameraPawn::AddPanInput(const FVector2D& Input)
{
	if (Input.IsNearlyZero())
	{
		return;
	}

	// Ручная панорама разрывает автофокус/следование и кадр выстрела (как в XCOM).
	ClearFollowTarget();
	AbandonShotFraming();
	bHasFocusGoal = false;

	// Направления берём от текущего yaw камеры, движение — в плоскости земли.
	const FRotator YawRot(0.f, SpringArm->GetRelativeRotation().Yaw, 0.f);
	const FVector Forward = YawRot.RotateVector(FVector::ForwardVector);
	const FVector Right = YawRot.RotateVector(FVector::RightVector);

	const float Delta = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	AddActorWorldOffset((Forward * Input.Y + Right * Input.X) * PanSpeed * Delta);
}

void ATacticalCameraPawn::AddRotationStep(float Direction)
{
	if (!FMath::IsNearlyZero(Direction))
	{
		// Нормализуем сразу, иначе yaw копится без ограничений (см. Tick).
		// TacticalYaw — постоянный выбор игрока; action-camera меняет только
		// TargetYaw и после себя всегда возвращается к TacticalYaw.
		TacticalYaw = FRotator::NormalizeAxis(
			TacticalYaw + RotationStep * FMath::Sign(Direction));
		if (!bShotFraming)
		{
			TargetYaw = TacticalYaw;
		}
	}
}

void ATacticalCameraPawn::AddZoomInput(float Input)
{
	TacticalZoom = FMath::Clamp(TacticalZoom - Input * ZoomStep, MinZoom, MaxZoom);
	if (!bShotFraming)
	{
		TargetZoom = TacticalZoom;
	}
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
	if (FollowTarget.IsValid())
	{
		FollowTarget = nullptr;
		bHasFocusGoal = false;
	}
}

void ATacticalCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

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

	// Высота точки вращения задана МИРОВОЙ координатой и пересчитывается в
	// смещение относительно ТЕКУЩЕЙ высоты пешки каждый кадр — иначе расчёт и
	// построение расходятся на любом перепаде высот (пешка по Z не двигается).
	const float DesiredOffsetZ =
		FMath::Clamp(PivotWorldZ - GetActorLocation().Z, -MaxPivotZOffset, MaxPivotZOffset);
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

	// Кадр выстрела с конечной длительностью сам себя снимает.
	if (bShotFraming && ShotFrameTimeLeft >= 0.f)
	{
		ShotFrameTimeLeft -= DeltaSeconds;
		if (ShotFrameTimeLeft <= 0.f)
		{
			ClearShotFraming();
		}
	}
}

// --- Кадр выстрела / прицеливания (XCOM) --------------------------------------------

void ATacticalCameraPawn::FrameShot(const AActor* Shooter, const AActor* Target)
{
	EnterShotFraming(Shooter, Target, -1.f); // держим до ClearShotFraming
}

void ATacticalCameraPawn::FrameShotForDuration(const AActor* Shooter, const AActor* Target, float Duration)
{
	EnterShotFraming(Shooter, Target, Duration > 0.f ? Duration : ShotFrameDuration);
}

void ATacticalCameraPawn::EnterShotFraming(const AActor* Shooter, const AActor* Target, float Duration)
{
	if (!Shooter || !Target)
	{
		return;
	}

	// Позицию/наклон до кадра запоминаем ОДИН раз за вход в кадр:
	// повторный FrameShot (переключение цели табом) не должен затирать его уже
	// кадровыми значениями — иначе после выхода игрок останется в наезде навсегда.
	if (!bShotFraming)
	{
		PreShotPitch = TargetPitch;
		PreShotPivotZ = PivotWorldZ;
		// Если камера СЕЙЧАС летит к цели (только что выбрали бойца — glide ещё
		// идёт), «прежняя» позиция — это КОНЕЦ полёта, а не промежуточная точка,
		// иначе возврат после выстрела/отмены встанет посреди карты.
		PreShotLocation = bHasFocusGoal
			? FVector(FocusGoal.X, FocusGoal.Y, GetActorLocation().Z)
			: GetActorLocation();
	}
	bShotFraming = true;
	ShotFrameTimeLeft = Duration;

	// --- Что кадр ОБЯЗАН показать: грудь стрелка и грудь цели ------------------
	// Обе точки участвуют и в проверке видимости, и в решении «влезают ли в FOV».
	const FVector ShooterAim = Shooter->GetActorLocation() + FVector(0.f, 0.f, ShotFrameAimHeight);
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

	const FVector LookPoint = FMath::Lerp(ShooterAim, TargetAim, Bias);

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
	FVector BestCam = ShooterAim - Axis * Back + Side * Shoulder + FVector(0.f, 0.f, Lift);
	float BestPenalty = FLT_MAX;
	float BestSideSign = LastShoulderSign != 0.f ? LastShoulderSign : 1.f;

	auto RunLadder = [&]()
	{
		BestPenalty = FLT_MAX;
		for (int32 Step = 0; Step < 3; ++Step)
		{
			for (const float SideSign : {1.f, -1.f})
			{
				FVector Cam = ShooterAim
					- Axis * Back
					+ Side * (Shoulder * SideSign)
					+ FVector(0.f, 0.f, Lift + Step * ShotFrameClearanceLift);

				Cam = FitSubjectsInFrame(Cam, LookPoint, ShooterAim, TargetAim);
				Cam = PullCameraOutOfGeometry(LookPoint, Cam);

				// Подъём — тоже компромисс: чем выше камера, тем меньше «из-за
				// плеча» и больше «сверху». Штраф порядка веса закрытого корпуса.
				const float Penalty = ScoreShotCandidate(Cam, LookPoint, ShooterAim, TargetAim, SideSign)
					+ Step * PenaltyShooterWaistBlocked;
				if (Penalty < BestPenalty)
				{
					BestPenalty = Penalty;
					BestCam = Cam;
					BestSideSign = SideSign;
				}
			}
			// Кадр чистый — дальше поднимать незачем (подъём сам штрафуется).
			if (BestPenalty <= KINDA_SMALL_NUMBER)
			{
				break;
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

	// --- Перевод в параметры пружины -------------------------------------------
	// Камера пружины = Pivot − Rot.Vector() × Arm. Берём Rot как взгляд из
	// найденной позиции в точку взгляда, а Arm — как расстояние до неё: тогда
	// конец пружины попадает РОВНО в BestCam, и посчитанное совпадает с
	// показанным (прежняя схема это свойство теряла).
	const FVector ToLook = LookPoint - BestCam;
	const FRotator ShotRot = ToLook.Rotation();
	const float Arm = FMath::Clamp(ToLook.Size(), ShotFrameMinArm, ShotFrameMaxArm);

	TargetYaw = ShotRot.Yaw;
	TargetPitch = FMath::Clamp(ShotRot.Pitch, -85.f, 45.f);
	TargetZoom = Arm;
	PivotWorldZ = LookPoint.Z;

	// Следование за бегущим на время кадра снимаем — иначе перетянет фокус.
	FollowTarget = nullptr;
	FocusGoal = LookPoint; // XY доводится полётом пешки, Z — через PivotWorldZ
	bHasFocusGoal = true;
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

FVector ATacticalCameraPawn::FitSubjectsInFrame(const FVector& CamPos, const FVector& LookPoint,
	const FVector& SubjectA, const FVector& SubjectB) const
{
	// Полууглы кадра: горизонтальный — из FOV камеры, вертикальный — с учётом
	// соотношения сторон. Вертикальный уже, поэтому именно он обычно и решает.
	const float FovDegrees = Camera ? Camera->FieldOfView : 90.f;
	const float TanH = FMath::Tan(FMath::DegreesToRadians(0.5f * FovDegrees)) * ShotFrameFovSafety;
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
		for (const FVector& Subject : { SubjectA, SubjectB })
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
	const FVector& ShooterAim, const FVector& TargetAim, float SideSign) const
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
	bShotFraming = false;
	ShotFrameTimeLeft = -1.f;

	// Новый focus/follow/pan может перечеркнуть ПОЗИЦИЮ старого кадра, но не имеет
	// права превращать временный yaw/zoom action-camera в глобальный ракурс.
	// Возвращаем постоянные пользовательские значения и обычный наклон.
	TargetYaw = TacticalYaw;
	TargetZoom = TacticalZoom;
	TargetPitch = PreShotPitch;
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
	bShotFraming = false;
	ShotFrameTimeLeft = -1.f;

	// Полный возврат ракурса (XCOM): поворот, наклон, зум, высота обзора и
	// позиция — как до кадра. Плавно, тем же glide-механизмом, что и фокус.
	TargetYaw = TacticalYaw;
	TargetZoom = TacticalZoom;
	TargetPitch = PreShotPitch;
	PivotWorldZ = PreShotPivotZ;
	FocusGoal = PreShotLocation;
	bHasFocusGoal = true;
}
