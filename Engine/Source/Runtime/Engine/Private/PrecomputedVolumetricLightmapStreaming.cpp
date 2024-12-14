// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PrecomputedVolumetricLightmap.cpp
=============================================================================*/

#include "PrecomputedVolumetricLightmapStreaming.h"
#include "PrecomputedVolumetricLightmap.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "ContentStreaming.h"
#include "WorldPartition/StaticLightingData/VolumetricLightmapGrid.h"
#include "Serialization/VersionedArchive.h"
#include "Serialization/MemoryReader.h"
#include "GameFramework/WorldSettings.h"

using FVersionedMemoryReaderView = TVersionedReader<FMemoryReaderView>;

enum ETimedExecutionControl
{
	Continue, 
	Restart, 
	Stop,
};

template<class ItemCollectionType, class ExecuteFn>
bool TimedExecution(ItemCollectionType& Items, float TimeLimit, const ExecuteFn& Execute) 		
{
	double EndTime = FPlatformTime::Seconds() + TimeLimit;	
	if (TimeLimit == 0)
	{
		EndTime = FLT_MAX;
	}
	
	bool bContinue = true; 
	
	while (bContinue)	
	{
		bContinue = false; 

		for (auto& It : Items)
		{
			float ThisTimeLimit = static_cast<float>(EndTime - FPlatformTime::Seconds());

			if (ThisTimeLimit < .001f) // one ms is the granularity of the platform event system
			{
				return false;
			}

			ETimedExecutionControl Control = Execute(ThisTimeLimit, It);

			if (Control == ETimedExecutionControl::Restart)
			{
				bContinue = true;
				break;
			}
			else if (Control == ETimedExecutionControl::Stop)
			{
				break;
			}			
		}
	}
		
	return true;
}

class FVolumetricLightmapGridStreamingManager : public IStreamingManager
{
public:

	friend class FVolumetricLightmapGridManager;
	
	FVolumetricLightmapGridManager* Owner;

	FVolumetricLightmapGridStreamingManager(FVolumetricLightmapGridManager* InOwner)
		: Owner(InOwner)
	{
		IStreamingManager::Get().AddStreamingManager(this);
	}

	~FVolumetricLightmapGridStreamingManager()
	{
		IStreamingManager::Get().RemoveStreamingManager(this);
	}

	virtual void Tick(float DeltaTime, bool bProcessEverything = false) override
	{
		
	}

	virtual void UpdateResourceStreaming(float DeltaTime, bool bProcessEverything = false) override
	{
		 int32 nbViews = IStreamingManager::Get().GetNumViews();

		 //@todo_ow: Manage multiple views properly, add better render/world setting to determine extent / budget
		 if (nbViews > 0)
		 {			
			FBox::FReal StreamDistance = Owner->World->GetWorldSettings()->VolumetricLightmapLoadingRange;

			FStreamingViewInfo ViewInfo = IStreamingManager::Get().GetViewInformation(0);
			FBox Bounds = FBox( ViewInfo.ViewOrigin - FVector(StreamDistance, StreamDistance, StreamDistance), ViewInfo.ViewOrigin  + FVector(StreamDistance, StreamDistance, StreamDistance));
			Owner->UpdateBounds(Bounds);
		 }
	}

	virtual int32 BlockTillAllRequestsFinished(float TimeLimit = 0.0f, bool bLogResults = false) override
	{
		return Owner->WaitForPendingRequest(TimeLimit);
	}

	virtual void CancelForcedResources() override {}
	virtual void NotifyLevelChange() override {}
	virtual void SetDisregardWorldResourcesForFrames(int32 NumFrames) override {}
	virtual void AddLevel(class ULevel* Level) override {}
	virtual void RemoveLevel(class ULevel* Level) override {}

	virtual void NotifyLevelOffset(class ULevel* Level, const FVector& Offset) override { check(false); /* Unsupported */ } 
	virtual int32 GetNumWantingResources() const override
	{ 
		return Owner->GetNumPendingRequests();
	}
};


FVolumetricLightmapGridManager::FVolumetricLightmapGridManager(UWorld* InWorld, FVolumetricLightMapGridDesc* InGrid)
	: World(InWorld)
	, Registry(InWorld->PersistentLevel->MapBuildData)
	, Grid(InGrid)
{
	StreamingManager = MakeUnique<FVolumetricLightmapGridStreamingManager>(this);
}

FVolumetricLightmapGridManager::~FVolumetricLightmapGridManager()
{
	check(LoadedCells.IsEmpty());
}

int32 FVolumetricLightmapGridManager::GetNumPendingRequests()
{
	return PendingCellRequests.Num();
}

int32 FVolumetricLightmapGridManager::WaitForPendingRequest(float TimeLimit)
{
	bool bTimedOut = TimedExecution(PendingCellRequests, TimeLimit, [this](float TimeLimit, CellRequest& Request) -> ETimedExecutionControl
	{		
		if (Request.IORequest && Request.IORequest->WaitCompletion(TimeLimit))
		{	
			ProcessRequests();
			return ETimedExecutionControl::Restart; // Restart iteration once requests have been processded since PendingCellRequests is modified by ProcessRequests()
		}

		return ETimedExecutionControl::Continue;
	});

	return PendingCellRequests.Num();
}


void FVolumetricLightmapGridManager::ReleaseCellData(FVolumetricLightMapGridCell* GridCell, FSceneInterface* InScene)
{
	if (GridCell->Data)
	{			
		FPrecomputedVolumetricLightmapData* Data = GridCell->Data;
		GridCell->Data = nullptr;

		ENQUEUE_RENDER_COMMAND(DeleteVolumetricLightDataCommand)
			([Data] (FRHICommandListBase&)
		{
			Data->ReleaseResource();
			delete(Data);
		});
	}
}

void FVolumetricLightmapGridManager::RemoveFromScene(FSceneInterface* InScene)
{
	for (auto& It : LoadedCells)
	{
		FPrecomputedVolumetricLightmap* Lightmap = It.Value;
		FVolumetricLightMapGridCell* GridCell = It.Key;

		if (Lightmap)
		{
			Lightmap->RemoveFromScene(InScene);
		}

		ReleaseCellData(GridCell, InScene);
	}

	LoadedCells.Reset();

	StreamingManager.Reset();	
}

IBulkDataIORequest* FVolumetricLightmapGridManager::RequestVolumetricLightMapCell(FVolumetricLightMapGridCell& Cell)
{	
	if (!Cell.BulkData.GetElementCount())
	{
		return nullptr;
	}

	FBulkDataIORequestCallBack RequestCallback = [&Cell](bool bWasCancelled, IBulkDataIORequest* IORequest)
	{
		if (!bWasCancelled)
		{
			void* Memory = IORequest->GetReadResults();
			if (Memory)
			{
				FMemoryView MemoryView(Memory, IORequest->GetSize());

				FVersionedMemoryReaderView FileDataAr(MemoryView, true);
				FPrecomputedVolumetricLightmapData* Data = nullptr;	

				FileDataAr << Data;

				//@todo_ow: this is fine for now since the other thread checks for both IO completion and 
				// the presence of the ptr, when we implement cancellation we'll need to have proper synchronization
				check(Data);
				check(!Cell.Data);
				Cell.Data = Data;
				
				FMemory::Free(Memory);
			}
		}
		else
		{
			check(!IORequest->GetReadResults());
			check(!Cell.Data);
		}
	};

	IBulkDataIORequest* IORequest = Cell.BulkData.CreateStreamingRequest(AIOP_Normal, &RequestCallback, nullptr);

	if (!IORequest)
	{
		check(Cell.BulkData.IsBulkDataLoaded());
		// For unsaved data we can't stream it, we need to do an immediate load
		Grid->LoadVolumetricLightMapCell(Cell);
	}

	return IORequest;
}

void FVolumetricLightmapGridManager::UpdateBounds(const FBox& InBounds)
{
	//@todo_ow: Make a pass on this to minimize heap allocations
	check(Grid);	

	// @todo_ow: Add check to see if bounds changed enough? there's a whole bunch of logic around this we'll see if we do it later
	TArray<FVolumetricLightMapGridCell*> IntersectingCells = Grid->GetIntersectingCells(InBounds, true);

	// Build list of cells to add & remove	
	TArray<FVolumetricLightMapGridCell*> CellsToRequest;
	
	TSet<FVolumetricLightMapGridCell*>  CellsToRemove;	
	LoadedCells.GetKeys(CellsToRemove);

	for (FVolumetricLightMapGridCell* Cell : IntersectingCells)
	{
		if (FPrecomputedVolumetricLightmap** LightMap = LoadedCells.Find(Cell))
		{		
			CellsToRemove.Remove(Cell);
		}
		else 
		{
			CellsToRequest.Add(Cell);
		}
	}

	// Make the necessary IO requests for new cells
	for (FVolumetricLightMapGridCell* Cell : CellsToRequest)
	{
		if (!PendingCellRequests.ContainsByPredicate([Cell](CellRequest& Request) { return Request.Cell == Cell; }))
		{
			CellRequest Request;
			Request.Cell = Cell;
			Request.IORequest = RequestVolumetricLightMapCell(*Cell);	
			if (Request.IORequest)
			{
				Request.Status = CellRequest::Requested;
			}
			else
			{
				Request.Status = CellRequest::Ready;
			}

			PendingCellRequests.Add(Request);
		}
	}

	// Since cells to remove are obtained from subtracting all IntersectingCells from the LoadedCells set, pending requested cells are never
	// in CellsToRemove, so this bit is not useful for now. We could extract cells to remove from the pending rq
	// @todo_ow: Handle removal of requested cells, an optimization for rarer/edgier cases so we'll do this later, plus this ones needs actual synchronization with the async request
	/*for (FVolumetricLightMapGridCell* Cell : CellsToRemove)
	{	
		if (CellRequest* Request = PendingCellRequests.FindByPredicate([Cell](CellRequest& InRequest) { return InRequest.Cell == Cell; }))
		{
			check(Request->Status == CellRequest::Requested);
			Request->Status = CellRequest::Cancelled;			
			Request->IORequest->Cancel();
			delete Request->IORequest;
			Request->IORequest = nullptr;
		}
	}*/
	
	// Remove all unnecessary cells
	for (FVolumetricLightMapGridCell* Cell : CellsToRemove)
	{
		FPrecomputedVolumetricLightmap* Lightmap = LoadedCells[Cell];
		if (Lightmap)
		{			
			Lightmap->RemoveFromScene(World->Scene);
		}

		LoadedCells.Remove(Cell);

		ReleaseCellData(Cell, World->Scene);
	}

	// Update currently tracked bounds
	Bounds = InBounds;

	ProcessRequests();
}

int32 FVolumetricLightmapGridManager::ProcessRequests()
{	
	TArray<FVolumetricLightMapGridCell*> CellsToAdd;

	// Process pending IO requests and move to add list if ready
	//@todo_ow: Use an inline array and just mark the requests that need to be removed
	TArray<CellRequest> UpdatedRequests;

	for (CellRequest& Request : PendingCellRequests)
	{
		if (Request.Status == CellRequest::Ready)
		{
			check(Request.Cell->Data);
			check(Request.IORequest == nullptr);
			CellsToAdd.Add(Request.Cell);
		}
		else if (Request.Status == CellRequest::Requested)
		{
			if (Request.IORequest->PollCompletion() && Request.Cell->Data)
			{
				CellsToAdd.Add(Request.Cell);
				
				delete Request.IORequest;
				Request.IORequest = nullptr;
				Request.Status = CellRequest::Ready;
			}
			else
			{
				UpdatedRequests.Add(Request);
			}
		}
		else
		{
			checkf(false, TEXT("Unexpected request status\n"));
		}
	}

	PendingCellRequests = MoveTemp(UpdatedRequests);

	// Add all necessary cells
	for (FVolumetricLightMapGridCell* Cell : CellsToAdd)
	{
		FPrecomputedVolumetricLightmap* Lightmap = nullptr;
		if (Cell->Data)
		{	
			Lightmap = new FPrecomputedVolumetricLightmap();
			Lightmap->AddToScene(World->Scene, Registry, Cell->Data, false);
		}
		else
		{
			check(Cell->BulkData.GetElementCount() == 0);
		}
		LoadedCells.Add(Cell, Lightmap);
	}

	return PendingCellRequests.Num();
}