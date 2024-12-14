// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionRuntimeCellData.h"
#include "WorldPartitionRuntimeCellDataHashSet.generated.h"

UCLASS(Within = WorldPartitionRuntimeCell, MinimalAPI)
class UWorldPartitionRuntimeCellDataHashSet : public UWorldPartitionRuntimeCellData
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	ENGINE_API virtual void DumpStateLog(FHierarchicalLogArchive& Ar) const;
#endif

	UPROPERTY()
	bool bIs2D;
};
