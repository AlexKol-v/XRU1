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
	/**
	 * Общий профиль-родитель (обычно `DA_UnitAudio_Common`). Любое НЕзаполненное
	 * здесь событие берётся из него: шаги, боль, смерть и foley у всех бойцов
	 * одинаковые, а свои остаются только выстрел и голос. Заполнять один и тот
	 * же звук в пяти ассетах — прямой путь к «у мародёра почему-то тишина».
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UUnitAudioDataAsset> ParentProfile;

	/** Реплики доменных событий боя. Незаполненное событие берётся у родителя. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Events")
	TMap<EUnitSoundEvent, FTacticsSoundCue> Events;

	/**
	 * Шаги по типу поверхности — ОПЦИОНАЛЬНО. Пустая карта (наш случай) означает
	 * «один звук шага везде»: тогда трейс физматериала под ногой не делается
	 * вовсе, играет Footstep.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps")
	TMap<TEnumAsByte<EPhysicalSurface>, FTacticsSoundCue> FootstepsBySurface;

	/** Основной звук шага (и фолбэк для нераспознанной поверхности). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Footsteps",
		meta = (DisplayName = "Footstep"))
	FTacticsSoundCue DefaultFootstep;

	/**
	 * Затухание 3D-звуков юнита. Одно на профиль: тактическая камера смотрит
	 * сверху с переменным зумом, и разнобой в радиусах слышимости читается как
	 * «часть выстрелов беззвучная».
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<class USoundAttenuation> Attenuation;

	/** Реплика события с учётом родителя; nullptr — событие молчит осознанно. */
	const FTacticsSoundCue* FindEvent(EUnitSoundEvent Event) const;

	/** Реплика шага для поверхности; при отсутствии — общий Footstep. */
	const FTacticsSoundCue& FindFootstep(EPhysicalSurface Surface) const;

	/** Нужен ли трейс поверхности: только если карта реально заполнена. */
	bool UsesSurfaceFootsteps() const;

	/** Затухание своё либо родительское. */
	USoundAttenuation* ResolveAttenuation() const;
};
