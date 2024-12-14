// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "HairDescription.h"
#include "HairStrandsDatas.h"
#include "RenderResource.h"
#include "GroomResources.h"
#include "HairStrandsInterface.h"
#include "Async/AsyncWork.h"
#include "Engine/SkeletalMesh.h"
#include "GroomBindingAsset.generated.h"


class UAssetUserData;
class UGeometryCache;
class UMaterialInterface;
class UNiagaraSystem;
class UGroomAsset;

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FGoomBindingGroupInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Curve Count"))
	int32 RenRootCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Curve LOD"))
	int32 RenLODCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Guide Count"))
	int32 SimRootCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Info", meta = (DisplayName = "Guide LOD"))
	int32 SimLODCount = 0;
};

/** Enum that describes the type of mesh to bind to */
UENUM(BlueprintType)
enum class EGroomBindingMeshType : uint8
{
	SkeletalMesh,
	GeometryCache
};

/*-----------------------------------------------------------------------------
	Async GroomBinding Compilation
-----------------------------------------------------------------------------*/

UENUM()
enum class EGroomBindingAsyncProperties : uint64
{
	None = 0,
	GroomBindingType = 1 << 0,
	Groom = 1 << 1,
	SourceSkeletalMesh = 1 << 2,
	TargetSkeletalMesh = 1 << 3,
	SourceGeometryCache = 1 << 4,
	TargetGeometryCache = 1 << 5,
	NumInterpolationPoints = 1 << 6,
	MatchingSection = 1 << 7,
	GroupInfos = 1 << 8,
	HairGroupResources = 1 << 9,
	HairGroupPlatformData = 1 << 10,
	All = MAX_uint64
};

ENUM_CLASS_FLAGS(EGroomBindingAsyncProperties);

enum class EGroomBindingAsyncPropertyLockType
{
	None = 0,
	ReadOnly = 1,
	WriteOnly = 2,
	ReadWrite = 3
};

ENUM_CLASS_FLAGS(EGroomBindingAsyncPropertyLockType);

// Any thread implicated in the build must have a valid scope to be granted access to protected properties without causing any stalls.
class FGroomBindingAsyncBuildScope
{
public:
	HAIRSTRANDSCORE_API FGroomBindingAsyncBuildScope(const UGroomBindingAsset* Asset);
	HAIRSTRANDSCORE_API ~FGroomBindingAsyncBuildScope();
	HAIRSTRANDSCORE_API static bool ShouldWaitOnLockedProperties(const UGroomBindingAsset* Asset);

private:
	const UGroomBindingAsset* PreviousScope = nullptr;
	// Only the thread(s) compiling this asset will have full access to protected properties without causing any stalls.
	static thread_local const UGroomBindingAsset* Asset;
};

struct FGroomBindingBuildContext
{
	FGroomBindingBuildContext() = default;
	// Non-copyable
	FGroomBindingBuildContext(const FGroomBindingBuildContext&) = delete;
	FGroomBindingBuildContext& operator=(const FGroomBindingBuildContext&) = delete;
	// Movable
	FGroomBindingBuildContext(FGroomBindingBuildContext&&) = default;
	FGroomBindingBuildContext& operator=(FGroomBindingBuildContext&&) = default;

	bool bReloadResource = false;
};

/**
 * Worker used to perform async compilation.
 */
class FGroomBindingAsyncBuildWorker : public FNonAbandonableTask
{
public:
	UGroomBindingAsset* GroomBinding;
	TOptional<FGroomBindingBuildContext> BuildContext;

	/** Initialization constructor. */
	FGroomBindingAsyncBuildWorker(
		UGroomBindingAsset* InGroomBinding,
		FGroomBindingBuildContext&& InBuildContext)
		: GroomBinding(InGroomBinding)
		, BuildContext(MoveTemp(InBuildContext))
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FGroomBindingAsyncBuildWorker, STATGROUP_ThreadPoolAsyncTasks);
	}

	void DoWork();
};

struct FGroomBindingAsyncBuildTask : public FAsyncTask<FGroomBindingAsyncBuildWorker>
{
	FGroomBindingAsyncBuildTask(
		UGroomBindingAsset* InGroomBinding,
		FGroomBindingBuildContext&& InBuildContext)
		: FAsyncTask<FGroomBindingAsyncBuildWorker>(InGroomBinding, MoveTemp(InBuildContext))
		, GroomBinding(InGroomBinding)
	{
	}

	const UGroomBindingAsset* GroomBinding;
};

/**
 * Implements an asset that can be used to store binding information between a groom and a skeletal mesh
 */
UCLASS(BlueprintType, hidecategories = (Object))
class HAIRSTRANDSCORE_API UGroomBindingAsset : public UObject, public IInterface_AsyncCompilation
{
	GENERATED_BODY()

#if WITH_EDITOR
	/** Notification when anything changed */
	DECLARE_MULTICAST_DELEGATE(FOnGroomBindingAssetChanged);
#endif

private:
	/** Type of mesh to create groom binding for */
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetGroomBindingType() or UGroomBindingAsset::SetGroomBindingType().")
	UPROPERTY(VisibleAnywhere, BlueprintGetter = GetGroomBindingType, BlueprintSetter = SetGroomBindingType, Category = "BuildSettings")
	EGroomBindingMeshType GroomBindingType = EGroomBindingMeshType::SkeletalMesh;

public:
	static FName GetGroomBindingTypeMemberName();
	UFUNCTION(BlueprintGetter) EGroomBindingMeshType GetGroomBindingType() const;
	UFUNCTION(BlueprintSetter) void SetGroomBindingType(EGroomBindingMeshType InGroomBindingType);

private:
	/** Groom to bind. */
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetGroom() or UGroomBindingAsset::SetGroom().")
	UPROPERTY(EditAnywhere, BlueprintGetter = GetGroom, BlueprintSetter = SetGroom, Category = "BuildSettings", AssetRegistrySearchable)
	TObjectPtr<UGroomAsset> Groom;

public:
	static FName GetGroomMemberName();
	UFUNCTION(BlueprintGetter) UGroomAsset* GetGroom() const;
	UFUNCTION(BlueprintSetter) void SetGroom(UGroomAsset* InGroom);

private:
	/** Skeletal mesh on which the groom has been authored. This is optional, and used only if the hair
		binding is done a different mesh than the one which it has been authored */
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetSourceSkeletalMesh() or UGroomBindingAsset::SetSourceSkeletalMesh().")
	UPROPERTY(EditAnywhere, BlueprintGetter = GetSourceSkeletalMesh, BlueprintSetter = SetSourceSkeletalMesh, Category = "BuildSettings")
	TObjectPtr<USkeletalMesh> SourceSkeletalMesh;

public:
	static FName GetSourceSkeletalMeshMemberName();
	UFUNCTION(BlueprintGetter) USkeletalMesh* GetSourceSkeletalMesh() const;
	UFUNCTION(BlueprintSetter) void SetSourceSkeletalMesh(USkeletalMesh* InSkeletalMesh);

private:
	/** Skeletal mesh on which the groom is attached to. */
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetTargetSkeletalMesh() or UGroomBindingAsset::SetTargetSkeletalMesh().")
	UPROPERTY(EditAnywhere, BlueprintGetter = GetTargetSkeletalMesh, BlueprintSetter = SetTargetSkeletalMesh, Category = "BuildSettings")
	TObjectPtr<USkeletalMesh> TargetSkeletalMesh;

public:
	static FName GetTargetSkeletalMeshMemberName();
	UFUNCTION(BlueprintGetter) USkeletalMesh* GetTargetSkeletalMesh() const;
	UFUNCTION(BlueprintSetter) void SetTargetSkeletalMesh(USkeletalMesh* InSkeletalMesh);

private:
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetSourceGeometryCache() or UGroomBindingAsset::SetSourceGeometryCache().")
	UPROPERTY(VisibleAnywhere, BlueprintGetter = GetSourceGeometryCache, BlueprintSetter = SetSourceGeometryCache, Category = "BuildSettings")
	TObjectPtr<UGeometryCache> SourceGeometryCache;

public:
	static FName GetSourceGeometryCacheMemberName();
	UFUNCTION(BlueprintGetter) UGeometryCache* GetSourceGeometryCache() const;
	UFUNCTION(BlueprintSetter) void SetSourceGeometryCache(UGeometryCache* InGeometryCache);

private:
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetTargetGeometryCache() or UGroomBindingAsset::SetTargetGeometryCache().")
	UPROPERTY(VisibleAnywhere, BlueprintGetter = GetTargetGeometryCache, BlueprintSetter = SetTargetGeometryCache, Category = "BuildSettings")
	TObjectPtr<UGeometryCache> TargetGeometryCache;

public:
	static FName GetTargetGeometryCacheMemberName();
	UFUNCTION(BlueprintGetter) UGeometryCache* GetTargetGeometryCache() const;
	UFUNCTION(BlueprintSetter) void SetTargetGeometryCache(UGeometryCache* InGeometryCache);

private:
	/** Number of points used for the rbf interpolation */
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetNumInterpolationPoints() or UGroomBindingAsset::SetNumInterpolationPoints().")
	UPROPERTY(VisibleAnywhere, BlueprintGetter = GetNumInterpolationPoints, BlueprintSetter = SetNumInterpolationPoints, Category = "BuildSettings")
	int32 NumInterpolationPoints = 100;

public:
	static FName GetNumInterpolationPointsMemberName();
	UFUNCTION(BlueprintGetter) int32 GetNumInterpolationPoints() const;
	UFUNCTION(BlueprintSetter) void SetNumInterpolationPoints(int32 InNumInterpolationPoints);

private:
	/** Number of points used for the rbf interpolation */
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetMatchingSection() or UGroomBindingAsset::SetMatchingSection().")
	UPROPERTY(VisibleAnywhere, BlueprintGetter = GetMatchingSection, BlueprintSetter = SetMatchingSection, Category = "BuildSettings")
	int32 MatchingSection = 0;

public:
	static FName GetMatchingSectionMemberName();
	UFUNCTION(BlueprintGetter) int32 GetMatchingSection() const;
	UFUNCTION(BlueprintSetter) void SetMatchingSection(int32 InMatchingSection);

private:
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetGroupInfos() or UGroomBindingAsset::SetGroupInfos().")
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintGetter = GetGroupInfos, BlueprintSetter = SetGroupInfos, Category = "HairGroups", meta = (DisplayName = "Group"))
	TArray<FGoomBindingGroupInfo> GroupInfos;

public:
	static FName GetGroupInfosMemberName();
	UFUNCTION(BlueprintGetter) const TArray<FGoomBindingGroupInfo>& GetGroupInfos() const;
	UFUNCTION(BlueprintSetter) void SetGroupInfos(const TArray<FGoomBindingGroupInfo>& InGroupInfos);
	TArray<FGoomBindingGroupInfo>& GetGroupInfos();

	/** GPU and CPU binding data for both simulation and rendering. */
	struct FHairGroupResource
	{
		FHairStrandsRestRootResource* SimRootResources = nullptr;
		FHairStrandsRestRootResource* RenRootResources = nullptr;
		TArray<FHairStrandsRestRootResource*> CardsRootResources;
	};
	typedef TArray<FHairGroupResource> FHairGroupResources;

private:
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetHairGroupResources() or UGroomBindingAsset::SetHairGroupResources().")
	FHairGroupResources HairGroupResources;

public:
	static FName GetHairGroupResourcesMemberName();
	FHairGroupResources& GetHairGroupResources();
	const FHairGroupResources& GetHairGroupResources() const;
	void SetHairGroupResources(FHairGroupResources InHairGroupResources);

	/** Binding bulk data */
	struct FHairGroupPlatformData
	{
		TArray<FHairStrandsRootBulkData>		 SimRootBulkDatas;
		TArray<FHairStrandsRootBulkData>		 RenRootBulkDatas;
		TArray<TArray<FHairStrandsRootBulkData>> CardsRootBulkDatas;
	};

private:
	/** Queue of resources which needs to be deleted. This queue is needed for keeping valid pointer on the group resources 
	   when the binding asset is recomputed */
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetHairGroupPlatformData().")
	TQueue<FHairGroupResource> HairGroupResourcesToDelete;

	/** Platform data for each hair groups */
	UE_DEPRECATED(5.3, "Please do not access this member directly; use UGroomBindingAsset::GetHairGroupPlatformData().")
	TArray<FHairGroupPlatformData> HairGroupsPlatformData;

public:
	void AddHairGroupResourcesToDelete(FHairGroupResource& In);
	bool RemoveHairGroupResourcesToDelete(FHairGroupResource& Out);

	static FName GetHairGroupPlatformDataMemberName();
	const TArray<FHairGroupPlatformData>& GetHairGroupsPlatformData() const;
	TArray<FHairGroupPlatformData>& GetHairGroupsPlatformData();

public:
	//~ Begin UObject Interface.
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	virtual void PostLoad() override;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS // Suppress compiler warning on override of deprecated function
	UE_DEPRECATED(5.0, "Use version that takes FObjectPreSaveContext instead.")
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	UE_DEPRECATED(5.0, "Use version that takes FObjectPostSaveRootContext instead.")
	virtual void PostSaveRoot(bool bCleanupIsRequired) override;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;

	static bool IsCompatible(const USkeletalMesh* InSkeletalMesh, const UGroomBindingAsset* InBinding, bool bIssueWarning);
	static bool IsCompatible(const UGeometryCache* InGeometryCache, const UGroomBindingAsset* InBinding, bool bIssueWarning);
	static bool IsCompatible(const UGroomAsset* InGroom, const UGroomBindingAsset* InBinding, bool bIssueWarning);
	static bool IsBindingAssetValid(const UGroomBindingAsset* InBinding, bool bIsBindingReloading, bool bIssueWarning);

	/** Returns true if the target is not null and matches the binding type */ 
	bool HasValidTarget() const;

	/** Helper function to return the asset path name, optionally joined with the LOD index if LODIndex > -1. */
	FName GetAssetPathName(int32 LODIndex = -1);
	uint32 GetAssetHash() const { return AssetNameHash; }

#if WITH_EDITOR
	FOnGroomBindingAssetChanged& GetOnGroomBindingAssetChanged() { return OnGroomBindingAssetChanged; }

	/**  Part of UObject interface  */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
#endif // WITH_EDITOR

	/** Initialize resources. */
	void InitResource();

	/** Update resources. */
	void UpdateResource();

	/** Release the hair strands resource. */
	void ReleaseResource(bool bResetLoadedSize);

	/**
	 * Stream in all of this binding's streamable resources and make them accessible from the CPU.
	 *
	 * This is only needed for advanced use cases involving editing grooms or binding data.
	 *
	 * @param bWait If true, this call will block until the resources have been streamed in
	 */
	void StreamInForCPUAccess(bool bWait);

	void Reset();

	/** Return true if the binding asset is valid, i.e., correctly built and loaded. */
	bool IsValid() const { return bIsValid;  }

#if WITH_EDITOR
private:
	/** Used as a bit-field indicating which properties are read by async compilation. */
	std::atomic<uint64> AccessedProperties;
	/** Used as a bit-field indicating which properties are written to by async compilation. */
	std::atomic<uint64> ModifiedProperties;
	/** Holds the pointer to an async task if one exists. */
	TUniquePtr<FGroomBindingAsyncBuildTask> AsyncTask;

	bool IsAsyncTaskComplete() const
	{
		return AsyncTask == nullptr || AsyncTask->IsWorkDone();
	}

	bool TryCancelAsyncTasks()
	{
		if (AsyncTask)
		{
			if (AsyncTask->IsDone() || AsyncTask->Cancel())
			{
				AsyncTask.Reset();
			}
		}

		return AsyncTask == nullptr;
	}

	void ExecuteCacheDerivedDatas(FGroomBindingBuildContext& Context);
	void FinishCacheDerivedDatas(FGroomBindingBuildContext& Context);

public:
	/** IInterface_AsyncCompilation begin*/
	virtual bool IsCompiling() const override
	{
		return AsyncTask != nullptr || AccessedProperties.load(std::memory_order_relaxed) != 0;
	}
	/** IInterface_AsyncCompilation end*/

	FOnGroomBindingAssetChanged OnGroomBindingAssetChanged;

	void RecreateResources();
	void ChangeFeatureLevel(ERHIFeatureLevel::Type PendingFeatureLevel);
	void ChangePlatformLevel(ERHIFeatureLevel::Type PendingFeatureLevel);
#endif
private:
	void WaitUntilAsyncPropertyReleased(EGroomBindingAsyncProperties AsyncProperties, EGroomBindingAsyncPropertyLockType LockType) const;
	void AcquireAsyncProperty(uint64 AsyncProperties = MAX_uint64, EGroomBindingAsyncPropertyLockType LockType = EGroomBindingAsyncPropertyLockType::ReadWrite)
	{
#if WITH_EDITOR
		if ((LockType & EGroomBindingAsyncPropertyLockType::ReadOnly) == EGroomBindingAsyncPropertyLockType::ReadOnly)
		{
			AccessedProperties |= AsyncProperties;
		}

		if ((LockType & EGroomBindingAsyncPropertyLockType::WriteOnly) == EGroomBindingAsyncPropertyLockType::WriteOnly)
		{
			ModifiedProperties |= AsyncProperties;
		}
#endif
	}

	void ReleaseAsyncProperty(uint64 AsyncProperties = MAX_uint64, EGroomBindingAsyncPropertyLockType LockType = EGroomBindingAsyncPropertyLockType::ReadWrite)
	{
#if WITH_EDITOR
		if ((LockType & EGroomBindingAsyncPropertyLockType::ReadOnly) == EGroomBindingAsyncPropertyLockType::ReadOnly)
		{
			AccessedProperties &= ~AsyncProperties;
		}

		if ((LockType & EGroomBindingAsyncPropertyLockType::WriteOnly) == EGroomBindingAsyncPropertyLockType::WriteOnly)
		{
			ModifiedProperties &= ~AsyncProperties;
		}
#endif
	}

	static void FlushRenderingCommandIfUsed(const UGroomBindingAsset* In);
public:
#if WITH_EDITORONLY_DATA
	/** Build/rebuild a binding asset */
	void Build();

	void CacheDerivedDatas();

	bool HasAnyDependenciesCompiling() const;

	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform);
	virtual void ClearAllCachedCookedPlatformData();
	TArray<FHairGroupPlatformData>* GetCachedCookedPlatformData(const ITargetPlatform* TargetPlatform);

	void InvalidateBinding();
	void InvalidateBinding(class USkeletalMesh*);

	struct FCachedCookedPlatformData
	{
		TArray<FString> GroupDerivedDataKeys;
		TArray<FHairGroupPlatformData> GroupPlatformDatas;
	};
private:
	TArray<FCachedCookedPlatformData*> CachedCookedPlatformDatas;

	void RegisterGroomDelegates();
	void UnregisterGroomDelegates();
	void RegisterSkeletalMeshDelegates();
	void UnregisterSkeletalMeshDelegates();

	TArray<FString> CachedDerivedDataKey;
#endif
#if WITH_EDITOR
	ERHIFeatureLevel::Type CachedResourcesFeatureLevel = ERHIFeatureLevel::Num;
	ERHIFeatureLevel::Type CachedResourcesPlatformLevel = ERHIFeatureLevel::Num;
#endif
	bool bIsValid = false;
	uint32 AssetNameHash = 0;

	friend class FGroomBindingCompilingManager;
	friend class FGroomBindingAsyncBuildWorker;
};

UCLASS(BlueprintType, hidecategories = (Object))
class HAIRSTRANDSCORE_API UGroomBindingAssetList : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Transient, EditFixedSize, Category = "Bindings")
	TArray<TObjectPtr<UGroomBindingAsset>> Bindings;
};

struct FGroomBindingAssetMemoryStats
{
	struct FStats
	{
		uint32 Guides = 0;
		uint32 Strands= 0;
		uint32 Cards  = 0;
	};
	FStats CPU;
	FStats GPU;

	static FGroomBindingAssetMemoryStats Get(const UGroomBindingAsset::FHairGroupPlatformData& InCPU, const UGroomBindingAsset::FHairGroupResource& InGPU);
	void Accumulate(const FGroomBindingAssetMemoryStats& In);
	uint32 GetTotalCPUSize() const;
	uint32 GetTotalGPUSize() const;
};