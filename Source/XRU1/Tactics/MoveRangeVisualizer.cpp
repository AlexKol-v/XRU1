#include "MoveRangeVisualizer.h"
#include "UnitBase.h"
#include "ActionPointsComponent.h"
#include "ProceduralMeshComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
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
	CurrentUnit = Unit;
	if (!Unit || !Unit->GetActionPoints())
	{
		return;
	}

	const int32 ActionPoints = Unit->GetActionPoints()->CurrentActionPoints;
	if (ActionPoints <= 0)
	{
		return;
	}

	// Бюджеты по ДЛИНЕ ПУТИ: 1 AP = MoveRange, 2 AP = 2×MoveRange (GDD §5.3).
	const double BudgetOne = Unit->MoveRange;
	const double BudgetMax = (ActionPoints >= 2) ? BudgetOne * 2. : BudgetOne;

	if (!BuildDistanceField(Unit, BudgetOne, BudgetMax))
	{
		return;
	}

	if (ActionPoints >= 2)
	{
		// Синяя зона (после хода останется действие) + жёлтое кольцо «рывка».
		BuildContourSection(0, -1.0e9, BudgetOne, ZoneOneMaterial);
		BuildContourSection(1, BudgetOne, BudgetMax, ZoneTwoMaterial);
	}
	else
	{
		// Последний AP: любой ход завершает активацию юнита — вся зона жёлтая.
		BuildContourSection(0, -1.0e9, BudgetOne, ZoneTwoMaterial);
	}

	SetActorHiddenInGame(false);
}

void AMoveRangeVisualizer::Hide()
{
	ZoneMesh->ClearAllMeshSections();
	SetActorHiddenInGame(true);
	CurrentUnit = nullptr;
	bPathPreviewVisible = false;
	Field.Reset();
	FieldSamplesPerSide = 0;
}

// --- Поле дистанций ---------------------------------------------------------------

bool AMoveRangeVisualizer::BuildDistanceField(const AUnitBase* Unit, double BudgetOne, double BudgetMax)
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	const ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavData);
	if (!NavMesh)
	{
		return false;
	}

	const FVector Origin = Unit->GetActorLocation();

	// ВОЛНОВОЙ Dijkstra ПО СЕТКЕ СЭМПЛОВ (не по полигонам!): метрика октильная
	// (8 соседей, завышение ≤ ~8% и не зависит от размера полигонов Recast —
	// полигонный Dijkstra на огромных полигонах врал в разы). Ячейки соединены,
	// если между их точками проходит surface-raycast навмеша: стены и обрывы
	// отсекаются самим рейкастом, фантомов вне навмеша нет по построению.
	// Проекция соседа берётся от высоты текущей волны — поле корректно следует
	// пандусам и не прилипает к чужим этажам. Спорная полоса у порогов ниже
	// уточняется честным funnel-путём (метрика валидации приказа и ленты).
	const double MetricSlack = 1.12;
	const double ExpandLimit = BudgetMax * MetricSlack + CellSize;

	// Регулярная сетка сэмплов с запасом в ячейку по краям (для интерполяции границы).
	const float GridRadius = static_cast<float>(BudgetMax) + CellSize * 2.f;
	FieldSamplesPerSide = FMath::CeilToInt(2.f * GridRadius / CellSize) + 1;
	FieldOrigin = Origin - FVector(GridRadius, GridRadius, 0.f);
	const int32 NumSamples = FieldSamplesPerSide * FieldSamplesPerSide;
	Field.Reset();
	Field.SetNum(NumSamples);

	// Рабочие данные волны: позиция сэмпла на навмеше и его полигон.
	TArray<FVector> SamplePositions;
	SamplePositions.SetNumZeroed(NumSamples);
	TArray<NavNodeRef> SamplePolys;
	SamplePolys.Init(INVALID_NAVNODEREF, NumSamples);
	TArray<bool> ProjectionTried;
	ProjectionTried.Init(false, NumSamples);

	const FSharedConstNavQueryFilter Filter = NavMesh->GetDefaultQueryFilter();
	auto IndexOf = [this](int32 IX, int32 IY) { return IY * FieldSamplesPerSide + IX; };

	// Стартовая ячейка — под юнитом.
	const int32 StartIX = FMath::RoundToInt((Origin.X - FieldOrigin.X) / CellSize);
	const int32 StartIY = FMath::RoundToInt((Origin.Y - FieldOrigin.Y) / CellSize);
	FNavLocation StartLocation;
	if (!NavData->ProjectPoint(
			FVector(FieldOrigin.X + StartIX * CellSize, FieldOrigin.Y + StartIY * CellSize, Origin.Z),
			StartLocation, FVector(CellSize, CellSize, 300.f), Filter))
	{
		return false;
	}
	const int32 StartIndex = IndexOf(StartIX, StartIY);
	Field[StartIndex].PathDist = FVector::Dist2D(Origin, StartLocation.Location);
	Field[StartIndex].Z = StartLocation.Location.Z;
	Field[StartIndex].bReachable = true;
	SamplePositions[StartIndex] = StartLocation.Location;
	SamplePolys[StartIndex] = StartLocation.NodeRef;
	ProjectionTried[StartIndex] = true;

	struct FQueueEntry
	{
		double Dist;
		int32 Index;
		bool operator<(const FQueueEntry& Other) const { return Dist < Other.Dist; }
	};
	TArray<FQueueEntry> Heap;
	Heap.HeapPush({Field[StartIndex].PathDist, StartIndex});

	const int32 NeighborDX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
	const int32 NeighborDY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
	// Вертикальный допуск проекции соседа: перепад пандуса на одну ячейку.
	const FVector ProjectExtent(CellSize * 0.5f, CellSize * 0.5f, 250.f);

	while (Heap.Num() > 0)
	{
		FQueueEntry Top;
		Heap.HeapPop(Top, EAllowShrinking::No);
		if (Top.Dist > ExpandLimit)
		{
			break; // куча упорядочена: дальше только записи за пределом волны
		}
		if (Top.Dist > Field[Top.Index].PathDist + UE_KINDA_SMALL_NUMBER)
		{
			continue; // устаревшая запись кучи (ячейка уже релаксирована короче)
		}

		const int32 CX = Top.Index % FieldSamplesPerSide;
		const int32 CY = Top.Index / FieldSamplesPerSide;
		const FVector CurrentPos = SamplePositions[Top.Index];

		for (int32 Dir = 0; Dir < 8; ++Dir)
		{
			const int32 NX = CX + NeighborDX[Dir];
			const int32 NY = CY + NeighborDY[Dir];
			if (NX < 0 || NY < 0 || NX >= FieldSamplesPerSide || NY >= FieldSamplesPerSide)
			{
				continue;
			}
			const int32 NIdx = IndexOf(NX, NY);

			// Первая волна, дошедшая до ячейки, проецирует её на СВОЙ уровень.
			if (!ProjectionTried[NIdx])
			{
				ProjectionTried[NIdx] = true;
				const FVector Guess(FieldOrigin.X + NX * CellSize, FieldOrigin.Y + NY * CellSize, CurrentPos.Z);
				FNavLocation NavLoc;
				// Проекция не должна «уползать» вбок из своей ячейки (прилипание к соседней геометрии).
				if (NavData->ProjectPoint(Guess, NavLoc, ProjectExtent, Filter) &&
					FVector::DistSquared2D(NavLoc.Location, Guess) <= FMath::Square(CellSize * 0.5f))
				{
					SamplePositions[NIdx] = NavLoc.Location;
					SamplePolys[NIdx] = NavLoc.NodeRef;
				}
			}
			if (SamplePolys[NIdx] == INVALID_NAVNODEREF)
			{
				continue; // вне навмеша
			}

			const double NewDist = Top.Dist + FVector::Dist(CurrentPos, SamplePositions[NIdx]);
			if (NewDist > ExpandLimit || NewDist + UE_KINDA_SMALL_NUMBER >= Field[NIdx].PathDist)
			{
				continue;
			}

			// Связность: внутри одного (выпуклого) полигона — свободно; через
			// границу — рэйкаст по поверхности навмеша (стена/обрыв = блок).
			if (SamplePolys[NIdx] != SamplePolys[Top.Index])
			{
				FVector HitLocation;
				if (ARecastNavMesh::NavMeshRaycast(NavData, CurrentPos, SamplePositions[NIdx], HitLocation, Filter, this))
				{
					continue;
				}
			}

			Field[NIdx].PathDist = NewDist;
			Field[NIdx].Z = SamplePositions[NIdx].Z;
			Field[NIdx].bReachable = true;
			Heap.HeapPush({NewDist, NIdx});
		}
	}

	// Уточнение спорной полосы честным funnel-путём — той же метрикой, что
	// валидация приказа и лента; границы зоны и линии перестают расходиться.
	// Полоса ДВУСТОРОННЯЯ: октильная метрика завышает ≤ ~8%, поэтому сверху
	// порога сэмпл может на деле быть внутри бюджета; снизу уточняем тоже,
	// чтобы обе стороны границы были в одной метрике — иначе контур дрожит.
	const double RefineLow = 0.88;
	for (int32 IY = 0; IY < FieldSamplesPerSide; ++IY)
	{
		for (int32 IX = 0; IX < FieldSamplesPerSide; ++IX)
		{
			FZoneSample& Sample = Field[IY * FieldSamplesPerSide + IX];
			if (!Sample.bReachable)
			{
				continue;
			}
			const double D = Sample.PathDist;
			const bool bNearThreshold =
				(D > BudgetOne * RefineLow && D <= BudgetOne * MetricSlack) ||
				(D > BudgetMax * RefineLow && D <= BudgetMax * MetricSlack);
			if (!bNearThreshold)
			{
				continue;
			}

			const FVector SampleOnNav(
				FieldOrigin.X + IX * CellSize,
				FieldOrigin.Y + IY * CellSize,
				Sample.Z);
			FPathFindingQuery Query(Unit, *NavData, Origin, SampleOnNav);
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult Result = NavSys->FindPathSync(Query);
			if (Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial())
			{
				Sample.PathDist = Result.Path->GetLength();
			}
		}
	}
	return true;
}

// --- Marching squares ---------------------------------------------------------------

void AMoveRangeVisualizer::BuildContourSection(int32 SectionIndex, double MinDist, double MaxDist,
	UMaterialInterface* Material)
{
	if (FieldSamplesPerSide < 2)
	{
		return;
	}

	// Навдата для точных кромок у стен (surface-raycast до края навмеша).
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	const FSharedConstNavQueryFilter Filter = NavData ? NavData->GetDefaultQueryFilter() : nullptr;

	// «Внутренность» области MinDist < D ≤ MaxDist как знаковая функция:
	// f = min(D − MinDist, MaxDist − D) ≥ 0 внутри; ноль — искомый контур.
	// Недостижимые сэмплы прижимаются чуть за порог, чтобы граница
	// интерполировалась внутрь ячейки, а не схлопывалась в точку.
	const double UnreachableDist = MaxDist + CellSize;
	auto Inside = [&](int32 IX, int32 IY) -> double
	{
		const FZoneSample& S = Field[IY * FieldSamplesPerSide + IX];
		const double D = S.bReachable ? FMath::Min(S.PathDist, UnreachableDist) : UnreachableDist;
		return FMath::Min(D - MinDist, MaxDist - D);
	};
	auto SamplePos = [&](int32 IX, int32 IY) -> FVector
	{
		const FZoneSample& S = Field[IY * FieldSamplesPerSide + IX];
		return FVector(FieldOrigin.X + IX * CellSize, FieldOrigin.Y + IY * CellSize, S.Z);
	};

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	const FVector MeshOffset = FVector(0, 0, SurfaceOffset) - GetActorLocation();

	// Обход ячеек: полигон ячейки = внутренние углы + точки пересечения контура
	// с рёбрами (в порядке обхода) — покрывает все 16 случаев marching squares.
	for (int32 CY = 0; CY + 1 < FieldSamplesPerSide; ++CY)
	{
		for (int32 CX = 0; CX + 1 < FieldSamplesPerSide; ++CX)
		{
			// Углы против часовой: (0,0) → (1,0) → (1,1) → (0,1).
			const int32 CornerX[4] = {CX, CX + 1, CX + 1, CX};
			const int32 CornerY[4] = {CY, CY, CY + 1, CY + 1};

			double F[4];
			FVector V[4];
			bool bReach[4];
			int32 NumInside = 0;
			double MinZ = TNumericLimits<double>::Max();
			double MaxZ = -TNumericLimits<double>::Max();
			for (int32 i = 0; i < 4; ++i)
			{
				F[i] = Inside(CornerX[i], CornerY[i]);
				V[i] = SamplePos(CornerX[i], CornerY[i]);
				bReach[i] = Field[CornerY[i] * FieldSamplesPerSide + CornerX[i]].bReachable;
				NumInside += (F[i] >= 0.);
				if (bReach[i])
				{
					MinZ = FMath::Min(MinZ, static_cast<double>(V[i].Z));
					MaxZ = FMath::Max(MaxZ, static_cast<double>(V[i].Z));
				}
			}
			if (NumInside == 0)
			{
				continue;
			}
			// Ячейка накрывает обрыв (сэмплы на разных уровнях) — заливку не
			// натягиваем через вертикаль, оставляем разрыв на кромке.
			if (MaxZ - MinZ > 160.)
			{
				continue;
			}

			TArray<FVector, TInlineAllocator<8>> CellPoly;
			for (int32 i = 0; i < 4; ++i)
			{
				const int32 j = (i + 1) % 4;
				if (F[i] >= 0.)
				{
					CellPoly.Add(V[i]);
				}
				if ((F[i] >= 0.) != (F[j] >= 0.))
				{
					const int32 In = (F[i] >= 0.) ? i : j;   // внутренний угол
					const int32 Out = (In == i) ? j : i;      // внешний угол

					double T; // доля отрезка In→Out до границы
					if (!bReach[Out] && NavData)
					{
						// Снаружи — кромка навмеша (стена/обрыв): точное место даёт
						// surface-raycast (интерполяция поля здесь пилила бы по сетке).
						const FVector From = V[In];
						const FVector To(V[Out].X, V[Out].Y, V[In].Z);
						FVector HitLocation = To;
						const bool bHit = ARecastNavMesh::NavMeshRaycast(
							NavData, From, To, HitLocation, Filter, this);
						const double FullLen = FVector::Dist2D(From, To);
						T = (bHit && FullLen > 1.)
							? FMath::Clamp(FVector::Dist2D(From, HitLocation) / FullLen, 0.05, 1.0)
							: 1.0;
					}
					else
					{
						T = F[In] / (F[In] - F[Out]); // ноль f на ребре (порог бюджета)
					}

					FVector Cross = FMath::Lerp(V[In], V[Out], static_cast<float>(T));
					// Высота от достижимого угла (у недостижимого Z не сэмплирован).
					if (!bReach[Out])
					{
						Cross.Z = V[In].Z;
					}
					CellPoly.Add(Cross);
				}
			}
			if (CellPoly.Num() < 3)
			{
				continue;
			}

			const int32 Base = Vertices.Num();
			for (const FVector& P : CellPoly)
			{
				Vertices.Add(P + MeshOffset);
			}
			for (int32 i = 1; i + 1 < CellPoly.Num(); ++i)
			{
				Triangles.Add(Base);
				Triangles.Add(Base + i);
				Triangles.Add(Base + i + 1);
			}
		}
	}

	if (Triangles.Num() == 0)
	{
		return;
	}

	ZoneMesh->CreateMeshSection(SectionIndex, Vertices, Triangles,
		TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(),
		/*bCreateCollision=*/false);
	if (Material)
	{
		ZoneMesh->SetMaterial(SectionIndex, Material);
	}
}

// --- Превью пути ---------------------------------------------------------------

void AMoveRangeVisualizer::UpdatePathPreview(const FVector& GoalLocation)
{
	AUnitBase* Unit = CurrentUnit.Get();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	if (!Unit || !Unit->GetActionPoints() || !NavData)
	{
		HidePathPreview();
		return;
	}

	const int32 ActionPoints = Unit->GetActionPoints()->CurrentActionPoints;
	if (ActionPoints <= 0)
	{
		HidePathPreview();
		return;
	}

	// Путь как в валидации приказа (funnel); частичный путь = цель недостижима.
	FPathFindingQuery Query(Unit, *NavData, Unit->GetActorLocation(), GoalLocation);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult Result = NavSys->FindPathSync(Query);
	if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
	{
		HidePathPreview();
		return;
	}

	// Стоимость приказа: 1 AP в пределах MoveRange, 2 AP до 2×MoveRange.
	const double Length = Result.Path->GetLength();
	int32 Cost = 0;
	if (Length <= Unit->MoveRange)
	{
		Cost = 1;
	}
	else if (Length <= Unit->MoveRange * 2. && ActionPoints >= 2)
	{
		Cost = 2;
	}
	if (Cost == 0 || Cost > ActionPoints)
	{
		HidePathPreview(); // вне оплачиваемой зоны — приказ всё равно игнорируется
		return;
	}

	// Цвет: тратит ПОСЛЕДНИЙ AP («рывок») — жёлтый, иначе синий.
	UMaterialInterface* Material = (Cost == ActionPoints)
		? (PathDashMaterial ? PathDashMaterial.Get() : ZoneTwoMaterial.Get())
		: (PathOneMaterial ? PathOneMaterial.Get() : ZoneOneMaterial.Get());

	TArray<FVector> Points;
	Points.Reserve(Result.Path->GetPathPoints().Num());
	for (const FNavPathPoint& PathPoint : Result.Path->GetPathPoints())
	{
		Points.Add(PathPoint.Location);
	}
	BuildPathRibbon(Points, Material);
}

void AMoveRangeVisualizer::HidePathPreview()
{
	if (!bPathPreviewVisible)
	{
		return;
	}
	ZoneMesh->ClearMeshSection(2);
	ZoneMesh->ClearMeshSection(3);
	bPathPreviewVisible = false;
}

void AMoveRangeVisualizer::BuildPathRibbon(const TArray<FVector>& PathPoints, UMaterialInterface* Material)
{
	ZoneMesh->ClearMeshSection(2);
	ZoneMesh->ClearMeshSection(3);
	bPathPreviewVisible = false;
	if (PathPoints.Num() < 2)
	{
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	const FSharedConstNavQueryFilter Filter = NavData ? NavData->GetDefaultQueryFilter() : nullptr;
	auto ProjectToNav = [&](const FVector& Point) -> FVector
	{
		FNavLocation NavLoc;
		if (NavData && NavData->ProjectPoint(Point, NavLoc, FVector(CellSize, CellSize, 300.f), Filter))
		{
			return NavLoc.Location;
		}
		return Point;
	};

	// Точки нав-пути стоят редко (изломы у углов полигонов) — прямые отрезки
	// между ними режут рельеф на пандусах. Подразбиваем сегменты и проецируем
	// промежуточные точки на навмеш: лента ложится по поверхности.
	TArray<FVector> Dense;
	Dense.Add(PathPoints[0]);
	const float SubdivideStep = FMath::Max(25.f, CellSize * 0.75f);
	for (int32 i = 0; i + 1 < PathPoints.Num(); ++i)
	{
		const FVector SegStart = PathPoints[i];
		const FVector SegEnd = PathPoints[i + 1];
		const int32 NumSteps = FMath::Max(1, FMath::CeilToInt(FVector::Dist(SegStart, SegEnd) / SubdivideStep));
		for (int32 Step = 1; Step <= NumSteps; ++Step)
		{
			FVector Point = FMath::Lerp(SegStart, SegEnd, static_cast<float>(Step) / NumSteps);
			if (Step < NumSteps)
			{
				Point = ProjectToNav(Point); // изломы пути уже на навмеше
			}
			Dense.Add(Point);
		}
	}

	// Лента чуть выше заливки зоны, чтобы читалась поверх неё.
	const FVector MeshOffset = FVector(0, 0, SurfaceOffset + 4.f) - GetActorLocation();
	const float HalfWidth = PathWidth * 0.5f;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	Vertices.Reserve(Dense.Num() * 2);

	for (int32 i = 0; i < Dense.Num(); ++i)
	{
		// Направление в точке: усреднение соседних сегментов (митра на изломах).
		const FVector Prev = Dense[FMath::Max(i - 1, 0)];
		const FVector Next = Dense[FMath::Min(i + 1, Dense.Num() - 1)];
		FVector Dir = (Next - Prev);
		Dir.Z = 0.f;
		if (!Dir.Normalize())
		{
			Dir = FVector::ForwardVector;
		}
		const FVector Side(-Dir.Y * HalfWidth, Dir.X * HalfWidth, 0.f);

		Vertices.Add(Dense[i] - Side + MeshOffset);
		Vertices.Add(Dense[i] + Side + MeshOffset);
	}
	for (int32 i = 0; i + 1 < Dense.Num(); ++i)
	{
		const int32 B = i * 2;
		Triangles.Append({B, B + 1, B + 2, B + 1, B + 3, B + 2});
	}

	ZoneMesh->CreateMeshSection(2, Vertices, Triangles,
		TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);

	// Кружок-маркер точки назначения; обод проецируется на навмеш (склоны).
	TArray<FVector> MarkerVerts;
	TArray<int32> MarkerTris;
	const FVector Center = Dense.Last();
	const float MarkerRadius = PathWidth * 1.8f;
	const int32 NumSegments = 24;
	MarkerVerts.Add(Center + MeshOffset);
	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const float Angle = 2.f * PI * i / NumSegments;
		const FVector RimPoint = Center + FVector(FMath::Cos(Angle) * MarkerRadius, FMath::Sin(Angle) * MarkerRadius, 0.f);
		MarkerVerts.Add(ProjectToNav(RimPoint) + MeshOffset);
	}
	for (int32 i = 1; i <= NumSegments; ++i)
	{
		MarkerTris.Append({0, i, i + 1});
	}
	ZoneMesh->CreateMeshSection(3, MarkerVerts, MarkerTris,
		TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);

	if (Material)
	{
		ZoneMesh->SetMaterial(2, Material);
		ZoneMesh->SetMaterial(3, Material);
	}
	SetActorHiddenInGame(false);
	bPathPreviewVisible = true;
}
