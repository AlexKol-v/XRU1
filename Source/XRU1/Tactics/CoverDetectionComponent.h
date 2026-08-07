#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "CoverTypes.h"
#include "CoverDetectionComponent.generated.h"

class UGameplayEffect;
class UCoverTuningDataAsset;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCoverStateChanged, ECoverType, NewBestCover);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveCoverChanged, int32, NewRevision);

/**
 * Детекция укрытий вокруг юнита. Трейсит окружение по кардинальным направлениям
 * и определяет наличие стены (half/full cover) рядом. Отдельно умеет посчитать
 * тип укрытия ОТНОСИТЕЛЬНО конкретного врага (укрытие работает, только если стена
 * стоит между юнитом и источником огня).
 *
 * Статус укрытия отражается на ASC юнита бессрочным GameplayEffect'ом с тегом
 * Cover.Half / Cover.Full (для UI и способностей), а численный бонус к защите
 * вычитается из шанса попадания стрелка в момент выстрела — см.
 * UTacticsCombatStatics::ComputeHitChance (укрытие считается против конкретного
 * стрелка, как в XCOM: фланкирование его обнуляет).
 */
UCLASS(ClassGroup = (Tactics), meta = (BlueprintSpawnableComponent))
class XRU1_API UCoverDetectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCoverDetectionComponent();

	/**
	 * Пер-юнитное переопределение тюнинга укрытий/LOS. Пусто → глобальный
	 * CoverTuning с GameInstance, иначе CDO. Геометрия детекта (дистанция,
	 * высоты Half/Full, канал) и числа защиты (20/40) живут в ассете, не здесь —
	 * единый источник правды, тюнится дизайнером.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	TObjectPtr<UCoverTuningDataAsset> TuningOverride;

	/** GE, навешиваемый при половинчатом укрытии (выдаёт тег Cover.Half). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	TSubclassOf<UGameplayEffect> HalfCoverEffect;

	/** GE, навешиваемый при полном укрытии (выдаёт тег Cover.Full). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	TSubclassOf<UGameplayEffect> FullCoverEffect;

	/**
	 * Сколько юнит может сместиться от активной cover-anchor без перевыбора
	 * стены (см). Это гистерезис от микросдвигов/повторных evaluate на углу;
	 * обычный tactical move или подшаг к стене превышает порог и создаёт новую
	 * anchor. 0 — перевыбор только по RequestActiveCoverReselection/потере стены.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactics|Cover|Stability",
		meta = (ClampMin = "0", Units = "cm"))
	float ActiveCoverReselectDistance = 5.f;

	/** Лучшее укрытие из всех направлений (кэш последнего EvaluateSurroundings). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	ECoverType BestCoverAround = ECoverType::None;

	/**
	 * СТОРОНЫ укрытия юнита на его текущей позиции (кэш последнего
	 * EvaluateSurroundings). Обычно 1–2: лучи склеиваются по нормали стены,
	 * поэтому «угол ящика» даёт две стороны, а плоская стена — одну.
	 *
	 * ⚠️ Это кэш ЛОКАЛЬНОГО состояния (где я стою), а не «укрытие против врага».
	 * Инвариант «укрытие против конкретного стрелка не кэшировать» не нарушен:
	 * GetCoverAgainst пересобирает стороны трейсами на каждый вызов.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	TArray<FCoverSide> CoverSides;

	/**
	 * Направление на ЛУЧШУЮ стену (от юнита к стене, XY). Zero — укрытия нет.
	 * Совместимый старый API: для активной стены равно `-ActiveCoverNormal`.
	 * Нужно анимации (прижаться к стене нужной стороной) и превью peek.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	FVector BestCoverDirection = FVector::ZeroVector;

	/**
	 * Стабильная мировая позиция root/capsule, в которой выбрана активная стена.
	 * Это home anchor для будущего action context, а не точка глаза/поверхности.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	FVector ActiveCoverAnchor = FVector::ZeroVector;

	/** Нормаль активной стены ОТ стены к юниту (XY, нормализована). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	FVector ActiveCoverNormal = FVector::ZeroVector;

	/**
	 * Runtime-id активной поверхности. Стабилен, пока живёт компонент стены;
	 * это ключ action context, но не идентификатор для SaveGame.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	int64 ActiveCoverWallId = 0;

	/**
	 * Ревизия anchor/normal/WallId. Меняется и при Full→Full, когда тип укрытия
	 * прежний, но старая стена потеряна и выбрана другая.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	int32 ActiveCoverRevision = 0;

	/**
	 * КРАЙ УКРЫТИЯ (кэш последнего EvaluateSurroundings): мировое направление
	 * вдоль стены к ближайшему краю, Zero — края нет (глухая стена / нет укрытия).
	 *
	 * Единственный источник правды о крае: считается ОДИН раз за
	 * evaluate, атомарно с активной геометрией. Дёргать FindPeekEdgeSide
	 * трейсами на каждый NotifyUnitStateChanged (до 8 лучей на вызов) нельзя:
	 * вызов попадает между обновлениями active-геометрии — отсюда ложные края.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	FVector PeekEdgeDirection = FVector::ZeroVector;

	/** Расстояние до края (см); валидно при ненулевом PeekEdgeDirection. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	float PeekEdgeDistance = 0.f;

	/**
	 * СТОРОНА СТЕНЫ для выбора Left/Right-клипа: −1 — стена слева, +1 — справа,
	 * 0 — края нет. Считается В ПРОЕКТНОЙ СТОЙКЕ (боец стоит вдоль стены лицом к
	 * краю), то есть из чистой геометрии стены и края — и НЕ зависит от
	 * фактического поворота актора. Вывод из `CoverDirectionLocal.Y`
	 * ломается, как только юнит после доворотов оказывается не вдоль стены:
	 * порог не проходит и сторона застревает в 0 при живом крае.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactics|Cover")
	float PeekSideSign = 0.f;

	/** Есть ли край для выглядывания (по кэшу последнего EvaluateSurroundings). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Cover")
	bool HasPeekEdge() const { return !PeekEdgeDirection.IsNearlyZero(); }

	/** Плоскость стороны укрытия: нормаль (от стены к юниту, XY) + удаление. */
	struct FCoverSidePlane
	{
		FVector Normal = FVector::ZeroVector;
		float PlaneDistance = 0.f;
	};

	/**
	 * Плоскости СВОИХ стен (кэш последнего EvaluateSurroundings) — ЕДИНЫЙ
	 * ИСТОЧНИК ПРАВДЫ об укрытии юнита. Из этого же evaluate растут поза,
	 * иконка и GE-тег; выстрел (`GetCoverAgainst`) даёт бонус укрытия только
	 * если блокер лежит на одной из этих плоскостей. Иначе случайная геометрия
	 * (склон рампы, перепад пола) давала защиту юниту, который стоит в полный
	 * рост без иконки, — рассинхрон «выстрел с укрытием, а позы нет».
	 */
	TArray<FCoverSidePlane> CoverSidePlanes;

	UPROPERTY(BlueprintAssignable, Category = "Tactics|Cover")
	FOnCoverStateChanged OnCoverStateChanged;

	/** Отдельный сигнал смены геометрии укрытия; тип может остаться прежним. */
	UPROPERTY(BlueprintAssignable, Category = "Tactics|Cover")
	FOnActiveCoverChanged OnActiveCoverChanged;

	/**
	 * Пересчитывает укрытие по 4 кардинальным направлениям вокруг юнита, обновляет
	 * BestCoverAround и синхронизирует GE/тег укрытия на ASC владельца.
	 * Вызывать после каждого перемещения юнита.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cover")
	ECoverType EvaluateSurroundings();

	/**
	 * Снимает latch выбора стены перед tactical move. Публичные значения не
	 * обнуляются посреди движения: новая anchor публикуется атомарно при следующем
	 * EvaluateSurroundings, когда уже известна валидная конечная позиция.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cover")
	void RequestActiveCoverReselection();

	/**
	 * Совпадает ли blocking hit именно с зафиксированной активной стеной.
	 * C++-контракт для target-aware fire/peek; соседняя стена того же типа не
	 * считается продолжением текущей поверхности.
	 */
	bool MatchesActiveCoverHit(const FHitResult& Hit) const;

	/** Тип укрытия, эффективный против конкретной угрозы (стена должна быть между юнитом и врагом). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cover")
	ECoverType GetCoverAgainst(const AActor* Threat) const;

	/**
	 * Укрытие В ПРОИЗВОЛЬНОЙ ТОЧКЕ против угрозы — той же математикой, что у
	 * стоящего юнита. Base = точка ПОЛА (куда проецируется навмеш): высоты
	 * укрытия отсчитываются от пола, поэтому передавать сюда надо
	 * именно floor-точку, без прибавки половины капсулы. Нужна AI: «какое
	 * укрытие я получу, если встану сюда» — план и факт обязаны считаться
	 * одинаково, иначе враг бежит в «укрытие», которого по прибытии не окажется.
	 * Настройки (высоты/дистанция/канал) — с ЭТОГО компонента: у кого
	 * спрашиваем, тем и мерим.
	 */
	ECoverType EvaluateCoverAtLocation(const FVector& Base, const FVector& ThreatLocation) const;

	/**
	 * Общее ядро трейса укрытия (см. EvaluateCoverAtLocation).
	 * SphereRadius > 0 — толстый свип вместо волосяного луча: на скользящих
	 * углах тонкий луч проскакивал мимо стены, и укрытие «пропадало».
	 *
	 * Геометрия — `UTacticsCombatStatics::GetShotGeometryObjects()` (WorldStatic
	 * + WorldDynamic). Параметра «канал» больше НЕТ намеренно: трейс по каналу
	 * упирался в капсулы юнитов, и укрытием становился сам стрелок в упор.
	 *
	 * OutHit — необязательная выдача найденной стены (для `xru1.Cover.Debug`).
	 */
	static ECoverType TraceCoverAtLocation(const UWorld* World, const FVector& Base, const FVector& Direction,
		float TraceDistance, float HalfHeight, float FullHeight,
		const AActor* Ignored, float SphereRadius = 0.f, FHitResult* OutHit = nullptr);

	/**
	 * ДЛИНА луча укрытия против угрозы, стоящей в `ThreatPoint` (см). Стена
	 * засчитывается, только если она МЕЖДУ: дальше стрелка искать нечего.
	 *
	 * Без этого ограничения `CoverTraceDistance` (120 см) трейсился всегда, и в
	 * упор луч пролетал СКВОЗЬ стрелка в стену у него за спиной — цель получала
	 * «Full cover» от чужой стены и синий щит там, где XCOM даёт жёлтый.
	 * ≤ 0 — стрелок ближе толщины луча, укрытия быть не может (ближний бой =
	 * автоматический фланг).
	 */
	static float GetCoverTraceLength(const UCoverTuningDataAsset* Tuning,
		const FVector& Base, const FVector& ThreatPoint);

	/**
	 * СТОРОНЫ УКРЫТИЯ в точке Base (точка пола): лучи по кругу, стороны
	 * склеиваются по НОРМАЛИ найденной стены. Плоская стена рядом даёт одну
	 * сторону, а не три-четыре луча; угол ящика — две.
	 *
	 * Отбрасываются стены дальше `CoverSideDistanceSlack` от ближайшей: юнит
	 * прячется за ближней стеной, а не за всем, что попало в радиус трейса.
	 */
	static void GatherCoverSides(const UWorld* World, const FVector& Base,
		const UCoverTuningDataAsset* Tuning, const AActor* Ignored, TArray<FCoverSide>& OutSides);


	/**
	 * С КАКОЙ СТОРОНЫ КОНЧАЕТСЯ укрытие юнита: мировое направление ВДОЛЬ стены
	 * (XY, нормализовано) в сторону ближайшего края. Zero — укрытия нет или в
	 * пределах `PeekEdgeMaxDistance` края не нашлось (глухая стена).
	 *
	 * Нужно анимации выглядывания: боец в укрытии смотрит туда, где стена
	 * заканчивается, а не «в среднем вбок». Та же математика, что у огневых
	 * позиций (`UTacticsCombatStatics::GetFiringPositions`): шагаем вдоль стены
	 * с шагом `PeekEdgeStep`, пока трейс в стену не перестанет её находить.
	 *
	 * ⚠️ Отличие от огневых позиций: там край ищется ОТНОСИТЕЛЬНО ЦЕЛИ (ось —
	 * направление на цель), здесь цели нет — ось берётся от самой стены
	 * (`BestCoverDirection`). Поэтому это отдельная функция, а не параметр той:
	 * вопросы разные («откуда стрелять по нему» и «куда выглядывать вообще»).
	 *
	 * OutEdgeDistance — расстояние до края (см) от позиции юнита, 0 если края нет.
	 *
	 * ⚠️ В C++ НЕ вызывать напрямую: результат кэшируется в `PeekEdgeDirection` /
	 * `PeekEdgeDistance` / `PeekSideSign` при EvaluateSurroundings — читать кэш.
	 * Прямой вызов оставлен BlueprintPure только для совместимости с BP.
	 */
	UFUNCTION(BlueprintPure, Category = "Tactics|Cover")
	FVector FindPeekEdgeSide(float& OutEdgeDistance) const;

	/** Численный бонус защиты против конкретного стрелка (0 / Half / Full). */
	UFUNCTION(BlueprintPure, Category = "Tactics|Cover")
	float GetDefenseBonusAgainst(const AActor* Threat) const;

	/**
	 * Эффективный тюнинг укрытий этого юнита: TuningOverride → глобальный
	 * (GameInstance->CoverTuning) → CDO. НИКОГДА не nullptr.
	 */
	const UCoverTuningDataAsset* GetTuning() const;

protected:
	virtual void BeginPlay() override;

	/** Снимает старый GE укрытия и навешивает соответствующий новому состоянию. */
	void ApplyCoverEffect(ECoverType CoverType);

	/** Атомарно обновляет публичную геометрию и публикует её отдельную ревизию. */
	void SetActiveCoverGeometry(const FVector& NewAnchor, const FVector& NewNormal,
		int64 NewWallId, UPrimitiveComponent* NewComponent, float NewPlaneDistance);

	/** Хэндл активного GE укрытия на ASC владельца (для снятия при смене состояния). */
	FActiveGameplayEffectHandle ActiveCoverEffectHandle;

private:
	/** Слабая identity поверхности нужна только для runtime hysteresis. */
	TWeakObjectPtr<UPrimitiveComponent> ActiveCoverComponent;

	/** Плоскость стены: dot(ActiveCoverNormal, SurfacePoint). */
	float ActiveCoverPlaneDistance = 0.f;

	/** Явный unlock от владельца tactical move; обрабатывается следующим evaluate. */
	bool bActiveCoverReselectionRequested = false;
};
