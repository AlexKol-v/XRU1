#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MissionVfxDataAsset.generated.h"

class UNiagaraSystem;

/**
 * Niagara-эффекты событий миссии (ДЗ 4: свои VFX, привязанные к событиям
 * сценария). Зеркало звукового слоя: как реплики боя привязаны к
 * ПОДТВЕРЖДЁННЫМ квест-событиям, а не к нажатиям кнопок, так и эффекты
 * спавнятся `UMissionVfxSubsystem` по тем же каналам — из C++, не из BP
 * (03_ARCHITECTURE.md §12, §14).
 *
 * Пустое поле — события без эффекта, штатная настройка, а не ошибка.
 * Ссылка живёт в `BP_TacticsGameInstance` (`MissionVfx`), ассет —
 * `Data/Core/DA_MissionVfx`; фолбэк — CDO с пустыми полями (молчит).
 */
UCLASS(BlueprintType)
class XRU1_API UMissionVfxDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Лечение (Ability.Heal.Normal) — на цели лечения. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Способности")
	TObjectPtr<UNiagaraSystem> HealEffect;

	/** Подъём тяжелораненого (Ability.Heal.Revive) — на поднятом бойце. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Способности")
	TObjectPtr<UNiagaraSystem> ReviveEffect;

	/** Шаг обезвреживания (Objective.Defuse.Progressed) — искры на заряде. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Миссия")
	TObjectPtr<UNiagaraSystem> DefuseProgressEffect;

	/** Заряд снят (Objective.Defuse.Completed) — финальная вспышка на заряде. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Миссия")
	TObjectPtr<UNiagaraSystem> DefuseCompleteEffect;

	/** Боец эвакуирован (Objective.Evac.Unit) — всплеск в точке ухода. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Миссия")
	TObjectPtr<UNiagaraSystem> EvacUnitEffect;

	/**
	 * Столб синего дыма на зоне эвакуации («южные ворота, синий дым» — лор
	 * зовёт его прямо в реплике). Зажигается на всех `AEvacZone` при снятии
	 * заряда и живёт на акторе зоны до конца боя.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Миссия")
	TObjectPtr<UNiagaraSystem> EvacZoneSmokeEffect;

	/**
	 * Масштаб зонального эффекта. Базовая система — аура РАЗМЕРОМ С БОЙЦА, и в
	 * масштабе 1 на зоне 10×10 м её просто не видно (прогон 2026-08-06: «дым
	 * зажжён», а на экране пусто). 4–5 — купол читается на всю зону.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Миссия", meta = (ClampMin = "0.1"))
	float EvacZoneEffectScale = 4.5f;

	/**
	 * Сколько живёт РАЗОВЫЙ эффект, сек. Базовые системы набора зациклены
	 * (ауры подбора), и без принудительной деактивации искры обезвреживания и
	 * всплеск эвакуации висели в мире вечно (прогон 2026-08-06). Деактивация
	 * даёт частицам дожить хвост и умереть штатно; дым зоны это не касается —
	 * он задуман постоянным. 0 — не ограничивать.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX", meta = (ClampMin = "0"))
	float OneShotLifetimeSeconds = 4.f;

	/** Глобальный ассет: назначение в GameInstance, иначе CDO (пустой — молчит). */
	static const UMissionVfxDataAsset* Get(const UWorld* World);
};
