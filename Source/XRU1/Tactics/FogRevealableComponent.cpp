#include "FogRevealableComponent.h"

#include "FogOfWarSubsystem.h"
#include "ScenarioActorRegistry.h" // staged-акторами владеет постановка, а не туман
#include "TacticsCombatStatics.h"
#include "UnitBase.h"
#include "XRU1Log.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UFogRevealableComponent::UFogRevealableComponent()
{
	// Компонент ничего не опрашивает сам: решение приходит от подсистемы одним
	// вызовом на событие. Тик здесь означал бы второй, независимый от правил
	// источник видимости — ровно то, чего вся конструкция избегает.
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UFogRevealableComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		Fog->RegisterRevealable(this);
	}
}

void UFogRevealableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		Fog->UnregisterRevealable(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool UFogRevealableComponent::IsActorPresentationHidden(const AActor* Actor)
{
	const UFogRevealableComponent* Component = Actor
		? Actor->FindComponentByClass<UFogRevealableComponent>() : nullptr;
	return Component && Component->IsPresentationHidden();
}

void UFogRevealableComponent::AddScriptedRevealHold()
{
	++ScriptedRevealHolds;
	if (ScriptedRevealHolds == 1)
	{
		// Причину проставляем ЗДЕСЬ: показ идёт мимо `ResolveDesiredVisibility`,
		// и без этой строки в журнал попадает прошлая, уже неверная причина —
		// строка «ПОКАЗАН (сценарно неактивен)», то есть журнал утверждает
		// прямо противоположное тому, что произошло.
		LastDecisionReason = TEXT("сценарное удержание");
		// Показать НЕМЕДЛЕННО: такт уже ведёт камеру к этому актору, ждать
		// ближайшего пересчёта нельзя — иначе кадр начнётся с пустого места.
		ApplyVisibility(true);
		// И тут же попросить пересчёт: картинку мы поправили сами, но кэш и
		// подписчики (счётчик врагов, камера) узнают о ней только от подсистемы.
		if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
		{
			Fog->MarkVisibilityDirty(this);
		}
		UE_LOG(LogXRU1Fog, Log, TEXT("[Fog] %s: сценарное удержание показа взято"),
			*GetNameSafe(GetOwner()));
	}
}

void UFogRevealableComponent::RemoveScriptedRevealHold()
{
	ScriptedRevealHolds = FMath::Max(0, ScriptedRevealHolds - 1);
	if (ScriptedRevealHolds == 0)
	{
		// Обратно решать не нам: снятие удержания только просит пересчёт, а
		// прятать или нет — скажет подсистема по фактическому LOS.
		if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
		{
			Fog->MarkVisibilityDirty(this);
		}
		UE_LOG(LogXRU1Fog, Log, TEXT("[Fog] %s: сценарное удержание показа снято"),
			*GetNameSafe(GetOwner()));
	}
}

void UFogRevealableComponent::ClearScriptedRevealHolds()
{
	if (ScriptedRevealHolds > 0)
	{
		UE_LOG(LogXRU1Fog, Log, TEXT("[Fog] %s: сброшено %d сценарных удержаний"),
			*GetNameSafe(GetOwner()), ScriptedRevealHolds);
	}
	ScriptedRevealHolds = 0;
}

bool UFogRevealableComponent::ResolveDesiredVisibility(bool bComputedVisible) const
{
	// Порядок приоритетов — единственное место, где он записан.
	// 1) Сценарий главнее всего: постановка обучения не обсуждается с LOS.
	if (ScriptedRevealHolds > 0)
	{
		LastDecisionReason = TEXT("сценарное удержание");
		return true;
	}
	if (Override == EFogVisibilityOverride::AlwaysVisible)
	{
		LastDecisionReason = TEXT("Override=AlwaysVisible");
		return true;
	}
	if (Override == EFogVisibilityOverride::AlwaysHidden)
	{
		LastDecisionReason = TEXT("Override=AlwaysHidden");
		return false;
	}

	// 2) Актор, выключенный постановкой сценария, не показывается НИКОГДА: им
	//    владеет другой механизм (`UTacticalScenarioSubsystem::SetActorScenarioActive`),
	//    и туман не имеет права проявить голограмму следующей секции. Проверка
	//    обязательна: оба механизма пишут в один и тот же `bHidden` актора.
	if (!UTacticalScenarioSubsystem::IsActorScenarioActive(GetOwner()))
	{
		LastDecisionReason = TEXT("сценарно неактивен");
		return false;
	}

	// 3) Тела и лежачие остаются на экране (правило XCOM 2: `!IsAlive()`,
	//    `IsBleedingOut()`, `IsUnconscious()` → eForceVisible). Иначе труп, по
	//    которому только что стреляли, исчезал бы вместе с последним свидетелем.
	const AUnitBase* Unit = Cast<AUnitBase>(GetOwner());
	if (Unit && !UTacticsCombatStatics::IsUnitAlive(Unit) && !Unit->IsEvacuated())
	{
		LastDecisionReason = TEXT("тело/Downed видимы всегда");
		return true;
	}

	LastDecisionReason = bComputedVisible ? TEXT("виден отряду") : TEXT("вне зрения отряда");
	return bComputedVisible;
}

/**
 * Задержка перед скрытием (сек). Появление мгновенно, исчезновение — с паузой.
 *
 * ⚠️ Асимметрия намеренная. Видимость считается по НЕПРЕРЫВНОЙ позиции, а не по
 * тайлам, как в XCOM: боец, бегущий вдоль линии обзора, может пересекать границу
 * несколько раз в секунду, и без задержки враг мигал бы на экране (а журнал
 * заполнялся бы парами «СКРЫТ/ПОКАЗАН»). Опаздывать с ПОКАЗОМ нельзя — это
 * прямая потеря информации; опоздать со СКРЫТИЕМ на треть секунды безвредно.
 *
 * Тот же приём у самого проекта в перцепции AI: `SightRadius 2500` против
 * `LoseSightRadius 2800` — увидеть легче, чем потерять.
 */
static TAutoConsoleVariable<float> CVarFogHideGrace(
	TEXT("xru1.Fog.HideGrace"), 0.35f,
	TEXT("Туман войны: задержка перед скрытием ушедшего из зрения, сек (0 — сразу)."),
	ECVF_Default);

bool UFogRevealableComponent::CanHideNow() const
{
	// Юнит доигрывает действие — прятать нельзя. У Firaxis то же условие в
	// `ForceModelVisible`: `bRemovedFromPlay` прячет только когда текущий action
	// завершён. Монтаж — наш аналог «идёт action»: выстрел, попадание, смерть,
	// подъём. Пропадание пешки посреди него читается как визуальный баг.
	//
	// ⚠️ Движение сюда НЕ входит, хотя соблазн велик. Ровно бег за угол — главный
	// случай, ради которого туман и существует: если запретить скрытие «пока
	// юнит в пути», однажды увиденный враг останется на экране всю свою
	// перебежку через полкарты. Мигания это не даёт: пересчёт на бегу и так
	// троттлится (`xru1.Fog.MoveRecheck`), а XCOM пересчитывает видимость
	// бегущей пешки на КАЖДОМ пройденном тайле.
	const AUnitBase* Unit = Cast<AUnitBase>(GetOwner());
	const USkeletalMeshComponent* Mesh = Unit ? Unit->GetMesh() : nullptr;
	const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	return !(Anim && Anim->IsAnyMontagePlaying());
}

bool UFogRevealableComponent::ApplyVisibility(bool bVisible)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// ⚠️ Своих компонент не трогает НИКОГДА, даже когда его просят «показать».
	// Просьба приходит не только от пересчёта: сценарный такт `Scripted Move`
	// работает и с союзниками и берёт удержание показа для любого бойца. Без
	// этой оговорки удержание вернуло бы оверхед-худ союзника посреди кадра
	// выстрела — то есть у видимости худов отряда снова стало бы два владельца
	// (единственный — `UpdateSquadOverheadVisibility`).
	if (UFogOfWarSubsystem::IsPlayerSideActor(Owner))
	{
		return false;
	}

	// Откладывать имеет смысл только ПЕРЕХОД в скрытие. Уже скрытому актору
	// прятаться не во что, и без этой оговорки зациклившийся монтаж (idle-петля,
	// длинная смерть) держал бы `bHideDeferred` вечно — а подсистема из-за него
	// пересчитывала бы туман каждые 0.1 с до конца боя.
	if (!bVisible && !bPresentationHidden)
	{
		const UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (HideRequestedTime < 0.0)
		{
			HideRequestedTime = Now; // первый кадр, когда цель ушла из зрения
		}
		const float Grace = FMath::Max(0.f, CVarFogHideGrace.GetValueOnGameThread());
		const bool bGraceOver = (Now - HideRequestedTime) >= Grace;

		if (!bGraceOver || !CanHideNow())
		{
			// ⚠️ Выдержка — это НОРМАЛЬНЫЙ путь каждого скрытия, и печатать её по
			// умолчанию значит удваивать журнал ради строки «всё идёт как надо».
			// Про неё говорим только при включённом разборе. А вот «актор
			// доигрывает действие» — редкий и интересный случай (юнит завис в
			// монтаже и потому не прячется), он печатается всегда.
			if (!bHideDeferred && (bGraceOver || UFogOfWarSubsystem::IsExplainEnabled()))
			{
				UE_LOG(LogXRU1Fog, Log, TEXT("[Fog] %s: скрытие отложено (%s)"),
					*GetNameSafe(Owner),
					bGraceOver ? TEXT("актор доигрывает действие") : TEXT("выдержка перед скрытием"));
			}
			bHideDeferred = true;
			return false;
		}
	}
	bHideDeferred = false;
	// Показали (или уже скрыты) — отсчёт выдержки начнётся заново со следующего
	// ухода из зрения. Иначе одна давняя отметка разрешала бы скрытие мгновенно.
	if (bVisible)
	{
		HideRequestedTime = -1.0;
	}

	if (!bVisible && !bPresentationHidden)
	{
		WarnIfHiddenBreaksAnimation();
	}

	const bool bWantHidden = !bVisible;
	const bool bChanged = (bWantHidden != bPresentationHidden);
	bPresentationHidden = bWantHidden;

	// ⚠️ Переутверждаем состояние КАЖДЫЙ раз, а не только при изменении: флагом
	// `bHidden` актора владеет не только туман — постановка сценария
	// (`SetActorScenarioActive`) пишет в него же. Ранний выход «значение не
	// изменилось» оставлял бы включённую сценарием голограмму на экране, пока
	// туман считает её скрытой. Оба вызова внутри себя отбрасывают повтор того же
	// значения, так что переутверждение бесплатно.
	//
	// База: скрывает меш, декаль выбора И вложенные Child Actor'ы (оружие) —
	// см. разбор `USceneComponent::ShouldRender` в заголовке.
	Owner->SetActorHiddenInGame(bWantHidden);
	ApplyExtraPresentation(bVisible);

	if (bChanged)
	{
		UE_LOG(LogXRU1Fog, Log, TEXT("[Fog] %s → %s (%s)"),
			*GetNameSafe(Owner), bWantHidden ? TEXT("СКРЫТ") : TEXT("ПОКАЗАН"),
			LastDecisionReason);
	}
	return bChanged;
}

void UFogRevealableComponent::WarnIfHiddenBreaksAnimation()
{
	if (bAnimationTickChecked)
	{
		return;
	}
	bAnimationTickChecked = true;

	const AUnitBase* Unit = Cast<AUnitBase>(GetOwner());
	const USkeletalMeshComponent* Mesh = Unit ? Unit->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}

	// Скрытый юнит обязан ПРОДОЛЖАТЬ проигрывать монтажи: на их нотифаях висит
	// `FireCommit` и завершение действий хода. Проверено на ассетах проекта —
	// у юнитов стоит `AlwaysTickPose` (поза тикает всегда, кости обновляются
	// только когда отрендерен), и этого достаточно. Но настройка живёт в BP, и
	// смена её на «тикать только когда отрендерен» превратит скрытие врага в
	// зависший ход врага — баг, который без этого предупреждения ищут часами.
	const EVisibilityBasedAnimTickOption Option = Mesh->VisibilityBasedAnimTickOption;
	if (Option == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered)
	{
		UE_LOG(LogXRU1Fog, Warning,
			TEXT("[Fog] У %s меш стоит в OnlyTickPoseWhenRendered: скрытый туманом юнит ")
			TEXT("перестанет проигрывать монтажи, и его ход зависнет. Нужен AlwaysTickPose ")
			TEXT("или AlwaysTickPoseAndRefreshBones."),
			*GetNameSafe(GetOwner()));
	}
}

void UFogRevealableComponent::ApplyExtraPresentation(bool bVisible)
{
	AUnitBase* Unit = Cast<AUnitBase>(GetOwner());
	if (!Unit)
	{
		return;
	}

	// Оверхед-худ: `EWidgetSpace::Screen` живёт мимо флага скрытия актора.
	// Возврат — только если состояние самого юнита это позволяет: у лежащего и
	// мёртвого шкалы нет по своей причине, и туман не имеет права её вернуть.
	const bool bOverheadAllowed = bVisible && !Unit->IsDowned() && !Unit->IsDead()
		&& !Unit->IsEvacuated();
	Unit->SetOverheadHUDVisible(bOverheadAllowed);

	if (!bVisible)
	{
		// Custom Depth — состояние компонента меша: скрытый актор не рисуется, но
		// стоит ему вернуться, и старая обводка проступит через `M_OutlinePP`.
		Unit->SetHoverHighlight(false);
	}
}
