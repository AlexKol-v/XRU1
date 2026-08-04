#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CollisionQueryParams.h" // FCollisionObjectQueryParams — единая геометрия выстрела
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
	 * Только необратимая механика выстрела: использует уже зафиксированный итоговый
	 * шанс и применяет roll/урон/шум. Не поворачивает actor, не управляет камерой
	 * и НЕ завершает бой: CheckCombatOutcome вызывает action coordinator только
	 * после terminal presentation, чтобы lethal hit не оборвал montage/возврат.
	 */
	static bool ResolveShotMechanics(AActor* Shooter, AActor* Target, float ResolvedHitChance,
		float Damage, TSubclassOf<UGameplayEffect> DamageEffectClass, const FVector& ShotOrigin);

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

	/**
	 * ЕДИНОЕ определение «геометрии, которая останавливает пулю и образует
	 * укрытие»: object-типы `WorldStatic` + `WorldDynamic` (двигаемые пропсы).
	 *
	 * ⚠️ Почему object-query, а НЕ трейс по каналу. Капсула и меш `ACharacter`
	 * идут в профилях `Pawn`/`CharacterMesh`, у которых отклик на канал
	 * `WorldStatic` — **Block**. То есть `LineTraceByChannel(..., ECC_WorldStatic)`
	 * упирается в ЮНИТОВ. Пока укрытие считалось таким трейсом, а LOS —
	 * object-query, у системы было ДВА разных представления о геометрии, и
	 * укрытием становились: сам стрелок в упор (луч от цели к стрелку бил в его
	 * же капсулу → всегда Full), союзник рядом, труп. Отсюда «синий щит
	 * вплотную» вместо жёлтого. Теперь определение одно на все запросы.
	 *
	 * Юниты выстрелам не мешают (XCOM: сквозь своих стрелять можно) — и по той
	 * же причине не дают укрытия.
	 */
	static const FCollisionObjectQueryParams& GetShotGeometryObjects();

	/**
	 * ВСЕ огневые позиции стрелка, из которых ЕСТЬ линия огня по цели.
	 * Список может быть пуст (стрелять неоткуда).
	 *
	 * ⚠️ Зачем отдельно от `GetFiringStance`. Та возвращает ПЕРВУЮ подходящую
	 * позицию (порядок: центр → step-out → края) — это верно для выбора
	 * АНИМАЦИИ, но неверно для расчёта фланга. Стрелок выбирает, откуда стрелять,
	 * и выберет позицию, которая обходит укрытие цели. Пока фланг считался от
	 * «первой» позиции, ситуация «стою у угла, точка выглядывания заведомо во
	 * фланге, а щит синий» была неизбежна: центр давал линию огня поверх низкой
	 * стены, и на нём перебор останавливался.
	 */
	static void GetViableFiringPositions(const AActor* Shooter, const AActor* Target,
		TArray<FVector, TInlineAllocator<4>>& OutPositions);

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
	 * Повторная проверка LOS из ЗАФИКСИРОВАННОЙ точки выстрела. Источник не
	 * пересчитывается из текущего transform стрелка; цель может показать только
	 * реально видимую центральную/краевую точку своего текущего укрытия.
	 */
	static bool HasLineOfSightFromFrozenOrigin(const UWorld* World, const FVector& FiringEyeLocation,
		const AActor* Target);

	/**
	 * Точки, которыми цель может быть ВИДНА/ПОРАЖЕНА из FromEye: центр глаз,
	 * пик-позиции у краёв её укрытия и корпус. XCOM-правило: боец в укрытии
	 * «занимает» и соседний peek-тайл — симметрично для стрельбы И для попадания
	 * под выстрел.
	 *
	 * ЕДИНСТВЕННЫЙ источник набора точек цели. Решение «видно ли», выбор стойки,
	 * огневые позиции и commit-валидация обязаны целиться в ОДИН набор: их
	 * рассинхрон (стойка целилась только в глаза/корпус) давал вечный цикл
	 * «AI видит цель по пику, а замороженная позиция её не видит» (лог 2026-07-30).
	 */
	static void GetTargetExposedPoints(const UWorld* World, const AActor* Target,
		const FVector& FromEye, TArray<FVector, TInlineAllocator<4>>& OutPoints);

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
	 * анимации Ф10 и превью Ф11). Тонкая обёртка над `FindFiringSolution` для
	 * мест, где «решения нет» и «решение из центра» обрабатываются одинаково
	 * (камера, расчёт укрытия): решения нет → Open, точка = центр глаз.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static EFiringStance GetFiringStance(const AActor* Shooter, const AActor* Target, FVector& OutFiringEyeLocation);

	/**
	 * ЕДИНЫЙ ИСТОЧНИК ПРАВДЫ «может ли этот боец выстрелить в эту цель».
	 *
	 * Ищет ПЕРВУЮ огневую позицию (порядок GetFiringPositions: центр → step-up →
	 * края укрытия), из которой есть линия огня хотя бы в одну exposed-точку цели,
	 * и возвращает её вместе со стойкой. `false` — стрелять неоткуда.
	 *
	 * ⚠️ Зачем отдельно от `HasLineOfSight`. Та отвечает на вопрос ВИДИМОСТИ и
	 * перебирает более широкий набор (в т.ч. корпусную точку стрелка, которой нет
	 * среди огневых позиций). Пока «можно стрелять» решала она, а activation
	 * проверял замороженную точку выстрела (`HasLineOfSightFromFrozenOrigin`),
	 * истин было ДВЕ: HUD показывал шанс, игрок жал выстрел и получал
	 * `[FireAction] Reject at activation: из замороженной позиции нет линии огня`
	 * (лог PIE 2026-08-04, Assault→Marauder_12 дважды подряд), а AI на том же
	 * расхождении терял ход целиком («цель заблокирована до конца хода»).
	 * Теперь предикат доступности и точка выстрела приходят из ОДНОГО перебора.
	 *
	 * Стойка (см. `IsUnitInCoverPose`): решение из центра/step-up у бойца в позе
	 * укрытия → `OverCover` (встать, довернуться, выстрелить), в открытом поле →
	 * `Open`, боковой край → `StepOut`.
	 */
	static bool FindFiringSolution(const AActor* Shooter, const AActor* Target,
		FVector& OutFiringEyeLocation, EFiringStance& OutStance);

	/**
	 * Боец ПРЯМО СЕЙЧАС сидит за полуукрытием или прижат к высокой стене — то
	 * есть на экране он в позе укрытия, а не на ногах. Читает тот же
	 * `FUnitVisualState`, что и Anim Blueprint: отдельного «анимационного»
	 * определения укрытия быть не должно.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsUnitInCoverPose(const AActor* Unit);

	/**
	 * ФЛАНГ (§III.4, Ф8): цель СТОИТ в укрытии «вообще» (BestCoverAround != None),
	 * но против ЭТОГО стрелка укрытие не работает (GetCoverAgainst == None).
	 *
	 * Ровно XCOM-семантика жёлтого щита: синий — укрытие работает против меня,
	 * жёлтый — цель за укрытием, но я зашёл сбоку, пусто — цель в чистом поле.
	 * Ничего не кэшируем (инвариант §V.2 п.4): трейсы считаются на месте, иначе
	 * сломается разрушаемость.
	 *
	 * ⚠️ Отсутствие фланга НЕ означает наличие укрытия — три состояния
	 * различаются парой (IsTargetFlankedBy, GetCoverAgainst), а не одним bool.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsTargetFlankedBy(const AActor* Target, const AActor* Shooter);

	/**
	 * То же, но выстрел идёт ИЗ ПРОИЗВОЛЬНОЙ ТОЧКИ — для планирования AI
	 * («буду ли я фланкировать его, если встану сюда»). План и факт считаются
	 * одной математикой: `IsTargetFlankedBy` — тонкая обёртка над этой функцией.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static bool IsTargetFlankedByLocation(const AActor* Target, const FVector& ShooterLocation);

	/**
	 * ЕДИНЫЙ источник состояния щита цели против стрелка (Ф8). Все UI-поверхности
	 * — панель цели, иконка над головой, будущее превью — обязаны звать ЭТО, а не
	 * собирать три состояния из пары bool'ов каждая по-своему.
	 *
	 * OutShieldCover — какой ФОРМЫ рисовать щит (половинчатый/полный):
	 *  - Covered → укрытие против стрелка (оно же даёт бонус к защите);
	 *  - Flanked → локальное укрытие цели (BestCoverAround) — против стрелка его
	 *    нет, но щит рисуем по тому укрытию, за которым цель физически стоит;
	 *  - None    → ECoverType::None.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static ECoverShield GetCoverShieldAgainst(const AActor* Target, const AActor* Shooter,
		ECoverType& OutShieldCover);

	/**
	 * ЕДИНСТВЕННАЯ реализация правила «сторона видит цель»: хотя бы один живой
	 * наблюдатель из Viewers находится в `SquadVisionRange` от цели И имеет до
	 * неё боевой LOS.
	 *
	 * ⚠️ Зачем статик, а не метод подсистемы. Это правило нужно ДВУМ разным
	 * потребителям с разными полномочиями: Squadsight снайпера (сторона стрелка,
	 * работает и для AI) и туман войны (только сторона игрока, с кэшем). Пока
	 * реализаций было две (`SquadHasLineOfSight` и
	 * `UFogOfWarSubsystem::IsActorCurrentlyVisible`), они отличались лишь
	 * исключением себя — и это ровно тот «второй источник правды», от которого
	 * расходятся правила стрельбы и картинка.
	 *
	 * Порядок проверок значим: живость → дистанция → трейсы. Дистанция отсекает
	 * до сферо-свипов (в XCOM это `bBeyondSightRadius` — «кэш неполон, цель за
	 * радиусом обзора»), иначе каждый запрос платит за заведомо далёкие пары.
	 */
	static bool AnyUnitSees(const TArray<AActor*>& Viewers, const AActor* Target,
		const AActor* Exclude = nullptr);

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
	 * Насколько корпус актора отвёрнут от точки (град, 0…180; только yaw).
	 * Метрика читаемости выстрела: 0 — смотрит точно на цель. Используется
	 * фазой доворота и логами выстрела («в кого он вообще целился»).
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Combat")
	static float GetFacingErrorDegrees(const AActor* Actor, const FVector& TargetLocation);

	/**
	 * Шум боя в точке: враги источника в радиусе Radius переходят в режим
	 * разведки (Investigate, идут на звук) — XCOM-жёлтая тревога от выстрелов.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Combat")
	static void NotifyCombatNoise(AActor* Instigator, const FVector& Location, float Radius = 2000.f);

	/** Включён ли `xru1.Cover.Debug` — общий предикат для диагностики укрытий. */
	static bool IsCoverDebugEnabled();

	/**
	 * Точка на пути по навмешу от Start до Goal, не дальше PathBudget по длине
	 * пути (обрезка хода юнита бюджетом AP). false — путь вообще не построился.
	 *
	 * ⚠️ Стоящие юниты путь НЕ укорачивают (правило XCOM: сквозь своих ходить
	 * можно, нельзя только закончить ход на их клетке). Занятость проверяется на
	 * КОНЦЕ маршрута — `AdjustGoalOutOfUnits` и отбор кандидатов в
	 * `FindCoverPoint`. Прежнее усечение по клиренсу делало недостижимой любую
	 * точку за спиной союзника и выстраивало ботов в колонну.
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

	// ⚠️ `FindPathClearanceLimit` УДАЛЕНА (2026-07-25) вместе с единственным
	// вызовом — см. комментарий в .cpp. Её собственный заголовок предупреждал,
	// что «путь задевает юнита» != «дойти нельзя», а использовалась она именно
	// как ответ на «достижимо ли»; это и строило ботов в колонну.

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
