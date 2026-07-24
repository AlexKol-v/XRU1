#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CoverTypes.h"
#include "TacticsCombatStatics.generated.h"

class UGameplayEffect;
class AUnitBase;
class UCoverDetectionComponent;
class UCoverTuningDataAsset;

/**
 * Общие боевые расчёты выстрела: шанс попадания с учётом укрытия цели
 * (относительно стрелка, как в XCOM), глухой обороны и применение урона через
 * GAS. Используются реакцией Overwatch, вражеским AI и способностями атаки.
 */
UCLASS()
class XRU1_API UTacticsCombatStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Жив ли юнит (Health > 0). Тяжело раненые и мёртвые — «не живы». */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsUnitAlive(const AActor* Unit);

	/** Эвакуирован ли юнит (покинул поле живым). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsUnitEvacuated(const AActor* Unit);

	/** Тяжело ранен (Downed): лежит, ждёт медика. */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsUnitDowned(const AActor* Unit);

	/** Враждебны ли акторы друг другу (по IGenericTeamAgentInterface / TeamManager). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool AreHostile(const AActor* A, const AActor* B);

	/**
	 * Итоговый шанс попадания (0..100): BaseHitChance минус защита укрытия цели
	 * ПРОТИВ этого стрелка (при глухой обороне цели — удвоенная). Зажат [5..95].
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static float ComputeHitChance(const AActor* Shooter, const AActor* Target, float BaseHitChance);

	// --- Тюнинг укрытий/LOS/высоты (Data Asset, Ф3) ----------------------------
	//
	// Все числовые параметры укрытий, линии видимости, выглядывания и высоты
	// теперь живут в UCoverTuningDataAsset. Раньше они были размазаны: static
	// constexpr здесь, UPROPERTY на UCoverDetectionComponent, литерал множителя
	// hunker в ComputeHitChance. Единый доступ — через GetCoverTuning.

	/**
	 * Ассет тюнинга укрытий: GameInstance->CoverTuning, иначе CDO (дефолты равны
	 * прежним числам — без назначенного ассета поведение не меняется). НИКОГДА не
	 * возвращает nullptr. Функции статические, мира может не быть — тогда CDO.
	 */
	static const UCoverTuningDataAsset* GetCoverTuning(const UWorld* World);

	/**
	 * Модификатор точности от дистанции до цели (± к aim). Берётся из
	 * `AimByDistanceCurve` юнита (дизайнерский профиль оружия: дробовик/снайперка),
	 * а без кривой — встроенный профиль «винтовки»: +10 в упор, 0 на средней,
	 * до −15 на дальней. Через него дистанция ВЛИЯЕТ на выстрел — раньше 30 см
	 * и 3000 см давали одинаковые 75%.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static float GetAimDistanceModifier(const AUnitBase* Shooter, float Distance);

	/**
	 * Разыгрывает выстрел: бросок на попадание против укрытия цели; при попадании
	 * применяет DamageEffectClass (SetByCaller Data.Damage = -Damage ± 10%) к ASC
	 * цели, оповещает врагов поблизости шумом (NotifyCombatNoise) и просит
	 * TurnManager проверить конец боя. BaseHitChance 100/0 — скриптовые выстрелы
	 * туториала. Возвращает true при попадании.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat")
	static bool ResolveShot(AActor* Shooter, AActor* Target, float BaseHitChance, float Damage,
		TSubclassOf<UGameplayEffect> DamageEffectClass);

	/**
	 * Дальность визуального контакта отряда (см): на неё смотрят Squadsight
	 * снайпера и «видит ли отряд действующего врага» для камеры. Один порог на
	 * оба случая — иначе камера и правила стрельбы расходятся.
	 */
	static constexpr float SquadVisionRange = 2500.f;

	// --- Линия видимости (XCOM-правила) ---------------------------------------
	//
	// Не одиночный луч, а «выглядывание»: стрелок пробует ТРИ позиции (центр и
	// шаг вбок в обе стороны — step-out XCOM из-за угла укрытия) по ДВУМ точкам
	// цели (глаза/корпус), лучи — СФЕРЫ радиуса LosSphereRadius. Сфера решает
	// баг «выстрел через щель на стыке мешей»: волосяная щель пропускала
	// линейный трейс, но толщину ствола она не пропустит. Проверяется только
	// геометрия мира (WorldStatic/WorldDynamic) — юниты выстрелам не мешают,
	// как в XCOM (сквозь своих стрелять можно).

	// EyeHeightOffset / LosPeekOffset / LosSphereRadius переехали в
	// UCoverTuningDataAsset (Ф3), читаются через GetCoverTuning.

	/** Есть ли линия огня между юнитами (XCOM-правила выше). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool HasLineOfSight(const AActor* Viewer, const AActor* Target);

	/**
	 * Включён ли CVar `xru1.LOS.Debug` (см. .cpp). Даёт вызывающему коду дёшево
	 * проверить флаг БЕЗ прямого доступа к приватной static-переменной другого
	 * файла — используется, например, PlayerTick контроллера, чтобы каждый кадр
	 * перерисовывать огневые позиции выбранного юнита (иначе дебаг виден только
	 * в момент реального запроса LOS — при смене выбора/наведении).
	 */
	static bool IsLOSDebugEnabled();

	/**
	 * Линия огня из ПРОИЗВОЛЬНОЙ точки глаз (для AI: «увижу ли цель, если встану
	 * туда»). Та же математика, что HasLineOfSight, — иначе AI планировал бы по
	 * одним правилам, а стрелял по другим.
	 *
	 * Два пути. БЫСТРЫЙ (поведение как раньше, дословно): центр + грубый step-out
	 * ±LosPeekOffset против двух точек цели. ЗАПАСНОЙ (только если быстрый не дал
	 * результата и Shooter задан): огневые позиции стрелка × позиции цели, обе
	 * через GetFiringPositions (края укрытий, симметричное выглядывание — Ф5).
	 * Запасной путь — надмножество быстрого, поэтому включать его после неудачи
	 * безопасно, а стоит он лишь когда прямой видимости нет. Shooter по умолчанию
	 * nullptr — старые вызовы (AI-план до Ф9) остаются на быстром пути.
	 */
	static bool HasLineOfSightFromLocation(const UWorld* World, const FVector& EyeLocation,
		const AActor* Target, const AActor* Shooter = nullptr);

	/**
	 * ЕДИНЫЙ источник позиций выглядывания. Собирает точки ГЛАЗ, из которых Unit
	 * может стрелять/быть виден, и НИЧЕГО не решает про видимость (перебор пар —
	 * в HasLineOfSightFromLocation). Зовётся ДВАЖДЫ с переставленными аргументами:
	 * для стрелка (§III.1) и для цели (§III.2, Ф5) — отдельной функции для цели
	 * заводить не нужно.
	 *
	 * Список НИКОГДА не пуст: минимум центр (EyeLocation). Порядок ЗНАЧИМ —
	 * центр → быстрый step-out → края укрытия: по нему GetFiringStance различает
	 * OverCover и StepOut. Unit == nullptr → только центр (нет капсулы — нет peek).
	 */
	static void GetFiringPositions(const UWorld* World, const AActor* Unit,
		const FVector& EyeLocation, const FVector& OtherLocation,
		TArray<FVector, TInlineAllocator<4>>& OutEyePositions);

	/**
	 * Стойка выстрела и точка глаз, из которой стрелок реально стреляет (для
	 * анимации Ф10 и превью Ф11). LOS из центра + half/full между стрелком и
	 * целью → OverCover; LOS из центра без укрытия → Open; LOS только из
	 * выглядывания у края → StepOut. Нет LOS → Open, точка = центр глаз.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static EFiringStance GetFiringStance(const AActor* Shooter, const AActor* Target, FVector& OutFiringEyeLocation);

	/** Видит ли цель ХОТЬ ОДИН живой союзник юнита (для Squadsight снайпера). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool SquadHasLineOfSight(const AActor* Unit, const AActor* Target);

	/**
	 * Мгновенно разворачивает Actor лицом (yaw) к TargetLocation; крен/тангаж не
	 * трогает. Общий для взятия цели на прицел (читаемость наводки) и выстрела
	 * (не стрелять «в спину») — одна логика, поэтому повороты не расходятся.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat")
	static void FaceActorTowards(AActor* Actor, const FVector& TargetLocation);

	/**
	 * Шум боя в точке: враги источника в радиусе Radius переходят в режим
	 * разведки (Investigate, идут на звук) — XCOM-жёлтая тревога от выстрелов.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat")
	static void NotifyCombatNoise(AActor* Instigator, const FVector& Location, float Radius = 2000.f);

	/**
	 * Точка на пути по навмешу от Start до Goal, не дальше PathBudget по длине
	 * пути (обрезка хода юнита бюджетом AP). Путь дополнительно УСЕКАЕТСЯ там,
	 * где бегущий не протиснется мимо стоящего юнита (навмеш о них не знает) —
	 * не отказ, а «дойти насколько можно»: иначе AI впустую пропускал бы ход,
	 * упёршись в союзника. false — путь вообще не построился.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat", meta = (WorldContext = "WorldContextObject"))
	static bool GetPointAlongPathBudget(UObject* WorldContextObject, const AActor* Mover, const FVector& Start,
		const FVector& Goal, float PathBudget, FVector& OutPoint);

	/**
	 * ЕДИНСТВЕННЫЙ источник правила стоимости перемещения (GDD §5.3):
	 * путь ≤ MoveRange — 1 AP, ≤ 2×MoveRange — 2 AP, дальше — нельзя.
	 * Возвращает 0, если приказ неоплатен (не хватает AP или слишком далеко).
	 * Зона хода, превью пути и валидация клика обязаны звать ЭТО, иначе
	 * пороги разъезжаются и «зона показывает одно, клик делает другое».
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static int32 GetMoveCostForDistance(const AUnitBase* Unit, float PathLength, int32 AvailableActionPoints);

	// --- Занятость: юниты НЕ мутируют навмеш (XCOM-подход) --------------------
	//
	// Навмеш статичен; «нельзя встать в юнита / зона огибает юнитов» решается
	// на уровне ЗАПРОСОВ дисками занятости. Никаких асинхронных перестроек
	// тайлов — зона хода и валидация приказов считаются синхронно и точно.

	/**
	 * Радиус «занятой клетки» вокруг СТОЯЩЕГО юнита (см) — аналог занятого
	 * тайла XCOM. Сам по себе он неполон: считать по нему нельзя, потому что
	 * бегущий имеет собственную ширину. Рабочая величина — GetUnitClearance.
	 */
	static constexpr float UnitObstacleRadius = 60.f;

	/**
	 * ЕДИНЫЙ просвет ЦЕНТР-В-ЦЕНТР между Mover и чужой занятой клеткой:
	 * `UnitObstacleRadius` + радиус капсулы самого Mover (берётся с актора, а не
	 * константой — BP может переопределить капсулу).
	 *
	 * Это «раздувание препятствия на радиус агента» — стандартный приём
	 * навигации: препятствие растят на радиус того, кто едет, и дальше считают
	 * агента точкой. У нас он один и тот же и для «встать», и для «пробежать
	 * мимо» — СОЗНАТЕЛЬНО. Раньше правила были разные (встать — 60, пройти —
	 * 60+радиус), и это давало неразрешимое противоречие: клетка, куда встать
	 * можно, но откуда нельзя выйти. Вдобавок волна поля судила о проходе по
	 * меньшему радиусу и рисовала маршрут в щель между двумя бойцами, куда
	 * третий не влезает.
	 *
	 * Единственное послабление — «выйти из тесноты»: отрезок, который только
	 * УДАЛЯЕТСЯ от нарушенного просвета, разрешён (иначе боец, оказавшийся
	 * вплотную к союзнику, не смог бы сдвинуться вообще).
	 */
	static float GetUnitClearance(const AActor* Mover);

	/**
	 * Юнит сейчас в пути (его позиция переходная, диск занятости не ставится).
	 * ЕДИНЫЙ предикат: спрашивает AIController (статус path following + отрезки
	 * маршрута), а не velocity — тормозящий после финиша боец уже стоит на
	 * своей клетке и обязан её занимать.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsUnitInTransit(const AActor* Unit);

	/**
	 * Позиции дисков занятости: живые неэвакуированные юниты обеих сторон,
	 * кроме Ignored (сам ходящий) и кроме БЕГУЩИХ (их позиция переходная —
	 * диск встанет по финишу, AIController дёрнет перестройку зоны).
	 */
	static void GetUnitObstacles(UWorld* World, const AActor* Ignored, TArray<FVector>& OutPositions);

	/**
	 * Длина вдоль пути до первого места, где просвет между полилинией и диском
	 * занятости меньше Clearance (бегущий там не протиснется). < 0 — путь чист
	 * целиком. Obstacles передаются ГОТОВЫМИ: в горячих циклах (уточнение поля
	 * зоны хода) сбор дисков делается один раз снаружи, а не на каждый вызов.
	 *
	 * ВАЖНО: «путь задевает юнита» != «дойти нельзя» — навмеш строит прямую и в
	 * чистом поле пройдёт сквозь одиночного бойца, которого Detour Crowd спокойно
	 * обходит на бегу. Ответ на «достижимо ли» даёт ВОЛНА поля дистанций
	 * (она знает про обходы), а это — метрика «докуда точно дойдём по прямой».
	 */
	static double FindPathClearanceLimit(const TArray<FVector>& PathPoints,
		const TArray<FVector>& Obstacles, float Clearance);

	/**
	 * Выталкивает цель перемещения из диска занятости на его край (клик «в»
	 * стоящего юнита или впритык). false — вытолкнуть некуда (цель остаётся
	 * заблокированной), приказ стоит отклонить.
	 */
	static bool AdjustGoalOutOfUnits(UWorld* World, const AActor* Mover, FVector& InOutGoal);

	/**
	 * То же по ГОТОВОМУ снимку дисков. Нужна там, где снимок уже сделан и судить
	 * надо именно по нему: поле дистанций строится на одном наборе дисков, и
	 * запрос приказа обязан пользоваться тем же — иначе юнит, сдвинувшийся между
	 * построением зоны и кликом, развёл бы подсветку с ответом.
	 */
	static bool AdjustGoalOutOfUnits(const TArray<FVector>& Obstacles, const AActor* Mover, FVector& InOutGoal);
};
