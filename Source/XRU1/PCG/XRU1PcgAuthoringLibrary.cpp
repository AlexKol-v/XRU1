#include "XRU1PcgAuthoringLibrary.h"

#include "XRU1Log.h"

#include "PCGGraph.h"
#include "PCGNode.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"

int32 UXRU1PcgAuthoringLibrary::DisableSpawnerCollision(const FString& GraphAssetPath)
{
	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphAssetPath);
	if (!Graph)
	{
		UE_LOG(LogXRU1UI, Error, TEXT("[PCG] граф не найден: %s"), *GraphAssetPath);
		return 0;
	}

	int32 Fixed = 0;
	for (UPCGNode* Node : Graph->GetNodes())
	{
		UPCGStaticMeshSpawnerSettings* Spawner = Node
			? Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings()) : nullptr;
		UPCGMeshSelectorWeighted* Selector = Spawner
			? Cast<UPCGMeshSelectorWeighted>(Spawner->MeshSelectorParameters) : nullptr;
		if (!Selector)
		{
			continue;
		}
		for (FPCGMeshSelectorWeightedEntry& Entry : Selector->MeshEntries)
		{
			// Дефолтная коллизия меша перекрыла бы профиль — выключаем обе.
			Entry.Descriptor.bUseDefaultCollision = false;
			Entry.Descriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Entry.Descriptor.BodyInstance.SetCollisionProfileName(TEXT("NoCollision"));
			++Fixed;
		}
		Selector->MarkPackageDirty();
	}

	Graph->MarkPackageDirty();
	UE_LOG(LogXRU1UI, Display, TEXT("[PCG] коллизия выключена у %d записей мешей в %s"),
		Fixed, *GraphAssetPath);
	return Fixed;
}
