#include "MoveRangeVisualizer.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "ProceduralMeshComponent.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Engine/World.h"

AMoveRangeVisualizer::AMoveRangeVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;

	ZoneMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ZoneMesh"));
	SetRootComponent(ZoneMesh);
	ZoneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneMesh->SetCastShadow(false);
}

void AMoveRangeVisualizer::ShowForUnit(AUnitBase* Unit)
{
	Hide();
	if (!Unit || !Unit->GetActionPoints())
	{
		return;
	}

	const int32 ActionPoints = Unit->GetActionPoints()->CurrentActionPoints;
	if (ActionPoints <= 0)
	{
		return;
	}

	const FVector Origin = Unit->GetActorLocation();
	TSet<uint64> UsedPolys;

	// Зона 1 AP всегда; кольцо 2 AP — только если есть второе очко.
	BuildZoneSection(0, Origin, Unit->MoveRange, UsedPolys, ZoneOneMaterial);
	if (ActionPoints >= 2)
	{
		BuildZoneSection(1, Origin, Unit->MoveRange * 2.f, UsedPolys, ZoneTwoMaterial);
	}

	SetActorHiddenInGame(false);
}

void AMoveRangeVisualizer::Hide()
{
	ZoneMesh->ClearAllMeshSections();
	SetActorHiddenInGame(true);
}

void AMoveRangeVisualizer::BuildZoneSection(int32 SectionIndex, const FVector& Origin,
	float MaxPathDistance, TSet<uint64>& InOutUsedPolys, UMaterialInterface* Material)
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ARecastNavMesh* NavMesh = NavSys ? Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance()) : nullptr;
	if (!NavMesh)
	{
		return;
	}

	// Полигон навмеша под юнитом — старт обхода.
	const FSharedConstNavQueryFilter Filter = NavMesh->GetDefaultQueryFilter();
	const NavNodeRef CenterRef = NavMesh->FindNearestPoly(Origin, FVector(200.f, 200.f, 400.f), Filter, this);
	if (CenterRef == INVALID_NAVNODEREF)
	{
		return;
	}

	// Полигоны, достижимые по СТОИМОСТИ ПУТИ (не по прямой): findPolysAroundCircle
	// делает Dijkstra от старта; радиус круга = бюджет (прямое расстояние всегда
	// не больше длины пути, поэтому круг гарантированно покрывает нужные полигоны),
	// а точный отсев — по OutPolysCost.
	TArray<NavNodeRef> Polys;
	TArray<float> Costs;
	if (!NavMesh->FindPolysAroundCircle(Origin, CenterRef, MaxPathDistance, Filter, this, &Polys, nullptr, &Costs))
	{
		return;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;

	for (int32 PolyIdx = 0; PolyIdx < Polys.Num(); ++PolyIdx)
	{
		const NavNodeRef Poly = Polys[PolyIdx];
		// Отсев по реальной длине пути и защита от дублей в кольце 2 AP.
		if ((Costs.IsValidIndex(PolyIdx) && Costs[PolyIdx] > MaxPathDistance) ||
			InOutUsedPolys.Contains(Poly))
		{
			continue;
		}
		InOutUsedPolys.Add(Poly);

		TArray<FVector> PolyVerts;
		if (!NavMesh->GetPolyVerts(Poly, PolyVerts) || PolyVerts.Num() < 3)
		{
			continue;
		}

		// Фан-триангуляция выпуклого полигона навмеша.
		const int32 BaseIndex = Vertices.Num();
		for (const FVector& V : PolyVerts)
		{
			Vertices.Add(V + FVector(0.f, 0.f, SurfaceOffset) - GetActorLocation());
		}
		for (int32 i = 1; i + 1 < PolyVerts.Num(); ++i)
		{
			Triangles.Add(BaseIndex);
			Triangles.Add(BaseIndex + i);
			Triangles.Add(BaseIndex + i + 1);
		}
	}

	if (Triangles.Num() == 0)
	{
		return;
	}

	const TArray<FVector> EmptyNormals;
	const TArray<FVector2D> EmptyUVs;
	const TArray<FColor> EmptyColors;
	const TArray<FProcMeshTangent> EmptyTangents;

	ZoneMesh->CreateMeshSection(SectionIndex, Vertices, Triangles,
		EmptyNormals, EmptyUVs, EmptyColors, EmptyTangents, /*bCreateCollision=*/false);
	if (Material)
	{
		ZoneMesh->SetMaterial(SectionIndex, Material);
	}
}
