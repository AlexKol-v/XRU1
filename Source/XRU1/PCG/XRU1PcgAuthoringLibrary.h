#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "XRU1PcgAuthoringLibrary.generated.h"

/**
 * Точечные правки PCG-графов из скриптов агента.
 *
 * Зачем отдельная C++-функция: `FPCGSoftISMComponentDescriptor` не экспонирует
 * в Python ни `BodyInstance`, ни коллизионный профиль — а коллизия у
 * декоративных обломков ломает бой дважды: физически (мешают перемещению) и
 * через туман войны (гигантский объём/меши перехватывают LOS-трейсы видимости,
 * и карта «слепнет»).
 */
UCLASS()
class XRU1_API UXRU1PcgAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Выключает коллизию у всех мешей всех Static Mesh Spawner нод графа
	 * (профиль `NoCollision` + запрет дефолтной коллизии меша). Возвращает
	 * число исправленных записей; пакет помечается dirty, сохранение — на
	 * вызывающем.
	 */
	UFUNCTION(BlueprintCallable, Category = "XRU1|PCG Authoring")
	static int32 DisableSpawnerCollision(const FString& GraphAssetPath);
};
