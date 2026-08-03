#include "ScenarioActorRegistry.h"
#include "XRU1Log.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "FogOfWarSubsystem.h" // включение/выключение staged-актора меняет видимость
#include "GameFramework/Controller.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"

// --- UScenarioActorIdComponent -------------------------------------------------

UScenarioActorIdComponent::UScenarioActorIdComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Компонент обязан отработать до сбора сторон боя: GameMode запускает бой
	// только после OnLevelShown, но BeginPlay streamed-акторов идёт раньше.
	bAutoActivate = true;
}

void UScenarioActorIdComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UTacticalScenarioSubsystem* Registry = World->GetSubsystem<UTacticalScenarioSubsystem>())
		{
			Registry->RegisterScenarioActor(this);
			if (bStartDeactivated)
			{
				Registry->SetActorScenarioActive(GetOwner(), false);
			}
			else if (bStartDowned)
			{
				// Актор активен с самого начала — Downed применяем здесь, иначе
				// его поставит первое включение через SetActorScenarioActive.
				Registry->ApplyStartDowned(this);
			}
		}
	}

	if (AnchorId.IsNone())
	{
		UE_LOG(LogXRU1Scenario, Warning,
			TEXT("[Scenario] У %s пустой AnchorId — актор не будет найден задачами сценария"),
			*GetNameSafe(GetOwner()));
	}
}

void UScenarioActorIdComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UTacticalScenarioSubsystem* Registry = World->GetSubsystem<UTacticalScenarioSubsystem>())
		{
			Registry->UnregisterScenarioActor(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// --- UTacticalScenarioSubsystem ------------------------------------------------

void UTacticalScenarioSubsystem::RegisterScenarioActor(UScenarioActorIdComponent* Component)
{
	if (!Component || Component->AnchorId.IsNone())
	{
		return;
	}
	Registry.AddUnique(Component->AnchorId, Component);
}

void UTacticalScenarioSubsystem::UnregisterScenarioActor(UScenarioActorIdComponent* Component)
{
	if (!Component)
	{
		return;
	}
	Registry.RemoveSingle(Component->AnchorId, Component);
}

AActor* UTacticalScenarioSubsystem::FindScenarioActor(FName AnchorId) const
{
	if (AnchorId.IsNone())
	{
		return nullptr;
	}

	TArray<TWeakObjectPtr<UScenarioActorIdComponent>> Found;
	Registry.MultiFind(AnchorId, Found);
	for (const TWeakObjectPtr<UScenarioActorIdComponent>& Component : Found)
	{
		if (Component.IsValid() && Component->GetOwner())
		{
			return Component->GetOwner();
		}
	}
	return nullptr;
}

TArray<AActor*> UTacticalScenarioSubsystem::FindScenarioActors(FName AnchorId) const
{
	TArray<AActor*> Result;
	if (AnchorId.IsNone())
	{
		return Result;
	}

	TArray<TWeakObjectPtr<UScenarioActorIdComponent>> Found;
	Registry.MultiFind(AnchorId, Found);
	for (const TWeakObjectPtr<UScenarioActorIdComponent>& Component : Found)
	{
		if (Component.IsValid() && Component->GetOwner())
		{
			Result.Add(Component->GetOwner());
		}
	}
	return Result;
}

AActor* UTacticalScenarioSubsystem::FindScenarioActorInWorld(
	const UObject* WorldContextObject, FName AnchorId)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UTacticalScenarioSubsystem* Registry = World
		? World->GetSubsystem<UTacticalScenarioSubsystem>() : nullptr;
	return Registry ? Registry->FindScenarioActor(AnchorId) : nullptr;
}

FName UTacticalScenarioSubsystem::GetScenarioAnchorId(const AActor* Actor)
{
	const UScenarioActorIdComponent* Component = Actor
		? Actor->FindComponentByClass<UScenarioActorIdComponent>() : nullptr;
	return Component ? Component->AnchorId : NAME_None;
}

bool UTacticalScenarioSubsystem::IsActorScenarioActive(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	// Обычный актор карты без компонента всегда участвует: сценарная постановка
	// не должна требовать отметки на каждом мешe окружения.
	const UScenarioActorIdComponent* Component =
		Actor->FindComponentByClass<UScenarioActorIdComponent>();
	return !Component || Component->IsScenarioActive();
}

int32 UTacticalScenarioSubsystem::SetScenarioActorActive(FName AnchorId, bool bActive)
{
	int32 Changed = 0;
	int32 Found = 0;
	for (AActor* Actor : FindScenarioActors(AnchorId))
	{
		++Found;
		if (SetActorScenarioActive(Actor, bActive))
		{
			++Changed;
		}
	}
	// Warning только когда якоря действительно нет: «актор уже в нужном
	// состоянии» — штатная ситуация (пример: Клин активен с самого старта,
	// а шаг на всякий случай включает его ещё раз).
	if (Found == 0)
	{
		UE_LOG(LogXRU1Scenario, Warning, TEXT("[Scenario] SetScenarioActorActive: AnchorId %s не найден"),
			*AnchorId.ToString());
	}
	return Changed;
}

bool UTacticalScenarioSubsystem::SetActorScenarioActive(AActor* Actor, bool bActive)
{
	UScenarioActorIdComponent* Component = Actor
		? Actor->FindComponentByClass<UScenarioActorIdComponent>() : nullptr;
	if (!Component || Component->bScenarioActive == bActive)
	{
		return false;
	}

	Component->bScenarioActive = bActive;

	// Presentation и gameplay переключаются ОДНИМ вызовом: скрытый, но
	// коллизионный и «видимый» для perception актор ломает LOS, укрытия и AI.
	Actor->SetActorHiddenInGame(!bActive);
	Actor->SetActorEnableCollision(bActive);
	Actor->SetActorTickEnabled(bActive);

	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AController* PawnController = Pawn->GetController())
		{
			PawnController->SetActorTickEnabled(bActive);
		}
	}

	// Участие в сторонах боя — часть того же переключателя: выключенная
	// голограмма не должна ни ходить, ни считаться живым врагом.
	if (AUnitBase* Unit = Cast<AUnitBase>(Actor))
	{
		if (UWorld* World = GetWorld())
		{
			if (UTurnManagerSubsystem* TurnManager = World->GetSubsystem<UTurnManagerSubsystem>())
			{
				if (bActive)
				{
					TurnManager->RegisterUnitInCombat(Unit);
				}
				else
				{
					TurnManager->UnregisterUnitFromCombat(Unit);
				}
			}
		}
		Unit->NotifyUnitStateChanged();
		if (bActive)
		{
			ApplyStartDowned(Component);

			// Меш, долго простоявший скрытым, с дефолтной оптимизацией «тикать
			// позу только когда отрендерен» начинает движение «скольжением»:
			// первый же бег включённого staged-бойца требует полного тика позы.
			if (USkeletalMeshComponent* Mesh = Unit->GetMesh())
			{
				Mesh->VisibilityBasedAnimTickOption =
					EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
		}
	}

	// Туман обязан узнать о смене состава мира: включённая голограмма может
	// оказаться вне зрения отряда (тогда её прячет уже туман), а выключенная —
	// исчезнуть из кэша видимых. Оба механизма пишут в один `bHidden` актора,
	// поэтому пересчёт здесь не опционален.
	if (UFogOfWarSubsystem* Fog = UFogOfWarSubsystem::Get(this))
	{
		Fog->MarkVisibilityDirty(Actor);
	}

	Component->OnScenarioActiveChanged(bActive);
	return true;
}

void UTacticalScenarioSubsystem::ApplyStartDowned(UScenarioActorIdComponent* Component)
{
	if (!Component || !Component->bStartDowned || Component->bDownedApplied)
	{
		return;
	}
	AUnitBase* Unit = Cast<AUnitBase>(Component->GetOwner());
	if (!Unit)
	{
		return;
	}
	Component->bDownedApplied = true;
	// Тихо: это стартовая расстановка сценария, а не полученный в бою урон.
	Unit->SetDowned(true, /*ReviveHealth=*/30.f, /*bPlaySound=*/false);
}

// --- AScenarioAnchorPoint ------------------------------------------------------

AScenarioAnchorPoint::AScenarioAnchorPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	ScenarioId = CreateDefaultSubobject<UScenarioActorIdComponent>(TEXT("ScenarioId"));

#if WITH_EDITORONLY_DATA
	EditorIcon = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon)
	{
		EditorIcon->SetupAttachment(RootComponent);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif
}
