// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "SpanAllocator.h"
#include "RayTracingMeshDrawCommands.h"

#if RHI_RAYTRACING

enum class ERayTracingSceneLayer : uint8
{
	Base = 0,
	Decals,

	NUM
};

enum class ERayTracingSceneLayerMask
{
	None = 0,
	Base = 1 << (uint32)ERayTracingSceneLayer::Base,
	Decals = 1 << (uint32)ERayTracingSceneLayer::Decals,
	All = Base | Decals,
};

struct FRayTracingSBTAllocation
{
public:

	FRayTracingSBTAllocation() = default;

	bool IsValid() const
	{
		return NumRecords > 0;
	}

	/**
	 * Get the InstanceContributionToHitGroupIndex for the given layer which is stored in the RayTracingInstance data
	 */
	uint32 GetInstanceContributionToHitGroupIndex(ERayTracingSceneLayer Layer) const
	{
		// InstanceContributionToHitGroupIndex is stored at the first segment index because all other segments are directly allocated after this one
		return GetRecordIndex(Layer, 0);
	}

	/**
	 * Get the base SBT record index for the given layer and segment index
	 */
	RENDERER_API uint32 GetRecordIndex(ERayTracingSceneLayer Layer, uint32 SegmentIndex) const;
	RENDERER_API int32 GetSegmentCount() const;
	bool HasLayer(ERayTracingSceneLayer Layer) const { return EnumHasAnyFlags(ERayTracingSceneLayerMask(1 << (uint32)Layer), AllocatedLayers); }

private:

	void InitStatic(ERayTracingSceneLayerMask InAllocatedLayers, uint32 InBaseRecordIndex, uint32 InRecordsPerLayer, uint32 InNumRecords, const FRHIRayTracingGeometry* InGeometry, const FRayTracingCachedMeshCommandFlags& InFlags)
	{
		check(InAllocatedLayers != ERayTracingSceneLayerMask::None);
		AllocatedLayers = InAllocatedLayers;
		BaseRecordIndex = InBaseRecordIndex;
		RecordsPerLayer = InRecordsPerLayer;
		NumRecords = InNumRecords;
		Geometry = InGeometry;
		Flags = InFlags;
	}

	void InitDynamic(ERayTracingSceneLayerMask InAllocatedLayers, uint32 InBaseRecordIndex, uint32 InRecordsPerLayer, uint32 InNumRecords)
	{
		check(InAllocatedLayers != ERayTracingSceneLayerMask::None);
		AllocatedLayers = InAllocatedLayers;
		BaseRecordIndex = InBaseRecordIndex;
		RecordsPerLayer = InRecordsPerLayer;
		NumRecords = InNumRecords;
	}

	friend class FRayTracingShaderBindingTable;

	uint32 BaseRecordIndex = 0;
	uint32 RecordsPerLayer = 0;
	uint32 NumRecords = 0;
	ERayTracingSceneLayerMask AllocatedLayers;

	// Store the original geometry and flags in the allocation object so it can be used to build the lookup key again used for deduplication
	const FRHIRayTracingGeometry* Geometry = nullptr;
	FRayTracingCachedMeshCommandFlags Flags;
};

/**
* Shader binding table use for raytracing
*/
class FRayTracingShaderBindingTable
{
public:
		
	RENDERER_API FRayTracingShaderBindingTable();
	RENDERER_API ~FRayTracingShaderBindingTable();
	 
	/**
	 * Allocate RHI shader binding table which can contain all static allocations and all current dynamic allocations - single frame SBT
	 */
	RENDERER_API FShaderBindingTableRHIRef AllocateRHI(FRHICommandListBase& RHICmdList, ERayTracingShaderBindingMode ShaderBindingMode, ERayTracingHitGroupIndexingMode HitGroupIndexingMode, uint32 NumMissShaderSlots, uint32 NumCallableShaderSlots, uint32 LocalBindingDataSize) const;
	 
	/**
	 * Get the total number of allocated geometry segments (static and dynamic)
	 */
	RENDERER_API uint32 GetNumGeometrySegments() const;

	/**
	 * Allocate single static range of records for the given SegmentCount for all layers in the AllocatedLayersMask
	 */
	FRayTracingSBTAllocation* AllocateStaticRange(ERayTracingSceneLayerMask AllocatedLayers, uint32 SegmentCount)
	{
		FScopeLock ScopeLock(&StaticAllocationCS);
		FRayTracingCachedMeshCommandFlags DefaultFlags;
		return AllocateStaticRangeInternal(AllocatedLayers, SegmentCount, nullptr, DefaultFlags);
	}
	
	/**
	 * Allocate or share static allocation range - sharing can happen if geometry and cached RT MDC flags are the same (will result in exactly the same binding data written in the SBT)
	 */
	RENDERER_API FRayTracingSBTAllocation* AllocateStaticRange(uint32 SegmentCount, const FRHIRayTracingGeometry* Geometry, FRayTracingCachedMeshCommandFlags Flags);
	RENDERER_API void FreeStaticRange(const FRayTracingSBTAllocation* Allocation);
	 
	/**
	 * Allocate dynamic SBT range which can be reused again when ResetDynamicAllocationData is called
	 */	
	RENDERER_API FRayTracingSBTAllocation* AllocateDynamicRange(ERayTracingSceneLayerMask AllocatedLayers, uint32 SegmentCount);

	/**
	 * Mark all currently allocated dynamic ranges as free again so they can be allocated
	 */		
	RENDERER_API void ResetDynamicAllocationData();

	/**
	 * Reset the static allocation lock again (used for validation)
	 */
	void ResetStaticAllocationLock()
	{
		bStaticAllocationsLocked = false;
	}

private:
	/**
	 * Get the maximum amount of static allocated segments (highest allocation index with free ranges included)
	 */
	uint32 GetMaxAllocatedStaticSegmentCount() const;
		
	/**
	* Allocate single static range of records for the given SegmentCount for all layers in the AllocatedLayersMask
	*/
	RENDERER_API FRayTracingSBTAllocation* AllocateStaticRangeInternal(ERayTracingSceneLayerMask AllocatedLayers, uint32 SegmentCount, const FRHIRayTracingGeometry* Geometry, FRayTracingCachedMeshCommandFlags Flags);

	struct FAllocationKey
	{
		const FRHIRayTracingGeometry* Geometry;
		FRayTracingCachedMeshCommandFlags Flags;

		bool operator==(const FAllocationKey& Other) const
		{
			return Geometry == Other.Geometry &&
				Flags == Other.Flags;
		}

		bool operator!=(const FAllocationKey& Other) const
		{
			return !(*this == Other);
		}

		friend uint32 GetTypeHash(const FAllocationKey& Key)
		{
			return HashCombine(GetTypeHash(Key.Geometry), GetTypeHash(Key.Flags));
		}
	};

	struct FRefCountedAllocation
	{
		FRayTracingSBTAllocation* Allocation;
		uint32 RefCount = 0;
	};
	
	uint32 NumShaderSlotsPerGeometrySegment = 0;						 //< Number of slots per geometry segment (engine wide fixed)
	bool bStaticAllocationsLocked = false;								 //< Static allocations are not allowed when this bool is set (used for validation)

	FCriticalSection StaticAllocationCS;								 //< Critical section used to access all static allocation data
	FSpanAllocator StaticRangeAllocator;								 //< Range allocator to find free static record ranges
	TMap<FAllocationKey, FRefCountedAllocation> TrackedAllocationMap;	 //< All static allocation with refcount tracking
	
	TArray<FRayTracingSBTAllocation*> ActiveDynamicAllocations;			 //< All current active dynamic allocations
	TArray<FRayTracingSBTAllocation*> FreeDynamicAllocationPool;		 //< Free dynamic allocation pool (for faster allocations)

	uint32 TotalStaticAllocationCount = 0;								 //< Total amount of static allocations (without deduplications)
	uint32 AllocatedStaticSegmentCount = 0;								 //< Total amount of allocated static segments (with deduplication)
		
	uint32 NumDynamicGeometrySegments = 0;								 //< Current number of allocated dynamic segments
	uint32 CurrentDynamicRangeOffset = 0;								 //< Current working SBT record offset for the next dynamic allocation
};

#endif // RHI_RAYTRACING

