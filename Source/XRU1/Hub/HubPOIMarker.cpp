#include "HubPOIMarker.h"

#include "XRU1Log.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

AHubPOIMarker::AHubPOIMarker()
{
	// Пульсация свечения требует тика; базовый класс его не включает.
	PrimaryActorTick.bCanEverTick = true;

	// Сфера-маркер: базовый Marker из AMissionPointOfInterest.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshAsset(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	if (Marker && SphereMeshAsset.Succeeded())
	{
		Marker->SetStaticMesh(SphereMeshAsset.Object);
		Marker->SetRelativeScale3D(FVector(0.6f));
		Marker->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
		// Клики ловит HoverBounds базового класса — меш не должен ему мешать.
		Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Marker->SetCastShadow(false);
	}

	Beam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beam"));
	Beam->SetupAttachment(Root);
	if (CylinderMeshAsset.Succeeded())
	{
		Beam->SetStaticMesh(CylinderMeshAsset.Object);
	}
	// Столб от поверхности карты до сферы: тонкий и высотой в половину подъёма
	// сферы (меш цилиндра высотой 100 см с центром в середине).
	Beam->SetRelativeScale3D(FVector(0.08f, 0.08f, 1.2f));
	Beam->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCastShadow(false);

	if (HoverBounds)
	{
		// Кликабельная зона накрывает и сферу, и столб.
		HoverBounds->SetSphereRadius(110.f);
		HoverBounds->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	}
	if (PopupWidget)
	{
		PopupWidget->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
	}
}

void AHubPOIMarker::BeginPlay()
{
	Super::BeginPlay();

	BaseMarkerScale = Marker ? Marker->GetRelativeScale3D() : FVector::OneVector;

	if (MarkerMaterial)
	{
		if (Marker)
		{
			SphereMaterial = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
			Marker->SetMaterial(0, SphereMaterial);
		}
		if (Beam)
		{
			BeamMaterial = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
			Beam->SetMaterial(0, BeamMaterial);
		}
	}

	OnHoverChanged.AddUniqueDynamic(this, &AHubPOIMarker::HandleHoverChanged);
	RefreshVisualState();

	// Почему точка серая — вопрос, который иначе решается только гаданием.
	const FText Reason = GetLockedReason();
	UE_LOG(LogXRU1UI, Display, TEXT("[Hub] Точка '%s': %s"),
		*GetActorNameOrLabel(),
		Reason.IsEmpty() ? TEXT("доступна") : *Reason.ToString());
}

void AHubPOIMarker::HandleHoverChanged(bool /*bHovered*/)
{
	RefreshVisualState();
}

void AHubPOIMarker::RefreshVisualState()
{
	// Гейт перечитывается на каждое изменение состояния (старт, наведение):
	// миссия могла разблокироваться, пока игрок проходил обучение. Результат
	// кэшируется — Tick не должен опрашивать слот кампании каждый кадр.
	const bool bLocked = IsLocked();
	bCachedLocked = bLocked;
	const FLinearColor Color = bLocked
		? LockedColor
		: (bIsHovered ? HoveredColor : AvailableColor);

	// Прозрачность тоже состояние: доступная точка должна читаться как живая
	// метка, а заблокированная — как погашенная. Одного цвета мало на ярком
	// бирюзовом столе карты.
	const float Opacity = bLocked ? LockedOpacity : AvailableOpacity;

	if (SphereMaterial)
	{
		SphereMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		SphereMaterial->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		if (bLocked)
		{
			// Статичное «погашенное» свечение ставится здесь, а не в тике.
			SphereMaterial->SetScalarParameterValue(TEXT("Glow"), BaseGlow * LockedGlowScale);
		}
	}
	if (BeamMaterial)
	{
		BeamMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		BeamMaterial->SetScalarParameterValue(TEXT("Opacity"), Opacity * 0.8f);
		if (bLocked)
		{
			BeamMaterial->SetScalarParameterValue(TEXT("Glow"), BaseGlow * LockedGlowScale);
		}
	}
	if (Marker)
	{
		Marker->SetRelativeScale3D(BaseMarkerScale * ((!bLocked && bIsHovered) ? HoverScale : 1.f));
	}

	UE_LOG(LogXRU1UI, Verbose, TEXT("[Hub] маркер '%s': locked=%d hovered=%d color=%s"),
		*GetActorNameOrLabel(), bLocked ? 1 : 0, bIsHovered ? 1 : 0, *Color.ToString());
}

void AHubPOIMarker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Заблокированная точка не пульсирует: её свечение выставлено один раз в
	// RefreshVisualState. Здесь опирается на КЭШ, а не на IsLocked() — тот
	// уходит в GameInstance → слот → LoadSynchronous требований миссии, и
	// каждый кадр это лишняя работа ради значения, которое в пределах одного
	// захода в хаб не меняется.
	if (bCachedLocked || PulseAmplitude <= 0.f || !SphereMaterial)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Time = World ? World->GetTimeSeconds() : 0.f;
	const float Glow = BaseGlow + FMath::Sin(Time * PulseSpeed) * PulseAmplitude;
	SphereMaterial->SetScalarParameterValue(TEXT("Glow"), Glow);
	if (BeamMaterial)
	{
		BeamMaterial->SetScalarParameterValue(TEXT("Glow"), Glow * 0.6f);
	}
}
