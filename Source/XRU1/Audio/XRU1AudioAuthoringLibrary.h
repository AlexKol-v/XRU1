#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "XRU1AudioAuthoringLibrary.generated.h"

/**
 * Editor-сборка аудио-ассетов обработки.
 *
 * Зачем C++: пресеты Source Effects (`USourceEffectFilterPreset` и т.п.) не
 * создаются из Python (нет фабрик в API), а «радио-эффект» рации — это именно
 * цепочка обработки ИСТОЧНИКА, которую ассет озвучки несёт с собой
 * (`USoundBase::SourceEffectChain`): назначил цепочку — и каждая реплика
 * автоматически звучит «по рации», без правок мест воспроизведения.
 */
UCLASS()
class XRU1_API UXRU1AudioAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Создаёт (или возвращает существующую) цепочку «радио»:
	 * BandPass-фильтр ~1.7 кГц (узкая телефонная полоса) → BitCrusher
	 * (8 кГц / 10 бит — цифровой хруст рации). Числа — классика обработки
	 * walkie-talkie: срез низов и верхов + лёгкая деградация сигнала.
	 *
	 * `ChainFolder` — путь каталога (например `/Game/XRU1Game/Audio/Effects`).
	 * Возвращает путь ассета цепочки; пустая строка — ошибка.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Audio Authoring")
	static FString CreateRadioEffectChain(const FString& ChainFolder);

	/**
	 * Назначает цепочку в `SourceEffectChain` всем звукам списка.
	 * Возвращает число изменённых ассетов; сохранение — на вызывающем.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|Audio Authoring")
	static int32 AssignSourceEffectChain(const TArray<FString>& SoundAssetPaths,
		const FString& ChainAssetPath);
};
