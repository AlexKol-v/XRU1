#include "TacticalCameraPawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "Engine/EngineTypes.h" // ECollisionChannel / ECC_Visibility для трейсов кадра
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

	if (bInstant)
	{
		// Телепорт (старт боя): двигаем только в плоскости земли, высота пешки своя.
		bHasFocusGoal = false;
		SetActorLocation(FVector(Location.X, Location.Y, GetActorLocation().Z));
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

	// Подъём точки вращения к линии груди в кадре выстрела (в тактике — 0).
	FVector Offset = SpringArm->TargetOffset;
	Offset.Z = FMath::FInterpTo(Offset.Z, TargetLookHeight, DeltaSeconds, InterpSpeed);
	SpringArm->TargetOffset = Offset;

	// Полёт к цели фокуса / следование за актором (XCOM-glide, только XY).
	if (FollowTarget.IsValid())
	{
		FocusGoal = FollowTarget->GetActorLocation();
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
		PreShotLookHeight = TargetLookHeight;
		// Если камера СЕЙЧАС летит к цели (только что выбрали бойца — glide ещё
		// идёт), «прежняя» позиция — это КОНЕЦ полёта, а не промежуточная точка,
		// иначе возврат после выстрела/отмены встанет посреди карты.
		PreShotLocation = bHasFocusGoal
			? FVector(FocusGoal.X, FocusGoal.Y, GetActorLocation().Z)
			: GetActorLocation();
	}
	bShotFraming = true;
	ShotFrameTimeLeft = Duration;

	const FVector ShooterLocation = Shooter->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();

	// --- Дистанция задаёт отъезд и точку обзора (адаптивная композиция XCOM) ---
	// Близкий выстрел — крупный план плеча; дальний — отъезд + точка обзора ближе
	// к середине, иначе на дальней цели «не видно ни стрелка, ни половины цели»
	// (была фиксированная композиция независимо от дистанции).
	const float Dist = FVector::Dist2D(ShooterLocation, TargetLocation);
	const float FarAlpha = ShotFrameFarDistance > 1.f
		? FMath::Clamp(Dist / ShotFrameFarDistance, 0.f, 1.f) : 0.f;

	const float Bias = FMath::Lerp(ShotFrameTargetBias, ShotFrameTargetBiasFar, FarAlpha);
	const float ShotArm = FMath::Clamp(
		FMath::Lerp(ShotFrameZoom, ShotFrameZoomFar, FarAlpha), ShotFrameZoomNear, MaxZoom);

	// --- Наклон подстраивается под перепад высот стрелок↔цель ------------------
	// Цель НИЖЕ (HeightDelta<0) → −(отрицательное) = наклон круче вниз; цель ВЫШЕ
	// → наклон площе. Так уступы/этажи читаются в кадре, а не режут композицию.
	const float HeightDelta = TargetLocation.Z - ShooterLocation.Z;
	const float Pitch = FMath::Clamp(
		ShotFramePitch - HeightDelta * ShotFramePitchPerZ, -75.f, -4.f);

	// --- Ось стрелок→цель и точка обзора ---------------------------------------
	// Пружина тянется НАЗАД вдоль своего yaw → камера позади точки обзора. Yaw =
	// направление стрелок→цель + плечевой доворот: цель открывается «из-за плеча».
	FVector Axis = TargetLocation - ShooterLocation;
	Axis.Z = 0.f;
	float BaseYaw = TargetYaw;
	if (Axis.Normalize())
	{
		BaseYaw = Axis.Rotation().Yaw;
	}

	const FVector PivotXY = FMath::Lerp(ShooterLocation, TargetLocation, Bias);
	const FVector PivotWorld(PivotXY.X, PivotXY.Y, ShooterLocation.Z + ShotFrameLookHeight);
	const FVector TargetAim(TargetLocation.X, TargetLocation.Y, TargetLocation.Z + ShotFrameLookHeight);

	// ВЫБОР ПЛЕЧА по геометрии — правит «всегда 1 ракурс» и вид в стену.
	const float ShoulderOffset = PickShoulderOffset(
		Shooter, Target, PivotWorld, TargetAim, BaseYaw, Pitch, ShotArm);
	const float ChosenYaw = BaseYaw + ShoulderOffset;

	// БЕЗОПАСНАЯ длина пружины — считаем ОДИН раз здесь, а не поштучной коллизией
	// пружины (её тест каждый кадр упирался в укрытие у стрелка и давал резкий
	// скачок + мигание на близких целях). Если позади точки обзора стена — камера
	// встанет чуть перед ней; значение затем плавно интерполируется в Tick.
	float SafeArm = ShotArm;
	if (const UWorld* World = GetWorld())
	{
		const FVector DesiredCam = PivotWorld - FRotator(Pitch, ChosenYaw, 0.f).Vector() * ShotArm;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Shooter);
		Params.AddIgnoredActor(Target);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, PivotWorld, DesiredCam, ECC_Visibility, Params))
		{
			constexpr float WallPadding = 40.f; // отступ камеры от стены, см
			SafeArm = FMath::Clamp(Hit.Distance - WallPadding, ShotFrameZoomNear, ShotArm);
		}
	}

	TargetYaw = ChosenYaw;
	TargetZoom = SafeArm;
	TargetPitch = Pitch;
	TargetLookHeight = ShotFrameLookHeight; // обзор на линии груди

	// Следование за бегущим на время кадра снимаем — иначе перетянет фокус.
	FollowTarget = nullptr;
	FocusGoal = PivotXY; // XY точки обзора; высота берётся из TargetLookHeight
	bHasFocusGoal = true;
}

float ATacticalCameraPawn::PickShoulderOffset(
	const AActor* Shooter, const AActor* Target,
	const FVector& PivotWorld, const FVector& TargetAim,
	float BaseYaw, float Pitch, float Arm) const
{
	const float Offset = FMath::Abs(ShotFrameYawOffset);
	if (Offset < 1.f)
	{
		return 0.f; // доворот отключён — строго вдоль оси
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return Offset; // без мира — прежнее детерминированное поведение (одно плечо)
	}

	// Очко стороны: 1.0 идеально (камера видит цель и не за стеной), меньше — чем
	// сильнее перекрыто. Пробуем оба плеча и берём лучшее.
	auto ScoreSide = [&](float SignedOffset) -> float
	{
		const FRotator Rot(Pitch, BaseYaw + SignedOffset, 0.f);
		const FVector CamPos = PivotWorld - Rot.Vector() * Arm;

		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Shooter);
		Params.AddIgnoredActor(Target);

		float Score = 1.f;
		FHitResult Hit;

		// (а) камера не должна быть отгорожена стеной от точки обзора (иначе кадр
		//     смотрит В стену со стороны камеры).
		const float PivotToCam = FVector::Dist(PivotWorld, CamPos);
		if (PivotToCam > 1.f && World->LineTraceSingleByChannel(
			Hit, PivotWorld, CamPos, ECC_Visibility, Params))
		{
			Score -= (1.f - Hit.Distance / PivotToCam); // стена у пивота — хуже всего
		}

		// (б) цель должна читаться из позиции камеры.
		const float CamToTarget = FVector::Dist(CamPos, TargetAim);
		if (CamToTarget > 1.f && World->LineTraceSingleByChannel(
			Hit, CamPos, TargetAim, ECC_Visibility, Params))
		{
			Score -= (1.f - Hit.Distance / CamToTarget);
		}
		return Score;
	};

	const float RightScore = ScoreSide(+Offset);
	const float LeftScore = ScoreSide(-Offset);
	// При равенстве — стабильно правое плечо, чтобы кадр не дёргался между сторонами.
	return (LeftScore > RightScore + KINDA_SMALL_NUMBER) ? -Offset : +Offset;
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
	TargetLookHeight = PreShotLookHeight;
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
	TargetLookHeight = PreShotLookHeight;
	FocusGoal = PreShotLocation;
	bHasFocusGoal = true;
}
