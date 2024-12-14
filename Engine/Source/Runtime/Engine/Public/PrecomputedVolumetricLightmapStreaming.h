// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

class FSceneInterface;
class UWorld;
class IBulkDataIORequest;
class FPrecomputedVolumetricLightmap;

struct FVolumetricLightMapGridDesc;
struct FVolumetricLightMapGridCell;
class FVolumetricLightmapGridStreamingManager;
class UMapBuildDataRegistry;

class FVolumetricLightmapGridManager
{	
public:

	FVolumetricLightmapGridManager(UWorld* InWorld, FVolumetricLightMapGridDesc* Grid);
	~FVolumetricLightmapGridManager();

	void UpdateBounds(const FBox& Bounds);
	int32 ProcessRequests();
	void RemoveFromScene(class FSceneInterface* InScene);
	int32 WaitForPendingRequest(float Timelimit);
	int32 GetNumPendingRequests();	

private:
	
	friend class FVolumetricLightmapGridStreamingManager;

	void ReleaseCellData(FVolumetricLightMapGridCell* GridCell, FSceneInterface* InScene);	

	UWorld* World = nullptr;
	UMapBuildDataRegistry* Registry = nullptr;
	FVolumetricLightMapGridDesc* Grid = nullptr;
	
	struct CellRequest
	{
		enum EStatus
		{
			Created,
			Requested,
			Ready,
			Cancelled,
			Done
		};

		EStatus Status = Created;
		IBulkDataIORequest* IORequest = nullptr;
		FVolumetricLightMapGridCell* Cell = nullptr;			
	};	

	IBulkDataIORequest* RequestVolumetricLightMapCell(FVolumetricLightMapGridCell& Cell);
	
	TArray<CellRequest>	PendingCellRequests;
	TMap<FVolumetricLightMapGridCell*, FPrecomputedVolumetricLightmap*> LoadedCells;	

	FBox Bounds;

	TUniquePtr<FVolumetricLightmapGridStreamingManager> StreamingManager;
};

