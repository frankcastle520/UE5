// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IO/IoDispatcher.h"
#include "Memory/SharedBuffer.h"
#include "NaniteResources.h"
#include "UnifiedBuffer.h"
#include "SpanAllocator.h"

namespace UE
{
	namespace DerivedData
	{
		class FRequestOwner; // Can't include DDC headers from here, so we have to forward declare
		struct FCacheGetChunkRequest;
	}
}

class FRDGBuilder;

namespace Nanite
{

struct FPageKey
{
	uint32 RuntimeResourceID	= INDEX_NONE;
	uint32 PageIndex			= INDEX_NONE;

	friend FORCEINLINE uint32 GetTypeHash(const FPageKey& Key)
	{
		return Key.RuntimeResourceID * 0xFC6014F9u + Key.PageIndex * 0x58399E77u;
	}

	FORCEINLINE bool operator==(const FPageKey& Other) const 
	{
		return RuntimeResourceID == Other.RuntimeResourceID && PageIndex == Other.PageIndex;
	}

	FORCEINLINE bool operator!=(const FPageKey& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE bool operator<(const FPageKey& Other) const
	{
		return RuntimeResourceID != Other.RuntimeResourceID ? RuntimeResourceID < Other.RuntimeResourceID : PageIndex < Other.PageIndex;
	}
};

struct FStreamingRequest
{
	FPageKey	Key;
	uint32		Priority;
	
	FORCEINLINE bool operator<(const FStreamingRequest& Other) const 
	{
		return Key != Other.Key ? Key < Other.Key : Priority > Other.Priority;
	}
};

/*
 * Streaming manager for Nanite.
 */
class FStreamingManager : public FRenderResource
{
public:
	FStreamingManager();
	
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;

	void	Add(FResources* Resources);
	void	Remove(FResources* Resources);

	ENGINE_API void BeginAsyncUpdate(FRDGBuilder& GraphBuilder);			// Called once per frame before any Nanite rendering has occurred. Must be called before EndUpdate.
	ENGINE_API void EndAsyncUpdate(FRDGBuilder& GraphBuilder);				// Called once per frame before any Nanite rendering has occurred. Must be called after BeginUpdate.
	ENGINE_API bool IsAsyncUpdateInProgress();
	ENGINE_API void	SubmitFrameStreamingRequests(FRDGBuilder& GraphBuilder);		// Called once per frame after the last request has been added.

	ENGINE_API FRDGBuffer* GetStreamingRequestsBuffer(FRDGBuilder& GraphBuilder) const;
	ENGINE_API FRDGBufferSRV* GetHierarchySRV(FRDGBuilder& GraphBuilder) const;
	ENGINE_API FRDGBufferSRV* GetClusterPageDataSRV(FRDGBuilder& GraphBuilder) const;
	ENGINE_API FRDGBufferSRV* GetImposterDataSRV(FRDGBuilder& GraphBuilder) const;

	ENGINE_API uint32 GetStreamingRequestsBufferVersion() const;
	
	float GetQualityScaleFactor() const
	{
		return QualityScaleFactor;
	}
	
	uint32 GetMaxStreamingPages() const	
	{
		return MaxStreamingPages;
	}

	uint32 GetMaxHierarchyLevels() const
	{
		return MaxHierarchyLevels;
	}

	inline bool HasResourceEntries() const
	{
		return NumResources > 0u;
	}

	TMap<uint32, uint32> GetAndClearModifiedResources()
	{
		return MoveTemp(ModifiedResources);
	}

	ENGINE_API void		PrefetchResource(const FResources* Resource, uint32 NumFramesUntilRender);
	ENGINE_API void		RequestNanitePages(TArrayView<uint32> RequestData);
#if WITH_EDITOR
	ENGINE_API uint64	GetRequestRecordBuffer(TArray<uint32>& OutRequestData);
	ENGINE_API void		SetRequestRecordBuffer(uint64 Handle);
#endif

private:
	friend class FStreamingUpdateTask;

	struct FResourcePrefetch
	{
		uint32	RuntimeResourceID;
		uint32	NumFramesUntilRender;
	};

	struct FAsyncState
	{
		struct FGPUStreamingRequest*	GPUStreamingRequestsPtr = nullptr;
		uint32							NumGPUStreamingRequests = 0;
		uint32							NumReadyPages = 0;
		bool							bUpdateActive = false;
		bool							bBuffersTransitionedToWrite = false;
	};

	struct FPendingPage
	{
#if WITH_EDITOR
		FSharedBuffer			SharedBuffer;
		enum class EState
		{
			None,
			DDC_Pending,
			DDC_Ready,
			DDC_Failed,
			Memory,
			Disk,
		} State = EState::None;
		uint32					RetryCount = 0;
#endif
		FIoBuffer				RequestBuffer;
		FBulkDataBatchReadRequest Request;

		uint32					GPUPageIndex = INDEX_NONE;
		FPageKey				InstallKey;
		uint32					BytesLeftToStream = 0;
	};

	struct FHeapBuffer
	{
		int32							TotalUpload = 0;
		FSpanAllocator					Allocator;
		FRDGScatterUploadBuffer			UploadBuffer;
		TRefCountPtr<FRDGPooledBuffer>	DataBuffer;

		void Release()
		{
			UploadBuffer = {};
			DataBuffer = {};
		}
	};

	struct FVirtualPage
	{
		uint32 Priority				= 0u;						// Priority != 0u means referenced this frame
		uint32 RegisteredPageIndex	= INDEX_NONE;

		FORCEINLINE bool operator==(const FVirtualPage& Other) const
		{
			return Priority == Other.Priority && RegisteredPageIndex == Other.RegisteredPageIndex;
		}
	};

	struct FNewPageRequest
	{
		FPageKey Key;
		uint32 VirtualPageIndex = INDEX_NONE;
	};

	struct FRegisteredPage
	{
		FPageKey	Key;
		uint32		VirtualPageIndex = INDEX_NONE;
		uint8		RefCount = 0;
	};

	struct FResidentPage
	{
		FPageKey	Key;
		uint8		MaxHierarchyDepth	= 0xFF;
	};

	struct FRootPageInfo
	{
		FResources* Resources = nullptr;
		uint32		RuntimeResourceID = INDEX_NONE;
		uint32		VirtualPageRangeStart = INDEX_NONE;
		uint16		NumClusters = 0u;
		uint8		MaxHierarchyDepth = 0xFF;
	};

	TArray<FRootPageInfo>	RootPageInfos;
	TArray<uint8>			RootPageVersions;
	
	FHeapBuffer				ClusterPageData;	// FPackedCluster*, GeometryData { Index, Position, TexCoord, TangentX, TangentZ }*
	FHeapBuffer				Hierarchy;
	FHeapBuffer				ImposterData;
	TArray<uint32>			ClusterLeafFlagUpdates;
	
	TPimplPtr<class FHierarchyDepthManager>	HierarchyDepthManager;
	uint32					MaxHierarchyLevels = 0;

	uint32					MaxStreamingPages = 0;
	uint32					MaxRootPages = 0;
	uint32					NumInitialRootPages = 0;
	uint32					PrevNumInitialRootPages = 0;
	uint32					MaxPendingPages = 0;
	uint32					MaxPageInstallsPerUpdate = 0;

	uint32					NumResources = 0;
	uint32					NumPendingPages = 0;
	uint32					NextPendingPageIndex = 0;
	float					QualityScaleFactor = 1.0f;
	bool					bClusterPageDataAllocated = false;

	uint32					StatNumRootPages = 0;
	uint32					StatPeakRootPages = 0;
	uint32					StatVisibleSetSize = 0;
	uint32					StatPrevUpdateTime = 0;
	uint32					StatNumAllocatedRootPages = 0;
	uint32					StatNumHierarchyNodes = 0;
	uint32					StatPeakHierarchyNodes = 0;
	float					StatStreamingPoolPercentage = 0.0f;
	
	uint64					PrevUpdateTick = 0;

	TArray<FResources*>					PendingAdds;

	TMultiMap<uint32, FResources*>		PersistentHashResourceMap;			// TODO: MultiMap to handle potential collisions and issues with there temporarily being two meshes with the same hash because of unordered add/remove.
	
	TMap<uint32, uint32>				ModifiedResources;					// Key = RuntimeResourceID, Value = NumResidentClusters

	FSpanAllocator						VirtualPageAllocator;
	TArray<FVirtualPage>				RegisteredVirtualPages;

	typedef TArray<uint32, TInlineAllocator<16>> FRegisteredPageDependencies;
	TArray<FRegisteredPage>				RegisteredPages;
	TArray<FRegisteredPageDependencies>	RegisteredPageDependencies;

	TArray<uint32>						RegisteredPageIndexToLRU;
	TArray<uint32>						LRUToRegisteredPageIndex;

	TArray<FResidentPage>				ResidentPages;
	TArray<FFixupChunk*>				ResidentPageFixupChunks;			// Fixup information for resident streaming pages. We need to keep this around to be able to uninstall pages.
	TMap<FPageKey, uint32>				ResidentPageMap;					// This update is deferred to the point where the page has been loaded and committed to memory.

	TArray<FNewPageRequest>				RequestedNewPages;
	TArray<uint32>						RequestedRegisteredPages;

	TArray<FPendingPage>				PendingPages;
	TArray<uint8>						PendingPageStagingMemory;
	TPimplPtr<class FRingBufferAllocator>	PendingPageStagingAllocator;
	
	TPimplPtr<class FStreamingPageUploader>	PageUploader;
	TPimplPtr<class FReadbackManager>	ReadbackManager;

	TPimplPtr<class FQualityScalingManager>		QualityScalingManager;

	FGraphEventArray					AsyncTaskEvents;
	FAsyncState							AsyncState;

#if WITH_EDITOR
	UE::DerivedData::FRequestOwner*		RequestOwner;

	uint64								PageRequestRecordHandle = (uint64)-1;
	TMap<FPageKey, uint32>				PageRequestRecordMap;
#endif
	TArray<uint32>						PendingExplicitRequests;
	TArray<FResourcePrefetch>			PendingResourcePrefetches;

	// Transient lifetime, but persisted to reduce allocations
	TArray<FStreamingRequest>			PrioritizedRequestsHeap;
	TArray<uint32>						GPUPageDependencies;
	TArray<FPageKey>					SelectedPages;

	bool AddRequest(uint32 RuntimeResourceID, uint32 PageIndex, uint32 VirtualPageIndex, uint32 Priority);
	bool AddRequest(uint32 RuntimeResourceID, uint32 PageIndex, uint32 Priority);
	void AddPendingGPURequests();
	void AddPendingExplicitRequests();
	void AddPendingResourcePrefetchRequests();
	void AddParentRequests();
	void AddParentRegisteredRequestsRecursive(uint32 RegisteredPageIndex, uint32 Priority);
	void AddParentNewRequestsRecursive(const FResources& Resources, uint32 RuntimeResourceID, uint32 PageIndex, uint32 VirtualPageRangeStart, uint32 Priority);

	FRootPageInfo*	GetRootPage(uint32 RuntimeResourceID);
	FResources*		GetResources(uint32 RuntimeResourceID);

	void SelectHighestPriorityPagesAndUpdateLRU(uint32 MaxSelectedPages);

	void RegisterStreamingPage(uint32 RegisteredPageIndex, const FPageKey& Key);
	void UnregisterStreamingPage(const FPageKey& Key);

	void MoveToEndOfLRUList(uint32 RegisteredPageIndex);
	void CompactLRU();
	void VerifyLRU();

	void ApplyFixups(const FFixupChunk& FixupChunk, const FResources& Resources, bool bIsUninstall);

	bool ArePageDependenciesCommitted(uint32 RuntimeResourceID, uint32 DependencyPageStart, uint32 DependencyPageNum);

	void ProcessNewResources(FRDGBuilder& GraphBuilder, FRDGBuffer* ClusterPageDataBuffer);
	FRDGBuffer* ResizePoolAllocationIfNeeded(FRDGBuilder& GraphBuilder);
	
	uint32 DetermineReadyPages(uint32& TotalPageSize);
	void InstallReadyPages(uint32 NumReadyPages);
	void UninstallGPUPage(uint32 GPUPageIndex, bool bApplyFixup);

	void AddClusterLeafFlagUpdate(uint32 MaxStreamingPages, uint32 GPUPageIndex, uint32 ClusterIndex, uint32 NumClusters, bool bReset, bool bUninstall);
	void FlushClusterLeafFlagUpdates(FRDGBuilder& GraphBuilder, FRDGBuffer* ClusterPageDataBuffer);

	void ResetStreamingStateCPU();
	void UpdatePageConfiguration();
	

	void AsyncUpdate();

#if NANITE_SANITY_CHECK_STREAMING_REQUESTS
	void SanityCheckStreamingRequests(const struct FGPUStreamingRequest* StreamingRequestsPtr, const uint32 NumStreamingRequests);
#endif

#if WITH_EDITOR
	void RecordGPURequests();
	UE::DerivedData::FCacheGetChunkRequest BuildDDCRequest(const FResources& Resources, const FPageStreamingState& PageStreamingState, const uint32 PendingPageIndex);
	void RequestDDCData(TConstArrayView<UE::DerivedData::FCacheGetChunkRequest> DDCRequests);
#endif
};

extern ENGINE_API TGlobalResource< FStreamingManager > GStreamingManager;

} // namespace Nanite