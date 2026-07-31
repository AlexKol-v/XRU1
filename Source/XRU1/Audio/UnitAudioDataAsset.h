#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TacticsAudioTypes.h"
#include "UnitAudioDataAsset.generated.h"

/**
 * Звуковой профиль класса юнита: выстрел своего оружия, реакции на урон, шаги по
 * поверхностям и подтверждения способностей.
 *
 * Профиль намеренно отделён от Blueprint юнита: четыре класса отличаются оружием
 * и голосом, но логика «когда звучит» одна и живёт в C++. Добавление нового
 * звука не требует правки ни одного графа — только заполнения этого ассета.
 */
UCLASS(BlueprintType)
class XRU1_API UUnitAudioDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Реплики доменных событий боя. Незаполненное событие просто молчит. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Events")
	TMap<EUnitSoundEvent, FTacticsSoundCue> Events;

	/**
	 * Шаги по типу поверхности. Ключ — Physical Surface из Project Settings;
	 * поверхность берётся трейсом под ногой в момент AnimNotify.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	TMap<TEnumAsByte<EPhysicalSurface>, FTacticsSoundCue> FootstepsBySurface;

	/** Шаг, когда поверхность не распознана или не перечислена выше. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	FTacticsSoundCue DefaultFootstep;

	/**
	 * Затухание 3D-звуков юнита. Одно на профиль: тактическая камера смотрит
	 * сверху с переменным зумом, и разнобой в радиусах слышимости читается как
	 * «часть выстрелов беззвучная».
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<class USoundAttenuation> Attenuation;

	/** Реплика события или nullptr. */
	const FTacticsSoundCue* FindEvent(EUnitSoundEvent Event) const { return Events.Find(Event); }

	/** Реплика шага для поверхности; при отсутствии — DefaultFootstep. */
	const FTacticsSoundCue& FindFootstep(EPhysicalSurface Surface) const;
};
