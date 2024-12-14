// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/BitArray.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Containers/ContainersFwd.h"
#include "Containers/Set.h"
#include "Containers/StringFwd.h"
#include "Containers/UnrealString.h"
#include "HAL/Event.h"
#include "HAL/Platform.h"
#include "HAL/PlatformCrt.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/MpscQueue.h"
#include "Containers/RingBuffer.h"
#include "Cooker/CookTypes.h"
#include "Cooker/TypedBlockAllocator.h"
#include "HAL/CriticalSection.h"
#include "TargetDomain/TargetDomainUtils.h"
#include "Templates/UniquePtr.h"
#include "UObject/ICookInfo.h"
#include "UObject/NameTypes.h"

#include <atomic>

class FAssetPackageData;
class IAssetRegistry;
class ICookedPackageWriter;
class ITargetPlatform;
class UCookOnTheFlyServer;
namespace UE::Cook { class FRequestQueue; }
namespace UE::Cook { struct FDiscoveryQueueElement; }
namespace UE::Cook { struct FFilePlatformRequest; }
namespace UE::Cook { struct FPackageData; }
namespace UE::Cook { struct FPackageDatas; }
namespace UE::Cook { struct FPackagePlatformData; }
namespace UE::Cook { struct FPackageTracker; }

namespace UE::Cook
{

/**
 * A group of external requests sent to CookOnTheFlyServer's tick loop. Transitive dependencies are found and all of the
 * requested or dependent packagenames are added as requests together to the cooking state machine.
 */
class FRequestCluster
{
public:
	FRequestCluster(UCookOnTheFlyServer& COTFS, TArray<FFilePlatformRequest>&& InRequests);
	FRequestCluster(UCookOnTheFlyServer& COTFS, FPackageDataSet&& InRequests);
	FRequestCluster(UCookOnTheFlyServer& COTFS, TRingBuffer<FDiscoveryQueueElement>& DiscoveryQueue);
	FRequestCluster(FRequestCluster&&) = default;

	/**
	 * Calculate the information needed to create a PackageData, and transitive search dependencies for all requests.
	 * Called repeatedly (due to timeslicing) until bOutComplete is set to true.
	 */
	void Process(const FCookerTimer& CookerTimer, bool& bOutComplete);

	/** PackageData container interface: return the number of PackageDatas owned by this container. */
	int32 NumPackageDatas() const;
	/** PackageData container interface: remove the PackageData from this container. */
	void RemovePackageData(FPackageData* PackageData);
	void OnNewReachablePlatforms(FPackageData* PackageData);
	void OnPlatformAddedToSession(const ITargetPlatform* TargetPlatform);
	void OnRemoveSessionPlatform(const ITargetPlatform* TargetPlatform);
	void RemapTargetPlatforms(TMap<ITargetPlatform*, ITargetPlatform*>& Remap);

	/** PackageData container interface: whether the PackageData is owned by this container. */
	bool Contains(FPackageData* PackageData) const;
	/**
	 * Remove all PackageDatas owned by this container and return them.
	 * OutRequestsToLoad is the set of PackageDatas sorted by leaf to root load order.
	 * OutRequestToDemote is the set of Packages that are uncookable or have already been cooked.
	 * If called before Process sets bOutComplete=true, all packages are put in OutRequestToLoad and are unsorted.
	 */
	void ClearAndDetachOwnedPackageDatas(TArray<FPackageData*>& OutRequestsToLoad,
		TArray<TPair<FPackageData*, ESuppressCookReason>>& OutRequestsToDemote,
		TMap<FPackageData*, TArray<FPackageData*>>& OutRequestGraph);
	/**
	 * Report packages that are in request state and assigned to this Cluster, but that should not be counted as in
	 * progress for progress displays because this cluster has marked them as already cooked or as to be demoted. */
	int32 GetPackagesToMarkNotInProgress() const;

	static TConstArrayView<FName> GetLocalizationReferences(FName PackageName, UCookOnTheFlyServer& InCOTFS);
	static TArray<FName> GetAssetManagerReferences(FName PackageName);
	static void IsRequestCookable(const ITargetPlatform* TargetPlatform, FPackageData& PackageData,
		UCookOnTheFlyServer& COTFS, ESuppressCookReason& OutReason, bool& bOutCookable, bool& bOutExplorable);

private:
	struct FGraphSearch;

	/** GraphSearch cached data for a packagename that has already been visited. */
	struct FVisitStatus
	{
		FPackageData* PackageData = nullptr;
		bool bVisited = false;
	};

	/** Status for where a vertex is on the journey through having its CookDependency information fetched from DDC. */
	enum class EAsyncQueryStatus : uint8
	{
		NotRequested,
		SchedulerRequested,
		AsyncRequested,
		Complete,
	};

	/** Per-platform data in an active query for a vertex's dependencies/previous incremental results. */
	struct FQueryPlatformData
	{
		EAsyncQueryStatus GetAsyncQueryStatus();
		bool CompareExchangeAsyncQueryStatus(EAsyncQueryStatus& Expected, EAsyncQueryStatus Desired);

	public:
		// All fields other than CookAttachments and AsyncQueryStatus are read/write on Scheduler thread only
		/**
		 * Data looked up about the package's dependencies from the PackageWriter's previous cook of the package.
		 * Thread synchronization: this field is write-once from the async thread and is not readable until
		 * bSchedulerThreadFetchCompleted.
		 */
		UE::TargetDomain::FCookAttachments CookAttachments;
		bool bSchedulerThreadFetchCompleted = false;
		bool bExploreRequested = false;
		bool bExploreCompleted = false;
		bool bIterativelyUnmodifiedRequested = false;
		bool bTransitiveBuildDependenciesResolvedAsNotModified = false;
		TOptional<bool> bIterativelyUnmodified;
	private:
		std::atomic<EAsyncQueryStatus> AsyncQueryStatus;
	};

	/**
	 * GraphSearch data for an package referenced by the cluster. VertexData is created when a package is discovered
	 * from the dependencies of a referencer package. It remains allocated for the rest of the Cluster's lifetime.
	 */
	struct FVertexData
	{
		FVertexData(FName InPackageName, UE::Cook::FPackageData* InPackageData, FGraphSearch& GraphSearch);
		const FAssetPackageData* GetGeneratedAssetPackageData();

		/* Async thread is not allowed to access PackageData, so store its name.The name is immutable for vertex lifetime. */
		FName PackageName;
		TArray<FVertexData*> IterativelyModifiedListeners;
		UE::Cook::FPackageData* PackageData = nullptr;
		bool bAnyCookable = true;
		bool bPulledIntoCluster = false;
		/** Settings and Results for each of the GraphSearch's FetchPlatforms. Element n corresponds to FetchPlatform n. */
		TUniquePtr<FQueryPlatformData[]> PlatformData;
	};

	/**
	 * Each FVertexData includes has-been-cooked existence and dependency information that is looked up
	 * from PackageWriter storage of previous cooks. The lookup can have significant latency and per-query
	 * costs. We therefore do the lookups for vertices asynchronously and in batches. An FQueryVertexBatch
	 * is a collection of FVertexData that are sent in a single lookup batch. The batch is destroyed
	 * once the results for all requested vertices are received.
	 */
	struct FQueryVertexBatch
	{
		FQueryVertexBatch(FGraphSearch& InGraphSearch);
		void Reset();
		void Send();

		void RecordCacheResults(FName PackageName, int32 PlatformIndex,
			UE::TargetDomain::FCookAttachments&& CookAttachments);

		struct FPlatformData
		{
			TArray<FName> PackageNames;
		};

		TArray<FPlatformData> PlatformDatas;
		/**
		 * Map of the requested vertices by name. The map is created during Send and is
		 * read-only afterwards (so the map is multithread-readable). The Vertices pointed to have their own
		 * rules for what is accessible from the async work threads.
		 * */
		TMap<FName, FVertexData*> Vertices;
		/** Accessor for the GraphSearch; only thread-safe functions and variables should be accessed. */
		FGraphSearch& ThreadSafeOnlyVars;
		/** Number of vertex*platform requests that still await results. Batch is done when NumPendingRequests == 0. */
		std::atomic<uint32> NumPendingRequests;
	};

	/** Platform information that is constant (usually, some events can change it) during the cluster's lifetime. */
	struct FFetchPlatformData
	{
		const ITargetPlatform* Platform = nullptr;
		ICookedPackageWriter* Writer = nullptr;
		bool bIsPlatformAgnosticPlatform = false;
		bool bIsCookerLoadingPlatform = false;
	};
	// Platforms are listed in various arrays, always in the same order. Some special case entries exist and are added
	// at specified indices in the arrays.
	static constexpr int32 PlatformAgnosticPlatformIndex = 0;
	static constexpr int32 CookerLoadingPlatformIndex = 1;
	static constexpr int32 FirstSessionPlatformIndex = 2;

	/** How much traversal the GraphSearch should do based on settings for the entire cook. */
	enum class ETraversalTier
	{
		/** Do not fetch any edgedata. Used on CookWorkers; the director already did the fetch. */
		None,
		/**
		 * Fetch the edgedata and use it for ancillary calculation like updating whether a package is iteratively
		 * unmodified. Do not explore the discovered dependencies.
		 */
		FetchEdgeData,
		/** Fetch the edgedata, update ancillary calculations, and explore the discovered dependencies. */
		FollowDependencies,
		All=FollowDependencies,
	};

	/**
	 * Variables and functions that are only used during PumpExploration. PumpExploration executes a graph search
	 * over the graph of packages (vertices) and their hard/soft dependencies upon other packages (edges). 
	 * Finding the dependencies for each package uses previous cook results and is executed asynchronously.
	 * After the graph is searched, packages are sorted topologically from leaf to root, so that packages are
	 * loaded/saved by the cook before the packages that need them to be in memory to load.
	 */
	struct FGraphSearch
	{
	public:
		FGraphSearch(FRequestCluster& InCluster, ETraversalTier TraversalTier);
		~FGraphSearch();

		// All public functions are callable only from the process thread
		/** Skip the entire GraphSearch and just visit the Cluster's current OwnedPackageDatas. */
		void VisitWithoutDependencies();
		/** Start a search from the Cluster's current OwnedPackageDatas. */
		void StartSearch();
		void OnNewReachablePlatforms(FPackageData* PackageData);

		/**
		 * Visit newly reachable PackageDatas, queue a fetch of their dependencies, harvest new reachable PackageDatas
		 * from the results of the fetch.
		 */
		void TickExploration(bool& bOutDone);
		/** Sleep (with timeout) until work is available in TickExploration */
		void WaitForAsyncQueue(double WaitTimeSeconds);

		/**
		 * Edges in the dependency graph found during graph search.
		 * Only includes PackageDatas that are part of this cluster
		 */
		TMap<FPackageData*, TArray<FPackageData*>>& GetGraphEdges();

	private:
		// Scratch data structures used to avoid dynamic allocations; lifetime of each use is only on the stack
		struct FScratchPlatformDependencyBits
		{
			TBitArray<> HasPlatformByIndex;
			EInstigator InstigatorType = EInstigator::SoftDependency;
		};
		struct FExploreEdgesContext
		{
		public:
			FExploreEdgesContext(FRequestCluster& InCluster, FGraphSearch& InGraphSearch);

			/**
			 * Process the results from async edges fetch and queue the found dependencies-for-visiting. Only does
			 * portions of the work for each FQueryPlatformData that were requested by the flags on the PlatformData.
			 */
			void Explore(FVertexData& InVertex);

		private:
			void Initialize(FVertexData& InVertex);
			void CalculatePlatformsToProcess();
			bool TryCalculateIterativelyUnmodified();
			void CalculatePackageDataDependenciesPlatformAgnostic();
			void CalculateDependenciesAndIterativelySkippable();
			void QueueVisitsOfDependencies();
			void MarkExploreComplete();

			void AddPlatformDependency(FName DependencyName, int32 PlatformIndex, EInstigator InstigatorType);
			void AddPlatformDependencyRange(TConstArrayView<FName> Range, int32 PlatformIndex, EInstigator InstigatorType);
			void ProcessPlatformAttachments(int32 PlatformIndex, const ITargetPlatform* TargetPlatform,
				FFetchPlatformData& FetchPlatformData, FPackagePlatformData& PackagePlatformData,
				UE::TargetDomain::FCookAttachments& PlatformAttachments, bool bExploreDependencies);

			void SetIsIterativelyUnmodified(int32 PlatformIndex, bool bIterativelyUnmodified,
				FPackagePlatformData& PackagePlatformData);

		private:
			FRequestCluster& Cluster;
			FGraphSearch& GraphSearch;
			FVertexData* Vertex = nullptr;
			FPackageData* PackageData = nullptr;
			TArray<FName>* DiscoveredDependencies = nullptr;
			TArray<FName> HardGameDependencies;
			TArray<FName> HardEditorDependencies;
			TArray<FName> SoftGameDependencies;
			TArray<FName> CookerLoadingDependencies;
			TArray<int32, TInlineAllocator<10>> PlatformsToProcess;
			TArray<int32, TInlineAllocator<10>> PlatformsToExplore;
			TMap<FName, FScratchPlatformDependencyBits> PlatformDependencyMap;
			TSet<FName> HardDependenciesSet;
			TSet<FName> SkippedPackages;
			TArray<FVertexData*> UnreadyTransitiveBuildVertices;
			FName PackageName;
			int32 LocalNumFetchPlatforms = 0;
			bool bFetchAnyTargetPlatform = false;
		};
		friend struct FQueryVertexBatch;
		friend struct FVertexData;
		
		// Functions callable only from the Process thread
		/** Log diagnostic information about the search, e.g. timeout warnings. */
		void UpdateDisplay();

		/** Asynchronously fetch the dependencies and previous incremental results for a vertex */
		void QueueEdgesFetch(FVertexData& Vertex, TConstArrayView<int32> PlatformIndexes);
		/** Calculate and store the vertex's PackageData's cookability for each reachable platform. Kick off edges fetch. */
		void VisitVertex(FVertexData& VertexData);
		/** Calculate and store the vertex's PackageData's cookability for the platform. */
		void VisitVertexForPlatform(FVertexData& VertexData, const ITargetPlatform* Platform,
			FPackagePlatformData& PlatformData, ESuppressCookReason& AccumulatedSuppressCookReason);
		void ResolveTransitiveBuildDependencyCycle();

		/** Find or add a Vertex for PackageName. If PackageData is provided, use it, otherwise look it up. */
		FVertexData& FindOrAddVertex(FName PackageName, FGenerationHelper* ParentGenerationHelper = nullptr);
		FVertexData& FindOrAddVertex(FName PackageName, FPackageData& PackageData);
		/** Batched allocation for vertices. */
		FVertexData* AllocateVertex(FName PackageName, FPackageData* PackageData);
		/** Queue a vertex for visiting and dependency traversal */
		void AddToVisitVertexQueue(FVertexData& Vertex);

		// Functions that must be called only within the Lock
		/** Allocate memory for a new batch; returned batch is not yet constructed. */
		FQueryVertexBatch* AllocateBatch();
		/** Free an allocated batch. */ 
		void FreeBatch(FQueryVertexBatch* Batch);
		/** Pop vertices from VerticesToRead into batches, if there are enough of them. */
		void CreateAvailableBatches(bool bAllowIncompleteBatch);
		/** Pop a single batch vertices from VerticesToRead. */
		FQueryVertexBatch* CreateBatchOfPoppedVertices(int32 BatchSize);

		// Functions that are safe to call from any thread
		/** Notify process thread of batch completion and deallocate it. */
		void OnBatchCompleted(FQueryVertexBatch* Batch);
		/** Notify process thread of vertex completion. */
		void KickVertex(FVertexData* Vertex);

		/** Total number of platforms known to the cluster, including the special cases. */
		int32 NumFetchPlatforms() const;
		/** Total number of non-special-case platforms known to the cluster.Identical to COTFS's session platforms */
		int32 NumSessionPlatforms() const;

		TArrayView<FQueryPlatformData> GetPlatformDataArray(FVertexData& Vertex);

	private:
		// Variables that are read-only during multithreading
		TArray<FFetchPlatformData> FetchPlatforms;
		FRequestCluster& Cluster;
		ETraversalTier TraversalTier = ETraversalTier::All;

		// Variables that are accessible only from the Process thread
		/** A set of stack and scratch variables used when calculating and exploring the edges of a vertex. */
		FExploreEdgesContext ExploreEdgesContext;
		TMap<FPackageData*, TArray<FPackageData*>> GraphEdges;
		TMap<FName, FVertexData*> Vertices;
		TSet<FVertexData*> VisitVertexQueue;
		TSet<FVertexData*> PendingTransitiveBuildDependencyVertices;
		TTypedBlockAllocatorFreeList<FVertexData> VertexAllocator;
		/** Vertices queued for async processing that are not yet numerous enough to fill a batch. */
		TRingBuffer<FVertexData*> PreAsyncQueue;
		/** Time-tracker for timeout warnings in Poll */
		double LastActivityTime = 0.;
		int32 RunAwayTickLoopCount = 0;

		// Variables that are accessible from multiple threads, guarded by Lock
		FCriticalSection Lock;
		TTypedBlockAllocatorResetList<FQueryVertexBatch> BatchAllocator;
		TSet<FQueryVertexBatch*> AsyncQueueBatches;

		// Variables that are accessible from multiple threads, internally threadsafe
		TMpscQueue<FVertexData*> AsyncQueueResults;
		FEventRef AsyncResultsReadyEvent;
	};

	/** Tracks flags about this cluster's processing state for its OwnedPackageDatas. */
	struct FProcessingFlags
	{
		/** Whether this struct has been set valid, used to identify whether values exist in a TMap. */
		bool IsValid() const;
		/** The package's SuppressCookReason, either NotSuppressed or a reason it was suppressed. */
		ESuppressCookReason GetSuppressReason() const;
		/** Whether the package was marked as cooked for any platform by this cluster. */
		bool WasMarkedCooked() const;
		/** Whether the values indicate the package should be added to the cluster's PackagesToMarkNotInProgress. */
		bool ShouldMarkNotInProgress() const;

		void SetValid();
		void SetSuppressReason(ESuppressCookReason Value);
		void SetWasMarkedCooked(bool bValue);

	private:
		ESuppressCookReason SuppressCookReason = ESuppressCookReason::NotSuppressed;
		bool bValid = false;
		bool bWasMarkedCooked = false;
	};

private:
	explicit FRequestCluster(UCookOnTheFlyServer& COTFS);
	void ReserveInitialRequests(int32 RequestNum);
	void PullIntoCluster(FPackageData& PackageData);
	void FetchPackageNames(const FCookerTimer& CookerTimer, bool& bOutComplete);
	void PumpExploration(const FCookerTimer& CookerTimer, bool& bOutComplete);
	void StartAsync(const FCookerTimer& CookerTimer, bool& bOutComplete);
	bool IsIncrementalCook() const;
	void SetPackageDataSuppressReason(FPackageData& PackageData, ESuppressCookReason Reason,
		bool* bOutExisted = nullptr);
	void SetPackageDataWasMarkedCooked(FPackageData& PackageData, bool bValue, bool* bOutExisted = nullptr);
	void IsRequestCookable(const ITargetPlatform* TargetPlatform, FName PackageName, FPackageData& PackageData,
		ESuppressCookReason& OutReason, bool& bOutCookable, bool& bOutExplorable);
	static void IsRequestCookable(const ITargetPlatform* TargetPlatform, FName PackageName, FPackageData& PackageData,
		UCookOnTheFlyServer& InCOTFS, FStringView InDLCPath, ESuppressCookReason& OutReason, bool& bOutCookable,
		bool& bOutExplorable);

	TArray<FFilePlatformRequest> FilePlatformRequests;
	/**
	 * Set of all packageDatas owned by this cluster (they are in the request state and this is the requeststate
	 * container that records them). The count of PackageDatas matching certain properties is stored in
	 * PackagesToMarkNotInProgress and must be updated whenever values change in OwnedPackageDatas. Call
	 * SetPackageData... functions or RemovePackageData instead of modifying it directly.
	 */
	TFastPointerMap<FPackageData*, FProcessingFlags> OwnedPackageDatas;
	TMap<FPackageData*, TArray<FPackageData*>> RequestGraph;
	FString DLCPath;
	TUniquePtr<FGraphSearch> GraphSearch; // Needs to be dynamic-allocated because of large alignment
	UCookOnTheFlyServer& COTFS;
	FPackageDatas& PackageDatas;
	IAssetRegistry& AssetRegistry;
	FPackageTracker& PackageTracker;
	FBuildDefinitions& BuildDefinitions;
	int32 PackagesToMarkNotInProgressCount = 0;
	bool bAllowHardDependencies = true;
	bool bAllowSoftDependencies = true;
	bool bErrorOnEngineContentUse = false;
	bool bPackageNamesComplete = false;
	bool bDependenciesComplete = false;
	bool bStartAsyncComplete = false;
	bool bAllowIterativeResults = false;
	bool bPreQueueBuildDefinitions = true;
};


///////////////////////////////////////////////////////
// Inline implementations
///////////////////////////////////////////////////////

inline int32 FRequestCluster::GetPackagesToMarkNotInProgress() const
{
	return PackagesToMarkNotInProgressCount;
}

inline bool FRequestCluster::FProcessingFlags::IsValid() const
{
	return bValid;
}

inline ESuppressCookReason FRequestCluster::FProcessingFlags::GetSuppressReason() const
{
	return SuppressCookReason;
}

inline bool FRequestCluster::FProcessingFlags::WasMarkedCooked() const
{
	return bWasMarkedCooked;
}

inline bool FRequestCluster::FProcessingFlags::ShouldMarkNotInProgress() const
{
	return bValid & 
		((SuppressCookReason != ESuppressCookReason::NotSuppressed) | bWasMarkedCooked);
}

inline void FRequestCluster::FProcessingFlags::SetValid()
{
	bValid = true;
}

inline void FRequestCluster::FProcessingFlags::SetSuppressReason(ESuppressCookReason Value)
{
	SuppressCookReason = Value;
}

inline void FRequestCluster::FProcessingFlags::SetWasMarkedCooked(bool bValue)
{
	bWasMarkedCooked = bValue;
}

inline int32 FRequestCluster::FGraphSearch::NumFetchPlatforms() const
{
	return FetchPlatforms.Num();
}

inline int32 FRequestCluster::FGraphSearch::NumSessionPlatforms() const
{
	return FetchPlatforms.Num() - 2;
}

inline TArrayView<FRequestCluster::FQueryPlatformData> FRequestCluster::FGraphSearch::GetPlatformDataArray(
	FVertexData& Vertex)
{
	return TArrayView< FQueryPlatformData>(Vertex.PlatformData.Get(), NumFetchPlatforms());
}

inline FRequestCluster::EAsyncQueryStatus FRequestCluster::FQueryPlatformData::GetAsyncQueryStatus()
{
	return AsyncQueryStatus.load(std::memory_order_acquire);
}

inline bool FRequestCluster::FQueryPlatformData::CompareExchangeAsyncQueryStatus(EAsyncQueryStatus& Expected,
	EAsyncQueryStatus Desired)
{
	return AsyncQueryStatus.compare_exchange_strong(Expected, Desired,
		// For the read operation to see whether we should set it, we need only relaxed memory order;
		// we don't care about the values of other related variables that depend on it when deciding whether
		// it is our turn to set it.
		// For the write operation if we decide to set it, we need release memory order to guard reads of
		// the variables that depend on it (e.g. CookAttachments).
		std::memory_order_release /* success memory order */,
		std::memory_order_relaxed /* failure memory order */
	);
}

}
