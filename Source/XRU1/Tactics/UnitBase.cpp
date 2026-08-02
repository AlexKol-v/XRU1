#include "UnitBase.h"
#include "XRU1Log.h"
#include "ActionPointsComponent.h"
#include "CoverDetectionComponent.h"
#include "CoverTuningDataAsset.h"
#include "GA_Attack.h"
#include "GA_Overwatch.h"
#include "TacticalAbility.h"
#include "TacticalClassAbilities.h"
#include "TacticsGameplayTags.h"
#include "TacticsCombatStatics.h" // IsUnitInTransit / GetFiringStance для VisualState
#include "UnitAIController.h"     // GetMoveStatus: отмена подшага ТОЛЬКО по новому приказу
#include "Navigation/PathFollowingComponent.h" // EPathFollowingStatus
#include "TacticsAudioSubsystem.h"
#include "TDAttributeSet.h"
#include "UnitAudioDataAsset.h"
#include "UnitClasses.h"
#include "UnitVfxDataAsset.h"
#include "ShotTracerActor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimMontage.h"       // длительность DeathMontage для рагдолла
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"     // профили коллизии павших/рагдолла
#include "TimerManager.h"
#include "NavigationInvokerComponent.h"
#include "NavigationSystem.h"            // подшаг не должен выводить юнита за навмеш
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

AUnitBase::AUnitBase()
{
	// Тик нужен двум коротким визуальным движениям — подшагу к укрытию и
	// довороту на месте (см. Tick). В остальное время он выходит сразу.
	PrimaryActorTick.bCanEverTick = true;

	ActionPoints = CreateDefaultSubobject<UActionPointsComponent>(TEXT("ActionPoints"));
	CoverDetection = CreateDefaultSubobject<UCoverDetectionComponent>(TEXT("CoverDetection"));

	// Navigation Invoker: навмеш генерится вокруг юнита (RuntimeGeneration=Dynamic).
	// Радиусы применяются в BeginPlay (могут быть переопределены в BP до старта).
	NavInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavInvoker"));
	NavInvoker->SetGenerationRadii(NavInvokerGenerationRadius, NavInvokerRemovalRadius);

	// Общие способности — безопасные нативные дефолты. BP может заменить их
	// наследниками с монтажами, но новый BP-юнит больше не останется без действий.
	AttackAbilityClass = UGA_Attack::StaticClass();
	OverwatchAbilityClass = UGA_Overwatch::StaticClass();
	HunkerAbilityClass = UGA_HunkerDown::StaticClass();

	// Кольцо-декаль выбора: проекция вниз под ногами. Материал (M_SelectionRing)
	// и размер тюнингуются в BP; скрыто, пока контроллер не выберет юнита.
	SelectionDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("SelectionDecal"));
	SelectionDecal->SetupAttachment(RootComponent);
	SelectionDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f)); // проекция вниз, на пол
	SelectionDecal->SetRelativeLocation(FVector(0.f, 0.f, -88.f));   // к ногам (центр капсулы ~88 см над полом)
	SelectionDecal->DecalSize = FVector(120.f, 60.f, 60.f);         // X — глубина проекции, Y/Z — радиус кольца
	SelectionDecal->SetVisibility(false);

	// Юнитов двигает AIController (MoveToLocation), а не ввод игрока. По умолчанию
	// path following задаёт скорость НАПРЯМУЮ, не заполняя Acceleration — из-за
	// чего шаблонный локомоушен-ABP (условие Should Move = Speed>0 AND
	// Acceleration!=0) не переключался с idle при движении. Включаем
	// acceleration-driven follow: Acceleration заполняется, анимация бега играет,
	// плюс естественный разгон/торможение. Действует на все BP-юниты (свои и врагов).
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;
	}

	// Навмеш юниты НЕ трогают (никаких вырезов/динамических препятствий):
	// занятость решается на уровне запросов дисками UnitObstacleRadius
	// (см. UTacticsCombatStatics::GetUnitObstacles) — как клетки-occupancy
	// в XCOM. Капсула навигацию не затрагивает вовсе.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCanEverAffectNavigation(false);
	}
}

int32 AUnitBase::GetAbilityUsesRemaining(TSubclassOf<UTacticalAbility> AbilityClass) const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC || !AbilityClass)
	{
		return -1;
	}

	// InstancedPerActor: остаток применений живёт на primary-инстансе способности.
	// Фолбэк на Spec->Ability (CDO): если BP переопределил политику инстансинга,
	// ApplyCost пишет UsesRemaining именно туда — не путаем «нет инстанса» с «без лимита».
	if (const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass))
	{
		const UTacticalAbility* Instance = Cast<UTacticalAbility>(Spec->GetPrimaryInstance());
		if (!Instance)
		{
			Instance = Cast<UTacticalAbility>(Spec->Ability.Get());
		}
		if (Instance)
		{
			return Instance->GetUsesRemaining();
		}
	}
	return -1;
}

// --- Подсветка выбора/наведения ---------------------------------------------

void AUnitBase::SetSelectionHighlight(bool bSelected)
{
	if (SelectionDecal)
	{
		SelectionDecal->SetVisibility(bSelected && !bIsDead && !bIsEvacuated);
	}
}

void AUnitBase::SetHoverHighlight(bool bHovered)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	if (bHovered && !bIsDead && !bIsEvacuated)
	{
		// Цвет обводки выбирает post-process материал по stencil-значению.
		const bool bAlly =
			GetGenericTeamId().GetId() == TacticsTeamIds::Player;
		MeshComponent->SetCustomDepthStencilValue(bAlly ? HoverStencilAlly : HoverStencilEnemy);
		MeshComponent->SetRenderCustomDepth(true);
	}
	else
	{
		MeshComponent->SetRenderCustomDepth(false);
	}
}

void AUnitBase::PlayUnitSound(EUnitSoundEvent Event)
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UTacticsAudioSubsystem* Audio = GameInstance
		? GameInstance->GetSubsystem<UTacticsAudioSubsystem>() : nullptr;
	if (!AudioProfile || !Audio)
	{
		return;
	}

	if (const FTacticsSoundCue* Cue = AudioProfile->FindEvent(Event))
	{
		// Звук привязан к актору: боец во время выстрела делает StepOut и
		// возвращается в anchor, и звук должен ехать вместе с ним.
		Audio->PlayCueAttached(*Cue, this, AudioProfile->ResolveAttenuation());
	}
}

FVector AUnitBase::GetMuzzleWorldLocation(const FVector& Fallback) const
{
	const UUnitVfxDataAsset* Profile = VfxProfile;
	const FName SocketName = Profile ? Profile->MuzzleSocketName : NAME_None;
	if (SocketName.IsNone())
	{
		return Fallback;
	}

	// Оружие — Child Actor («Gun» в BP юнита) и внутри само составное (рама,
	// цевьё, прицелы), поэтому смотрим и себя, и все вложенные акторы.
	TArray<AActor*> Actors;
	Actors.Add(const_cast<AUnitBase*>(this));
	TArray<AActor*> Attached;
	GetAllChildActors(Attached, /*bIncludeDescendants=*/true);
	Actors.Append(Attached);

	// 1. Пустой Scene Component с этим именем или тегом — самый удобный способ
	// для дизайнера: точку дула видно и двигают мышью прямо в BP оружия.
	for (const AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}
		TArray<USceneComponent*> Components;
		Actor->GetComponents<USceneComponent>(Components);
		for (const USceneComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}
			if (Component->GetFName() == SocketName || Component->ComponentHasTag(SocketName))
			{
				return Component->GetComponentLocation();
			}
		}
	}

	// 2. Сокет с тем же именем на любом меше (если он всё-таки заведён в скелете).
	for (const AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}
		TArray<UMeshComponent*> Meshes;
		Actor->GetComponents<UMeshComponent>(Meshes);
		for (const UMeshComponent* Candidate : Meshes)
		{
			if (Candidate && Candidate->DoesSocketExist(SocketName))
			{
				return Candidate->GetSocketLocation(SocketName);
			}
		}
	}
	return Fallback;
}

void AUnitBase::PlayShotVfx(AActor* Target, bool bHit, const FVector& ShotOrigin)
{
	const UUnitVfxDataAsset* Profile = VfxProfile;
	UWorld* World = GetWorld();
	if (!Profile || !World || !Target)
	{
		return;
	}

	const FVector Muzzle = GetMuzzleWorldLocation(ShotOrigin);
	FVector AimPoint = Target->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	if (!bHit && Profile->MissSpread > 0.f)
	{
		// Уводим мимо детерминированно относительно линии выстрела: вбок и вверх,
		// как «пуля прошла рядом». Случайная сторона — чтобы промахи не выглядели
		// одинаково.
		const FVector MissDirection = (AimPoint - Muzzle).GetSafeNormal();
		const FVector Side = FVector::CrossProduct(MissDirection, FVector::UpVector).GetSafeNormal();
		const float Sign = FMath::RandBool() ? 1.f : -1.f;
		AimPoint += Side * Profile->MissSpread * Sign
			+ FVector(0.f, 0.f, Profile->MissSpread * 0.5f);
	}

	const FVector ShotDirection = (AimPoint - Muzzle).GetSafeNormal();
	if (ShotDirection.IsNearlyZero())
	{
		return;
	}
	const FRotator AimRotation = ShotDirection.Rotation();
	const FRotator ShotRotation = Profile->ShotRotationOffset.IsNearlyZero()
		? AimRotation
		: (AimRotation.Quaternion() * Profile->ShotRotationOffset.Quaternion()).Rotator();

	if (Profile->MuzzleFlash)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Profile->MuzzleFlash,
			Muzzle, ShotRotation);
	}

	// Куда пуля реально упирается. Одна трассировка кормит и трассер, и эффект
	// попадания: разные точки у них расходились — след обрывался у цели, а искры
	// вспыхивали за ней.
	FVector EndPoint = AimPoint;
	FVector ImpactNormal = -ShotDirection;
	UNiagaraSystem* ImpactSystem = nullptr;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShotVfx), /*bTraceComplex=*/true, this);
	Params.bReturnPhysicalMaterial = true;
	if (bHit)
	{
		// Попадание засчитано механикой — значит пуля дошла. Пробуем ТОЛЬКО тело
		// цели: укрытие на линии не должно перехватывать попавший выстрел.
		const FVector TraceEnd = Muzzle + ShotDirection * (FVector::Dist(Muzzle, AimPoint) + 100.f);
		if (Target->ActorLineTraceSingle(Hit, Muzzle, TraceEnd, ECC_Visibility, Params))
		{
			EndPoint = Hit.ImpactPoint;
			ImpactNormal = Hit.ImpactNormal;
		}
		ImpactSystem = Profile->ImpactFlesh ? Profile->ImpactFlesh.Get() : Profile->DefaultImpact.Get();
	}
	else
	{
		Params.AddIgnoredActor(Target);
		const FVector TraceEnd = Muzzle + ShotDirection * 6000.f;
		if (World->LineTraceSingleByChannel(Hit, Muzzle, TraceEnd, ECC_Visibility, Params))
		{
			EndPoint = Hit.ImpactPoint;
			ImpactNormal = Hit.ImpactNormal;
			const EPhysicalSurface Surface = Hit.PhysMaterial.IsValid()
				? Hit.PhysMaterial->SurfaceType.GetValue() : SurfaceType_Default;
			ImpactSystem = Profile->FindImpact(Surface);
		}
		else
		{
			// Пуля ушла «в молоко»: трассер гаснет в конце линии, бить искрами
			// в пустоту нечем.
			EndPoint = TraceEnd;
		}
	}

	float FlightTime = 0.f;
	if (Profile->Tracer)
	{
		if (Profile->bTracerFlies)
		{
			FlightTime = AShotTracerActor::Launch(this, Profile, Muzzle, EndPoint);
		}
		else
		{
			// Спавним неактивной: user-параметры читаются на спавне системы, после
			// Activate менять их поздно.
			UNiagaraComponent* TracerComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World, Profile->Tracer, Muzzle, ShotRotation, FVector(1.f),
				/*bAutoDestroy=*/true, /*bAutoActivate=*/false);
			if (TracerComponent)
			{
				Profile->ApplyTracerParameters(TracerComponent, Muzzle, EndPoint);
				TracerComponent->Activate(true);
			}
			if (Profile->TracerSpeed > 1.f)
			{
				FlightTime = FVector::Dist(Muzzle, EndPoint) / Profile->TracerSpeed;
			}
		}
	}

	if (!ImpactSystem)
	{
		return;
	}
	// Искры ждут прилёта: попадание, вспыхнувшее раньше собственного трассера,
	// читается как два разных выстрела.
	if (FlightTime > KINDA_SMALL_NUMBER)
	{
		FTimerHandle ImpactTimer;
		World->GetTimerManager().SetTimer(ImpactTimer,
			FTimerDelegate::CreateWeakLambda(this,
				[this, ImpactSystem, EndPoint, ImpactNormal, ShotDirection]()
				{
					SpawnImpactVfx(ImpactSystem, EndPoint, ImpactNormal, ShotDirection);
				}),
			FlightTime, /*bLoop=*/false);
	}
	else
	{
		SpawnImpactVfx(ImpactSystem, EndPoint, ImpactNormal, ShotDirection);
	}
}

void AUnitBase::SpawnImpactVfx(UNiagaraSystem* ImpactSystem, const FVector& Location,
	const FVector& Normal, const FVector& Direction) const
{
	UWorld* World = GetWorld();
	if (!ImpactSystem || !World)
	{
		return;
	}

	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, ImpactSystem, Location, Normal.Rotation(), FVector(1.f),
		/*bAutoDestroy=*/true, /*bAutoActivate=*/false);
	if (!Component)
	{
		return;
	}
	if (const UUnitVfxDataAsset* Profile = VfxProfile)
	{
		Profile->ApplyImpactParameters(Component, Normal, Direction);
	}
	Component->Activate(true);
}

void AUnitBase::SetHealthDirect(float NewHealth)
{
	const float Clamped = FMath::Clamp(NewHealth, 1.f, GetMaxHealth());
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), Clamped);
	}
	else if (Attributes)
	{
		Attributes->InitHealth(Clamped);
	}
	// HUD/портреты обязаны увидеть новое значение немедленно.
	NotifyUnitStateChanged();
}

void AUnitBase::SetPendingScriptedShot(const FScriptedShotOverride& Override, AActor* ScriptedTarget)
{
	PendingScriptedShot = Override;
	bHasPendingScriptedShot = Override.IsMeaningful();
	PendingScriptedShotTarget = ScriptedTarget;
}

bool AUnitBase::ConsumePendingScriptedShot(const AActor* Target, FScriptedShotOverride& OutOverride)
{
	if (!bHasPendingScriptedShot)
	{
		return false;
	}
	// Форс принадлежит конкретной цели шага. Если приказ по ней не прошёл, а AI
	// выбрал себе другую жертву, постановочные числа не должны на неё перетечь:
	// шаг ждёт выстрел по своему бойцу, а не «промах 0%» по случайному.
	if (PendingScriptedShotTarget.IsValid() && PendingScriptedShotTarget.Get() != Target)
	{
		return false;
	}
	PendingScriptedShotTarget = nullptr;

	// Форс одноразовый: второй выстрел того же юнита в том же ходу обязан
	// считаться по обычным правилам, иначе шаг обучения «залипнет» на 100%.
	OutOverride = PendingScriptedShot;
	bHasPendingScriptedShot = false;
	return true;
}

void AUnitBase::NotifyUnitStateChanged()
{
	// Срез для анимаций пересобирается ЗДЕСЬ и только здесь: это единственная
	// точка, которую системы обязаны дёргать после фактического изменения
	// состояния. Значит ABP и HUD всегда видят одну и ту же картину.
	RebuildVisualState();
	OnUnitStateChanged.Broadcast();
}

void AUnitBase::RebuildVisualState()
{
	FUnitVisualState State;

	const UCoverDetectionComponent* Cover = GetCoverDetection();
	State.Cover = Cover ? Cover->BestCoverAround : ECoverType::None;
	State.CoverDirection = Cover ? Cover->BestCoverDirection : FVector::ZeroVector;
	if (!State.CoverDirection.IsNearlyZero())
	{
		// В систему координат юнита: ABP думает «стена справа/слева/спереди», а
		// не мировыми осями — иначе поза зависела бы от поворота камеры.
		State.CoverDirectionLocal = GetActorRotation().UnrotateVector(State.CoverDirection);
	}

	// СТОРОНА УКРЫТИЯ для выбора Left/Right-клипа — из кэша компонента укрытий
	// (§6: единый источник — геометрия стены и края). Прежний вывод из
	// `CoverDirectionLocal.Y` был вторым источником правды и ломался, как только
	// юнит после settlement оказывался не вдоль стены (доказано логом [Peek]:
	// Y=0.02–0.21 при живом крае): порог 0.35 не проходил, сторона застревала
	// в 0, и выглядывание не начиналось никогда.
	State.PeekSideLocal = Cover ? Cover->PeekSideSign : 0.f;
	// Во время уже начатого cosmetic peek сторона неизменяема: перевыбор стены
	// не имеет права посреди клипа переключить Left/Right на противоположный.
	if (bPeekActive && !FMath::IsNearlyZero(FrozenPeekSide))
	{
		State.PeekSideLocal = FrozenPeekSide;
	}

	// ЕСТЬ ЛИ КУДА ВЫГЛЯДЫВАТЬ — отдельный вопрос от стороны: у глухой стены
	// сторона известна, а края нет. Тоже кэш EvaluateSurroundings: пересборка
	// среза больше НЕ трейсит мир (раньше — до 8 лучей на каждый notify).
	bHasPeekEdge = Cover && Cover->HasPeekEdge();

	// Подшаг к стене — тоже движение: иначе юнит ехал бы к укрытию в статичной
	// позе вместо шага (см. HugCover).
	State.bMoving = UTacticsCombatStatics::IsUnitInTransit(this) || bCoverHugStepping;
	State.bPlayerSide = GetGenericTeamId() == FGenericTeamId(TacticsTeamIds::Player);
	State.PendingTurnYaw = bTurningInPlace ? PendingTurnAmount : 0.f;
	State.bShouldPeek = bPeekActive;

	// ПОЗА — приоритетом сверху вниз, тем же порядком, что у статуса в HUD
	// (`UTacticalHUDStyleData::GetStatusForUnit`): один порядок на иконку и на
	// анимацию, иначе игрок видит «глухая оборона» в HUD и бег в кадре.
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	const auto HasTag = [ASC](const FGameplayTag& Tag)
	{
		return ASC && ASC->HasMatchingGameplayTag(Tag);
	};

	if (bIsDead && !bDeathMontageOwnsPose)                    { State.Pose = EUnitPose::Dead; }
	else if (bIsDead)                                         { State.Pose = EUnitPose::Stand; }
	else if (bIsDowned)                                       { State.Pose = EUnitPose::Downed; }
	else if (HasTag(TacticsGameplayTags::State_HunkeredDown)) { State.Pose = EUnitPose::Hunkered; }
	else if (HasTag(TacticsGameplayTags::State_Overwatch))    { State.Pose = EUnitPose::Overwatch; }
	else if (State.bMoving)                                   { State.Pose = EUnitPose::Moving; }
	else if (State.Cover == ECoverType::Full)                 { State.Pose = EUnitPose::HighCover; }
	else if (State.Cover == ECoverType::Half)                 { State.Pose = EUnitPose::CrouchCover; }
	else                                                      { State.Pose = EUnitPose::Stand; }

	VisualState = State;
}

void AUnitBase::HugCover()
{
	const UCoverDetectionComponent* Cover = GetCoverDetection();
	if (!Cover || Cover->BestCoverAround == ECoverType::None)
	{
		// «Боец не прижимается к стене» почти всегда означает именно это: боевой
		// слой укрытия здесь ничего не нашёл. Причина видна только в логе, потому
		// что визуально «встал рядом со стеной» и «стена не засчитана» одинаковы.
		if (UTacticsCombatStatics::IsCoverDebugEnabled())
		{
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Cover] %s: прижатие пропущено — BestCoverAround=None"),
				*GetName());
		}
		return; // укрытия нет — прижиматься не к чему
	}
	FVector ToWall = Cover->BestCoverDirection;
	ToWall.Z = 0.f;
	if (!ToWall.Normalize())
	{
		return;
	}

	// (1) РАЗВОРОТ ВДОЛЬ СТЕНЫ, ЛИЦОМ К КРАЮ УКРЫТИЯ.
	//
	// ⚠️ Не «лицом в стену». Cover-анимации сняты для бойца, стоящего БОКОМ к
	// укрытию и смотрящего вдоль него на угол, из-за которого будет выглядывать.
	// При развороте носом в стену стороны вырождаются (стена спереди, слева и
	// справа одинаково), клип начинает доворачивать всё тело, компенсируя
	// неверную стойку, — отсюда «развернулся целиком» и «голова назад».
	//
	// Стоя вдоль стены, юнит имеет однозначную сторону укрытия
	// (`CoverDirectionLocal.Y`), и она же выбирает Left/Right-клип.
	//
	// ⚠️ БЕЗ анимации доворота: здесь уже играют подшаг и вход в позу укрытия,
	// а наложенный сверху `Turn_180` выглядит как вывернутое назад тело.
	//
	// Край — из кэша (§6): вызывающие обязаны сделать EvaluateSurroundings перед
	// HugCover (оба вызова в UnitAIController так и идут), значит кэш свежий.
	const FVector EdgeSide = Cover->PeekEdgeDirection;
	const float EdgeDistance = Cover->PeekEdgeDistance;
	const FVector FaceDirection = EdgeSide.IsNearlyZero()
		? (bCoverHugFaceWall ? ToWall : -ToWall) // глухая стена — выглядывать некуда
		: EdgeSide;
	const FVector DesiredFacingTarget = GetActorLocation() + FaceDirection * 100.f;

	// Дальность подшага = дальность, на которой стена ещё СЧИТАЕТСЯ укрытием.
	// Один источник правды: если стена достаточно близка, чтобы дать cover, она
	// достаточно близка, чтобы к ней прижаться. Прежний фиксированный лимит
	// 45 см оставлял бойца в «укрытии» в метре от стены (лог: план 45 см,
	// «упёрлись в ничего») — стена давала cover с CoverTraceDistance, а подшаг
	// до неё не доставал.
	const UCoverTuningDataAsset* Tuning = UTacticsCombatStatics::GetCoverTuning(GetWorld());
	const float HugReach = Tuning->CoverTraceDistance;

	if (UTacticsCombatStatics::IsCoverDebugEnabled())
	{
		// Печатаем МАКСИМУМ подшага; фактический сдвиг логируется ниже, после
		// свипа. Их расхождение и есть ответ на «почему не прижался».
		// Плановый yaw печатаем, чтобы отличать «HugCover довернул не туда» от
		// «после HugCover юнита развернул кто-то ещё»: итоговая стойка видна в
		// строке [Peek] (CoverDirLocal.Y), расхождение с планом = чужой поворот.
		UE_LOG(LogXRU1Combat, Display,
			TEXT("[Cover] %s: прижатие — cover=%d, сторон %d, край %s (%.0f см при лимите поиска %.0f), дальность подшага %.0f см, доворот к yaw=%.0f (сейчас %.0f)"),
			*GetName(), static_cast<int32>(Cover->BestCoverAround), Cover->CoverSides.Num(),
			EdgeSide.IsNearlyZero() ? TEXT("НЕ найден") : TEXT("найден"),
			EdgeDistance, Tuning->PeekEdgeMaxDistance,
			HugReach, FaceDirection.Rotation().Yaw, GetActorRotation().Yaw);
	}

	if (CoverHugMaxNudge <= 0.f)
	{
		// Подшаг выключен настройкой: боец только доворачивается и остаётся там,
		// куда его привёл маршрут. Снаружи это читается как «не прижался».
		FaceTowardsSmooth(DesiredFacingTarget, /*bPlayTurnAnimation=*/false, CoverHugTurnRate);
		return;
	}

	// (2) ПОДШАГ вплотную. Куда именно — решает свип капсулой: если между юнитом
	// и стеной кто-то есть, встаём ровно до него и не лезем сквозь геометрию.
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	UWorld* World = GetWorld();
	if (!Capsule || !World)
	{
		FaceTowardsSmooth(DesiredFacingTarget, /*bPlayTurnAnimation=*/false, CoverHugTurnRate);
		return;
	}

	const FVector Start = GetActorLocation();
	const FVector End = Start + ToWall * HugReach;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverHug), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());

	// ⚠️ ЕДИНСТВЕННОЕ место в Tactics/, где трейс идёт ПО КАНАЛУ, и это верно:
	// здесь вопрос физический — «куда пролезет капсула», а не «что остановит
	// пулю». Значит юниты обязаны учитываться (в союзника вжиматься нельзя), то
	// есть `GetShotGeometryObjects` тут был бы ошибкой.
	if (!World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
		Capsule->GetCollisionObjectType(), Shape, Params))
	{
		// Свип не нащупал стену, хотя укрытие засчитано. Слепой шаг «на всю
		// дальность» здесь запрещён: раньше он был ограничен 45 см и был почти
		// безвреден, а на полной дальности укрытия увёл бы бойца в открытое
		// поле. Не знаем, куда шагать, — стоим и доворачиваемся.
		if (UTacticsCombatStatics::IsCoverDebugEnabled())
		{
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Cover] %s: свип к стене НИЧЕГО не нашёл на %.0f см — подшаг пропущен, только доворот"),
				*GetName(), HugReach);
		}
		FaceTowardsSmooth(DesiredFacingTarget, /*bPlayTurnAnimation=*/false, CoverHugTurnRate);
		return;
	}
	FVector TargetLocation = Start + ToWall * FMath::Max(0.f, Hit.Distance - CoverHugClearance);

	if (UTacticsCombatStatics::IsCoverDebugEnabled())
	{
		UE_LOG(LogXRU1Combat, Display,
			TEXT("[Cover] %s: свип к стене — фактический сдвиг %.0f см (упёрлись в %s)"),
			*GetName(), FVector::Dist2D(Start, TargetLocation),
			Hit.GetActor() ? *GetNameSafe(Hit.GetActor()) : TEXT("ничего"));
	}

	// ⚠️ НЕ ВЫХОДИТЬ ЗА НАВМЕШ. Навмеш отступает от стен на радиус агента, а
	// подтяг идёт ВПЛОТНУЮ — вставший там боец оказывается вне навмеша, и тогда
	// у него пропадает зона хода целиком: волна в AMoveRangeVisualizer стартует
	// с проекции его позиции и без неё не строится вовсе. Поэтому от конечной
	// точки отступаем назад, пока проекция не найдётся.
	const FVector PreNavTarget = TargetLocation; // для лога: сколько съела проекция
	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		const float FloorOffset = Capsule->GetScaledCapsuleHalfHeight();
		const FVector StartFoot = Start - FVector(0.f, 0.f, FloorOffset);
		const FVector TargetFoot = TargetLocation - FVector(0.f, 0.f, FloorOffset);
		const FVector NavExtent(20.f, 20.f, 120.f);

		FVector Reachable = Start; // ноль подшага — всегда валиден, юнит уже стоит
		FNavLocation Projected;
		for (float Alpha = 1.f; Alpha > 0.f; Alpha -= 0.25f)
		{
			if (NavSys->ProjectPointToNavigation(FMath::Lerp(StartFoot, TargetFoot, Alpha),
				Projected, NavExtent))
			{
				// ProjectPointToNavigation возвращает точку ПОД НОГАМИ, а actor
				// хранит центр капсулы. Прямое присваивание Projected.Location
				// утопило бы root на CapsuleHalfHeight.
				Reachable = Projected.Location + FVector(0.f, 0.f, FloorOffset);
				break;
			}
		}
		TargetLocation = Reachable;
	}

	// Проекция на навмеш — единственный участник, который может МОЛЧА срезать
	// шаг до нуля (навмеш отступает от стен на радиус агента). Видимым это
	// делает только лог: «свип нашёл стену, а юнит не пошёл» без него неотличимо
	// от любой другой причины.
	if (UTacticsCombatStatics::IsCoverDebugEnabled()
		&& FVector::Dist2D(PreNavTarget, TargetLocation) > 1.f)
	{
		UE_LOG(LogXRU1Combat, Display,
			TEXT("[Cover] %s: навмеш урезал подшаг — было %.0f см, стало %.0f см"),
			*GetName(), FVector::Dist2D(Start, PreNavTarget),
			FVector::Dist2D(Start, TargetLocation));
	}

	// Идти уже некуда — не поднимаем шаг ради пары миллиметров.
	if (FVector::DistSquared2D(Start, TargetLocation) < FMath::Square(CoverHugArriveTolerance))
	{
		if (UTacticsCombatStatics::IsCoverDebugEnabled())
		{
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Cover] %s: подшаг не нужен (%.1f см < допуска %.1f) — только доворот"),
				*GetName(), FVector::Dist2D(Start, TargetLocation), CoverHugArriveTolerance);
		}
		FaceTowardsSmooth(DesiredFacingTarget, /*bPlayTurnAnimation=*/false, CoverHugTurnRate);
		return;
	}

	// ⚠️ Точку ведёт `CharacterMovement`, а не `SetActorLocation`: только так у
	// юнита появляется настоящая `Velocity`, а значит и анимация шага. Телепорт
	// сюда возвращать нельзя — он и выглядит рывком, и оставляет ABP со Speed=0.
	// Скорость на время подшага занижаем до шага: 45 см на беговых 600 см/с —
	// это 0.07 с, за которые не читается вообще ничего.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// Сохраняем ИСХОДНУЮ скорость только на первом входе: повторный HugCover
		// во время подшага иначе «сохранил» бы уже заниженную и оставил юнита
		// шагающим до конца боя.
		if (!bCoverHugStepping)
		{
			CoverHugSavedMaxSpeed = Movement->MaxWalkSpeed;
		}
		Movement->MaxWalkSpeed = CoverHugStepSpeed;
	}
	CoverHugStepTarget = TargetLocation;
	CoverHugFacingDirection = FaceDirection.GetSafeNormal2D();
	CoverHugStepElapsed = 0.f;
	// Новый cover step отменяет прежний turn: иначе Tick продолжит крутить actor
	// одновременно с физическим подшагом.
	bTurningInPlace = false;
	PendingTurnAmount = 0.f;
	ActiveTurnRate = 0.f;
	bCoverHugStepping = true;

	if (UTacticsCombatStatics::IsCoverDebugEnabled())
	{
		UE_LOG(LogXRU1Combat, Display,
			TEXT("[Cover] %s: подшаг СТАРТОВАЛ — %.0f см к (%.0f, %.0f)"),
			*GetName(), FVector::Dist2D(Start, TargetLocation),
			TargetLocation.X, TargetLocation.Y);
	}

	// Поза становится «идёт» ДО первого кадра подшага, иначе юнит успеет
	// проехать часть пути в статичной позе укрытия.
	NotifyUnitStateChanged();
}

void AUnitBase::FaceTowardsSmooth(const FVector& TargetLocation, bool bPlayTurnAnimation,
	float TurnRateOverride)
{
	FVector ToTarget = TargetLocation - GetActorLocation();
	ToTarget.Z = 0.f;
	if (!ToTarget.Normalize())
	{
		return;
	}

	const float CurrentYaw = GetActorRotation().Yaw;
	const float DesiredYaw = ToTarget.Rotation().Yaw;
	const float Delta = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);

	// Мелкий доворот проигрывать нечем: клипы начинаются с 45°, а на 10° любой
	// из них выглядит как подёргивание. Такие углы сводим сразу.
	if (FMath::Abs(Delta) < TurnInPlaceMinAngle)
	{
		// Предыдущий большой turn мог ещё жить в Tick. Перед мгновенным малым
		// поворотом атомарно гасим его, иначе следующий кадр повернёт actor назад.
		bTurningInPlace = false;
		TurnTargetYaw = DesiredYaw;
		PendingTurnAmount = 0.f;
		ActiveTurnRate = 0.f;
		UTacticsCombatStatics::FaceActorTowards(this, TargetLocation);
		if (UTacticsCombatStatics::IsCoverDebugEnabled())
		{
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Turn] %s: мгновенный доворот, yaw=%.0f (дельта %.0f° < %.0f°)"),
				*GetName(), DesiredYaw, Delta, TurnInPlaceMinAngle);
		}
		NotifyUnitStateChanged();
		return;
	}

	TurnTargetYaw = DesiredYaw;
	bTurningInPlace = true;
	ActiveTurnRate = TurnRateOverride > 0.f ? TurnRateOverride : TurnInPlaceRate;

	// ПОЛНАЯ дельта — снимок для ABP: по ней выбирается клип 45/090/135/180 и
	// сторона. Покадровой дельты для этого мало: она не говорит, сколько ещё
	// осталось повернуть.
	PendingTurnAmount = bPlayTurnAnimation ? Delta : 0.f;
	NotifyUnitStateChanged();
}

void AUnitBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCoverHugStepping)
	{
		UpdateCoverHugStep(DeltaSeconds);
	}
	if (bTurningInPlace)
	{
		UpdateTurnInPlace(DeltaSeconds);
	}
	UpdateCoverPeek(DeltaSeconds);
}

void AUnitBase::UpdateCoverPeek(float DeltaSeconds)
{
	// Выглядывать можно только стоя в укрытии, не двигаясь и имея КУДА выглянуть
	// (край укрытия в пределах трейса). В любом другом состоянии таймер сброшен.
	const bool bCanPeek =
		(VisualState.Pose == EUnitPose::CrouchCover || VisualState.Pose == EUnitPose::HighCover) &&
		!VisualState.bMoving && bHasPeekEdge &&
		!FMath::IsNearlyZero(VisualState.PeekSideLocal);

	if (!bCanPeek)
	{
		CoverIdleTime = 0.f;
		if (bPeekActive)
		{
			bPeekActive = false;
			if (bTurnBodyOnPeek)
			{
				FaceCoverWall();
			}
			FrozenPeekSide = 0.f;
			NotifyUnitStateChanged();
		}
		return;
	}

	// ⚠️ Доворот НЕ прерывает выглядывание и не сбрасывает таймер: сам peek его и
	// запускает (разворот к краю). Прерывать здесь означало бы гасить выглядывание
	// на первом же кадре после старта.
	if (!bPeekActive && bTurningInPlace)
	{
		return; // ждём, пока боец довернётся к цели, и только потом копим время
	}

	if (NextPeekDelay <= 0.f)
	{
		NextPeekDelay = FMath::FRandRange(CoverPeekDelayMin, CoverPeekDelayMax);
	}

	CoverIdleTime += DeltaSeconds;

	if (!bPeekActive)
	{
		if (CoverIdleTime >= NextPeekDelay)
		{
			bPeekActive = true;
			CoverIdleTime = 0.f;
			FrozenPeekSide = VisualState.PeekSideLocal; // до разворота, см. поле
			if (bTurnBodyOnPeek)
			{
				FacePeekEdge();
			}
			NotifyUnitStateChanged(); // ABP уходит в Look
		}
		return;
	}

	if (CoverIdleTime >= CoverPeekDuration)
	{
		bPeekActive = false;
		FrozenPeekSide = 0.f;
		CoverIdleTime = 0.f;
		// Новая задержка каждый раз: иначе бойцы в отряде выглядывают синхронно,
		// как метрономы.
		NextPeekDelay = FMath::FRandRange(CoverPeekDelayMin, CoverPeekDelayMax);
		if (bTurnBodyOnPeek)
		{
			FaceCoverWall(); // возврат к укрытию нужен только если сами доворачивали
		}
		NotifyUnitStateChanged(); // ABP возвращается в Loop
	}
}

void AUnitBase::FacePeekEdge()
{
	const UCoverDetectionComponent* Cover = GetCoverDetection();
	if (!Cover)
	{
		return;
	}

	// Край — из кэша (§6): peek стартует только у стоящего юнита, чей кэш
	// пересчитан последним EvaluateSurroundings на этой позиции.
	const FVector EdgeSide = Cover->PeekEdgeDirection;
	FVector ToWall = Cover->BestCoverDirection;
	ToWall.Z = 0.f;
	if (EdgeSide.IsNearlyZero() || !ToWall.Normalize())
	{
		return;
	}

	// ⚠️ Доминирует направление К СТЕНЕ, край лишь подмешивается: боец
	// ДОВОРАЧИВАЕТСЯ к углу (≈35°), а не отворачивается вдоль стены. Полный
	// разворот вбок читается как «пошёл куда-то», а не «выглянул».
	const FVector LookDirection = (ToWall + EdgeSide * 0.7f).GetSafeNormal();
	FaceTowardsSmooth(GetActorLocation() + LookDirection * 200.f,
		/*bPlayTurnAnimation=*/false, CoverHugTurnRate);
}

void AUnitBase::FaceCoverWall()
{
	const UCoverDetectionComponent* Cover = GetCoverDetection();
	if (!Cover || Cover->BestCoverAround == ECoverType::None)
	{
		return;
	}
	FVector ToWall = Cover->BestCoverDirection;
	ToWall.Z = 0.f;
	if (!ToWall.Normalize())
	{
		return;
	}
	const FVector FaceDirection = bCoverHugFaceWall ? ToWall : -ToWall;
	FaceTowardsSmooth(GetActorLocation() + FaceDirection * 100.f,
		/*bPlayTurnAnimation=*/false, CoverHugTurnRate);
}

void AUnitBase::UpdateCoverHugStep(float DeltaSeconds)
{
	// Пришёл новый приказ на движение — подшаг больше не актуален, и держать
	// заниженную скорость нельзя: боец пополз бы весь ход шагом.
	//
	// ⚠️ «Новый приказ» — ТОЛЬКО активный path following. Здесь дважды сидел
	// один и тот же баг самоотмены: сначала IsUnitInTransit (видит скорость,
	// которую создаёт сам подшаг), затем AUnitAIController::IsMoving (включает
	// PendingSettlementUnit и IsMoveSettlementInProgress — то есть сам подшаг).
	// Оба раза шаг умирал на первом тике (лог: план 21–55 см, факт 0–1 см,
	// «подшаг завершён» не печатался), а bApplyCoverFacing=false заодно молча
	// отменял доворот к краю. Настоящий новый приказ во время подшага почти
	// невозможен (MoveAlongRoute отвергает его при живом settlement) — проверка
	// оставлена страховкой от прямых MoveTo* мимо MoveAlongRoute.
	const AUnitAIController* UnitAI = Cast<AUnitAIController>(GetController());
	if (UnitAI && UnitAI->GetMoveStatus() != EPathFollowingStatus::Idle)
	{
		if (UTacticsCombatStatics::IsCoverDebugEnabled())
		{
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Cover] %s: подшаг ОТМЕНЁН — активен path following (новый приказ)"),
				*GetName());
		}
		FinishCoverHugStep(/*bApplyCoverFacing=*/false);
		return;
	}

	CoverHugStepElapsed += DeltaSeconds;

	FVector ToTarget = CoverHugStepTarget - GetActorLocation();
	ToTarget.Z = 0.f;
	const float Distance = ToTarget.Size();

	// Дошли — или уперлись во что-то и стоим (страховка по времени: путь не
	// длиннее дальности укрытия, тройного запаса по времени заведомо хватает).
	const float Timeout = FMath::Max(1.f,
		(UTacticsCombatStatics::GetCoverTuning(GetWorld())->CoverTraceDistance
			/ FMath::Max(1.f, CoverHugStepSpeed)) * 3.f);
	if (Distance <= CoverHugArriveTolerance || CoverHugStepElapsed >= Timeout)
	{
		if (UTacticsCombatStatics::IsCoverDebugEnabled())
		{
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Cover] %s: подшаг %s за %.2f с (осталось %.1f см)"),
				*GetName(),
				Distance <= CoverHugArriveTolerance ? TEXT("ДОШЁЛ") : TEXT("остановлен ПО ТАЙМАУТУ"),
				CoverHugStepElapsed, Distance);
		}
		FinishCoverHugStep();
		return;
	}

	AddMovementInput(ToTarget / Distance, 1.f);
}

void AUnitBase::FinishCoverHugStep(bool bApplyCoverFacing)
{
	bCoverHugStepping = false;
	CoverHugStepElapsed = 0.f;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (CoverHugSavedMaxSpeed > 0.f)
		{
			Movement->MaxWalkSpeed = CoverHugSavedMaxSpeed;
		}
		Movement->StopMovementImmediately(); // без этого юнит по инерции проезжает стену
	}

	// Позиция сдвинулась — укрытие и HUD обязаны пересчитаться от НОВОЙ точки.
	if (UCoverDetectionComponent* MutableCover = GetCoverDetection())
	{
		MutableCover->EvaluateSurroundings();
	}
	if (bApplyCoverFacing && !CoverHugFacingDirection.IsNearlyZero())
	{
		if (UTacticsCombatStatics::IsCoverDebugEnabled())
		{
			// Вторая точка следа доворотов (первая — «прижатие»): если итоговая
			// стойка в [Peek] не совпадёт с этим yaw — юнита развернул кто-то
			// после завершения всей последовательности HugCover.
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Cover] %s: подшаг завершён — доворот к yaw=%.0f (сейчас %.0f)"),
				*GetName(), CoverHugFacingDirection.Rotation().Yaw, GetActorRotation().Yaw);
		}
		FaceTowardsSmooth(GetActorLocation() + CoverHugFacingDirection * 100.f,
			/*bPlayTurnAnimation=*/false, CoverHugTurnRate);
	}
	CoverHugFacingDirection = FVector::ZeroVector;
	NotifyUnitStateChanged();
}

void AUnitBase::UpdateTurnInPlace(float DeltaSeconds)
{
	const FRotator Current = GetActorRotation();
	const float Delta = FMath::FindDeltaAngleDegrees(Current.Yaw, TurnTargetYaw);
	const float Step = (ActiveTurnRate > 0.f ? ActiveTurnRate : TurnInPlaceRate) * DeltaSeconds;

	if (FMath::Abs(Delta) <= Step)
	{
		SetActorRotation(FRotator(Current.Pitch, TurnTargetYaw, Current.Roll));
		bTurningInPlace = false;
		PendingTurnAmount = 0.f;
		ActiveTurnRate = 0.f;
		if (UTacticsCombatStatics::IsCoverDebugEnabled())
		{
			// Точка сверки для «не разворачивается»: если этот yaw верный, а в
			// кадре юнит смотрит иначе — его развернул кто-то ПОСЛЕ доворота.
			UE_LOG(LogXRU1Combat, Display,
				TEXT("[Turn] %s: доворот завершён, yaw=%.0f"),
				*GetName(), TurnTargetYaw);
		}
		NotifyUnitStateChanged(); // ABP гасит анимацию доворота
		return;
	}

	SetActorRotation(FRotator(Current.Pitch, Current.Yaw + FMath::Sign(Delta) * Step, Current.Roll));
}

UAnimMontage* AUnitBase::GetFireMontageFor(const AActor* Target, EFiringStance& OutStance,
	FVector& OutFiringEyeLocation, FVector& OutPresentationRootLocation) const
{
	// Стойка и точка выстрела берутся у ЕДИНОГО источника — того же, что решает
	// геометрию боя. Никакой отдельной «анимационной» логики укрытий быть не
	// должно: разойдётся с тем, что засчитала игра.
	OutStance = UTacticsCombatStatics::GetFiringStance(this, Target, OutFiringEyeLocation);
	OutPresentationRootLocation = GetActorLocation();

	if (OutStance == EFiringStance::StepOut)
	{
		const UCoverTuningDataAsset* Tuning = UTacticsCombatStatics::GetCoverTuning(GetWorld());
		// GetFiringPositions уже вернул nav-projected root, прошедший occupancy и
		// capsule sweep. Вторая независимая проекция здесь раньше сдвигала капсулу
		// до 51 см, но оставляла frozen eye на старом месте — механика и персонаж
		// стреляли из разных точек. Обратное преобразование теперь точное.
		OutPresentationRootLocation = OutFiringEyeLocation
			- FVector(0.f, 0.f, Tuning->EyeHeightOffset);
	}
	switch (OutStance)
	{
	case EFiringStance::OverCover: return FireMontageOverCover ? FireMontageOverCover : FireMontageOpen;
	case EFiringStance::StepOut:   return FireMontageStepOut   ? FireMontageStepOut   : FireMontageOpen;
	default:                       return FireMontageOpen;
	}
}

void AUnitBase::PostInitializeComponents()
{
	// BaseMaxHealth уже содержит итоговый C++/BP-дефолт конкретного класса.
	// В APawn Super::PostInitializeComponents() создаёт default controller, а
	// PossessedBy затем применяет StartupEffects. Поэтому базу GAS намеренно
	// задаём ДО Super: будущие стартовые эффекты модифицируют её, а не стираются.
	const float StartMaxHealth = FMath::Max(1.f, BaseMaxHealth);
	// InitialHealth > 0 — постановочный экземпляр стартует раненым (кламп 1..Max).
	const float StartHealth = InitialHealth > 0.f
		? FMath::Clamp(InitialHealth, 1.f, StartMaxHealth)
		: StartMaxHealth;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->SetNumericAttributeBase(UTDAttributeSet::GetMaxHealthAttribute(), StartMaxHealth);
		ASC->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), StartHealth);
	}
	else if (Attributes)
	{
		// Защитный fallback для нестандартного наследника без ASC.
		Attributes->InitMaxHealth(StartMaxHealth);
		Attributes->InitHealth(StartHealth);
	}

	Super::PostInitializeComponents();
}

void AUnitBase::BeginPlay()
{
	Super::BeginPlay();

	// Класс юнита — источник истины для роли. Дополнительно восстанавливаем
	// канонический позывной, если новый BP оставили пустым либо он унаследовал
	// «Клин» после дублирования BP_Unit_Assault.
	const bool bHasCopiedAssaultName = UnitDisplayName.ToString() == TEXT("Клин");
	if (IsA<AUnit_Sniper>())
	{
		UnitRole = EUnitRole::Sniper;
		if (UnitDisplayName.IsEmpty() || bHasCopiedAssaultName)
		{
			UnitDisplayName = NSLOCTEXT("XRU1Units", "SniperName", "Оса");
		}
	}
	else if (IsA<AUnit_Healer>())
	{
		UnitRole = EUnitRole::Healer;
		if (UnitDisplayName.IsEmpty() || bHasCopiedAssaultName)
		{
			UnitDisplayName = NSLOCTEXT("XRU1Units", "HealerName", "Шприц");
		}
	}
	else if (IsA<AUnit_Tank>())
	{
		UnitRole = EUnitRole::Tank;
		if (UnitDisplayName.IsEmpty() || bHasCopiedAssaultName)
		{
			UnitDisplayName = NSLOCTEXT("XRU1Units", "TankName", "Молот");
		}
	}
	else if (IsA<AUnit_Assault>())
	{
		UnitRole = EUnitRole::Assault;
		if (UnitDisplayName.IsEmpty())
		{
			UnitDisplayName = NSLOCTEXT("XRU1Units", "AssaultName", "Клин");
		}
	}

	GrantClassAbilities();

	// Гарантируем выключенный Custom Depth на старте: если в BP-меше стоит галка
	// «Render CustomDepth Pass», юнит светился бы обводкой до первого наведения.
	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetRenderCustomDepth(false);
	}

	// Смерть/ранение отслеживаем по атрибуту Health (урон приходит только через GAS).
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetHealthAttribute())
			.AddUObject(this, &AUnitBase::HandleHealthChanged);
	}
	if (CoverDetection)
	{
		CoverDetection->OnActiveCoverChanged.AddDynamic(this, &AUnitBase::HandleActiveCoverChanged);
	}
}

void AUnitBase::HandleActiveCoverChanged(int32 /*NewRevision*/)
{
	// Full→Full не вызывает старый OnCoverStateChanged, но направление позы и
	// сторона peek уже другие. Публикуем один согласованный visual snapshot.
	NotifyUnitStateChanged();
}

void AUnitBase::PreviewAimAtTarget(const AActor* Target)
{
	if (!Target || bIsDead || bIsDowned || bIsEvacuated)
	{
		return;
	}

	const UCoverDetectionComponent* Cover = GetCoverDetection();
	const bool bHasActiveCover = Cover && Cover->BestCoverAround != ECoverType::None &&
		!Cover->ActiveCoverNormal.IsNearlyZero();
	if (!bHasActiveCover)
	{
		FaceTowardsSmooth(Target->GetActorLocation());
		return;
	}

	// В укрытии actor rotation — часть стабильного cover snapshot. Cycling цели
	// меняет только aim/camera presentation и не конкурирует с HugCover/peek.
	const bool bHadTurn = bTurningInPlace || !FMath::IsNearlyZero(PendingTurnAmount) ||
		!FMath::IsNearlyZero(ActiveTurnRate);
	bTurningInPlace = false;
	TurnTargetYaw = GetActorRotation().Yaw;
	PendingTurnAmount = 0.f;
	ActiveTurnRate = 0.f;
	if (bHadTurn)
	{
		NotifyUnitStateChanged();
	}
}

void AUnitBase::GrantClassAbilities()
{
	if (!HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	auto Grant = [ASC, this](TSubclassOf<UGameplayAbility> AbilityClass)
	{
		if (AbilityClass)
		{
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	};

	Grant(AttackAbilityClass);
	Grant(OverwatchAbilityClass);
	Grant(HunkerAbilityClass);
	Grant(ClassAbilityClass);
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : ClassAbilities)
	{
		Grant(AbilityClass);
	}
}

void AUnitBase::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (bIsDead || bIsEvacuated)
	{
		return;
	}

	if (Data.NewValue <= 0.f)
	{
		if (bCanBeDowned)
		{
			if (!bIsDowned)
			{
				SetDowned(true);
			}
		}
		else
		{
			Die();
		}
		// SetDowned/Die уже отправили один итоговый refresh.
		return;
	}

	// Обычный урон/лечение живого юнита меняет HPBar портрета. Переход
	// 0 -> positive создаёт SetDowned(false), который уведомит один раз уже
	// после снятия тега Downed, поэтому промежуточный delegate здесь пропускаем.
	if (Data.OldValue > 0.f)
	{
		// HitReact только на реальный урон, не на лечение (NewValue > OldValue).
		if (Data.NewValue < Data.OldValue)
		{
			OnHitReact();
			PlayUnitSound(EUnitSoundEvent::Hit);
		}
		NotifyUnitStateChanged();
	}
}

void AUnitBase::SetDowned(bool bNewDowned, float ReviveHealth, bool bPlaySound)
{
	if (bIsDead || bIsEvacuated || bIsDowned == bNewDowned)
	{
		return;
	}

	bIsDowned = bNewDowned;
	// Звук ставится по подтверждённой смене состояния, а не по запуску montage:
	// отменённое падение/подъём не должны оставить звук без события. Сценарная
	// РАССТАНОВКА (Клин лежит с самого старта) звук не даёт — иначе бой начинается
	// с вскрика на пустом месте.
	if (bPlaySound)
	{
		PlayUnitSound(bIsDowned ? EUnitSoundEvent::Downed : EUnitSoundEvent::Revive);
	}
	// Тяжелораненый лежит без шкалы; поднятый медиком получает её обратно.
	SetOverheadHUDVisible(!bIsDowned);
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();

	if (bIsDowned)
	{
		// Падение: HP в 0 (если форс из скрипта), тег State.Downed, никаких действий.
		if (ASC)
		{
			if (const UTDAttributeSet* Attrs = ASC->GetSet<UTDAttributeSet>())
			{
				if (Attrs->GetHealth() > 0.f)
				{
					ASC->ApplyModToAttribute(UTDAttributeSet::GetHealthAttribute(),
						EGameplayModOp::Override, 0.f);
				}
			}
			ASC->AddLooseGameplayTag(TacticsGameplayTags::State_Downed);
			ASC->CancelAllAbilities();
		}
		if (ActionPoints)
		{
			ActionPoints->SpendAllRemaining();
		}
		// Лежащий раненый не мешает движению/навмешу (как и труп).
		ApplyDefeatedCollision(true);
	}
	else
	{
		// Подъём медиком/скриптом.
		if (ASC)
		{
			ASC->RemoveLooseGameplayTag(TacticsGameplayTags::State_Downed);
			ASC->ApplyModToAttribute(UTDAttributeSet::GetHealthAttribute(),
				EGameplayModOp::Override, FMath::Max(1.f, ReviveHealth));
		}
		ApplyDefeatedCollision(false); // встал — снова блокирует и режет навмеш
	}

	OnDownedChanged(bIsDowned);
	NotifyUnitStateChanged();
}

void AUnitBase::Die()
{
	if (bIsDead)
	{
		return;
	}
	bIsDead = true;
	PlayUnitSound(EUnitSoundEvent::Death);
	// Шкала HP над трупом — самый заметный визуальный мусор боя.
	SetOverheadHUDVisible(false);
	// Пока назначенный montage играет через BP-хук, Dead state не запускает
	// вторую death sequence под тем же Default Slot. Если montage отсутствует,
	// старый Dead state остаётся штатным fallback.
	bDeathMontageOwnsPose = DeathMontage != nullptr;
	bDeathPresentationActive = bDeathMontageOwnsPose;

	// Труп не подсвечивается: гасим кольцо и обводку.
	SetSelectionHighlight(false);
	SetHoverHighlight(false);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}
	// Труп не блокирует выстрелы/перемещение и не режет навмеш; меш остаётся
	// для анимации смерти в BP.
	ApplyDefeatedCollision(true);
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	OnDied();
	NotifyUnitStateChanged();

	// Анимация смерти не доводит тело до пола (клип заканчивается «на весу», а на
	// склоне поза вообще висит в воздухе). Поэтому по её окончании тело уходит в
	// физику и доваливается само. Длительность берём у назначенного монтажа —
	// один источник правды с тем, что реально играет BP.
	const float Delay = DeathMontage
		? FMath::Max(RagdollDelay, DeathMontage->GetPlayLength() + 0.25f)
		: RagdollDelay;
	if (Delay > 0.f)
	{
		GetWorldTimerManager().SetTimer(RagdollTimerHandle, this, &AUnitBase::StartRagdoll, Delay, false);
	}
	else
	{
		StartRagdoll();
	}
}

void AUnitBase::NotifyDeathMontageFinished(bool bInterrupted)
{
	if (!bIsDead || !bDeathPresentationActive)
	{
		return;
	}
	UE_LOG(LogXRU1Combat, Verbose, TEXT("[Death] %s montage %s — terminal ragdoll"),
		*GetNameSafe(this), bInterrupted ? TEXT("interrupted") : TEXT("completed"));
	StartRagdoll();
}

void AUnitBase::StartRagdoll()
{
	// Terminal-флаг гасим до любых ранних выходов: callback и watchdog могут
	// прийти в соседних кадрах, но владение death-pose при этом остаётся навсегда.
	GetWorldTimerManager().ClearTimer(RagdollTimerHandle);
	bDeathPresentationActive = false;

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp || MeshComp->IsSimulatingPhysics())
	{
		return;
	}

	// Движение выключаем ДО физики: иначе CharacterMovement продолжает считать
	// падение капсулы, у которой уже нет коллизии, и тащит актор сквозь пол.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// Профиль `Ragdoll`: тело сталкивается с миром (лежит на полу, а не проваливается),
	// но по каналу Pawn прозрачно — сквозь труп можно пробежать.
	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	MeshComp->SetCanEverAffectNavigation(false);
	MeshComp->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, /*bIncludeSelf=*/true);
	MeshComp->WakeAllRigidBodies();
}

void AUnitBase::ApplyDefeatedCollision(bool bDefeated)
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// ⚠️ НЕ `NoCollision`. Капсула остаётся query-объектом, который никого не
		// блокирует, но ловит трейсы: по упавшему бойцу игрок обязан попасть
		// курсором, иначе медик физически не может выбрать его целью подъёма.
		if (bDefeated)
		{
			// Block ТОЛЬКО по `Visibility`: этот канал не участвует в движении
			// (CharacterMovement свипает по `Pawn`), поэтому бегущие проходят
			// сквозь упавшего, а курсорный трейс в него попадает. `Overlap` здесь
			// не годится — одиночный трейс возвращает лишь блокирующие хиты.
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
		else
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
		}
		Capsule->SetCanEverAffectNavigation(!bDefeated);
	}
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// НЕ резать навмеш телом (критично при RuntimeGeneration=Dynamic) и
		// пропускать бегущих сквозь тело. На подъёме навмеш восстанавливаем;
		// отклик по Pawn на живом мертвецу и так не мешает (капсула снова блокирует).
		MeshComp->SetCanEverAffectNavigation(!bDefeated);
		if (bDefeated)
		{
			MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	}

	// Упавший (раненый или труп) не должен продолжать «идти»: движение с
	// отключённой физикой капсулы утаскивает тело сквозь пол, особенно на склоне.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (bDefeated)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		else if (Movement->MovementMode == MOVE_None)
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
}

void AUnitBase::Evacuate()
{
	if (bIsDead || bIsEvacuated)
	{
		return;
	}
	bIsEvacuated = true;
	PlayUnitSound(EUnitSoundEvent::Evacuated);
	SetOverheadHUDVisible(false);

	SetSelectionHighlight(false);
	SetHoverHighlight(false);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	OnEvacuated();
	NotifyUnitStateChanged();
}
