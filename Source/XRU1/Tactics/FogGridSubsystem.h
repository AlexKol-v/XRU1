#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Subsystems/WorldSubsystem.h"
#include "FogGridSubsystem.generated.h"

class AActor;
class UFogOfWarConfigDataAsset;
class UMaterialInstanceDynamic;
class UTexture2D;

/**
 * ВИЗУАЛЬНЫЙ слой тумана: затемнение местности тремя состояниями GDD §5.9
 * (`Unknown` — почти чёрное, `Explored` — приглушённое, `Visible` — как есть).
 *
 * ⚠️ ГЛАВНОЕ ПРАВИЛО, нарушить которое проще всего: сетка НИЧЕГО не решает.
 * Она вход рендера, и только. Кто кого видит, можно ли стрелять, что показывает
 * HUD — по-прежнему `UTacticsCombatStatics::AnyUnitSees` и `UFogOfWarSubsystem`.
 * Поэтому здесь НЕТ ни одного публичного запроса вида «видно ли точку»: появись
 * он, первый же потребитель сделал бы сетку вторым источником правды, и картинка
 * начала бы расходиться с правилами стрельбы (docs/10_FOG_OF_WAR.md §1).
 * Живых противников прячет `UFogRevealableComponent` — это другая задача и
 * другой владелец, затемнение местности его не заменяет.
 *
 * Модель взята у XCOM 2 (`XComWorldData`):
 *  - мир квантуется ОДИН раз (`BuildWorldData` — «Run when users save a map, this
 *    will quantize the playable game space and build a 3d tile grid»), дальше
 *    рантайм ходит по воксельным данным, а не трейсит (`bUseLineChecksForFOW = false`).
 *    У нас запекание битовой маски блокеров происходит на старте сценария, а
 *    видимость растеризуется DDA-лучами по массиву. Наивный вариант «трейс из
 *    каждого источника в каждую клетку» — это ~14 400 сферо-свипов на пересчёт;
 *  - одна FOW-текстура с прямоугольником обновления (`CurrentUpdateBox`) и
 *    сглаживанием края во времени (`FOWEnvelopeSpeed = 170`). У нас так же: один
 *    `UTexture2D`, заливка только изменившегося прямоугольника, плавный край;
 *  - «карта без неизвестного» — штатный режим (`bShowNeverSeenAsHaveSeen`), у нас
 *    это `UTacticalScenarioDataAsset::bStartFullyExplored`.
 *
 * Обновление — ПО СОБЫТИЮ: подписка на пересчёт `UFogOfWarSubsystem`. Своего
 * расписания у сетки нет, поэтому она наследует и троттлинг движения (0.1 с), и
 * правило «ноль работы в кадре, где ничего не произошло».
 *
 * Этажей (Z-bands) нет намеренно: на карте нет боевого второго этажа, а гейт
 * акторов трёхмерен и без них (§2.4). Полоса ровно одна; отдельного поля под
 * этажи не заводится, пока нет потребителя (06_CONVENTIONS §3) — расширение
 * начинается с превращения `Blockers`/`Visible`/`Explored` в массив полос.
 */
UCLASS()
class XRU1_API UFogGridSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Сетка тумана мира (может вернуть nullptr вне игрового мира). */
	static UFogGridSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * Новая сессия: границы и блокеры перезапекаются, `Explored` начинается с
	 * нуля (или со всей карты при `bStartFullyExplored`), сценарные раскрытия
	 * снимаются. Зовётся оттуда же, откуда `UFogOfWarSubsystem::ResetForScenario`,
	 * — состояние не имеет права пережить `ScenarioRunId`.
	 */
	void ResetForScenario(FName ScenarioId, int32 RunId, bool bStartFullyExplored);

	/**
	 * Динамический инстанс PP-материала тумана с пешки-камеры. Параметры (текстура,
	 * origin и размер сетки, цвета состояний) пишет сюда подсистема: камера знает,
	 * КУДА повесить блендабл, а сетка — ЧТО в него положить.
	 */
	void RegisterFogMaterial(UMaterialInstanceDynamic* Material);

	// --- Сценарное раскрытие местности ---------------------------------------

	/**
	 * Источник обзора, не являющийся бойцом: раскрывает местность вокруг точки
	 * или актора. Прямой аналог `XComWorldData::CreateFOWViewer` /
	 * `X2Action_RevealArea` — тем же приёмом XCOM показывает игроку сектор по
	 * скрипту, не выдавая ему зрения.
	 *
	 * Нужен потому, что чёрная карта и режиссура обучения конфликтуют: беат ведёт
	 * камеру в сектор, где отряд ещё не был, и без раскрытия игрок увидит чёрный
	 * кадр вместо постановки.
	 *
	 * `Anchor` задан — раскрытие едет за актором (сценарная перебежка), иначе
	 * стоит в `Location`. `Radius` < 0 — из конфига. `Duration` > 0 — снимется
	 * само (страховка от несмятого удержания), иначе живёт до `RemoveScriptedReveal`.
	 * Возвращает дескриптор; 0 — сетка выключена/не построена.
	 */
	int32 AddScriptedReveal(const AActor* Anchor, const FVector& Location,
		float Radius = -1.f, float Duration = -1.f);

	/** Снять раскрытие по дескриптору (0 игнорируется — вызов парен и безопасен). */
	void RemoveScriptedReveal(int32 Handle);

	/**
	 * Распечатать сетку в журнал текстовой картой (`xru1.Fog.GridDump`).
	 * По скриншоту тень рельефа и полосу затемнения не различить — дамп отвечает
	 * фактами: где блокеры, что видно сейчас, что разведано.
	 */
	void DumpGridToLog() const;

	// --- UTickableWorldSubsystem ---------------------------------------------

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Не-юнитный источник обзора (см. `AddScriptedReveal`). */
	struct FScriptedReveal
	{
		TWeakObjectPtr<const AActor> Anchor;
		FVector Location = FVector::ZeroVector;
		float Radius = 0.f;
		/** Игровое время снятия; < 0 — до явного `RemoveScriptedReveal`. */
		double ExpiryTime = -1.0;
		int32 Handle = 0;
	};

	/**
	 * Границы, размер клетки и битовая маска «клетка не пропускает взгляд».
	 * Запекается один раз на сценарий: трейс вниз ищет пол, проба на высоте глаз
	 * решает, стоит ли там непрозрачная геометрия. Object-типы — те же, что у
	 * боевой линии огня (`UTacticsCombatStatics::GetShotGeometryObjects`): иначе
	 * картинка расходилась бы с правилами уже на уровне определения стены.
	 */
	void BuildGrid();

	/** Высота глаз над полом (см): половина капсулы юнита + `EyeHeightOffset`. */
	float GetVisionHeightAboveFloor() const;

	/** Пересобрать `Visible` от всех источников; `Explored |= Visible`. */
	void RasterizeVisibility();

	/** DDA-лучи из точки: клетки помечаются видимыми до первого блокера включительно. */
	void CastVisionRays(const FVector& Origin, float Range);

	/** Снять сценарные раскрытия, у которых вышло время. */
	void ExpireScriptedReveals();

	/** Сдвинуть отображаемые значения к целевым и залить изменившийся прямоугольник. */
	void AdvanceDisplayAndUpload(float DeltaSeconds, bool bInstant);

	/**
	 * Пересчитать сглаженные маски. Зовётся из растеризации, а не из тика: маска
	 * меняется на порядок реже, чем идут кадры.
	 */
	void RebuildSmoothedMasks();

	/**
	 * Значение клетки со сглаживанием по соседям (0..255). Именно здесь бинарная
	 * маска превращается в мягкую границу: размывать надо низкоразрешённую маску,
	 * а не полноэкранную картинку.
	 */
	uint8 SampleSmoothed(const TBitArray<>& Mask, int32 X, int32 Y, int32 Radius) const;

	/** Создать/пересоздать текстуру под текущий размер сетки. */
	void EnsureTexture();

	/** Разослать параметры сетки в зарегистрированные материалы. */
	void PushMaterialParameters();

	/** Пересчёт видимости произошёл — сетка обязана пересобраться. */
	void HandleVisibilityRecomputed();

	int32 CellIndex(int32 X, int32 Y) const { return Y * GridWidth + X; }

	/** Активен ли слой (`xru1.Fog.Grid`). */
	static bool IsGridEnabled();

	// --- Геометрия -----------------------------------------------------------

	/** Мировые X/Y минимального угла сетки. */
	FVector2D GridOrigin = FVector2D::ZeroVector;

	float CellSize = 100.f;
	int32 GridWidth = 0;
	int32 GridHeight = 0;

	/** Высота, на которой запекались блокеры, — для журнала и диагностики. */
	float BakedVisionHeight = 0.f;

	bool bGridValid = false;

	// --- Состояние -----------------------------------------------------------

	/** Клетка не пропускает взгляд (запекается один раз на сценарий). */
	TBitArray<> Blockers;

	/** Видно прямо сейчас (пересобирается каждый пересчёт). */
	TBitArray<> Visible;

	/** Видели хоть раз. Только растёт — до конца сессии сценария. */
	TBitArray<> Explored;

	/**
	 * Отображаемые значения (0..255) — они отстают от логических на время
	 * сглаживания края. Аналог `FOWEnvelopeSpeed`: без него каждый шаг бойца
	 * перекрашивал бы полосу клеток мгновенно, и граница «лестницей» бросалась бы
	 * в глаза сильнее самого тумана.
	 */
	TArray<uint8> DisplayVisible;
	TArray<uint8> DisplayExplored;

	/**
	 * Сглаженные (0..255) значения масок — то, к чему стремятся отображаемые.
	 * Считаются один раз на растеризацию: мягкость границы — свойство маски, а не
	 * кадра, и пересчитывать её каждый тик незачем.
	 */
	TArray<uint8> SmoothedVisible;
	TArray<uint8> SmoothedExplored;

	/** BGRA-буфер текстуры: R — текущая видимость, G — `Explored`. */
	TArray<uint8> TextureData;

	/** Значения ещё не догнали логические — есть работа для тика. */
	bool bDisplayDirty = false;

	/**
	 * Текстуру надо залить ЦЕЛИКОМ, а не только изменившимся прямоугольником.
	 * Ставится при создании текстуры: `CreateTransient` не инициализирует память,
	 * и неизменившиеся тексели иначе навсегда остались бы мусором.
	 */
	bool bTextureNeedsFullUpload = true;

	/** Пришёл пересчёт видимости — растеризовать в ближайшем тике (коалесинг). */
	bool bVisibilityDirty = false;

	/** Позиции источников прошлой растеризации: не сдвинулись — работы нет. */
	TArray<FVector> LastSourcePositions;

	TArray<FScriptedReveal> ScriptedReveals;
	int32 NextRevealHandle = 1;

	/** Есть ли раскрытия с временем жизни — иначе тик их не проверяет. */
	bool bHasTimedReveals = false;

	// --- Рендер --------------------------------------------------------------

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FogTexture;

	/**
	 * Материалы тумана живых камер. Слабые ссылки: пешка-камера пересоздаётся при
	 * смене сценария, и держать её MID реестром нельзя.
	 */
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> FogMaterials;

	/** Значение `xru1.Fog.Grid` в прошлом тике — переключение обновляет материал. */
	bool bGridEnabledLastTick = true;

	/** Отладочные выключатели в прошлом тике: их смена запускает пересчёт. */
	bool bBlockersLastTick = true;
	bool bScriptedRevealsLastTick = true;

	// --- Сессия --------------------------------------------------------------

	FName ActiveScenarioId;
	int32 ActiveRunId = 0;

	/** Профиль текущего сценария: стартовать разведанной картой (см. `bStartFullyExplored`). */
	bool bScenarioStartsExplored = false;

	/** Дескриптор подписки на пересчёт видимости — снимается в `Deinitialize`. */
	FDelegateHandle VisibilityRecomputedHandle;
};
