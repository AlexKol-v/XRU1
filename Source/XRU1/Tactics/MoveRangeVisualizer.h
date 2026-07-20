#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MoveRangeVisualizer.generated.h"

class AUnitBase;
class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * Визуализация зоны хода выбранного юнита (GDD §5.3) и превью пути к курсору.
 *
 * Зона: один Dijkstra-обход навмеша (GetPolysWithinPathingDistance с DebugData)
 * даёт стоимость пути до каждого полигона; поверх строится регулярная сетка
 * сэмплов, и по полю дистанций marching squares вырезает ГЛАДКИЙ контур:
 *  - секция 0 — зона 1 AP (синяя; при последнем AP — жёлтая, как «рывок» XCOM),
 *  - секция 1 — кольцо 2 AP (жёлтая), примыкает к синей без пересечений.
 *
 * Путь: секции 2 (лента по земле) и 3 (кружок-маркер цели); цвет по стоимости
 * приказа (синий = остаётся действие, жёлтый = тратит последний AP).
 *
 * Спавнится тактическим контроллером; зона обновляется при выборе юнита и
 * трате AP, путь — при движении курсора (PlayerTick контроллера).
 */
UCLASS(Blueprintable)
class XRU1_API AMoveRangeVisualizer : public AActor
{
	GENERATED_BODY()

public:
	AMoveRangeVisualizer();

	// --- Материалы (задаются в BP) -------------------------------------------

	/** Материал зоны 1 AP (полупрозрачный синий). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UMaterialInterface> ZoneOneMaterial;

	/** Материал зоны 2 AP / «рывка» (полупрозрачный жёлтый). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UMaterialInterface> ZoneTwoMaterial;

	/** Материал ленты пути за 1 AP (если пусто — ZoneOneMaterial). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UMaterialInterface> PathOneMaterial;

	/** Материал ленты пути, тратящего последний AP (если пусто — ZoneTwoMaterial). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UMaterialInterface> PathDashMaterial;

	/** Материал каймы зоны 1 AP (если пусто — PathOneMaterial → ZoneOneMaterial). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UMaterialInterface> BorderOneMaterial;

	/** Материал каймы внешнего края 2 AP (если пусто — PathDashMaterial → ZoneTwoMaterial). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UMaterialInterface> BorderTwoMaterial;

	// --- Дизайнерские параметры ----------------------------------------------

	/** Шаг сетки сэмплирования поля дистанций (см). Меньше = глаже контур, дороже перестройка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange", meta = (ClampMin = "20", ClampMax = "200"))
	float CellSize = 50.f;

	/** Подъём заливки над полом против z-fighting (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange")
	float SurfaceOffset = 8.f;

	/** Ширина ленты пути (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange", meta = (ClampMin = "4"))
	float PathWidth = 14.f;

	/** Ширина каймы по краю зоны (см). 0 — кайму не рисовать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange", meta = (ClampMin = "0", ClampMax = "40"))
	float BorderWidth = 10.f;

	/** Итерации сглаживания каймы (Chaikin corner-cutting). 0 — без сглаживания. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange", meta = (ClampMin = "0", ClampMax = "4"))
	int32 BorderSmoothIterations = 2;

	// --- API -------------------------------------------------------------------

	/**
	 * Перестраивает зону под юнита: 1 AP = MoveRange, 2 AP = 2×MoveRange по пути.
	 * Другие юниты — диски занятости на уровне запроса (навмеш статичен), зона
	 * строится синхронно. false — юнит стоит вне навмеша (нештатная ситуация
	 * уровня); «нечего показывать по правилам» (нет AP) — это true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|MoveRange")
	bool ShowForUnit(AUnitBase* Unit);

	/** Прячет зону и путь (юнит снят с выбора / чужая фаза). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|MoveRange")
	void Hide();

	/** Лента пути от текущего юнита к точке (зовётся контроллером на движение курсора). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|MoveRange")
	void UpdatePathPreview(const FVector& GoalLocation);

	/** Прячет только превью пути (курсор ушёл с земли / чужая фаза). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|MoveRange")
	void HidePathPreview();

protected:
	/** Сэмпл поля дистанций: длина пути до точки сетки и высота пола. */
	struct FZoneSample
	{
		double PathDist = TNumericLimits<double>::Max(); // недостижимо
		double Z = 0.;
		bool bReachable = false;
		/** Недостижим из-за диска занятости юнита (не кромки навмеша): контур
		 *  на границе интерполируется, а не ищется raycast'ом до стены. */
		bool bBlockedByUnit = false;
	};

	/**
	 * Строит поле дистанций сеткой поверх Dijkstra-обхода навмеша; сэмплы в
	 * спорной полосе у порогов уточняются честным funnel-путём (той же метрикой,
	 * что валидация приказа и лента). True, если есть достижимые сэмплы.
	 */
	bool BuildDistanceField(const AUnitBase* Unit, double BudgetOne, double BudgetMax);

	/**
	 * Marching squares: секция-заливка области MinDist < дистанция ≤ MaxDist.
	 * Для сплошной зоны MinDist < 0; для кольца 2 AP MinDist = бюджет 1 AP.
	 * Попутно собирает хорды ВНЕШНЕЙ границы области (внутренний порог кольца
	 * пропускается — эту линию уже нарисовала кайма синей зоны) и строит по ним
	 * сглаженную кайму в секции BorderSectionIndex.
	 */
	void BuildContourSection(int32 SectionIndex, double MinDist, double MaxDist, UMaterialInterface* Material,
		int32 BorderSectionIndex, UMaterialInterface* BorderMaterial);

	/**
	 * Кайма зоны: сшивает хорды границы в полилинии, сглаживает (Chaikin) и
	 * строит ленту шириной BorderWidth в указанной секции меша.
	 */
	void BuildZoneBorder(int32 SectionIndex, const TArray<TPair<FVector, FVector>>& Segments,
		UMaterialInterface* Material);

	/** Лента по точкам пути + кружок-маркер в конце (секции 2 и 3). */
	void BuildPathRibbon(const TArray<FVector>& PathPoints, UMaterialInterface* Material);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange")
	TObjectPtr<UProceduralMeshComponent> ZoneMesh;

	/** Юнит, для которого построена зона (для превью пути). */
	TWeakObjectPtr<AUnitBase> CurrentUnit;

	/** Показана ли сейчас лента пути (чтобы не чистить секции каждый тик). */
	bool bPathPreviewVisible = false;

	// --- Поле дистанций (валидно после BuildDistanceField) --------------------
	TArray<FZoneSample> Field;
	int32 FieldSamplesPerSide = 0;
	FVector FieldOrigin = FVector::ZeroVector; // мировой угол сетки (мин. X/Y)
};
