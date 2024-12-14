// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDataGatherer.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/PathTree.h"
#include "Containers/RingBuffer.h"
#include "HAL/CriticalSection.h"
#include "Math/NumericLimits.h"
#include "ModuleDescriptor.h"
#include "PackageDependencyData.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

// Disable premade asset registry until the target specifies it in UBT
#ifndef ASSETREGISTRY_ENABLE_PREMADE_REGISTRY_IN_EDITOR
#define ASSETREGISTRY_ENABLE_PREMADE_REGISTRY_IN_EDITOR 0
#endif

class FDependsNode;
struct FARFilter;
class FAssetDataGatherer;
struct FFileChangeData;
class FPackageReader;
class UAssetRegistryImpl;
namespace UE::AssetRegistry::Impl { struct FEventContext; }
namespace UE::AssetRegistry::Premade { enum class ELoadResult : uint8; }
namespace UE::AssetRegistry::Premade { struct FAsyncConsumer; }
namespace UE::AssetRegistry::Impl { struct FInitializeContext; }
namespace UE::AssetRegistry::Impl { struct FScanPathContext; }
namespace UE::AssetRegistry::Impl { struct FTickContext; }
namespace UE::AssetRegistry::Impl { struct FClassInheritanceContext; }
namespace UE::AssetRegistry { template<typename TScopeLockType> class TRWScopeLockWithPriority; }
namespace UE::AssetRegistry { using FInterfaceReadScopeLock = TRWScopeLockWithPriority<FReadScopeLock>; }
namespace UE::AssetRegistry { using FInterfaceWriteScopeLock = TRWScopeLockWithPriority<FWriteScopeLock>; }
namespace UE::AssetRegistry { using FInterfaceRWScopeLock = class FRWScopeLockWithPriority; }

#if WITH_EDITOR
namespace UE::AssetDependencyGatherer::Private { class FRegisteredAssetDependencyGatherer; }
#endif

namespace UE::AssetRegistry::Premade
{

/**
 * A struct to consume a Premade asset registry on an async thread. It supports a cheap Wait() call so that it can be Waited on
 * by frequent AssetRegistry interface calls for the rest of the process.
 */
struct FAsyncConsumer
{
	~FAsyncConsumer();
	/** Sets the Consumer into need-to-wait mode; Wait will block until Consume is called. */
	void PrepareForConsume();
	/**
	 * Does not return until after the Premade AssetRegistryState has been added into the target AssetRegistry, or it has 
	 * been decided not to be used. For performance reasons, this must be called within the WriteScopeLock, and the caller 
	 * must handle the possibility that it leaves and reenters the lock.
	 */
	void Wait(UAssetRegistryImpl& UARI, FInterfaceWriteScopeLock& ScopeLock);
	/** Callback from the async thread to consume the Premade ARState */
	void Consume(UAssetRegistryImpl& UARI, UE::AssetRegistry::Impl::FEventContext& EventContext, ELoadResult LoadResult, FAssetRegistryState&& ARState);

private:
	/**
	 * ReferenceCounter for the Consumed event. Also used to decide whether Waiting is necessary. Read/Write only inside the lock.
	 */
	int32 ReferenceCount = 0;
	/** Event used to Wait for Consume. Allocated/deallocated within the lock. Can be waited on outside the lock if RefCount is held. */
	FEvent* Consumed = nullptr;
};

} // namespace UE::AssetRegistry::Premade

namespace UE::AssetRegistry
{

namespace Private
{
	/** Custom lock class that can prioritize waiters upon request. This is used to allow threads with critical requests 
	 *  to request that the gatherer thread pause its work and allow the higher pri threads to jump in. Note that for this
	 *  to work correctly it must be used with the associated TPriorityScopeLock types 
	 *  (TScopeLockWithPriority and FRWScopeLockWithPriority)
	 */
	class FRWLockWithPriority : public FRWLock
	{
	public:
		bool HasWaiters() const { return HighPriorityWaitersCount.load(std::memory_order_relaxed) > 0; }
	
	private:
		friend FInterfaceReadScopeLock;
		friend FInterfaceWriteScopeLock;
		friend FInterfaceRWScopeLock;
		std::atomic<int32> HighPriorityWaitersCount;
	};

	enum ELockPriority : uint8
	{
		PriorityLow,
		PriorityHigh
	};

	using FInterfaceRWLock = FRWLockWithPriority;
} // namespace Private within namespace UE::AssetRegistry

namespace Impl
{
	/** Container for global class inheritance data; the AssetRegistry may use a persistent buffer or a stack local buffer. */
	struct FClassInheritanceBuffer
	{
		/** Map from Class->SuperClass for all classes including native classes and blueprint classes. Updated on demand. */
		TMap<FTopLevelAssetPath, FTopLevelAssetPath> InheritanceMap;
		/** Map from Class->(All subclasses) for all classes including native classes and blueprint classes. Updated on demand. */
		TMap<FTopLevelAssetPath, TArray<FTopLevelAssetPath>> ReverseInheritanceMap;
		/** Snapshot of GetCurrentAllClassesVersionNumber() at the time of the last update, to invalidate on changes to classes. */
		uint64 SavedAllClassesVersionNumber = MAX_uint64;
		/** Dirty flag to invalidate on other changes requiring a recompute. */
		bool bDirty = true;

		/** Report whether the dirty flag and other checks indicate this buffer does not need to be updated. */
		bool IsUpToDate(uint64 CurrentAllClassesVersionNumber) const;
		/** Delete data and free allocations. */
		void Clear();
		/** Report size of dynamic allocations. */
		SIZE_T GetAllocatedSize() const;
	};

	/** Status of gathering, returned from the Tick function */
	enum class EGatherStatus : uint8
	{
		TickActiveGatherActive,
		TickActiveGatherIdle,
		TickGameThreadActiveGatherIdle,
		Complete,
		UnableToProgress,
		WaitingForEvents,
	};
	
	/** Affects how rules are applied to improve loading/runtime performance */
	enum EPerformanceMode : uint8
	{
		// Handling slow async load
		BulkLoading,

		// Not changing, optimize for runtime queries
		MostlyStatic,
	};

	/** Provides handling for time slicing during TickGatherer */
	struct FInterruptionContext
	{
	public:
		typedef TFunction<bool(void)> ShouldExitEarlyCallbackType;

		FInterruptionContext() = default;
		explicit FInterruptionContext(double InTickStartTime, double InMaxRunningTime)
			: TickStartTime(InTickStartTime)
			, MaxRunningTime(InMaxRunningTime)
		{
		}

		FInterruptionContext(double InTickStartTime, double InMaxRunningTime, ShouldExitEarlyCallbackType& Callback)
			: TickStartTime(InTickStartTime)
			, MaxRunningTime(InMaxRunningTime)
			, EarlyExitCallback(Callback) {}

		void SetEarlyExitCallback(const ShouldExitEarlyCallbackType& InCallback)
		{
			EarlyExitCallback = InCallback;
		}
		void SetUnlimitedTickTime()
		{
			TickStartTime = -1.;
			MaxRunningTime = -1.;
		}
		void SetLimitedTickTime(double InTickStartTime, double InMaxRunningTime)
		{
			TickStartTime = InTickStartTime;
			MaxRunningTime = InMaxRunningTime;
		}
		double GetTickStartTime() const { return TickStartTime; }
		bool IsTimeSlicingEnabled() const { return TickStartTime > 0; }
		bool WasInterrupted() const { return OutInterrupted; }
		bool ShouldExitEarly();
		void RequestEarlyExit() { OutInterrupted = true; }

	private:
		// A negative value disables time slicing
		double TickStartTime = -1.;
		// The maximum time should allow before interruption. If TickStartTime is negative, this is ignored
		double MaxRunningTime = -1.;
		// If provided, this is always checked
		ShouldExitEarlyCallbackType EarlyExitCallback;
		// True if we ran out of time, EarlyExitCallback returned true, or RequestEarlyExit() was called
		bool OutInterrupted = false;
	};
} // namespace Impl within namespace UE::AssetRegistry
} // namespace UE::AssetRegistry

/** Returns true if ANY work remains to be done. This work might require the game thread. */
FORCEINLINE bool IsTickActive(UE::AssetRegistry::Impl::EGatherStatus Status) 
{ 
	return (Status == UE::AssetRegistry::Impl::EGatherStatus::TickGameThreadActiveGatherIdle) 
		|| (Status == UE::AssetRegistry::Impl::EGatherStatus::TickActiveGatherActive)
		|| (Status == UE::AssetRegistry::Impl::EGatherStatus::TickActiveGatherIdle);
}

namespace UE::AssetRegistry
{

/**
 * Threading helper class for UAssetRegistryImpl that holds all of the data.
 * This class is not threadsafe; all const functions must be called from within a ReadLock and all non-const 
 * from within a WriteLock.
 */
class FAssetRegistryImpl
{
	using FAssetDataMap = UE::AssetRegistry::Private::FAssetDataMap;
	using FCachedAssetKey = UE::AssetRegistry::Private::FCachedAssetKey;

public:
	/** Constructor initializes as little as possible; meaningful operations are first done during Initialize. */
	FAssetRegistryImpl();
	/** Construct the AssetRegistryImpl, including initial scans if applicable. */
	void Initialize(Impl::FInitializeContext& Context);

	// Helpers for functions of the same name from UAssetRegistryImpl


	bool HasAssets(const FName PackagePath, const bool bRecursive) const;
	FSoftObjectPath GetRedirectedObjectPath(const FSoftObjectPath& ObjectPath, UE::AssetRegistry::Impl::FEventContext* EventContext, 
		UE::AssetRegistry::Impl::FClassInheritanceContext* InheritanceContext, bool bNeedsScanning);
	bool GetAncestorClassNames(Impl::FClassInheritanceContext& InheritanceContext, FTopLevelAssetPath ClassName,
		TArray<FTopLevelAssetPath>& OutAncestorClassNames) const;
	void CompileFilter(Impl::FClassInheritanceContext& InheritanceContext, const FARFilter& InFilter,
		FARCompiledFilter& OutCompiledFilter) const;
	void SetTemporaryCachingMode(bool bEnable);
	void SetTemporaryCachingModeInvalidated();
	bool AddPath(Impl::FEventContext& EventContext, FStringView PathToAdd);
	void SearchAllAssets(Impl::FEventContext& EventContext, Impl::FClassInheritanceContext& InheritanceContext,
		bool bSynchronousSearch);
	bool GetVerseFilesByPath(FName PackagePath, TArray<FName>* OutFilePaths, bool bRecursive) const;
	void ScanPathsSynchronous(Impl::FScanPathContext& Context);
	void PrioritizeSearchPath(const FString& PathToPrioritize);
	void ScanModifiedAssetFiles(Impl::FEventContext& EventContext, Impl::FClassInheritanceContext& InheritanceContext,
		const TArray<FString>& InFilePaths, UE::AssetRegistry::EScanFlags ScanFlags);
	void Serialize(FArchive& Ar, Impl::FEventContext& EventContext);
	void AppendState(Impl::FEventContext& EventContext, const FAssetRegistryState& InState,
		FAssetRegistryState::EInitializationMode Mode = FAssetRegistryState::EInitializationMode::Append, bool bEmitAssetEvents = false);
	void GetAllocatedSize(bool bLogDetailed, SIZE_T& StateSize, SIZE_T& StaticSize, SIZE_T& SearchSize) const;
	bool IsLoadingAssets() const;
	void SetManageReferences(const TMultiMap<FAssetIdentifier, FAssetIdentifier>& ManagerMap,
		bool bClearExisting, UE::AssetRegistry::EDependencyCategory RecurseType,
		TSet<FDependsNode*>& ExistingManagedNodes, IAssetRegistry::ShouldSetManagerPredicate ShouldSetManager);
	bool SetPrimaryAssetIdForObjectPath(Impl::FEventContext& EventContext, const FSoftObjectPath& ObjectPath, FPrimaryAssetId PrimaryAssetId);
	bool ResolveRedirect(const FString& InPackageName, FString& OutPackageName) const;
#if WITH_EDITOR
	void OnDirectoryChanged(Impl::FEventContext& EventContext, Impl::FClassInheritanceContext& InheritanceContext,
		TArray<FFileChangeData>& FileChangesProcessed);
	void OnDirectoryRescanRequired(Impl::FEventContext& EventContext,
		Impl::FClassInheritanceContext& InheritanceContext, FString& DirPath, int64 BeforeTimeStamp);
	void AddLoadedAssetToProcess(const UObject& AssetLoaded);
#endif
	void OnContentPathMounted(Impl::FEventContext& EventContext, Impl::FClassInheritanceContext& InheritanceContext,
		const FString& InAssetPath, const FString& AssetPathWithTrailingSlash, const FString& FileSystemPath);
	void OnContentPathDismounted(Impl::FEventContext& EventContext, const FString& InAssetPath,
		const FString& AssetPathNoTrailingSlash, const FString& FileSystemPath);


	// Other helper functions called by UAssetRegistryImpl


	/**
	 * Update cached values about classes, and enable some global multithreaded access,
	 * when all startup plugins finish loading.
	 */
	void OnPluginLoadingComplete(bool bPhaseSuccessful);
	/** Update cached values about native classes after they have possibly changed. */
	void RefreshNativeClasses();

	/** Enumerate assets in the State, filtering by filter and package not in PackagesToSkip */
	void EnumerateDiskAssets(const FARCompiledFilter& InFilter, TSet<FName>& PackagesToSkip,
		TFunctionRef<bool(const FAssetData&)> Callback, UE::AssetRegistry::EEnumerateAssetsFlags InEnumerateFlags) const;
	/** Enumerate all assets in the State. PackageNamesToSkip are filtered. EEnumerateAssetsFlags provide additional filtering. */
	void EnumerateAllDiskAssets(TSet<FName>& PackageNamesToSkip, TFunctionRef<bool(const FAssetData&)> Callback, UE::AssetRegistry::EEnumerateAssetsFlags InEnumerateFlags = UE::AssetRegistry::EEnumerateAssetsFlags::None) const;

	/** Waits for the gatherer to be idle if it is operating synchronously. */
	void WaitForGathererIdleIfSynchronous();
	/** Waits for the gatherer to be idle. */
	void WaitForGathererIdle(float TimeoutSeconds);
	/** Consume any results from the gatherer and return its status */
	Impl::EGatherStatus TickGatherer(Impl::FTickContext& TickContext);

	/**
	 * At some times during editor startup, depending on config settings, we might want to block
	 * on the gather, and not tick the rest of the editor. When we want that to happen, we set
	 * this flag. This function can be read outside of the lock (we need to do that during Tick).
	 */
	bool IsGameThreadTakeOverGatherEachTick() const;
	void SetGameThreadTakeOverGatherEachTick(bool bValue);

	/** Send a log message with the search statistics. 
	 *  StartTime is used to report wall clock search time in the case of background scan
	 */
	void LogSearchDiagnostics(double StartTime);
	/** Look for and load a single AssetData result from the gatherer. */
	void TickGatherPackage(Impl::FEventContext& EventContext, const FString& PackageName, const FString& LocalPath);
	void ClearGathererCache();
#if WITH_EDITOR
	void AssetsSaved(UE::AssetRegistry::Impl::FEventContext& EventContext, TArray<FAssetData>&& Assets);
	void GetProcessLoadedAssetsBatch(TArray<const UObject*>& OutLoadedAssets, uint32 BatchSize,
		bool bUpdateDiskCacheAfterLoad);
	void PushProcessLoadedAssetsBatch(Impl::FEventContext& EventContext,
		TArrayView<FAssetData> LoadedAssetDatas, TArrayView<const UObject*> UnprocessedFromBatch);
	/** Call LoadCalculatedDependencies on each Package updated after the last LoadCalculatedDependencies. */
	void LoadCalculatedDependencies(TArray<FName>* AssetPackageNamessToCalculate, Impl::FClassInheritanceContext& InheritanceContext, 
		TSet<FName>* InPackagesNeedingDependencyCalculation, Impl::FInterruptionContext& InOutInterruptionContext);
	/** For each package in BackgroundPackages, if there is a registered gatherer for its asset class, this function will 
	 *  move the package to GameThreadPackages otherwise it will be removed from the set. Any entries in GameThreadPackages that
	 *  do not have a corresponding registered gatherer will likewise be removed. The end condition for this function is that 
	 *  BackgroundPackages is empty and GameThreadPackages contains only those packages which require LoadCalculatedDependencies to
	 *  be called on them
	 */
	void PruneAndCoalescePackagesRequiringDependencyCalculation(TSet<FName>& BackgroundPackages, TSet<FName>& GameThreadPackages,
		Impl::FInterruptionContext& InOutInterruptionContext);

	/** Look for a CalculatedDependencies function registered for the asset(s) in the given package
	 *  and call that function to add calculated dependencies. Calculated dependencies are added only after
	 *  all normal dependencies gathered from the AssetRegistry data stored in the package have been loaded.
	 */
	void LoadCalculatedDependencies(FName PackageName, Impl::FClassInheritanceContext& InheritanceContext,
		bool& bOutHadActivity);
	/** Add a watch on a directory that to modify data for a package whenever packages in the directory are modified. */
	void AddDirectoryReferencer(FName PackageName, const FString& DirectoryLocalPathOrLongPackageName);
	/** Remove all directory watches for PackageName. */
	void RemoveDirectoryReferencer(FName PackageName);

	/** Called when new gatherer is registered. Requires subsequent call to RebuildAssetDependencyGathererMapIfNeeded */
	void OnAssetDependencyGathererRegistered() { bRegisteredDependencyGathererClassesDirty = true; }
#endif

	/** Adds an asset to the empty package list which contains packages that have no assets left in them */
	void AddEmptyPackage(FName PackageName);
	/** Removes an asset from the empty package list because it is no longer empty */
	bool RemoveEmptyPackage(FName PackageName);
	/** Adds a path to the cached paths tree. Returns true if the path was added and did not already exist */
	bool AddAssetPath(Impl::FEventContext& EventContext, FName PathToAdd);
	/** Removes a path to the cached paths tree. Returns true if successful. */
	bool RemoveAssetPath(Impl::FEventContext& EventContext, FName PathToRemove, bool bEvenIfAssetsStillExist = false);
	/** Removes the asset data associated with this package from the look-up maps */
	void RemovePackageData(Impl::FEventContext& EventContext, const FName PackageName);
	/** Adds the Verse file to the look up maps */
	void AddVerseFile(Impl::FEventContext& EventContext, FName VerseFilePathToAdd);
	/** Removes the Verse file from the look-up maps */
	void RemoveVerseFile(Impl::FEventContext& EventContext, FName VerseFilePathToRemove);

	/** Returns the names of all subclasses of the class whose name is ClassName */
	void GetSubClasses(Impl::FClassInheritanceContext& InheritanceContext, const TArray<FTopLevelAssetPath>& InClassNames,
		const TSet<FTopLevelAssetPath>& ExcludedClassNames, TSet<FTopLevelAssetPath>& SubClassNames) const;

	bool IsInitialSearchCompleted() const { return bInitialSearchCompleted.load(std::memory_order_relaxed); }
	bool IsTempCachingEnabled() const { return bIsTempCachingEnabled; }
	bool IsTempCachingAlwaysEnabled() const { return bIsTempCachingAlwaysEnabled; }
	bool IsInitialSearchStarted() const { return bInitialSearchStarted; }
	bool IsSearchAllAssets() const { return bSearchAllAssets; }
	Impl::FClassInheritanceBuffer& GetTempCachedInheritanceBuffer() { return TempCachedInheritanceBuffer; }
	uint64 GetSavedGeneratorClassesVersionNumber() const { return SavedGeneratorClassesVersionNumber; }
	uint64 GetSavedAllClassesVersionNumber() const { return SavedAllClassesVersionNumber; }
	static uint64 GetCurrentGeneratorClassesVersionNumber();
	static uint64 GetCurrentAllClassesVersionNumber();

	/** Get a copy of the cached serialization options that were parsed from ini */
	void CopySerializationOptions(FAssetRegistrySerializationOptions& OutOptions, ESerializationTarget Target) const;
	
	/** Query the performance mode, which modifies how data structures are loaded */
	Impl::EPerformanceMode GetPerformanceMode() const { return PerformanceMode; }
	void SetPerformanceMode(Impl::EPerformanceMode NewMode);
	bool ShouldSortDependencies() const;
	bool ShouldSortReferencers() const;

	const FAssetRegistryState& GetState() const;
	const FPathTree& GetCachedPathTree() const;
	const TSet<FName>& GetCachedEmptyPackages() const;

	/** Same as UE::AssetRegistry::Filtering::ShouldSkipAsset, but can be read from any thread under readlock */
	bool ShouldSkipAsset(FTopLevelAssetPath AssetClass, uint32 PackageFlags) const;
	bool ShouldSkipAsset(const UObject* InAsset) const;

	/** Finds all class names of classes capable of generating new UClasses */
	void CollectCodeGeneratorClasses();

	/**
	 * Block until the Premade AssetRegistry finishes loading, if there is one still loading.
	 * For performance reasons, this must be called within the WriteScopeLock, and the caller 
	 * must handle the possibility that it leaves and reenters the lock.
	 */
	void ConditionalLoadPremadeAssetRegistry(UAssetRegistryImpl& UARI, 
		UE::AssetRegistry::Impl::FEventContext& EventContext, FInterfaceWriteScopeLock& ScopeLock);

#if WITH_EDITOR
	/** Request to pause or resume background processing of scan results.
	 *  This can be used to allow a priority thread to perform along sequence of operations
	 *  without having to contend with the background thread for data access
	 */
	void RequestPauseBackgroundProcessing();
	void RequestResumeBackgroundProcessing();
	bool IsBackgroundProcessingPaused() const;
	uint32& GetBackgroundTickInterruptionsCount();

#endif
private:

	/**
	 * Enumerate all AssetData in memory and in the State that match the given path.
	 * The returned AssetData for objects present in memory have no AssetRegistryTags calculated
	 */
	void EnumerateAssetsByPathNoTags(FName PackagePath,
		TFunctionRef<bool(const FAssetData&)> Callback, bool bRecursive, bool bIncludeOnlyOnDiskAssets) const;
	/** Create redirectors read by this->GetRedirectedObjectPath, based on plugin settings */
	void InitRedirectors(Impl::FEventContext& EventContext, Impl::FClassInheritanceContext& InheritanceContext,
		bool& bOutRedirectorsNeedSubscribe);
	/** If collecting dependencies, create an FAssetPackageData for every script package, to make sure dependencies can find a data for them. */
	void ReadScriptPackages();
	/* Try to construct the gatherer if it does not already exist. If that is not possible (e.g., because we are shutting down)
	 * return false. If it was successfully created or already existed, return true.
	 */
	bool TryConstructGathererIfNeeded();
	/**  Called to set up timing variables and launch the on-constructor SearchAllAssets async call */
	void SearchAllAssetsInitialAsync(Impl::FEventContext& EventContext, Impl::FClassInheritanceContext& InheritanceContext);
	/**
	 * Called every tick to when data is retrieved by the background asset search.
	 * If TickStartTime is < 0, the entire list of gathered assets will be cached. Also used in sychronous searches
	 * The DeferredResults array contains assets that were not ready for processing due to required UClass's not being loaded
	 */
	void AssetSearchDataGathered(Impl::FEventContext& EventContext,
		TMultiMap<FName, TUniquePtr<FAssetData>>& AssetResults, 
		TMultiMap<FName, TUniquePtr<FAssetData>>& OutDeferredResults,\
		Impl::FInterruptionContext& InOutInterruptionContext);
	/** Validate assets gathered from disk before adding them to the AssetRegistry. */
	bool ShouldSkipGatheredAsset(FAssetData& AssetData);

	/**
	 * Called every tick when data is retrieved by the background path search.
	 * If TickStartTime is < 0, the entire list of gathered assets will be cached. Also used in sychronous searches
	 */
	void PathDataGathered(Impl::FEventContext& EventContext, TRingBuffer<FString>& PathResults, Impl::FInterruptionContext& InOutInterruptionContext);

	/** Called every tick when data is retrieved by the background dependency search */
	void DependencyDataGathered(TMultiMap<FName, FPackageDependencyData>& DependsResults, 
		TMultiMap<FName, FPackageDependencyData>& OutDeferredDependencyResults,	TSet<FName>* OutPackagesNeedingDependencyCalculation, 
		Impl::FInterruptionContext& InOutInterruptionContext);

	/** Called every tick when data is retrieved by the background search for cooked packages that do not contain asset data */
	void CookedPackageNamesWithoutAssetDataGathered(Impl::FEventContext& EventContext, 
		TRingBuffer<FString>& CookedPackageNamesWithoutAssetDataResults, Impl::FInterruptionContext& InOutInterruptionContext);

	/** Called every tick when data is retrieved by the background dependency search */
	void VerseFilesGathered(Impl::FEventContext& EventContext, TRingBuffer<FName>& VerseResults, Impl::FInterruptionContext& InOutInterruptionContext);

	/** Adds the asset data to the lookup maps */
	void AddAssetData(Impl::FEventContext& EventContext, FAssetData* AssetData);

	/** Updates an existing asset data with the new value and updates lookup maps */
	void UpdateAssetData(Impl::FEventContext& EventContext, FAssetData* AssetData, FAssetData&& NewAssetData, bool bKeepDeletedTags);

	/** Updates the tags on an existing asset data by adding the tags from NewAssetData that do not already exist. */
	void AddNonOverlappingTags(Impl::FEventContext& EventContext, FAssetData& ExistingAssetData,
		const FAssetData& NewAssetData);

	/** Removes the asset data from the lookup maps */
	bool RemoveAssetData(Impl::FEventContext& EventContext, FAssetData* AssetData);

#if WITH_EDITOR
	/** Called when two assets have the same ID but different packages. Logs it and returns which one to keep. */
	FAssetData* ResolveAssetIdCollision(FAssetData& A, FAssetData& B);
	/**
	 * Calls PostLoadAssetRegistryTags on the CDO of the asset class this data represents.
	 * @param AssetData Existing asset data
	 * @return Returns false if the required parent UClass is not yet available
	 */
	bool TryPostLoadAssetRegistryTags(FAssetData* AssetData);

	/** Update Redirect collector with redirects loaded from asset registry */
	void UpdateRedirectCollector();

	/** If new gatherers were registered, this call will refresh the mapping */
	void RebuildAssetDependencyGathererMapIfNeeded();
#endif // WITH_EDITOR
	void GetSubClasses_Recursive(Impl::FClassInheritanceContext& InheritanceContext, FTopLevelAssetPath InClassName,
		TSet<FTopLevelAssetPath>& SubClassNames, TSet<FTopLevelAssetPath>& ProcessedClassNames, const TSet<FTopLevelAssetPath>& ExcludedClassNames) const;

	/** Internal helper which processes a given state and adds its contents to the current registry */
	void CachePathsFromState(Impl::FEventContext& EventContext, const FAssetRegistryState& InState);

	/** Adds ClassPath-> ParentPath to CachedBPInheritanceMap. Applies CoreRedirects to NotYetRedirectedParentPath. */
	void AddCachedBPClassParent(const FTopLevelAssetPath& ClassPath, const FTopLevelAssetPath& NotYetRedirectedParentPath);

	/** Updates OutBuffer from loaded classes and registered-in-CachedBPInheritanceMap blueprint classes */
	void UpdateInheritanceBuffer(Impl::FClassInheritanceBuffer& OutBuffer) const;

	/** Conditionally loads the Premade AssetRegistry from disk, or queues it for asynchronous loading if not yet loaded. */
	void ConsumeOrDeferPreloadedPremade(UAssetRegistryImpl& UARI, Impl::FEventContext& EventContext);
	/** Moves a premade asset registry state into this AR */
	void LoadPremadeAssetRegistry(Impl::FEventContext& Context,
		Premade::ELoadResult LoadResult, FAssetRegistryState&& ARState);
	/** Add MountPoints of all AssetDatas currently registered in this->State to the list of PersistentMountPoints. */
	void UpdatePersistentMountPoints();
	void OnInitialSearchCompleted(Impl::FEventContext& EventContext);

	/** This function is not called and, currently, will always return 'true'. See the commented out call in
	 *	FAssetDataGatherer::TickInternal for further details on how it could be used. 
	 */
	friend FAssetDataGatherer;
	bool ClassRequiresGameThreadProcessing(const UClass* Class) const;

	friend UAssetRegistryImpl;
	/** This exists purely for use during shutdown to enable the UAssetRegistryImpl to
	 *  avoid waiting for the gatherer to terminate while holding the interface lock.
	 *  See UAssetRegistryImpl::OnEnginePreExit 
	 */
	TUniquePtr<FAssetDataGatherer>& AccessGlobalGatherer() { return GlobalGatherer; }

	void UpdateMaxSecondsPerFrame();

private:

	/** Internal state of the cached asset registry */
	FAssetRegistryState State;

	/** Database of known Verse files */
	TSet<FName> CachedVerseFiles;
	TMap<FName, TArray<FName>> CachedVerseFilesByPath;

	/** Default options used for serialization */
	FAssetRegistrySerializationOptions SerializationOptions;
	FAssetRegistrySerializationOptions DevelopmentSerializationOptions;

	/** The set of empty package names (packages which contain no assets but have not yet been saved) */
	TSet<FName> CachedEmptyPackages;

	/** The map of classes to their parents, only full for offline blueprints */
	TMap<FTopLevelAssetPath, FTopLevelAssetPath> CachedBPInheritanceMap;

	/** If true, search caching is enabled */
	bool bIsTempCachingEnabled;

	/** If true, search caching is enabled permanently */
	bool bIsTempCachingAlwaysEnabled;

	/** Persistent InheritanceBuffer used when SetTemporaryCachingMode is on */
	Impl::FClassInheritanceBuffer TempCachedInheritanceBuffer;

	uint64 SavedGeneratorClassesVersionNumber;
	uint64 SavedAllClassesVersionNumber;

	/** The tree of known cached paths that assets may reside within */
	FPathTree CachedPathTree;

	/** Async task that gathers asset information from disk */
	TUniquePtr<FAssetDataGatherer> GlobalGatherer;

	/** Lists of results from the gatherer thread that are waiting to get processed */
	FAssetDataGatherer::FResults BackgroundResults;

	/** Assets and dependencies that are not ready for processing because they can't yet run PostLoadAssetRegistrytags */
	TMultiMap<FName, TUniquePtr<FAssetData>> DeferredAssets;
	TMultiMap<FName, TUniquePtr<FAssetData>> DeferredAssetsForGameThread;
	TMultiMap<FName, FPackageDependencyData> DeferredDependencies;
	TMultiMap<FName, FPackageDependencyData> DeferredDependenciesForGameThread;

#if !NO_LOGGING
	/** Memory profiling information: How much memory is being used by the tags for each class. */
	TMap<FTopLevelAssetPath, int64> TagSizeByClass;
#endif

	/**
	 * MountPoints, in the format of LongPackageName with no trailing slash, that should not have their
	 * AssetDatas removed even if the MountPoint is dismounted.
	 */
	TSet<FName> PersistentMountPoints;

	/** Time spent processing Gather results */
	float StoreGatherResultsTimeSeconds;
	/** The highest number of pending results observed during initial gathering */
	int32 HighestPending = 0;

	/** Time the initial async search was started */
	double InitialSearchStartTime = 0.0f;
	/** Flag to indicate if we used an initial async search */
	bool bInitialSearchStarted;
	/** Flag to indicate if the initial background search has completed. All access are relaxed because
	 *  the actual search data can only be accessed under a proper lock.
	 */
	std::atomic<bool> bInitialSearchCompleted;
	/**
	 * Flag to indicate PreloadingComplete; finishing the background search is blocked until preloading complete
	 * because preloading can add assets.
	 */
	bool bPreloadingComplete = false;
	/** Status of the background search, so we can take actions when it changes to or from idle */
	Impl::EGatherStatus GatherStatus;
	/** What kind of performance mode this is in, used to optimize for initial loading vs runtime */
	Impl::EPerformanceMode PerformanceMode;

	/**
	 * Enables extra check to make sure path still mounted before adding.
	 * Removing mount point can happen between scan (background thread + multiple ticks and the add).
	 */
	bool bVerifyMountPointAfterGather;

	/** Record whether SearchAllAssets has been called; if so we will also search new mountpoints when added */
	bool bSearchAllAssets;

	bool bVerboseLogging;

	bool bForceCompletionEvenIfPostLoadsFail = false;
	bool bProcessedAnyAssetsAfterRetryDeferred = true;
	float MaxSecondsPerFrame = 0.04f;

	/** List of all class names derived from Blueprint (including Blueprint itself) */
	TSet<FTopLevelAssetPath> ClassGeneratorNames;

	struct FAssetRegistryPackageRedirect
	{
	public:
		FAssetRegistryPackageRedirect(const FString& InSourcePackageName, const FString& InDestPackageName)
			: SourcePackageName(InSourcePackageName), DestPackageName(InDestPackageName)
		{
		}
		FString SourcePackageName;
		FString DestPackageName;
	};
	TArray<FAssetRegistryPackageRedirect> PackageRedirects;

#if WITH_EDITOR
	/**
	 * The set of object paths that have had their dependencies gathered since the last idle,
	 * and that need to check for calculated dependencies at the next idle.
	 */
	TSet<FName> PackagesNeedingDependencyCalculation;
	TSet<FName> PackagesNeedingDependencyCalculationOnGameThread;

	/** List of objects that need to be processed because they were loaded or saved */
	TRingBuffer<TWeakObjectPtr<const UObject>> LoadedAssetsToProcess;

	/** The set of object paths that have had their disk cache updated from the in memory version */
	TSet<FSoftObjectPath> AssetDataObjectPathsUpdatedOnLoad;

	/** A map from directoryname to packagename of Packages that have CalculatedDependencies on packages in the directory. */
	TMultiMap<FString, FName> DirectoryReferencers;

	/**
	 * Number of times during the startup scan that TickOnBackgroundThread was interrupted by a request for the
	 * AssetRegistry's lock from another thread.
	 */
	uint32 BackgroundTickInterruptionsCount = 0;
	std::atomic<bool> bGameThreadTakeOverGatherEachTick;

	/** A map of per asset class dependency gatherer called in LoadCalculatedDependencies */
	TMultiMap<FTopLevelAssetPath, UE::AssetDependencyGatherer::Private::FRegisteredAssetDependencyGatherer*> RegisteredDependencyGathererClasses;
	mutable FRWLock RegisteredDependencyGathererClassesLock;
	bool bRegisteredDependencyGathererClassesDirty;
#endif
#if WITH_ENGINE && WITH_EDITOR
	/** Class names that return true for IsAsset but which should not be treated as assets in uncooked packages */
	TSet<FTopLevelAssetPath> SkipUncookedClasses;
	/** Class names that return true for IsAsset but which should not be treated as assets in cooked packages */
	TSet<FTopLevelAssetPath> SkipCookedClasses;
#endif
	UE::AssetRegistry::Premade::FAsyncConsumer AsyncConsumer;
	friend struct UE::AssetRegistry::Premade::FAsyncConsumer;
	friend struct Impl::FClassInheritanceContext;
};

namespace Impl
{

/**
 * Stores events that need to be broadcasted from the AssetRegistry; these events are added to 
 * by functions within a lock, and are broadcast after the lock is dropped. The broadcast is also deferred
 * until on the GameThread.
 */
struct FEventContext
{
	enum class EEvent : uint32
	{
		Added,
		Removed,
		Updated,
		UpdatedOnDisk,
		
		MAX
	};
	TOptional<IAssetRegistry::FFileLoadProgressUpdateData> ProgressUpdateData;
	TArray<TPair<FString, EEvent>> PathEvents;
	TArray<TPair<FAssetData, EEvent>> AssetEvents;
	TArray<TPair<FName, EEvent>> VerseEvents;
	TArray<FString> RequiredLoads;
	TArray<FString> BlockedFiles;
	bool bFileLoadedEventBroadcast = false;
	bool bScanStartedEventBroadcast = false;
	bool bHasSentFileLoadedEventBroadcast = false;

	/** Remove all stored events */
	void Clear();
	/** Report whether this context has any stored events */
	bool IsEmpty() const;
	/** Add all events from other onto this context's collection of events */
	void Append(FEventContext&& Other);
};


/*
 * An accessor for the inheritance map and related data for functions that need them; the source
 * of this data is an internal detail unneeded by the functions reading it. Readers of the data call
 * ConditionalUpdate to ensure it is up to date before reading it.
 */
struct FClassInheritanceContext
{
	/** The Buffer providing the data for this context. */
	FClassInheritanceBuffer* Buffer = nullptr;
	/** Back pointer to the AssetRegistryImpl to update the Buffer if necessary */
	FAssetRegistryImpl* AssetRegistryImpl = nullptr;
	/** Whether ConditionalUpdate needs to update inheritance data */
	bool bInheritanceMapUpToDate = false;
	/** Whether ConditionalUpdate needs to update code generate classes before it can update inheritance data */
	bool bCodeGeneratorClassesUpToDate = false;

	/** Set the context to use the data from the given buffer */
	void BindToBuffer(FClassInheritanceBuffer& InBuffer, FAssetRegistryImpl& InAssetRegistryImpl,
		bool bInInheritanceMapUpToDate, bool bInCodeGeneratorClassesUpToDate);
	/** Update the backing buffer if it is out of date */
	void ConditionalUpdate();
};

/** Context to transform, collect, and pass along ScanPathsSynchronous arguments and results */
struct FScanPathContext
{
	FScanPathContext(FEventContext& InEventContext, FClassInheritanceContext& InInheritanceContext,
		const TArray<FString>& InDirs, const TArray<FString>& InFiles,
		UE::AssetRegistry::EScanFlags InScanFlags = UE::AssetRegistry::EScanFlags::None,
		TArray<FSoftObjectPath>* FoundAssets = nullptr);

	TArray<FString> PackageDirs;
	TArray<FString> LocalDirs;
	TArray<FString> PackageFiles;
	TArray<FString> LocalFiles;
	TArray<FString> LocalPaths;
	FEventContext& EventContext;
	FClassInheritanceContext& InheritanceContext;
	TArray<FSoftObjectPath>* OutFoundAssets = nullptr;
	int32 NumFoundAssets = 0;
	bool bForceRescan = false;
	bool bIgnoreDenyListScanFilters = false;
	bool bIgnoreInvalidPathWarning = false;
	EGatherStatus Status = EGatherStatus::TickActiveGatherActive;
};

/** Callback types for FTickContext */
typedef TFunctionRef<void(const TMultiMap<FName, FAssetData*>&)> FAssetsFoundCallback;
typedef TFunctionRef<void(const TRingBuffer<FName>&)> FVerseFilesFoundCallback;

/** Input and output variables for the TickGatherer function. */
struct FTickContext
{
	FTickContext(Impl::FEventContext& InEventContext, Impl::FClassInheritanceContext& InInheritanceContext)
		: EventContext(InEventContext)
		, InheritanceContext(InInheritanceContext)
	{
	}
	Impl::FInterruptionContext InterruptionContext;
	Impl::FEventContext& EventContext;
	Impl::FClassInheritanceContext& InheritanceContext;
	TOptional<FAssetsFoundCallback> AssetsFoundCallback;
	TOptional<FVerseFilesFoundCallback> VerseFilesFoundCallback;
	bool bHandleCompletion = false;
	bool bHandleDeferred = false;
};

} // namespace Impl within namespace UE::AssetRegistry

namespace Utils
{

/** Different modes for RunAssetThroughFilter and related filter functions */
enum class EFilterMode : uint8
{
	/** Include things that pass the filter; include everything if the filter is empty */
	Inclusive,
	/** Exclude things that pass the filter; exclude nothing if the filter is empty */
	Exclusive,
};

/** Report whether the external-code-created filter is in a state that is safe to use in filtering */
bool IsFilterValid(const FARCompiledFilter& Filter);
/** Report whether the given AssetData passes/fails the given filter */
bool RunAssetThroughFilter(const FAssetData& AssetData, const FARCompiledFilter& Filter, const EFilterMode FilterMode);
/** Helper for RunAssetThroughFilter and RunAssetsThroughFilter that skips validity check */
bool RunAssetThroughFilter_Unchecked(const FAssetData& AssetData, const FARCompiledFilter& Filter, const bool bPassFilterValue);
/**
 * Given an array of asset data, trim the items that fail the filter based on the inclusion/exclusion mode used.
 *  - In inclusive mode it will remove all assets that fail the filter, and in exclusive mode it will remove all assets that pass the filter.
 *  - If the filter is empty, then the array will be untouched.
 */
void RunAssetsThroughFilter(TArray<FAssetData>& AssetDataList, const FARCompiledFilter& Filter, const EFilterMode FilterMode);

/** This will always read the ini, public version may return cache */
void InitializeSerializationOptionsFromIni(FAssetRegistrySerializationOptions& Options, const FString& PlatformIniName,
	ESerializationTarget Target = ESerializationTarget::ForGame);

/** Gets the current availability of an asset, primarily for streaming install purposes. */
EAssetAvailability::Type GetAssetAvailability(const FAssetData& AssetData);
/** Gets an ETA or percentage complete for an asset that is still in the process of being installed. */
float GetAssetAvailabilityProgress(const FAssetData& AssetData, EAssetAvailabilityProgressReportingType::Type ReportType);
/** Returns if a given report type is supported on the current platform */
bool GetAssetAvailabilityProgressTypeSupported(EAssetAvailabilityProgressReportingType::Type ReportType);
/**  Hint the streaming installers to prioritize a specific asset for install. */
void PrioritizeAssetInstall(const FAssetData& AssetData);

/**
 * Reads Asset file from a previously initialized package reader
 *
 * @param PackageReader	Previously initialized package reader that maps to the data to be read
 * @param AssetDataList	List of the read asset data
 */
bool ReadAssetFile(FPackageReader& PackageReader, IAssetRegistry::FLoadPackageRegistryData& InOutData);

/**
 * Returns true if path belongs to one of the mount points provided
 *
 * @param	Path				Path to check if mounted, example "/MyPlugin/SomeAsset"
 * @param	MountPointsNoTrailingSlashes		Mount points without the trailing slash. Example: "/MyPlugin"
 * @param	StringBuffer		String buffer to avoid re-allocation performance hit when searching TSet
 */
bool IsPathMounted(const FString& Path, const TSet<FString>& MountPointsNoTrailingSlashes, FString& StringBuffer);

/**
 * Iterate all UObjects where this->IsAsset and !ShouldSkipAsset, create an FAssetData for them and call the callback.
 * Stores packagenames of all assets found in OutPackageNamesWithAssets for later filtering of on-disk assets.
 * Stops iteration and sets bOutStopIteration=true iff the callback returns false.
 */
void EnumerateAllMemoryAssets(TSet<FName>& OutPackageNamesWithAssets, bool& bOutStopIteration, TFunctionRef<bool(FAssetData&&)> Callback);
/**
 * Helper for EnumerateMemoryAssets* functions. Calls the given callback on all memory assets that pass the portion of the
 * given filter that does not require tags, and passes in an FAssetData constructed from the UObject but missing tags.
 * Fills in OutPackageNamesWithAssets with names of all packages tested.
 */
void EnumerateMemoryAssetsHelper(const FARCompiledFilter& InFilter, TSet<FName>& OutPackageNamesWithAssets,
	bool& bOutStopIteration, TFunctionRef<bool(const UObject* Object, FAssetData&& PartialAssetData)> Callback,
	bool bSkipARFilteredAssets);
/**
 * Call the given callback on all UObjects in memory that pass the given filter.
 * Fills in OutPackageNamesWithAssets with names of all packages tested.
*/
void EnumerateMemoryAssets(const FARCompiledFilter& InFilter, TSet<FName>& OutPackageNamesWithAssets,
	bool& bOutStopIteration, UE::AssetRegistry::Private::FInterfaceRWLock& InterfaceLock, const FAssetRegistryState& GuardedDataState,
	TFunctionRef<bool(FAssetData&&)> Callback, bool bSkipARFilteredAssets);

} // namespace Utils within namespace UE::AssetRegistry


///////////////////////////////////////////////////////
// Inline implementations
///////////////////////////////////////////////////////


#if WITH_EDITOR
inline uint32& FAssetRegistryImpl::GetBackgroundTickInterruptionsCount()
{
	return BackgroundTickInterruptionsCount;
}

inline bool FAssetRegistryImpl::IsGameThreadTakeOverGatherEachTick() const
{
	return bGameThreadTakeOverGatherEachTick.load(std::memory_order_relaxed);
}

inline void FAssetRegistryImpl::SetGameThreadTakeOverGatherEachTick(bool bValue)
{
	bGameThreadTakeOverGatherEachTick.store(bValue, std::memory_order_relaxed);
}
#endif


} // namespace UE::AssetRegistry
