#include "TacticalCameraPawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"

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

	// Пружина тянется НАЗАД от точки обзора вдоль своего yaw — значит камера
	// окажется позади точки обзора. Ставим yaw = направление стрелок→цель +
	// плечевой доворот: точка обзора у стрелка → камера позади него, стрелок на
	// переднем плане, цель уходит вглубь кадра (ракурс «из-за плеча»).
	const FVector ShooterLocation = Shooter->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	FVector Axis = TargetLocation - ShooterLocation;
	Axis.Z = 0.f;
	if (Axis.Normalize())
	{
		TargetYaw = Axis.Rotation().Yaw + ShotFrameYawOffset;
	}

	TargetZoom = FMath::Clamp(ShotFrameZoom, MinZoom, MaxZoom);
	TargetPitch = ShotFramePitch;       // пологий «взгляд от плеча»
	TargetLookHeight = ShotFrameLookHeight; // обзор на линии груди

	// Точка обзора у СТРЕЛКА (bias мал): он на переднем плане, цель вдали.
	// Следование за бегущим на время кадра снимаем — иначе перетянет фокус.
	FollowTarget = nullptr;
	FocusGoal = FMath::Lerp(ShooterLocation, TargetLocation, ShotFrameTargetBias);
	bHasFocusGoal = true;
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
