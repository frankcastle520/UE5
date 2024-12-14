// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingShaderBindingTable.h"
#include "RayTracingDefinitions.h"
#include "RayTracingScene.h"
#include "RayTracing.h"
#include "RayTracingGeometry.h"

#if RHI_RAYTRACING

uint32 FRayTracingSBTAllocation::GetRecordIndex(ERayTracingSceneLayer Layer, uint32 SegmentIndex) const
{
	check(HasLayer(Layer));

	// Find out all the bits set below the given layer
	// and count the set bits to know the offset
	uint32 LayerMask = (1 << (uint32)Layer) - 1;
	LayerMask = (uint32)AllocatedLayers & LayerMask;
	uint32 RecordTypeBaseOffset = FMath::CountBits(LayerMask) * RecordsPerLayer;

	check(RecordTypeBaseOffset + SegmentIndex * RAY_TRACING_NUM_SHADER_SLOTS + RAY_TRACING_NUM_SHADER_SLOTS <= NumRecords);
	return BaseRecordIndex + RecordTypeBaseOffset + SegmentIndex * RAY_TRACING_NUM_SHADER_SLOTS;
}

int32 FRayTracingSBTAllocation::GetSegmentCount() const
{
	return NumRecords / RAY_TRACING_NUM_SHADER_SLOTS;
}

/**
 * Default constructor
 */
FRayTracingShaderBindingTable::FRayTracingShaderBindingTable() : NumShaderSlotsPerGeometrySegment(RAY_TRACING_NUM_SHADER_SLOTS)
{
}

/**
 * Make sure all dynamic allocation objects are freed and assure all static allocations have been requested deleted already
 */
FRayTracingShaderBindingTable::~FRayTracingShaderBindingTable()
{
	ResetDynamicAllocationData();
	for (FRayTracingSBTAllocation* SBTAllocation : FreeDynamicAllocationPool)
	{
		delete SBTAllocation;
	}
	FreeDynamicAllocationPool.Empty();
	
	// Assume empty?
	check(TrackedAllocationMap.IsEmpty());
	check(StaticRangeAllocator.GetSparselyAllocatedSize() == 0);
}

/**
 * Mark all currently allocated dynamic ranges as free again so they can be allocated
 * Setup the CurrentDynamicRangeOffset from where dynamic SBT records will be stored 
 * After this call no static SBT ranges can be allocated anymore until the end of the 'frame'
 */
void FRayTracingShaderBindingTable::ResetDynamicAllocationData()
{				
	// Release all dynamic allocation back to the pool
	FreeDynamicAllocationPool.Append(ActiveDynamicAllocations);
	ActiveDynamicAllocations.Empty(ActiveDynamicAllocations.Num());
	NumDynamicGeometrySegments = 0;
	
	// Static allocations are not allowed anymore because dynamic allocations are stored right after all static allocations
	bStaticAllocationsLocked = true;

	// Dynamic segments will be stored right after the currently allocated 
	uint32 AllocatedStaticSegmentSize = GetMaxAllocatedStaticSegmentCount();
	CurrentDynamicRangeOffset = AllocatedStaticSegmentSize * NumShaderSlotsPerGeometrySegment;
}

FShaderBindingTableRHIRef FRayTracingShaderBindingTable::AllocateRHI(
	FRHICommandListBase& RHICmdList, 
	ERayTracingShaderBindingMode ShaderBindingMode, 
	ERayTracingHitGroupIndexingMode HitGroupIndexingMode,
	uint32 NumMissShaderSlots, 
	uint32 NumCallableShaderSlots, 
	uint32 LocalBindingDataSize) const
{
	uint32 AllocatedStaticSegmentSize = GetMaxAllocatedStaticSegmentCount();
	
	FRayTracingShaderBindingTableInitializer SBTInitializer;
	SBTInitializer.ShaderBindingMode = ShaderBindingMode;
	SBTInitializer.HitGroupIndexingMode = HitGroupIndexingMode;
	SBTInitializer.NumGeometrySegments = AllocatedStaticSegmentSize + NumDynamicGeometrySegments;
	SBTInitializer.NumShaderSlotsPerGeometrySegment = NumShaderSlotsPerGeometrySegment;
	SBTInitializer.NumMissShaderSlots = NumMissShaderSlots;
	SBTInitializer.NumCallableShaderSlots = NumCallableShaderSlots;
	SBTInitializer.LocalBindingDataSize = LocalBindingDataSize;
	
	return RHICmdList.CreateRayTracingShaderBindingTable(SBTInitializer);
}

uint32 FRayTracingShaderBindingTable::GetNumGeometrySegments() const
{
	return GetMaxAllocatedStaticSegmentCount() + NumDynamicGeometrySegments;
}

uint32 FRayTracingShaderBindingTable::GetMaxAllocatedStaticSegmentCount() const
{
	//ensure(bStaticAllocationsLocked);
	return StaticRangeAllocator.GetMaxSize() / NumShaderSlotsPerGeometrySegment;
}

FRayTracingSBTAllocation* FRayTracingShaderBindingTable::AllocateStaticRangeInternal(
	ERayTracingSceneLayerMask AllocatedLayers, 
	uint32 SegmentCount, 
	const FRHIRayTracingGeometry* Geometry, 
	FRayTracingCachedMeshCommandFlags Flags)
{
	// Should be allowed to make static SBT allocations
	ensure(!bStaticAllocationsLocked);

	uint32 LayersCount = FMath::CountBits((uint32)AllocatedLayers);
	uint32 RecordsPerLayer = SegmentCount * NumShaderSlotsPerGeometrySegment;
	uint32 RecordCount = RecordsPerLayer * LayersCount;
	uint32 BaseIndex = StaticRangeAllocator.Allocate(RecordCount);
			
	FRayTracingSBTAllocation* Allocation = new FRayTracingSBTAllocation();
	Allocation->InitStatic(AllocatedLayers, BaseIndex, RecordsPerLayer, RecordCount, Geometry, Flags);
	
	AllocatedStaticSegmentCount += SegmentCount * LayersCount;
	
	return Allocation;
}

FRayTracingSBTAllocation* FRayTracingShaderBindingTable::AllocateStaticRange(uint32 SegmentCount, const FRHIRayTracingGeometry* Geometry, FRayTracingCachedMeshCommandFlags Flags)
{
	check(Geometry != nullptr);

	// No allocation if we are not rendering decals and all segments are decals
	if (RayTracing::ShouldExcludeDecals() && Flags.bAllSegmentsDecal)
	{
		return nullptr;
	}

	ERayTracingSceneLayerMask AllocatedLayers = ERayTracingSceneLayerMask::None;
	if (!Flags.bAllSegmentsDecal)
	{
		EnumAddFlags(AllocatedLayers, ERayTracingSceneLayerMask::Base);
	}
	if (Flags.bAnySegmentsDecal && !RayTracing::ShouldExcludeDecals())
	{
		EnumAddFlags(AllocatedLayers, ERayTracingSceneLayerMask::Decals);
	}
	if (AllocatedLayers == ERayTracingSceneLayerMask::None)
	{
		return nullptr;
	}

	FScopeLock ScopeLock(&StaticAllocationCS);

	// Setup the key needed for deduplication
	FAllocationKey Key;
	Key.Geometry = Geometry;
	Key.Flags = Flags;

	// Already allocated for given hash
	FRefCountedAllocation& Allocation = TrackedAllocationMap.FindOrAdd(Key);
	if (Allocation.RefCount == 0)
	{		
		Allocation.Allocation = AllocateStaticRangeInternal(AllocatedLayers, SegmentCount, Geometry, Flags);
	}
	else
	{
		TotalStaticAllocationCount++;
	}
	check(Allocation.Allocation->AllocatedLayers == AllocatedLayers);
	
	Allocation.RefCount++;
	return Allocation.Allocation;
}

void FRayTracingShaderBindingTable::FreeStaticRange(const FRayTracingSBTAllocation* Allocation)
{
	if (Allocation == nullptr)
	{
		return;
	}

	FScopeLock ScopeLock(&StaticAllocationCS);

	TotalStaticAllocationCount--;

	// If geometry is stored then it could have been deduplicatedf and we can build the allocation key again
	if (Allocation->Geometry)
	{
		FAllocationKey Key;
		Key.Geometry = Allocation->Geometry;
		Key.Flags = Allocation->Flags;

		FRefCountedAllocation* RefAllocation = TrackedAllocationMap.Find(Key);
		check(Allocation);
		RefAllocation->RefCount--;

		if (RefAllocation->RefCount == 0)
		{
			StaticRangeAllocator.Free(RefAllocation->Allocation->BaseRecordIndex, RefAllocation->Allocation->NumRecords);
			AllocatedStaticSegmentCount -= (RefAllocation->Allocation->NumRecords / NumShaderSlotsPerGeometrySegment);
		
			TrackedAllocationMap.Remove(Key);
			delete RefAllocation->Allocation;
		}
	}
	else
	{
		StaticRangeAllocator.Free(Allocation->BaseRecordIndex, Allocation->NumRecords);
		AllocatedStaticSegmentCount -= (Allocation->NumRecords / NumShaderSlotsPerGeometrySegment);		
		delete Allocation;
	}
}

FRayTracingSBTAllocation* FRayTracingShaderBindingTable::AllocateDynamicRange(ERayTracingSceneLayerMask AllocatedLayers, uint32 SegmentCount)
{	
	// Don't need lock right now because all dynamic allocation are allocated linearly on the same thread
	// So the FreeDynamicAllocationPool can't be shared with the static allocations right now because those would require a lock
	FRayTracingSBTAllocation* Allocation = FreeDynamicAllocationPool.IsEmpty() ? nullptr : FreeDynamicAllocationPool.Pop(EAllowShrinking::No);
	if (Allocation == nullptr)
	{
		Allocation = new FRayTracingSBTAllocation();
	}

	uint32 LayersCount = FMath::CountBits((uint32)AllocatedLayers);
	uint32 BaseIndex = CurrentDynamicRangeOffset;
	uint32 RecordsPerLayer = SegmentCount * NumShaderSlotsPerGeometrySegment;
	uint32 RecordCount = RecordsPerLayer * LayersCount;
	CurrentDynamicRangeOffset += RecordCount;
	Allocation->InitDynamic(AllocatedLayers, BaseIndex, RecordsPerLayer, RecordCount);

	NumDynamicGeometrySegments += SegmentCount * LayersCount;

	ActiveDynamicAllocations.Add(Allocation);

	return Allocation;
}

#endif // RHI_RAYTRACING