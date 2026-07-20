#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/Navigation/NavigationTypes.h" // NavNodeRef / INVALID_NAVNODEREF в объявлении HasClearLine
#include "MoveRangeVisualizer.generated.h"

class AUnitBase;
class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * ЕДИНЫЙ ответ поля на «можно ли приказать бойцу идти сюда и за сколько».
 *
 * Один и тот же план потребляют превью пути, валидация клика и сам приказ —
 * поэтому лента, подсветка и результат клика не могут разойтись: они не просто
 * зовут одну функцию, они пользуются ОДНИМ И ТЕМ ЖЕ результатом.
 */
USTRUCT(BlueprintType)
struct FMoveOrderPlan
{
	GENERATED_BODY()

	/** Приказ выполним: цель достижима по полю и оплачивается имеющимися AP. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|MoveRange")
	bool bReachable = false;

	/** Длина маршрута по полю (см). Это же число решает стоимость в AP. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|MoveRange")
	float PathLength = 0.f;

	/** Стоимость приказа (1 или 2 AP); 0 — приказ невыполним. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|MoveRange")
	int32 ActionPointCost = 0;

	/**
	 * Цель, ПРИВЕДЁННАЯ к полю: спроецирована на навмеш и вытолкнута из дисков
	 * занятости. Приказ отдаётся именно сюда — не в сырую точку клика, иначе
	 * посчитали бы одно, а пошли в другое.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|MoveRange")
	FVector Goal = FVector::ZeroVector;

	/**
	 * Ломаная маршрута от бойца до Goal по цепочке родителей поля. Каждый её
	 * отрезок проверен NavMeshRaycast'ом, поэтому PathLength — ВЕРХНЯЯ оценка
	 * кратчайшего навмеш-пути: боец физически не пробежит больше обещанного.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tactics|MoveRange")
	TArray<FVector> PathPoints;
};

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

	/**
	 * Шаг сетки сэмплирования поля дистанций (см). Точность границы зоны ≈ шаг/2.
	 * Цена КВАДРАТИЧНА: сэмплов ≈ (4·MoveRange/шаг)², и на каждый идут проекция
	 * на навмеш и рэйкасты волны.
	 *
	 * На узкие щели влияет слабо: щель между двумя бойцами проходима начиная с
	 * ≈ 2·GetUnitClearance + шаг, где 2·просвет (≈188) доминирует над шагом.
	 * Хотите пропускать более узкие проходы — это про `UnitObstacleRadius`,
	 * а не про разрешение.
	 *
	 * Замерить стоимость: `xru1.MoveRange.LogBuildTime 1` в консоли.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactics|MoveRange", meta = (ClampMin = "20", ClampMax = "200"))
	float CellSize = 35.f;

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

	/**
	 * Построено ли сейчас поле дистанций именно для этого юнита (зона видима).
	 * Валидация приказа опирается на поле, поэтому её надо гейтить этим.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|MoveRange")
	bool IsFieldBuiltFor(const AUnitBase* Unit) const;

	/**
	 * ЕДИНСТВЕННЫЙ ответ на «можно ли туда приказать, за сколько и каким путём».
	 * Его результат потребляют превью пути, валидация клика и сам приказ.
	 *
	 * Метрика — поле дистанций: волновой обход, который огибает диски занятости
	 * юнитов и потому ЗНАЕТ про обходные пути (прямой navmesh-запрос в чистом
	 * поле прокладывает прямую сквозь стоящего бойца и отличить «одиночного
	 * бойца обойдём» от «коридор перекрыт» не может). Зона рисуется из ЭТОГО ЖЕ
	 * поля теми же порогами, поэтому «что подсвечено — то и кликается» верно по
	 * построению.
	 *
	 * false — приказ невыполним (вне поля, за стеной, не хватает AP).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|MoveRange")
	bool PlanMoveTo(const FVector& Goal, FMoveOrderPlan& OutPlan) const;

	/** Короткая форма PlanMoveTo для BP: только стоимость (0 — нельзя). */
	UFUNCTION(BlueprintPure, Category = "Tactics|MoveRange")
	int32 GetMoveCostTo(const FVector& Goal) const;

protected:
	/** Сэмпл поля дистанций: длина пути до точки сетки и высота пола. */
	struct FZoneSample
	{
		double PathDist = TNumericLimits<double>::Max(); // недостижимо
		/**
		 * Позиция сэмпла НА НАВМЕШЕ (проекция узла сетки). Хранится целиком, а не
		 * одна высота: проекция может уползти от узла на пол-ячейки, и волна
		 * меряла расстояния по проекциям, тогда как контур, маршрут и запрос
		 * брали узел сетки — до 25 см расхождения между «посчитано» и «показано».
		 * Валидна при bReachable; иначе позицию даёт узел сетки (SampleLocation).
		 */
		FVector Position = FVector::ZeroVector;
		bool bReachable = false;
		/**
		 * Клетка занята чужим юнитом. Кэш волны: не пускать сюда уже умеет
		 * HasClearLine, но флаг снимает повторные проверки на каждой попытке.
		 */
		bool bBlockedByUnit = false;
		/**
		 * Предыдущий сэмпл на кратчайшем маршруте (INDEX_NONE — не достигнут,
		 * сам себе родитель — старт). Волна any-angle, поэтому цепочка родителей
		 * — это уже готовая ломаная маршрута: ЕЙ рисуется лента превью и ПО НЕЙ
		 * же посчитана длина. Отдельного funnel-запроса «для картинки» больше нет.
		 */
		int32 Parent = INDEX_NONE;
	};

	/**
	 * Строит поле дистанций сеткой сэмплов поверх навмеша (см. .cpp — там
	 * разобрана метрика). True, если старт спроецировался на навмеш.
	 */
	bool BuildDistanceField(const AUnitBase* Unit, double BudgetOne, double BudgetMax);

	/**
	 * Дистанция сэмпла для ЛЮБОГО потребителя: недостижимый отдаётся как
	 * FieldUnreachableDistance — значение чуть за максимальным порогом. Одна
	 * условность на отрисовку и на запрос (раньше их было две, и нарисованная
	 * зона оказывалась больше кликабельной).
	 */
	double SampleDistance(int32 SampleIndex) const;

	/** Мировая позиция сэмпла по его индексу в поле. */
	FVector SampleLocation(int32 SampleIndex) const;

	/**
	 * Свободна ли прямая между точками: навмеш-рэйкаст (стены/обрывы) плюс
	 * просвет до дисков занятости. Это ОДИН предикат проходимости на волну,
	 * на достройку последнего отрезка до цели и на связность — иначе поле
	 * знало бы одни правила, а маршрут шёл по другим.
	 */
	bool HasClearLine(const class ANavigationData* NavData, const FVector& From, const FVector& To,
		NavNodeRef FromPoly = INVALID_NAVNODEREF) const;

	/**
	 * Доля отрезка From→To до ВХОДА в занятую клетку (окружность радиуса
	 * FieldClearance вокруг ближайшего диска); 1 — отрезок диски не задевает.
	 * Нужна контуру: границу зоны у бойца надо резать по самой окружности, а не
	 * интерполяцией дистанций — сквозь диск дистанция скачет и интерполяция врёт.
	 */
	double FindObstacleEntry(const FVector& From, const FVector& To) const;

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

	/** Позиция бойца, от которой построено поле (начало ломаной маршрута). */
	FVector FieldStart = FVector::ZeroVector;

	/**
	 * Диски занятости, СНЯТЫЕ НА МОМЕНТ ПОСТРОЕНИЯ поля. Запрос обязан судить по
	 * тому же снимку, что и волна: иначе юнит успел бы сдвинуться между
	 * построением зоны и кликом, и подсветка разошлась бы с ответом.
	 */
	TArray<FVector> FieldObstacles;

	/**
	 * Просвет до чужой занятой клетки для ЭТОГО бойца (`GetUnitClearance`:
	 * занятая клетка + его собственный радиус). Снимается вместе с полем —
	 * у разных классов капсулы разные, и судить надо по тому, кто ходит.
	 */
	double FieldClearance = 0.;

	/**
	 * Дистанция, приписываемая недостижимому сэмплу: чуть дальше максимального
	 * порога, чтобы он гарантированно оказался снаружи любой зоны, но граница
	 * всё ещё интерполировалась ВНУТРЬ ячейки, а не схлопывалась в точку.
	 */
	double FieldUnreachableDistance = 0.;
};
