#include "TacticalQuestZone.h"

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "TacticalQuestEvents.h"
#include "TacticsCombatStatics.h"
#include "TutorialStyleData.h"
#include "TurnManagerSubsystem.h"
#include "UnitBase.h"

ATacticalQuestZone::ATacticalQuestZone()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->InitBoxExtent(FVector(200.f, 200.f, 100.f));
	// Не стандартный Trigger: его ObjectType = WorldDynamic, а cover и линия огня
	// трассируются LineTraceByObjectType по WorldStatic/WorldDynamic. Бокс зоны
	// тогда работает как стена — даёт бойцу внутри полное укрытие и не даёт
	// стрелять наружу. Профиль ScenarioTrigger (DefaultEngine.ini) оставляет
	// overlap с пешками, но выводит зону из этих трейсов.
	TriggerBox->SetCollisionProfileName(TEXT("ScenarioTrigger"));

	// Профиль даёт только ObjectType (чтобы cover/LOS-трейсы по WorldStatic и
	// WorldDynamic зону не видели). Ответы каналов задаём здесь и явно: полагаться
	// на DefaultResponse ini нельзя — неуказанный канал молча становится Block, и
	// тогда бокс зоны перехватывает курсорный трейс по Pawn. Клик по бойцу внутри
	// зоны попадал в саму зону, и целевые способности (лечение) выбрать было нельзя.
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);

	// Триггер не существует для навигации ни при каком профиле: иначе рекаст
	// вырезал бы навмеш ровно там, куда шаг обучения просит прийти.
	TriggerBox->SetCanEverAffectNavigation(false);
	SetRootComponent(TriggerBox);
}

void ATacticalQuestZone::BeginPlay()
{
	Super::BeginPlay();
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ATacticalQuestZone::HandleBeginOverlap);
}

void ATacticalQuestZone::SetHighlighted(bool bNewHighlighted)
{
	if (!bNewHighlighted)
	{
		if (HighlightDecal)
		{
			HighlightDecal->SetVisibility(false);
		}
		return;
	}

	if (!HighlightDecal)
	{
		UMaterialInterface* Material =
			UTutorialStyleData::Get(this)->ZoneMarkerMaterial.LoadSynchronous();
		if (!Material)
		{
			return; // материал не задан — подсветка опциональна
		}
		HighlightDecal = NewObject<UDecalComponent>(this, TEXT("ZoneHighlight"));
		HighlightDecal->SetupAttachment(TriggerBox);
		HighlightDecal->RegisterComponent();
		HighlightDecal->SetDecalMaterial(Material);
		// Декаль проецируется вниз; X — глубина проекции, Y/Z — половины прямоугольника.
		const FVector Extent = TriggerBox->GetScaledBoxExtent();
		HighlightDecal->DecalSize = FVector(300.f, Extent.Y, Extent.X);
		HighlightDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
		HighlightDecal->SetFadeScreenSize(0.f);
	}
	HighlightDecal->SetVisibility(true);
}

void ATacticalQuestZone::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	AUnitBase* Unit = Cast<AUnitBase>(OtherActor);
	const TWeakObjectPtr<AActor> WeakUnit(Unit);
	const UWorld* World = GetWorld();
	const UTurnManagerSubsystem* TurnManager = World ? World->GetSubsystem<UTurnManagerSubsystem>() : nullptr;
	if (!Unit || !TurnManager || !UTacticsCombatStatics::IsUnitAlive(Unit) ||
		!TurnManager->GetPlayerSideUnits().Contains(Unit) ||
		(bOneShotPerUnit && TriggeredUnits.Contains(WeakUnit)))
	{
		return;
	}

	if (!UTacticalQuestEvents::BroadcastQuestEventEx(this, EventChannel, Unit, this, 1))
	{
		return;
	}

	TriggeredUnits.Add(WeakUnit);
	OnTacticalUnitEntered(Unit);

	if (bDisableAfterFirstUnit)
	{
		TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
