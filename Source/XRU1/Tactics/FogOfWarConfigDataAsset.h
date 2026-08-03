#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FogOfWarConfigDataAsset.generated.h"

class UWorld;

/**
 * Тюнинг ВИЗУАЛЬНОГО слоя тумана — сетки затемнения местности (`UFogGridSubsystem`).
 *
 * ⚠️ Здесь нет и не может быть ни одного параметра, влияющего на ПРАВИЛА. Кто кого
 * видит, можно ли стрелять и что показывает UI, решают `UTacticsCombatStatics::AnyUnitSees`
 * и `UFogOfWarSubsystem`; сетка только рисует их результат ([docs/10_FOG_OF_WAR.md](../../../docs/10_FOG_OF_WAR.md) §1).
 * Дальность обзора сетка тоже НЕ настраивает — берёт общий
 * `UTacticsCombatStatics::SquadVisionRange`, иначе граница затемнения разошлась бы
 * с границей обнаружения, и игрок судил бы о правилах по картинке.
 *
 * По образцу `UCoverTuningDataAsset`: ассет назначается один раз в BP-наследнике
 * `UTacticsGameInstance` (поле `FogConfig`), достаётся через статический `Get`.
 * Дефолты класса — рабочие числа, поэтому без назначенного ассета слой работает.
 */
UCLASS(BlueprintType)
class XRU1_API UFogOfWarConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Конфиг с GameInstance, иначе CDO. НИКОГДА не возвращает nullptr. */
	static const UFogOfWarConfigDataAsset* Get(const UWorld* World);

	// --- Геометрия сетки -------------------------------------------------------

	/**
	 * Сторона клетки (см). XCOM квантует мир тайлами по 96 см
	 * (`WORLD_METERS_TO_UNITS_MULTIPLIER = 64`), но у него тайл ещё и рисуется
	 * тайловой геометрией, а у нас — экранным затемнением, где квант виден прямо
	 * как квадрат на земле. На 100 см квадраты читались с любого зума (прогон
	 * 2026-08-03), поэтому клетка мельче тайла XCOM. Ещё мельче — квадратично
	 * дороже запекание и растеризация.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Сетка", meta = (ClampMin = "25"))
	float CellSize = 75.f;

	/**
	 * Верхний предел стороны сетки в клетках. При превышении УКРУПНЯЕТСЯ КЛЕТКА, а
	 * не растёт память: 256×256 клеток — это 64K трейсов на запекание и текстура
	 * 256 КБ, дальше время старта боя становится заметным.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Сетка", meta = (ClampMin = "32", ClampMax = "1024"))
	int32 MaxGridResolution = 224;

	/**
	 * Запас вокруг объёмов навигации (см). Бойцы ходят по навмешу, но СМОТРЯТ
	 * дальше его границы: без запаса стена по краю арены осталась бы за пределами
	 * сетки и не затемнялась вовсе.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Сетка", meta = (ClampMin = "0"))
	float BoundsPadding = 600.f;

	/**
	 * Радиус пробы «клетка не пропускает взгляд» (см).
	 *
	 * ⚠️ Проба обязана быть ТОНКОЙ — это линия взгляда, а не объём. Толстая
	 * (в долю клетки) захватывала верхушки НИЗКИХ укрытий и превращала мешки с
	 * песком в глухую стену, отчего за каждым бруствером тянулась тень. В XCOM
	 * низкое укрытие обзор не рвёт вовсе: блокирует только высокое, а из-за
	 * низкого стреляют поверх.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Сетка", meta = (ClampMin = "1", ClampMax = "50"))
	float BlockerProbeRadius = 10.f;

	/**
	 * Плотность лучей растеризации: сколько лучей приходится на клетку дуги на
	 * границе обзора. < 1 даёт видимые «спицы» непросвеченных клеток у края.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Сетка", meta = (ClampMin = "0.5", ClampMax = "4"))
	float RaysPerEdgeCell = 2.f;

	/**
	 * Радиус сценарного раскрытия местности по умолчанию (см) — когда режиссура
	 * такта показывает игроку то, чего отряд ещё не разведал. Число XCOM:
	 * `X2Action_RevealArea.ScanningRadius = 768` юнитов = 8 тайлов = 12 м.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Сетка", meta = (ClampMin = "100"))
	float ScriptedRevealRadius = 1200.f;

	/**
	 * Обзор ТЯЖЕЛО РАНЕНОГО бойца (см) — маленький круг вокруг лежащего.
	 *
	 * Число из XCOM: `BLEEDOUT_SIGHT_RADIUS = 3` метра. Смысл ровно тот же —
	 * игрок обязан видеть, ГДЕ лежит его боец и что творится рядом с ним, иначе
	 * приказ «дойти и поднять» отдаётся вслепую. На правила это не влияет:
	 * зрения отряду Downed по-прежнему не даёт (`IsUnitAlive` для него ложна),
	 * речь только о картинке.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Сетка", meta = (ClampMin = "0"))
	float DownedVisionRange = 300.f;

	// --- Вид -------------------------------------------------------------------

	/**
	 * Радиус сглаживания маски в клетках (0 — выключено, 1 — окно 3×3, 2 — 5×5).
	 *
	 * Сглаживание идёт по САМОЙ МАСКЕ, а не по экрану: клетка получает долю
	 * видимых соседей, и граница перестаёт читаться лесенкой пикселей. Дешевле и
	 * чище, чем размывать полноэкранную картинку, — так же поступает XCOM,
	 * размывая низкоразрешённую FOW-текстуру.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Вид", meta = (ClampMin = "0", ClampMax = "3"))
	int32 MaskSmoothingRadius = 1;

	/**
	 * Ширина мягкой каймы у границы видимости (0 — резкий край, 1 — всё
	 * растушёвано).
	 *
	 * ⚠️ Не путать со сглаживанием маски. Сглаживание делает переход плавным, но
	 * из-за него ВНУТРИ зоны обзора появляется полутень; порог возвращает ядру
	 * полную яркость и оставляет мягкой только саму кромку. Правило простое: то,
	 * что боец видит, он видит целиком.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Вид", meta = (ClampMin = "0", ClampMax = "1"))
	float EdgeSoftness = 0.45f;

	/**
	 * Скорость сглаживания края во времени (долей яркости в секунду). Аналог
	 * `XComEngine.ini: FOWEnvelopeSpeed = 170` (170/255 ≈ 0.67 доли в секунду) —
	 * у нас быстрее, потому что тактическая зона меньше и шаг бойца открывает
	 * заметно больший кусок карты. 0 — переключать мгновенно (будет видна
	 * «лестница» клеток на каждом шаге).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Вид", meta = (ClampMin = "0"))
	float EdgeFadeSpeed = 3.5f;

	/**
	 * Оттенок неразведанной местности (GDD §5.9, состояние `Unknown`).
	 *
	 * ⚠️ Это именно ТОН, а не заливка: сплошной цвет стирает силуэты и лишает
	 * игрока чувства карты — «куда вообще идти» перестаёт читаться. Поэтому
	 * `Unknown` — это сильно притушенная и обесцвеченная сцена, помноженная на
	 * этот оттенок; очертания рельефа и построек остаются различимы.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Вид")
	FLinearColor UnknownColor = FLinearColor(0.55f, 0.68f, 1.f, 1.f);

	/**
	 * Яркость неразведанной местности (доля от исходной сцены). Настолько мало,
	 * чтобы читались только очертания, и настолько много, чтобы они читались.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Вид", meta = (ClampMin = "0", ClampMax = "1"))
	float UnknownBrightness = 0.12f;

	/**
	 * Показывать местность ЗА границами сетки без затемнения.
	 *
	 * По умолчанию false — туман накрывает и дальний план: иначе граница сетки
	 * читается на экране прямой линией, что выглядит как дефект. Дальний пейзаж
	 * при этом не пропадает: неразведанное — не заливка, а притушенная сцена
	 * (`UnknownBrightness`), и горы на горизонте остаются на месте.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Вид")
	bool bShowTerrainOutsideBounds = false;


	/**
	 * Яркость разведанной, но сейчас невидимой местности (доля от исходной сцены).
	 * Игрок обязан различать там рельеф и укрытия — иначе `Explored` перестаёт
	 * быть памятью и становится вторым `Unknown`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Вид", meta = (ClampMin = "0", ClampMax = "1"))
	float ExploredBrightness = 0.25f;

	/**
	 * Насколько обесцвечивается разведанная местность (0 — цвет как есть, 1 — серое).
	 * Обесцвечивание читается как «это память, а не то, что видно сейчас», и
	 * работает лучше простого затемнения.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog|Вид", meta = (ClampMin = "0", ClampMax = "1"))
	float ExploredDesaturation = 0.8f;
};
