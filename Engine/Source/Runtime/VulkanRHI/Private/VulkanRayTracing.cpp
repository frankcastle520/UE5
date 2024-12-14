// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanRayTracing.h"

#include "VulkanContext.h"
#include "VulkanDescriptorSets.h"
#include "BuiltInRayTracingShaders.h"
#include "Experimental/Containers/SherwoodHashTable.h"
#include "Async/ParallelFor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static int32 GVulkanRayTracingAllowCompaction = 1;
static FAutoConsoleVariableRef CVarVulkanRayTracingAllowCompaction(
	TEXT("r.Vulkan.RayTracing.AllowCompaction"),
	GVulkanRayTracingAllowCompaction,
	TEXT("Whether to automatically perform compaction for static acceleration structures to save GPU memory. (default = 1)\n"),
	ECVF_ReadOnly
);

static int32 GVulkanRayTracingMaxBatchedCompaction = 64;
static FAutoConsoleVariableRef CVarVulkanRayTracingMaxBatchedCompaction(
	TEXT("r.Vulkan.RayTracing.MaxBatchedCompaction"),
	GVulkanRayTracingMaxBatchedCompaction,
	TEXT("Maximum of amount of compaction requests and rebuilds per frame. (default = 64)\n"),
	ECVF_ReadOnly
);

static int32 GVulkanRayTracingAllowDeferredOperation = 0;
static FAutoConsoleVariableRef CVarVulkanRayTracingAllowDeferredOperation(
	TEXT("r.Vulkan.RayTracing.AllowDeferredOperation"),
	GVulkanRayTracingAllowDeferredOperation,
	TEXT("Whether to use Vulkan Deferred Operation for RT pipeline creation. (default = -1)\n")
	TEXT(" <0: Disabled\n")
	TEXT(" 0: Enabled, auto detect the maximum number of threads")
	TEXT(" >0: Enabled, use the specified number of threads"),
	ECVF_ReadOnly
);

static int32 GVulkanSubmitOnTraceRays = 0;
static FAutoConsoleVariableRef GCVarSubmitOnTraceRays(
	TEXT("r.Vulkan.SubmitOnTraceRays"),
	GVulkanSubmitOnTraceRays,
	TEXT("0 to not do anything special on trace rays (default)\n")\
	TEXT("1 to submit the cmd buffer after each trace rays"),
	ECVF_ReadOnly
);

static int32 GVulkanRayTracingMaxShaderGroupStride = 4096;
static FAutoConsoleVariableRef GCVarVulkanRayTracingMaxShaderGroupStride(
	TEXT("r.Vulkan.RayTracing.MaxShaderGroupStride"),
	GVulkanRayTracingMaxShaderGroupStride,
	TEXT("The default size to allocate for each record (default: 4096)."),
	ECVF_ReadOnly
);

// Ray tracing stat counters

DECLARE_STATS_GROUP(TEXT("Vulkan: Ray Tracing"), STATGROUP_VulkanRayTracing, STATCAT_Advanced);

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Created pipelines (total)"), STAT_VulkanRayTracingCreatedPipelines, STATGROUP_VulkanRayTracing);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Compiled shaders (total)"), STAT_VulkanRayTracingCompiledShaders, STATGROUP_VulkanRayTracing);

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Allocated bottom level acceleration structures"), STAT_VulkanRayTracingAllocatedBLAS, STATGROUP_VulkanRayTracing);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Allocated top level acceleration structures"), STAT_VulkanRayTracingAllocatedTLAS, STATGROUP_VulkanRayTracing);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Triangles in all BL acceleration structures"), STAT_VulkanRayTracingTrianglesBLAS, STATGROUP_VulkanRayTracing);

DECLARE_DWORD_COUNTER_STAT(TEXT("Built BL AS (per frame)"), STAT_VulkanRayTracingBuiltBLAS, STATGROUP_VulkanRayTracing);
DECLARE_DWORD_COUNTER_STAT(TEXT("Updated BL AS (per frame)"), STAT_VulkanRayTracingUpdatedBLAS, STATGROUP_VulkanRayTracing);
DECLARE_DWORD_COUNTER_STAT(TEXT("Built TL AS (per frame)"), STAT_VulkanRayTracingBuiltTLAS, STATGROUP_VulkanRayTracing);
DECLARE_DWORD_COUNTER_STAT(TEXT("Updated TL AS (per frame)"), STAT_VulkanRayTracingUpdatedTLAS, STATGROUP_VulkanRayTracing);

DECLARE_MEMORY_STAT(TEXT("Total BL AS Memory"), STAT_VulkanRayTracingBLASMemory, STATGROUP_VulkanRayTracing);
DECLARE_MEMORY_STAT(TEXT("Static BL AS Memory"), STAT_VulkanRayTracingStaticBLASMemory, STATGROUP_VulkanRayTracing);
DECLARE_MEMORY_STAT(TEXT("Dynamic BL AS Memory"), STAT_VulkanRayTracingDynamicBLASMemory, STATGROUP_VulkanRayTracing);
DECLARE_MEMORY_STAT(TEXT("TL AS Memory"), STAT_VulkanRayTracingTLASMemory, STATGROUP_VulkanRayTracing);
DECLARE_MEMORY_STAT(TEXT("Total Used Video Memory"), STAT_VulkanRayTracingUsedVideoMemory, STATGROUP_VulkanRayTracing);

DECLARE_CYCLE_STAT(TEXT("RTPSO Compile Shader"), STAT_RTPSO_CompileShader, STATGROUP_VulkanRayTracing);
DECLARE_CYCLE_STAT(TEXT("RTPSO Create Pipeline"), STAT_RTPSO_CreatePipeline, STATGROUP_VulkanRayTracing);


#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable : 4191) // warning C4191: 'type cast': unsafe conversion
#endif // PLATFORM_WINDOWS
bool FVulkanRayTracingPlatform::CheckVulkanInstanceFunctions(VkInstance inInstance)
{
	bool bFoundAllEntryPoints = true;
#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_RAYTRACING(CHECK_VK_ENTRYPOINTS);
#endif
	return bFoundAllEntryPoints;
}
#if PLATFORM_WINDOWS
#pragma warning(pop) // restore 4191
#endif

enum class EBLASBuildDataUsage
{	
	// Uses provided VB/IB when filling out BLAS build data
	Rendering = 0,

	// Does not use VB/IB. Special mode for estimating BLAS size.
	Size = 1
};

// Temporary brute force allocation helper, this should be handled by the memory sub-allocator
static uint32 FindMemoryType(VkPhysicalDevice Gpu, uint32 Filter, VkMemoryPropertyFlags RequestedProperties)
{
	VkPhysicalDeviceMemoryProperties Properties = {};
	VulkanRHI::vkGetPhysicalDeviceMemoryProperties(Gpu, &Properties);

	uint32 Result = UINT32_MAX;
	for (uint32 i = 0; i < Properties.memoryTypeCount; ++i)
	{
		const bool bTypeFilter = Filter & (1 << i);
		const bool bPropFilter = (Properties.memoryTypes[i].propertyFlags & RequestedProperties) == RequestedProperties;
		if (bTypeFilter && bPropFilter)
		{
			Result = i;
			break;
		}
	}

	check(Result < UINT32_MAX);
	return Result;
}

static void AddAccelerationStructureBuildBarrier(VkCommandBuffer CommandBuffer)
{
	VkMemoryBarrier Barrier;
	ZeroVulkanStruct(Barrier, VK_STRUCTURE_TYPE_MEMORY_BARRIER);
	Barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    
    // TODO: Revisit the compute stages here as we don't always need barrier to compute
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    
	VulkanRHI::vkCmdPipelineBarrier(CommandBuffer, srcStage, dstStage, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
}

static bool ShouldCompactAfterBuild(ERayTracingAccelerationStructureFlags BuildFlags)
{
	return EnumHasAllFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowCompaction | ERayTracingAccelerationStructureFlags::FastTrace)
		&& !EnumHasAnyFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowUpdate);
}

static VkBuildAccelerationStructureFlagBitsKHR TranslateRayTracingAccelerationStructureFlags(ERayTracingAccelerationStructureFlags Flags)
{
	uint32 Result = {};

	auto HandleFlag = [&Flags, &Result](ERayTracingAccelerationStructureFlags Engine, VkBuildAccelerationStructureFlagBitsKHR Native)
		{
			if (EnumHasAllFlags(Flags, Engine))
			{
				Result |= (uint32)Native;
				EnumRemoveFlags(Flags, Engine);
			}
		};

	HandleFlag(ERayTracingAccelerationStructureFlags::AllowUpdate, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
	HandleFlag(ERayTracingAccelerationStructureFlags::AllowCompaction, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
	HandleFlag(ERayTracingAccelerationStructureFlags::FastTrace, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
	HandleFlag(ERayTracingAccelerationStructureFlags::FastBuild, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR);
	HandleFlag(ERayTracingAccelerationStructureFlags::MinimizeMemory, VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR);

	checkf(!EnumHasAnyFlags(Flags, Flags), TEXT("Some ERayTracingAccelerationStructureFlags entries were not handled"));

#if VULKAN_SUPPORTS_RAY_TRACING_POSITION_FETCH
	Result |= (uint32)VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR;
#endif

	return VkBuildAccelerationStructureFlagBitsKHR(Result);
}

static ERayTracingAccelerationStructureFlags GetRayTracingAccelerationStructureBuildFlags(const FRayTracingGeometryInitializer& Initializer)
{
	ERayTracingAccelerationStructureFlags BuildFlags = ERayTracingAccelerationStructureFlags::None;

	if (Initializer.bFastBuild)
	{
		BuildFlags = ERayTracingAccelerationStructureFlags::FastBuild;
	}
	else
	{
		BuildFlags = ERayTracingAccelerationStructureFlags::FastTrace;
	}

	if (Initializer.bAllowUpdate)
	{
		EnumAddFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowUpdate);
	}

	if (!Initializer.bFastBuild && !Initializer.bAllowUpdate && Initializer.bAllowCompaction && GVulkanRayTracingAllowCompaction)
	{
		EnumAddFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowCompaction);
	}

	return BuildFlags;
}

static void GetBLASBuildData(
	const VkDevice Device,
	const TArrayView<const FRayTracingGeometrySegment> Segments,
	const ERayTracingGeometryType GeometryType,
	const FBufferRHIRef IndexBufferRHI,
	const uint32 IndexBufferOffset,
	ERayTracingAccelerationStructureFlags BuildFlags,
	const EAccelerationStructureBuildMode BuildMode,
	const EBLASBuildDataUsage Usage,
	FVkRtBLASBuildData& BuildData)
{
	FVulkanResourceMultiBuffer* const IndexBuffer = ResourceCast(IndexBufferRHI.GetReference());
	VkDeviceOrHostAddressConstKHR IndexBufferDeviceAddress = {};
	
	// We only need to get IB/VB address when we are getting data for rendering. For estimating BLAS size we set them to 0.
	// According to vulkan spec any VkDeviceOrHostAddressKHR members are ignored in vkGetAccelerationStructureBuildSizesKHR.
	uint32 IndexStrideInBytes = 0;
	if (IndexBufferRHI)
	{
		IndexBufferDeviceAddress.deviceAddress = Usage == EBLASBuildDataUsage::Rendering ? IndexBuffer->GetDeviceAddress() + IndexBufferOffset : 0;

		// In case we are just calculating size but index buffer is not yet in valid state we assume the geometry is using uint32 format
		IndexStrideInBytes = Usage == EBLASBuildDataUsage::Rendering
			? IndexBuffer->GetStride()
			: (IndexBuffer->GetSize() > 0) ? IndexBuffer->GetStride() : 4;
	}

	TArray<uint32, TInlineAllocator<1>> PrimitiveCounts;

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FRayTracingGeometrySegment& Segment = Segments[SegmentIndex];

		FVulkanResourceMultiBuffer* const VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());

		VkDeviceOrHostAddressConstKHR VertexBufferDeviceAddress = {};
		VertexBufferDeviceAddress.deviceAddress = Usage == EBLASBuildDataUsage::Rendering
			? VertexBuffer->GetDeviceAddress() + Segment.VertexBufferOffset
			: 0;

		VkAccelerationStructureGeometryKHR SegmentGeometry;
		ZeroVulkanStruct(SegmentGeometry, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);

		if (Segment.bForceOpaque)
		{
			SegmentGeometry.flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
		}

		if (!Segment.bAllowDuplicateAnyHitShaderInvocation)
		{
			// Allow only a single any-hit shader invocation per primitive
			SegmentGeometry.flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
		}

		uint32 PrimitiveOffset = 0;
		switch (GeometryType)
		{
			case RTGT_Triangles:
				SegmentGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;

				SegmentGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
				SegmentGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
				SegmentGeometry.geometry.triangles.vertexData = VertexBufferDeviceAddress;
				SegmentGeometry.geometry.triangles.maxVertex = Segment.MaxVertices;
				SegmentGeometry.geometry.triangles.vertexStride = Segment.VertexBufferStride;
				SegmentGeometry.geometry.triangles.indexData = IndexBufferDeviceAddress;

				switch (Segment.VertexBufferElementType)
				{
				case VET_Float3:
				case VET_Float4:
					SegmentGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
					break;
				default:
					checkNoEntry();
					break;
				}

				// No support for segment transform
				SegmentGeometry.geometry.triangles.transformData.deviceAddress = 0;
				SegmentGeometry.geometry.triangles.transformData.hostAddress = nullptr;

				if (IndexBufferRHI)
				{
					SegmentGeometry.geometry.triangles.indexType = (IndexStrideInBytes == 2) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
					// offset in bytes into the index buffer where primitive data for the current segment is defined
					PrimitiveOffset = Segment.FirstPrimitive * FVulkanRayTracingGeometry::IndicesPerPrimitive * IndexStrideInBytes;
				}
				else
				{
					SegmentGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
					// for non-indexed geometry, primitiveOffset is applied when reading from vertex buffer
					PrimitiveOffset = Segment.FirstPrimitive * FVulkanRayTracingGeometry::IndicesPerPrimitive * Segment.VertexBufferStride;
				}

				break;
			case RTGT_Procedural:
				checkf(Segment.VertexBufferStride >= (2 * sizeof(FVector3f)), TEXT("Procedural geometry vertex buffer must contain at least 2xFloat3 that defines 3D bounding boxes of primitives."));
				checkf(Segment.VertexBufferStride % 8 == 0, TEXT("Procedural geometry vertex buffer stride must be a multiple of 8."));

				SegmentGeometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
				
				SegmentGeometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
				SegmentGeometry.geometry.aabbs.data = VertexBufferDeviceAddress;
				SegmentGeometry.geometry.aabbs.stride = Segment.VertexBufferStride;

				break;
			default:
				checkf(false, TEXT("Unexpected ray tracing geometry type"));
				break;
		}

		BuildData.Segments.Add(SegmentGeometry);

		VkAccelerationStructureBuildRangeInfoKHR RangeInfo = {};
		RangeInfo.firstVertex = 0;

		// Disabled segments use an empty range. We still build them to keep the sbt valid.
		RangeInfo.primitiveCount = (Segment.bEnabled) ? Segment.NumPrimitives : 0;
		RangeInfo.primitiveOffset = PrimitiveOffset;
		RangeInfo.transformOffset = 0;

		BuildData.Ranges.Add(RangeInfo);

		PrimitiveCounts.Add(Segment.NumPrimitives);
	}

	BuildData.GeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	BuildData.GeometryInfo.flags = (EnumHasAnyFlags(BuildFlags, ERayTracingAccelerationStructureFlags::FastBuild))
		? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR 
		: VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	if (EnumHasAnyFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowUpdate))
	{
		BuildData.GeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	}
	if (EnumHasAnyFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowCompaction))
	{
		BuildData.GeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
	}
#if VULKAN_SUPPORTS_RAY_TRACING_POSITION_FETCH
	BuildData.GeometryInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR;
#endif

	BuildData.GeometryInfo.mode = (BuildMode == EAccelerationStructureBuildMode::Build) ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	BuildData.GeometryInfo.geometryCount = BuildData.Segments.Num();
	BuildData.GeometryInfo.pGeometries = BuildData.Segments.GetData();

	VulkanDynamicAPI::vkGetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildData.GeometryInfo,
		PrimitiveCounts.GetData(),
		&BuildData.SizesInfo);
}

// This structure is analogous to FHitGroupSystemParameters in D3D12 RHI.
// However, it only contains generic parameters that do not require a full shader binding table (i.e. no per-hit-group user data).
// It is designed to be used to access vertex and index buffers during inline ray tracing.
struct FVulkanRayTracingGeometryParameters
{
	union
	{
		struct
		{
			uint32 IndexStride : 8; // Can be just 1 bit to indicate 16 or 32 bit indices
			uint32 VertexStride : 8; // Can be just 2 bits to indicate float3, float2 or half2 format
			uint32 Unused : 16;
		} Config;
		uint32 ConfigBits = 0;
	};
	uint32 IndexBufferOffsetInBytes = 0;
	uint64 IndexBuffer = 0;
	uint64 VertexBuffer = 0;
};

FVulkanRayTracingGeometry::FVulkanRayTracingGeometry(ENoInit)
{}

FVulkanRayTracingGeometry::FVulkanRayTracingGeometry(FRHICommandListBase& RHICmdList, const FRayTracingGeometryInitializer& InInitializer, FVulkanDevice* InDevice)
	: FRHIRayTracingGeometry(InInitializer), Device(InDevice)
{
	INC_DWORD_STAT(STAT_VulkanRayTracingAllocatedBLAS);

	DebugName = !Initializer.DebugName.IsNone() ? Initializer.DebugName : FDebugName(FName(TEXT("BLAS")));
	OwnerName = Initializer.OwnerName;

	uint32 IndexBufferStride = 0;
	if (Initializer.IndexBuffer)
	{
		// In case index buffer in initializer is not yet in valid state during streaming we assume the geometry is using UINT32 format.
		IndexBufferStride = Initializer.IndexBuffer->GetSize() > 0
			? Initializer.IndexBuffer->GetStride()
			: 4;
	}

	checkf(!Initializer.IndexBuffer || (IndexBufferStride == 2 || IndexBufferStride == 4), TEXT("Index buffer must be 16 or 32 bit if in use."));

	SizeInfo = RHICalcRayTracingGeometrySize(Initializer);

	// If this RayTracingGeometry going to be used as streaming destination 
	// we don't want to allocate its memory as it will be replaced later by streamed version
	// but we still need correct SizeInfo as it is used to estimate its memory requirements outside of RHI.
	if (Initializer.Type == ERayTracingGeometryInitializerType::StreamingDestination)
	{
		return;
	}

	FString DebugNameString = Initializer.DebugName.ToString();
	FRHIResourceCreateInfo BlasBufferCreateInfo(*DebugNameString);
	AccelerationStructureBuffer = ResourceCast(RHICmdList.CreateBuffer(SizeInfo.ResultSize, BUF_AccelerationStructure, 0, ERHIAccess::BVHWrite, BlasBufferCreateInfo).GetReference());

	VkDevice NativeDevice = Device->GetInstanceHandle();

	VkAccelerationStructureCreateInfoKHR CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	CreateInfo.buffer = AccelerationStructureBuffer->GetHandle();
	CreateInfo.offset = AccelerationStructureBuffer->GetOffset();
	CreateInfo.size = SizeInfo.ResultSize;
	CreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	VERIFYVULKANRESULT(VulkanDynamicAPI::vkCreateAccelerationStructureKHR(NativeDevice, &CreateInfo, VULKAN_CPU_ALLOCATOR, &Handle));
	VULKAN_SET_DEBUG_NAME(*Device, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, Handle, TEXT("%s"), *DebugName.ToString());

	INC_MEMORY_STAT_BY(STAT_VulkanRayTracingUsedVideoMemory, SizeInfo.ResultSize);
	INC_MEMORY_STAT_BY(STAT_VulkanRayTracingBLASMemory, SizeInfo.ResultSize);
	if (Initializer.bAllowUpdate)
	{
		INC_MEMORY_STAT_BY(STAT_VulkanRayTracingDynamicBLASMemory, SizeInfo.ResultSize);
	}
	else
	{
		INC_MEMORY_STAT_BY(STAT_VulkanRayTracingStaticBLASMemory, SizeInfo.ResultSize);
	}

	VkAccelerationStructureDeviceAddressInfoKHR DeviceAddressInfo;
	ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
	DeviceAddressInfo.accelerationStructure = Handle;
	Address = VulkanDynamicAPI::vkGetAccelerationStructureDeviceAddressKHR(NativeDevice, &DeviceAddressInfo);

	INC_DWORD_STAT_BY(STAT_VulkanRayTracingTrianglesBLAS, Initializer.TotalPrimitiveCount);
}

FVulkanRayTracingGeometry::~FVulkanRayTracingGeometry()
{
	ReleaseBindlessHandles();

	DEC_DWORD_STAT(STAT_VulkanRayTracingAllocatedBLAS);
	DEC_DWORD_STAT_BY(STAT_VulkanRayTracingTrianglesBLAS, Initializer.TotalPrimitiveCount);
	if (Handle != VK_NULL_HANDLE)
	{
		DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
		DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingBLASMemory, AccelerationStructureBuffer->GetSize());

		ERayTracingAccelerationStructureFlags BuildFlags = GetRayTracingAccelerationStructureBuildFlags(Initializer);
		if (EnumHasAllFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowUpdate))
		{
			DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingDynamicBLASMemory, AccelerationStructureBuffer->GetSize());
		}
		else
		{
			DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingStaticBLASMemory, AccelerationStructureBuffer->GetSize());
		}

		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::AccelerationStructure, Handle);
	}	

	RemoveCompactionRequest();
}

void FVulkanRayTracingGeometry::Swap(FVulkanRayTracingGeometry& Other)
{
	::Swap(Handle, Other.Handle);
	::Swap(Address, Other.Address);
	::Swap(AccelerationStructureCompactedSize, Other.AccelerationStructureCompactedSize);

	AccelerationStructureBuffer = Other.AccelerationStructureBuffer;

	Initializer = Other.Initializer;

	// TODO: Update HitGroup Parameters
}

void FVulkanRayTracingGeometry::RemoveCompactionRequest()
{
	if (bHasPendingCompactionRequests)
	{
		check(AccelerationStructureBuffer);
		bool bRequestFound = Device->GetRayTracingCompactionRequestHandler()->ReleaseRequest(this);
		check(bRequestFound);
		bHasPendingCompactionRequests = false;
	}
}

void FVulkanRayTracingGeometry::CompactAccelerationStructure(FVulkanCmdBuffer& CmdBuffer, uint64 InSizeAfterCompaction)
{
	check(bHasPendingCompactionRequests);
	bHasPendingCompactionRequests = false;

	ensureMsgf(InSizeAfterCompaction > 0, TEXT("Compacted acceleration structure size is expected to be non-zero. This error suggests that GPU readback synchronization is broken."));
	if (InSizeAfterCompaction == 0)
	{
		return;
	}

	DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
	DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingBLASMemory, AccelerationStructureBuffer->GetSize());
	DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingStaticBLASMemory, AccelerationStructureBuffer->GetSize());

	// Move old AS into this temporary variable which gets released when this function returns	
	TRefCountPtr<FVulkanResourceMultiBuffer> OldAccelerationStructure = AccelerationStructureBuffer;
	VkAccelerationStructureKHR OldHandle = Handle;

	FString DebugNameString = Initializer.DebugName.ToString();
	FRHIResourceCreateInfo BlasBufferCreateInfo(*DebugNameString);
	AccelerationStructureBuffer = new FVulkanResourceMultiBuffer(Device, FRHIBufferDesc(InSizeAfterCompaction, 0, BUF_AccelerationStructure), BlasBufferCreateInfo);
	

	INC_MEMORY_STAT_BY(STAT_VulkanRayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
	INC_MEMORY_STAT_BY(STAT_VulkanRayTracingBLASMemory, AccelerationStructureBuffer->GetSize());
	INC_MEMORY_STAT_BY(STAT_VulkanRayTracingStaticBLASMemory, AccelerationStructureBuffer->GetSize());

	VkDevice NativeDevice = Device->GetInstanceHandle();

	VkAccelerationStructureCreateInfoKHR CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
	CreateInfo.buffer = AccelerationStructureBuffer->GetHandle();
	CreateInfo.offset = AccelerationStructureBuffer->GetOffset();
	CreateInfo.size = InSizeAfterCompaction;
	CreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	VERIFYVULKANRESULT(VulkanDynamicAPI::vkCreateAccelerationStructureKHR(NativeDevice, &CreateInfo, VULKAN_CPU_ALLOCATOR, &Handle));
	VULKAN_SET_DEBUG_NAME(*Device, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, Handle, TEXT("%s (compact)"), *DebugName.ToString());
	VkAccelerationStructureDeviceAddressInfoKHR DeviceAddressInfo;
	ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
	DeviceAddressInfo.accelerationStructure = Handle;
	Address = VulkanDynamicAPI::vkGetAccelerationStructureDeviceAddressKHR(NativeDevice, &DeviceAddressInfo);

	// Add a barrier to make sure acceleration structure are synchronized correctly for the copy command.
	AddAccelerationStructureBuildBarrier(CmdBuffer.GetHandle());

	VkCopyAccelerationStructureInfoKHR CopyInfo;
	ZeroVulkanStruct(CopyInfo, VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR);
	CopyInfo.src = OldHandle;
	CopyInfo.dst = Handle;
	CopyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
	VulkanDynamicAPI::vkCmdCopyAccelerationStructureKHR(CmdBuffer.GetHandle(), &CopyInfo);

	AccelerationStructureCompactedSize = InSizeAfterCompaction;

	Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::AccelerationStructure, OldHandle);
}


void FVulkanRayTracingGeometry::SetupHitGroupSystemParameters()
{
	const bool bIsTriangles = (Initializer.GeometryType == RTGT_Triangles);

	FVulkanBindlessDescriptorManager* BindlessDescriptorManager = Device->GetBindlessDescriptorManager();
	auto GetBindlessHandle = [BindlessDescriptorManager](FVulkanResourceMultiBuffer* Buffer, uint32 ExtraOffset) 
	{
		if (Buffer)
		{
			FRHIDescriptorHandle BindlessHandle = BindlessDescriptorManager->ReserveDescriptor(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
			BindlessDescriptorManager->UpdateBuffer(BindlessHandle, Buffer->GetHandle(), Buffer->GetOffset() + ExtraOffset, Buffer->GetCurrentSize() - ExtraOffset);
			return BindlessHandle;
		}
		return FRHIDescriptorHandle();
	};

	ReleaseBindlessHandles();

	HitGroupSystemParameters.Reset(Initializer.Segments.Num());

	FVulkanResourceMultiBuffer* IndexBuffer = ResourceCast(Initializer.IndexBuffer.GetReference());
	const uint32 IndexStride = IndexBuffer ? IndexBuffer->GetStride() : 0;
	HitGroupSystemIndexView = GetBindlessHandle(IndexBuffer, 0);

	for (const FRayTracingGeometrySegment& Segment : Initializer.Segments)
	{
		FVulkanResourceMultiBuffer* VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());
		const FRHIDescriptorHandle VBHandle = GetBindlessHandle(VertexBuffer, Segment.VertexBufferOffset);
		HitGroupSystemVertexViews.Add(VBHandle);


		FVulkanHitGroupSystemParameters& SystemParameters = HitGroupSystemParameters.AddZeroed_GetRef();
		SystemParameters.RootConstants.SetVertexAndIndexStride(Segment.VertexBufferStride, IndexStride);
		SystemParameters.BindlessHitGroupSystemVertexBuffer = VBHandle.GetIndex();

		if (bIsTriangles && (IndexBuffer != nullptr))
		{
			SystemParameters.BindlessHitGroupSystemIndexBuffer = HitGroupSystemIndexView.GetIndex();
			SystemParameters.RootConstants.IndexBufferOffsetInBytes = Initializer.IndexBufferOffset + IndexStride * Segment.FirstPrimitive * FVulkanRayTracingGeometry::IndicesPerPrimitive;
			SystemParameters.RootConstants.FirstPrimitive = Segment.FirstPrimitive;
		}
	}
}

void FVulkanRayTracingGeometry::ReleaseBindlessHandles()
{
	FVulkanBindlessDescriptorManager* BindlessDescriptorManager = Device->GetBindlessDescriptorManager();

	for (FRHIDescriptorHandle BindlesHandle : HitGroupSystemVertexViews)
	{
		BindlessDescriptorManager->Unregister(BindlesHandle);
	}
	HitGroupSystemVertexViews.Reset(Initializer.Segments.Num());

	if (HitGroupSystemIndexView.IsValid())
	{
		BindlessDescriptorManager->Unregister(HitGroupSystemIndexView);
		HitGroupSystemIndexView = FRHIDescriptorHandle();
	}
}

void FVulkanRayTracingGeometry::SetupInlineGeometryParameters(uint32 GeometrySegmentIndex, FVulkanRayTracingGeometryParameters& Parameters) const
{
	const FRayTracingGeometryInitializer& GeometryInitializer = GetInitializer();
	const FVulkanResourceMultiBuffer* IndexBuffer = ResourceCast(GeometryInitializer.IndexBuffer.GetReference());

	const uint32 IndexStride = IndexBuffer ? IndexBuffer->GetStride() : 0;
	const uint32 IndexOffsetInBytes = GeometryInitializer.IndexBufferOffset;
	const VkDeviceAddress IndexBufferAddress = IndexBuffer ? IndexBuffer->GetDeviceAddress() : VkDeviceAddress(0);

	const FRayTracingGeometrySegment& Segment = GeometryInitializer.Segments[GeometrySegmentIndex];

	const FVulkanResourceMultiBuffer* VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());
	checkf(VertexBuffer, TEXT("All ray tracing geometry segments must have a valid vertex buffer"));
	const VkDeviceAddress VertexBufferAddress = VertexBuffer->GetDeviceAddress();

	Parameters.Config.IndexStride = IndexStride;
	Parameters.Config.VertexStride = Segment.VertexBufferStride;
	if (IndexStride)
	{
		Parameters.IndexBufferOffsetInBytes = IndexOffsetInBytes + IndexStride * Segment.FirstPrimitive * 3;
		Parameters.IndexBuffer = static_cast<uint64>(IndexBufferAddress);
	}
	else
	{
		Parameters.IndexBuffer = 0;
	}
	Parameters.VertexBuffer = static_cast<uint64>(VertexBufferAddress) + Segment.VertexBufferOffset;
}

static void GetTLASBuildData(
	const VkDevice Device,
	const uint32 NumInstances,
	const VkDeviceAddress InstanceBufferAddress,
	ERayTracingAccelerationStructureFlags BuildFlags,
	EAccelerationStructureBuildMode BuildMode,
	FVkRtTLASBuildData& BuildData)
{
	VkDeviceOrHostAddressConstKHR InstanceBufferDeviceAddress = {};
	InstanceBufferDeviceAddress.deviceAddress = InstanceBufferAddress;

	BuildData.Geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	BuildData.Geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	BuildData.Geometry.geometry.instances.arrayOfPointers = VK_FALSE;
	BuildData.Geometry.geometry.instances.data = InstanceBufferDeviceAddress;

	BuildData.GeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	BuildData.GeometryInfo.mode = BuildMode == EAccelerationStructureBuildMode::Build ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	BuildData.GeometryInfo.flags = TranslateRayTracingAccelerationStructureFlags(BuildFlags);
	BuildData.GeometryInfo.geometryCount = 1;
	BuildData.GeometryInfo.pGeometries = &BuildData.Geometry;

	VulkanDynamicAPI::vkGetAccelerationStructureBuildSizesKHR(
		Device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&BuildData.GeometryInfo,
		&NumInstances,
		&BuildData.SizesInfo);
}

static VkGeometryInstanceFlagsKHR TranslateRayTracingInstanceFlags(ERayTracingInstanceFlags InFlags)
{
	VkGeometryInstanceFlagsKHR Result = 0;

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::TriangleCullDisable))
	{
		Result |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	}

	if (!EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::TriangleCullReverse))
	{
		// Counterclockwise is the default for UE.
		Result |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
	}

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::ForceOpaque))
	{
		Result |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	}

	if (EnumHasAnyFlags(InFlags, ERayTracingInstanceFlags::ForceNonOpaque))
	{
		Result |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
	}

	return Result;
}

FVulkanRayTracingScene::FVulkanRayTracingScene(FRayTracingSceneInitializer InInitializer, FVulkanDevice* InDevice)
	: FDeviceChild(InDevice)
	, Initializer(MoveTemp(InInitializer))

{
	INC_DWORD_STAT(STAT_VulkanRayTracingAllocatedTLAS);

	SizeInfo = RHICalcRayTracingSceneSize(Initializer);
}

FVulkanRayTracingScene::~FVulkanRayTracingScene()
{
	for (auto& Item : ShaderTables)
	{
		delete Item.Value;
	}

	if (AccelerationStructureBuffer)
	{
		DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
		DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingTLASMemory, AccelerationStructureBuffer->GetSize());
	}
	DEC_DWORD_STAT(STAT_VulkanRayTracingAllocatedTLAS);
}

void FVulkanRayTracingScene::BindBuffer(FRHIBuffer* InBuffer, uint32 InBufferOffset)
{
	check(IsInRHIThread() || !IsRunningRHIInSeparateThread());

	check(SizeInfo.ResultSize + InBufferOffset <= InBuffer->GetSize());
	
	if (AccelerationStructureBuffer)
	{
		DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
		DEC_MEMORY_STAT_BY(STAT_VulkanRayTracingTLASMemory, AccelerationStructureBuffer->GetSize());
	}

	AccelerationStructureBuffer = ResourceCast(InBuffer);

	INC_MEMORY_STAT_BY(STAT_VulkanRayTracingUsedVideoMemory, AccelerationStructureBuffer->GetSize());
	INC_MEMORY_STAT_BY(STAT_VulkanRayTracingTLASMemory, AccelerationStructureBuffer->GetSize());

	{
		checkf(!View.IsValid(), TEXT("Binding multiple buffers is not currently supported."));

		check(InBufferOffset % GRHIRayTracingAccelerationStructureAlignment == 0);

		View = MakeUnique<FVulkanView>(*Device, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
		VkAccelerationStructureKHR NativeAccelerationStructureHandle = View->InitAsAccelerationStructureView(
			AccelerationStructureBuffer
			, InBufferOffset
			//, SizeInfo.ResultSize
			// TODO: Using whole remaining size instead of SizeInfo.ResultSize reintroduces a validation error but use of SizeInfo.ResultSize broke RT on Adreno.
			, InBuffer->GetSize() - InBufferOffset
		)->GetAccelerationStructureView().Handle;

		FString DebugNameString = Initializer.DebugName.ToString();
		DebugNameString = (DebugNameString.IsEmpty()) ? TEXT("TLAS") : DebugNameString;
		VULKAN_SET_DEBUG_NAME(*Device, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, NativeAccelerationStructureHandle, TEXT("%s"), *DebugNameString);
	}
}

void BuildAccelerationStructure(
	FVulkanCommandListContext& CommandContext,
	FVulkanRayTracingScene& Scene,
	FVulkanResourceMultiBuffer* InScratchBuffer, uint32 InScratchOffset,
	FVulkanResourceMultiBuffer* InInstanceBuffer, uint32 InInstanceOffset,
	uint32 NumInstances,
	EAccelerationStructureBuildMode BuildMode)
{
	check(InInstanceBuffer != nullptr);
	checkf(NumInstances <= Scene.Initializer.MaxNumInstances, TEXT("NumInstances must be less or equal to MaxNumInstances"));

	checkf(Scene.AccelerationStructureBuffer.IsValid(), TEXT("A buffer must be bound to the ray tracing scene before it can be built."));
	checkf(Scene.View.IsValid(), TEXT("A buffer must be bound to the ray tracing scene before it can be built."));

	const bool bIsUpdate = BuildMode == EAccelerationStructureBuildMode::Update;

	if (bIsUpdate)
	{
		checkf(NumInstances == Scene.NumInstances, TEXT("Number of instances used to update TLAS must match the number used to build."));
	}
	else
	{
		Scene.NumInstances = NumInstances;
	}

	FBufferRHIRef ScratchBuffer;
	{
		TRHICommandList_RecursiveHazardous<FVulkanCommandListContext> RHICmdList(&CommandContext);

		if (InScratchBuffer == nullptr)
		{
			const uint64 ScratchBufferSize = bIsUpdate ? Scene.SizeInfo.UpdateScratchSize : Scene.SizeInfo.BuildScratchSize;

			FRHIResourceCreateInfo ScratchBufferCreateInfo(TEXT("BuildScratchTLAS"));
			ScratchBuffer = RHICmdList.CreateBuffer(ScratchBufferSize, BUF_StructuredBuffer | BUF_RayTracingScratch, 0, ERHIAccess::UAVCompute, ScratchBufferCreateInfo);
			InScratchBuffer = ResourceCast(ScratchBuffer.GetReference());
			InScratchOffset = 0;
		}
	}

	if (bIsUpdate)
	{
		checkf(InScratchBuffer, TEXT("TLAS update requires scratch buffer of at least %lld bytes."), Scene.SizeInfo.UpdateScratchSize);
	}
	else
	{
		checkf(InScratchBuffer, TEXT("TLAS build requires scratch buffer of at least %lld bytes."), Scene.SizeInfo.BuildScratchSize);
	}

	FVkRtTLASBuildData BuildData;

	VkAccelerationStructureBuildRangeInfoKHR BuildRangeInfo;
	VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos;

	const VkDeviceAddress InstanceBufferAddress = InInstanceBuffer->GetDeviceAddress() + InInstanceOffset;

	{
		GetTLASBuildData(Scene.GetParent()->GetInstanceHandle(), NumInstances, InstanceBufferAddress, Scene.Initializer.BuildFlags, BuildMode, BuildData);

		BuildData.GeometryInfo.dstAccelerationStructure = Scene.View->GetAccelerationStructureView().Handle;
		BuildData.GeometryInfo.srcAccelerationStructure = bIsUpdate ? Scene.View->GetAccelerationStructureView().Handle : nullptr;
		BuildData.GeometryInfo.scratchData.deviceAddress = InScratchBuffer->GetDeviceAddress() + InScratchOffset;

		BuildRangeInfo.primitiveCount = NumInstances;
		BuildRangeInfo.primitiveOffset = 0;
		BuildRangeInfo.transformOffset = 0;
		BuildRangeInfo.firstVertex = 0;

		pBuildRangeInfos = &BuildRangeInfo;

		if (bIsUpdate)
		{
			INC_DWORD_STAT(STAT_VulkanRayTracingUpdatedTLAS);
		}
		else
		{
			INC_DWORD_STAT(STAT_VulkanRayTracingBuiltTLAS);
		}
	}

	FVulkanCommandBufferManager& CommandBufferManager = *CommandContext.GetCommandBufferManager();
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();

	// Force a memory barrier to make sure all previous builds ops are finished before building the TLAS
	AddAccelerationStructureBuildBarrier(CmdBuffer->GetHandle());

	VulkanDynamicAPI::vkCmdBuildAccelerationStructuresKHR(CmdBuffer->GetHandle(), 1, &BuildData.GeometryInfo, &pBuildRangeInfos);

	// Acceleration structure build barrier is used here to ensure that the acceleration structure build is complete before any rays are traced
	AddAccelerationStructureBuildBarrier(CmdBuffer->GetHandle());

	CommandBufferManager.SubmitActiveCmdBuffer();
	CommandBufferManager.PrepareForNewActiveCommandBuffer();

	Scene.bBuilt = true;
}

FVulkanRayTracingShaderTable::FVulkanRayTracingShaderTable(FRHICommandListBase& RHICmdList, FVulkanDevice* Device, const FRayTracingShaderBindingTableInitializer& InInitializer)
	: FRHIShaderBindingTable(InInitializer)
	, FDeviceChild(Device)
	, ShaderBindingMode(InInitializer.ShaderBindingMode)
	, HitGroupIndexingMode(InInitializer.HitGroupIndexingMode)
	, HandleSize(Device->GetOptionalExtensionProperties().RayTracingPipelineProps.shaderGroupHandleSize)
	, HandleSizeAligned(Align(HandleSize, Device->GetOptionalExtensionProperties().RayTracingPipelineProps.shaderGroupHandleAlignment))
{
	check(ShaderBindingMode != ERayTracingShaderBindingMode::Disabled);

	if (EnumHasAnyFlags(ShaderBindingMode, ERayTracingShaderBindingMode::RTPSO))
	{
		auto InitAlloc = [HandleSizeAligned = HandleSizeAligned](FVulkanShaderTableAllocation& Alloc, uint32 InHandleCount, bool InUseLocalRecord)
		{
			Alloc.HandleCount = InHandleCount;
			Alloc.bUseLocalRecord = InUseLocalRecord;

			if (Alloc.HandleCount > 0)
			{
				if (InUseLocalRecord)
				{
					Alloc.Region.stride = (Alloc.HandleCount > 1) ? (uint32)GVulkanRayTracingMaxShaderGroupStride : 0;
					Alloc.Region.size = Alloc.HandleCount * (uint32)GVulkanRayTracingMaxShaderGroupStride;

				}
				else
				{
					checkSlow(InHandleCount == 1);
					Alloc.Region.stride = HandleSizeAligned;
					Alloc.Region.size = HandleSizeAligned;
				}

				// Host buffer
				Alloc.HostBuffer.SetNumUninitialized(Alloc.Region.size);
			}
		};

		InitAlloc(Raygen, 1, false);
		InitAlloc(Miss, Initializer.NumMissShaderSlots, true);
		InitAlloc(Callable, Initializer.NumCallableShaderSlots, true);

		uint32 NumHitGroupRecords = HitGroupIndexingMode == ERayTracingHitGroupIndexingMode::Allow ? Initializer.NumGeometrySegments * Initializer.NumShaderSlotsPerGeometrySegment : 1;
		InitAlloc(HitGroup, NumHitGroupRecords, true);
	}

	if (EnumHasAnyFlags(ShaderBindingMode, ERayTracingShaderBindingMode::Inline) && Initializer.NumGeometrySegments > 0)
	{
		// Doesn't make sense to have inline SBT without hitgroup indexing
		check(HitGroupIndexingMode == ERayTracingHitGroupIndexingMode::Allow);

		const uint32 ParameterBufferSize = Initializer.NumGeometrySegments * sizeof(FVulkanRayTracingGeometryParameters);
		InlineGeometryParameterData.SetNumUninitialized(ParameterBufferSize);
		
		FRHIResourceCreateInfo ParameterBufferCreateInfo(TEXT("RayTracingSceneMetadata"));
		InlineGeometryParameterBuffer = ResourceCast(RHICmdList.CreateBuffer(ParameterBufferSize, BUF_Dynamic | BUF_StructuredBuffer | BUF_ShaderResource, sizeof(FVulkanRayTracingGeometryParameters), ERHIAccess::SRVMask, ParameterBufferCreateInfo).GetReference());
	}
}

FVulkanRayTracingShaderTable::~FVulkanRayTracingShaderTable()
{
	ReleaseLocalBuffers();
}

void FVulkanRayTracingShaderTable::ReleaseLocalBuffers()
{
	ReleaseLocalBuffer(Device, Raygen);
	ReleaseLocalBuffer(Device, Miss);
	ReleaseLocalBuffer(Device, HitGroup);
	ReleaseLocalBuffer(Device, Callable);
}

void FVulkanRayTracingShaderTable::ReleaseLocalBuffer(FVulkanDevice* Device, FVulkanShaderTableAllocation& Alloc)
{
	if (Alloc.LocalBuffer != VK_NULL_HANDLE)
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Buffer, Alloc.LocalBuffer);
		Alloc.LocalBuffer = VK_NULL_HANDLE;
	}

	if (Alloc.LocalAllocation.IsValid())
	{
		Device->GetMemoryManager().FreeVulkanAllocation(Alloc.LocalAllocation);
	}

	Alloc.Region.deviceAddress = 0;
}

FVulkanRayTracingShaderTable::FVulkanShaderTableAllocation& FVulkanRayTracingShaderTable::GetAlloc(EShaderFrequency Frequency)
{
	switch (Frequency)
	{
	case SF_RayGen:       return Raygen;
	case SF_RayMiss:      return Miss;
	case SF_RayHitGroup:  return HitGroup;
	case SF_RayCallable:  return Callable;

	default:
		checkf(false, TEXT("Only usable with RayTracing shaders."));
		break;
	}

	static FVulkanShaderTableAllocation EmptyAlloc;
	return EmptyAlloc;
}

const VkStridedDeviceAddressRegionKHR* FVulkanRayTracingShaderTable::GetRegion(EShaderFrequency Frequency)
{
	const FVulkanShaderTableAllocation& Alloc = GetAlloc(Frequency);
	check(!Alloc.bIsDirty);
	return &Alloc.Region;
}

void FVulkanRayTracingShaderTable::SetSlot(EShaderFrequency Frequency, uint32 DstSlot, uint32 SrcHandleIndex, TConstArrayView<uint8> SrcHandleData)
{
	FVulkanShaderTableAllocation& Alloc = GetAlloc(Frequency);
	checkf((DstSlot == 0) || (Alloc.Region.stride != 0), TEXT("Attempting to index a record in a region without stride"));
	FMemory::Memcpy(&Alloc.HostBuffer[DstSlot * Alloc.Region.stride], &SrcHandleData[SrcHandleIndex * HandleSize], HandleSize);
	Alloc.bIsDirty = true;
}

void FVulkanRayTracingShaderTable::CommitRayGenShader(FVulkanCommandListContext& Context, EShaderFrequency ShaderFrequency, uint32 SrcHandleIndex, TConstArrayView<uint8> SrcHandleData)
{
	UE::TScopeLock Lock(RaygenMutex);
	SetSlot(ShaderFrequency, 0, SrcHandleIndex, SrcHandleData);
	Commit(Context);
}

void FVulkanRayTracingShaderTable::SetLocalShaderParameters(EShaderFrequency Frequency, uint32 RecordIndex, uint32 OffsetWithinRecord, const void* InData, uint32 InDataSize)
{
	FVulkanShaderTableAllocation& Alloc = GetAlloc(Frequency);

	checkfSlow(OffsetWithinRecord % 4 == 0, TEXT("SBT record parameters must be written on DWORD-aligned boundary"));
	checkfSlow(InDataSize % 4 == 0, TEXT("SBT record parameters must be DWORD-aligned"));
	checkf(OffsetWithinRecord + InDataSize <= Alloc.Region.stride ? Alloc.Region.stride : Alloc.Region.size, TEXT("SBT record write request is out of bounds"));
	checkf((RecordIndex == 0) || (Alloc.Region.stride != 0), TEXT("Attempting to index a record in a region without stride"));

	const uint32 WriteOffset = HandleSizeAligned + (Alloc.Region.stride * RecordIndex) + OffsetWithinRecord;
	FMemory::Memcpy(&Alloc.HostBuffer[WriteOffset], InData, InDataSize);

	Alloc.bIsDirty = true;
}

void FVulkanRayTracingShaderTable::SetInlineGeometryParameters(uint32 SegmentIndex, const void* InData, uint32 InDataSize)
{
	const uint32 WriteOffset = InDataSize * SegmentIndex;
	FMemory::Memcpy(&InlineGeometryParameterData[WriteOffset], InData, InDataSize);
}

#if VULKAN_SUPPORTS_RAY_TRACING_POSITION_FETCH

// Metadata buffer is unused with the support of position_fetch
FRHIShaderResourceView* FVulkanRayTracingShaderTable::GetOrCreateInlineBufferSRV(FRHICommandListBase& RHICmdList) { return nullptr; }

#else

FRHIShaderResourceView* FVulkanRayTracingShaderTable::GetOrCreateInlineBufferSRV(FRHICommandListBase& RHICmdList)
{
	if (!InlineGeometryParameterSRV.IsValid())
	{
		InlineGeometryParameterSRV = RHICmdList.CreateShaderResourceView(InlineGeometryParameterBuffer.GetReference(), FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Structured));
	}

	return InlineGeometryParameterSRV.GetReference();
}

#endif // VULKAN_SUPPORTS_RAY_TRACING_POSITION_FETCH

void FVulkanRayTracingShaderTable::Commit(FVulkanCommandListContext& Context)
{
	FVulkanCommandBufferManager* CommandBufferManager = Context.GetCommandBufferManager();
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();

	VkMemoryBarrier BarrierBefore = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT };
	VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &BarrierBefore, 0, nullptr, 0, nullptr);

	auto CommitBuffer = [Device = Device, CmdBuffer](FVulkanShaderTableAllocation& Alloc)
	{
		if (Alloc.bIsDirty)
		{
			if (!Alloc.HostBuffer.IsEmpty())
			{
				ReleaseLocalBuffer(Device, Alloc);

				const VkDevice DeviceHandle = Device->GetInstanceHandle();
				const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& RayTracingPipelineProps = Device->GetOptionalExtensionProperties().RayTracingPipelineProps;

				// Fetch staging buffer and fill it
				VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(Alloc.Region.size);
				FMemory::Memcpy(StagingBuffer->GetMappedPointer(), Alloc.HostBuffer.GetData(), Alloc.Region.size);

				// Alloc a new Local buffer
				{
					VkBufferCreateInfo BufferCreateInfo;
					ZeroVulkanStruct(BufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
					BufferCreateInfo.size = Alloc.Region.size;
					BufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
					VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(DeviceHandle, &BufferCreateInfo, VULKAN_CPU_ALLOCATOR, &Alloc.LocalBuffer));

					const VulkanRHI::EVulkanAllocationFlags AllocFlags = VulkanRHI::EVulkanAllocationFlags::AutoBind | VulkanRHI::EVulkanAllocationFlags::Dedicated;
					Device->GetMemoryManager().AllocateBufferMemory(Alloc.LocalAllocation, Alloc.LocalBuffer, AllocFlags, TEXT("LocalShaderTableAllocation"), RayTracingPipelineProps.shaderGroupBaseAlignment);

					VkBufferDeviceAddressInfoKHR DeviceAddressInfo;
					ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
					DeviceAddressInfo.buffer = Alloc.LocalBuffer;
					Alloc.Region.deviceAddress = VulkanRHI::vkGetBufferDeviceAddressKHR(DeviceHandle, &DeviceAddressInfo);
				}

				VkBufferCopy RegionInfo;
				RegionInfo.srcOffset = 0;
				RegionInfo.dstOffset = 0;
				RegionInfo.size = Alloc.Region.size;
				VulkanRHI::vkCmdCopyBuffer(CmdBuffer->GetHandle(), StagingBuffer->GetHandle(), Alloc.LocalBuffer, 1, &RegionInfo);

				Device->GetStagingManager().ReleaseBuffer(CmdBuffer, StagingBuffer);
			}
			else
			{
				checkSlow(Alloc.LocalBuffer == VK_NULL_HANDLE);
			}

			Alloc.bIsDirty = false;
		}
	};

	CommitBuffer(Raygen);
	CommitBuffer(Miss);
	CommitBuffer(HitGroup);
	CommitBuffer(Callable);

#if !VULKAN_SUPPORTS_RAY_TRACING_POSITION_FETCH
	// Also copy geometry parameter data to the GPU buffer
	if (InlineGeometryParameterBuffer)
	{
		TRHICommandList_RecursiveHazardous<FVulkanCommandListContext> RHICmdList(&Context);

		const uint32 ParameterBufferSize = InlineGeometryParameterData.Num();		
		void* MappedBuffer = InlineGeometryParameterBuffer->Lock(RHICmdList, RLM_WriteOnly, ParameterBufferSize, 0);
		FMemory::Memcpy(MappedBuffer, InlineGeometryParameterData.GetData(), ParameterBufferSize);
		InlineGeometryParameterBuffer->Unlock(RHICmdList);
	}
#endif // !VULKAN_SUPPORTS_RAY_TRACING_POSITION_FETCH

	VkMemoryBarrier BarrierAfter = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT };  // :todo-jn: VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR
	VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &BarrierAfter, 0, nullptr, 0, nullptr);
}

FRHIShaderBindingTable* FVulkanRayTracingScene::FindOrCreateShaderBindingTable(const FRHIRayTracingPipelineState* InPipeline)
{
	UE::TScopeLock Lock(Mutex);

	const FVulkanRayTracingPipelineState* Pipeline = ResourceCast(InPipeline);

	// Find existing table
	{
		TRefCountPtr<FVulkanRayTracingShaderTable>* FoundShaderTable = ShaderTables.Find(Pipeline);
		if (FoundShaderTable)
		{
			return FoundShaderTable->GetReference();
		}
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FRayTracingShaderBindingTableInitializer SBTInitializer;
	SBTInitializer.NumGeometrySegments = Initializer.NumTotalSegments;
	SBTInitializer.NumShaderSlotsPerGeometrySegment = Initializer.ShaderSlotsPerGeometrySegment;
	SBTInitializer.NumCallableShaderSlots = Initializer.NumCallableShaderSlots;
	SBTInitializer.NumMissShaderSlots = Initializer.NumMissShaderSlots;
	SBTInitializer.HitGroupIndexingMode = Pipeline->bAllowHitGroupIndexing ? ERayTracingHitGroupIndexingMode::Allow : ERayTracingHitGroupIndexingMode::Disallow;
	SBTInitializer.ShaderBindingMode = ERayTracingShaderBindingMode::RTPSO;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// Create new table (use FRHICommandListExecutor::GetImmediateCommandList() directly for now this is deprecated code)
	FVulkanRayTracingShaderTable* CreatedShaderTable = new FVulkanRayTracingShaderTable(FRHICommandListExecutor::GetImmediateCommandList(), Device, MoveTemp(SBTInitializer));
	ShaderTables.Add(Pipeline, CreatedShaderTable);

	return CreatedShaderTable;
}

FRayTracingAccelerationStructureSize FVulkanDynamicRHI::RHICalcRayTracingSceneSize(const FRayTracingSceneInitializer& Initializer)
{
	FVkRtTLASBuildData BuildData;
	const VkDeviceAddress InstanceBufferAddress = 0; // No device address available when only querying TLAS size
	GetTLASBuildData(Device->GetInstanceHandle(), Initializer.MaxNumInstances, InstanceBufferAddress, Initializer.BuildFlags, EAccelerationStructureBuildMode::Build, BuildData);

	FRayTracingAccelerationStructureSize Result;
	Result.ResultSize = BuildData.SizesInfo.accelerationStructureSize;
	Result.BuildScratchSize = BuildData.SizesInfo.buildScratchSize;
	Result.UpdateScratchSize = BuildData.SizesInfo.updateScratchSize;

	return Result;
}

FRayTracingAccelerationStructureSize FVulkanDynamicRHI::RHICalcRayTracingGeometrySize(const FRayTracingGeometryInitializer& Initializer)
{	
	FVkRtBLASBuildData BuildData;
	GetBLASBuildData(
		Device->GetInstanceHandle(),
		MakeArrayView(Initializer.Segments),
		Initializer.GeometryType,
		Initializer.IndexBuffer,
		Initializer.IndexBufferOffset,
		GetRayTracingAccelerationStructureBuildFlags(Initializer),
		EAccelerationStructureBuildMode::Build,
		EBLASBuildDataUsage::Size,
		BuildData);

	FRayTracingAccelerationStructureSize Result;
	Result.ResultSize = Align(BuildData.SizesInfo.accelerationStructureSize, GRHIRayTracingAccelerationStructureAlignment);
	Result.BuildScratchSize = Align(BuildData.SizesInfo.buildScratchSize, GRHIRayTracingScratchBufferAlignment);
	Result.UpdateScratchSize = Align(BuildData.SizesInfo.updateScratchSize, GRHIRayTracingScratchBufferAlignment);
	
	return Result;
}

FRayTracingSceneRHIRef FVulkanDynamicRHI::RHICreateRayTracingScene(FRayTracingSceneInitializer Initializer)
{
	return new FVulkanRayTracingScene(MoveTemp(Initializer), GetDevice());
}

FRayTracingGeometryRHIRef FVulkanDynamicRHI::RHICreateRayTracingGeometry(FRHICommandListBase& RHICmdList, const FRayTracingGeometryInitializer& Initializer)
{
	return new FVulkanRayTracingGeometry(RHICmdList, Initializer, GetDevice());
}

FRayTracingPipelineStateRHIRef FVulkanDynamicRHI::RHICreateRayTracingPipelineState(const FRayTracingPipelineStateInitializer& Initializer)
{
	return new FVulkanRayTracingPipelineState(GetDevice(), Initializer);
}

FShaderBindingTableRHIRef FVulkanDynamicRHI::RHICreateShaderBindingTable(FRHICommandListBase& RHICmdList, const FRayTracingShaderBindingTableInitializer& Initializer)
{
	return new FVulkanRayTracingShaderTable(RHICmdList, GetDevice(), Initializer);
}

void FVulkanCommandListContext::RHIClearRayTracingBindings(FRHIRayTracingScene* InScene)
{
	FVulkanRayTracingScene* Scene = ResourceCast(InScene);
	check(Scene);

	for (auto& Table : Scene->ShaderTables)
	{
		Table.Value->ReleaseLocalBuffers();
	}
}

void FVulkanCommandListContext::RHICommitRayTracingBindings(FRHIRayTracingScene* InScene)
{
	FVulkanRayTracingScene* Scene = ResourceCast(InScene);
	Scene->CommitShaderTables(*this);
}

void FVulkanCommandListContext::RHIClearShaderBindingTable(FRHIShaderBindingTable* InSBT)
{
	FVulkanRayTracingShaderTable* SBT = ResourceCast(InSBT);
	SBT->ReleaseLocalBuffers();
}

void FVulkanCommandListContext::RHICommitShaderBindingTable(FRHIShaderBindingTable* InSBT)
{
	FVulkanRayTracingShaderTable* SBT = ResourceCast(InSBT);
	SBT->Commit(*this);
}

void FVulkanCommandListContext::RHIBindAccelerationStructureMemory(FRHIRayTracingScene* Scene, FRHIBuffer* Buffer, uint32 BufferOffset)
{
	ResourceCast(Scene)->BindBuffer(Buffer, BufferOffset);
}

// Todo: High level rhi call should have transitioned and verified vb and ib to read for each segment
void FVulkanCommandListContext::RHIBuildAccelerationStructures(const TArrayView<const FRayTracingGeometryBuildParams> Params, const FRHIBufferRange& ScratchBufferRange)
{
	checkf(ScratchBufferRange.Buffer != nullptr, TEXT("BuildAccelerationStructures requires valid scratch buffer"));

	// Update geometry vertex buffers
	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		FVulkanRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());

		if (P.Segments.Num())
		{
			checkf(P.Segments.Num() == Geometry->Initializer.Segments.Num(),
				TEXT("If updated segments are provided, they must exactly match existing geometry segments. Only vertex buffer bindings may change."));

			for (int32 i = 0; i < P.Segments.Num(); ++i)
			{
				checkf(P.Segments[i].MaxVertices <= Geometry->Initializer.Segments[i].MaxVertices,
					TEXT("Maximum number of vertices in a segment (%u) must not be smaller than what was declared during FRHIRayTracingGeometry creation (%u), as this controls BLAS memory allocation."),
					P.Segments[i].MaxVertices, Geometry->Initializer.Segments[i].MaxVertices
				);

				Geometry->Initializer.Segments[i].VertexBuffer = P.Segments[i].VertexBuffer;
				Geometry->Initializer.Segments[i].VertexBufferElementType = P.Segments[i].VertexBufferElementType;
				Geometry->Initializer.Segments[i].VertexBufferStride = P.Segments[i].VertexBufferStride;
				Geometry->Initializer.Segments[i].VertexBufferOffset = P.Segments[i].VertexBufferOffset;
			}
		}
	}

	uint32 ScratchBufferSize = ScratchBufferRange.Size ? ScratchBufferRange.Size : ScratchBufferRange.Buffer->GetSize();

	checkf(ScratchBufferSize + ScratchBufferRange.Offset <= ScratchBufferRange.Buffer->GetSize(),
		TEXT("BLAS scratch buffer range size is %lld bytes with offset %lld, but the buffer only has %lld bytes. "),
		ScratchBufferRange.Size, ScratchBufferRange.Offset, ScratchBufferRange.Buffer->GetSize());

	const uint64 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;
	FVulkanResourceMultiBuffer* ScratchBuffer = ResourceCast(ScratchBufferRange.Buffer);
	uint32 ScratchBufferOffset = ScratchBufferRange.Offset;

	TArray<FVkRtBLASBuildData, TInlineAllocator<32>> TempBuildData;
	TArray<VkAccelerationStructureBuildGeometryInfoKHR, TInlineAllocator<32>> BuildGeometryInfos;
	TArray<VkAccelerationStructureBuildRangeInfoKHR*, TInlineAllocator<32>> BuildRangeInfos;
	TempBuildData.Reserve(Params.Num());
	BuildGeometryInfos.Reserve(Params.Num());
	BuildRangeInfos.Reserve(Params.Num());	

	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		FVulkanRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());
		const bool bIsUpdate = P.BuildMode == EAccelerationStructureBuildMode::Update;

		if (bIsUpdate)
		{
			INC_DWORD_STAT(STAT_VulkanRayTracingUpdatedBLAS);
		}
		else
		{
			INC_DWORD_STAT(STAT_VulkanRayTracingBuiltBLAS);
		}

		uint64 ScratchBufferRequiredSize = bIsUpdate ? Geometry->SizeInfo.UpdateScratchSize : Geometry->SizeInfo.BuildScratchSize;
		checkf(ScratchBufferRequiredSize + ScratchBufferOffset <= ScratchBufferSize,
			TEXT("BLAS scratch buffer size is %ld bytes with offset %ld (%ld bytes available), but the build requires %lld bytes. "),
			ScratchBufferSize, ScratchBufferOffset, ScratchBufferSize - ScratchBufferOffset, ScratchBufferRequiredSize);

		FVkRtBLASBuildData& BuildData = TempBuildData.AddDefaulted_GetRef();
		GetBLASBuildData(
			Device->GetInstanceHandle(),
			MakeArrayView(Geometry->Initializer.Segments),
			Geometry->Initializer.GeometryType,
			Geometry->Initializer.IndexBuffer,
			Geometry->Initializer.IndexBufferOffset,
			GetRayTracingAccelerationStructureBuildFlags(Geometry->Initializer),
			P.BuildMode,
			EBLASBuildDataUsage::Rendering,
			BuildData);

		check(BuildData.SizesInfo.accelerationStructureSize <= Geometry->AccelerationStructureBuffer->GetSize());

		BuildData.GeometryInfo.dstAccelerationStructure = Geometry->Handle;
		BuildData.GeometryInfo.srcAccelerationStructure = bIsUpdate ? Geometry->Handle : VK_NULL_HANDLE;

		VkDeviceAddress ScratchBufferAddress = ScratchBuffer->GetDeviceAddress() + ScratchBufferOffset;
		ScratchBufferOffset += ScratchBufferRequiredSize;

		checkf(ScratchBufferAddress % GRHIRayTracingScratchBufferAlignment == 0,
			TEXT("BLAS scratch buffer (plus offset) must be aligned to %ld bytes."),
			GRHIRayTracingScratchBufferAlignment);

		BuildData.GeometryInfo.scratchData.deviceAddress = ScratchBufferAddress;

		VkAccelerationStructureBuildRangeInfoKHR* const pBuildRanges = BuildData.Ranges.GetData();

		BuildGeometryInfos.Add(BuildData.GeometryInfo);
		BuildRangeInfos.Add(pBuildRanges);

		Geometry->SetupHitGroupSystemParameters();
	}
	
	FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	VulkanDynamicAPI::vkCmdBuildAccelerationStructuresKHR(CmdBuffer->GetHandle(), Params.Num(), BuildGeometryInfos.GetData(), BuildRangeInfos.GetData());

	// Add an acceleration structure build barrier after each acceleration structure build batch.
	// This is required because there are currently no explicit read/write barriers
	// for acceleration structures, but we need to ensure that all commands
	// are complete before BLAS is used again on the GPU.
	AddAccelerationStructureBuildBarrier(CmdBuffer->GetHandle());

	CommandBufferManager->SubmitActiveCmdBuffer();
	CommandBufferManager->PrepareForNewActiveCommandBuffer();

	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		FVulkanRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());

		ERayTracingAccelerationStructureFlags GeometryBuildFlags = GetRayTracingAccelerationStructureBuildFlags(Geometry->Initializer);
		if (ShouldCompactAfterBuild(GeometryBuildFlags))
		{
			Device->GetRayTracingCompactionRequestHandler()->RequestCompact(Geometry);
			Geometry->bHasPendingCompactionRequests = true;
		}
	}
}

void FVulkanCommandListContext::RHIBuildAccelerationStructure(const FRayTracingSceneBuildParams& SceneBuildParams)
{
	FVulkanRayTracingScene* const Scene = ResourceCast(SceneBuildParams.Scene);
	FVulkanResourceMultiBuffer* const ScratchBuffer = ResourceCast(SceneBuildParams.ScratchBuffer);
	FVulkanResourceMultiBuffer* const InstanceBuffer = ResourceCast(SceneBuildParams.InstanceBuffer);

	Scene->ReferencedGeometries.Reserve(SceneBuildParams.ReferencedGeometries.Num());

	for (FRHIRayTracingGeometry* ReferencedGeometry : SceneBuildParams.ReferencedGeometries)
	{
		Scene->ReferencedGeometries.Add(ReferencedGeometry);
	}

	Scene->PerInstanceGeometries = SceneBuildParams.PerInstanceGeometries;

	BuildAccelerationStructure(
		*this,
		*Scene,
		ScratchBuffer, SceneBuildParams.ScratchBufferOffset, 
		InstanceBuffer, SceneBuildParams.InstanceBufferOffset,
		SceneBuildParams.NumInstances,
		SceneBuildParams.BuildMode);
}

template<typename ShaderType>
static FRHIRayTracingShader* GetBuiltInRayTracingShader()
{
	const FGlobalShaderMap* const ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	auto Shader = ShaderMap->GetShader<ShaderType>();
	return static_cast<FRHIRayTracingShader*>(Shader.GetRayTracingShader());
}

void FVulkanDevice::InitializeRayTracing()
{
	if (GRHISupportsRayTracingShaders)
	{
		const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& RayTracingPipelineProps = GetOptionalExtensionProperties().RayTracingPipelineProps;
		if ((uint32)GVulkanRayTracingMaxShaderGroupStride > RayTracingPipelineProps.maxShaderGroupStride)
		{
			UE_LOG(LogRHI, Warning, TEXT("Specified value for r.Vulkan.RayTracing.MaxShaderGroupStride is too large for this device! It will be capped."));
		}
		GVulkanRayTracingMaxShaderGroupStride = FMath::Min<VkDeviceSize>(RayTracingPipelineProps.maxShaderGroupStride, GVulkanRayTracingMaxShaderGroupStride);
	}
}

// Temporary code to generate dummy UBs to bind when none is provided to prevent bindless code from crashing
// NOTE: Should currently only be used by InstanceCulling due to a binding that isn't stripped by DXC. See also USE_INSTANCE_CULLING_DATA for same issue in CS.
static FRWLock DummyUBLock;
static TMap<uint32, FUniformBufferRHIRef> DummyUBs;
static FVulkanUniformBuffer* GetDummyUB(FVulkanDevice* Device, uint32 UBLayoutHash)
{
	{
		FRWScopeLock ScopedReadLock(DummyUBLock, SLT_ReadOnly);
		FUniformBufferRHIRef* UBRef = DummyUBs.Find(UBLayoutHash);
		if (UBRef)
		{
			return ResourceCast(UBRef->GetReference());
		}
	}

	FRWScopeLock ScopedReadLock(DummyUBLock, SLT_Write);
	const FShaderParametersMetadata* DummyMetadata = FindUniformBufferStructByLayoutHash(UBLayoutHash);
	if (DummyMetadata && DummyMetadata->GetLayoutPtr())
	{
		const FRHIUniformBufferLayout* DummyLayout = DummyMetadata->GetLayoutPtr();
		TArray<uint8> DummyContent;
		DummyContent.SetNumZeroed(DummyLayout->ConstantBufferSize);
		FVulkanUniformBuffer* DummyUB = new FVulkanUniformBuffer(*Device, DummyLayout, DummyContent.GetData(), UniformBuffer_MultiFrame, EUniformBufferValidation::None);
		DummyUBs.Add(UBLayoutHash, DummyUB);
		const FString& LayoutName = DummyLayout->GetDebugName();
		UE_LOG(LogRHI, Warning, TEXT("Vulkan ray tracing using DummyUB for %s."), LayoutName.IsEmpty() ? TEXT("<unknown>") : *LayoutName);
		return DummyUB;
	}
	return nullptr;
}

void FVulkanDevice::CleanUpRayTracing()
{
	DummyUBs.Empty();
}


FVulkanRayTracingPipelineState::FVulkanRayTracingPipelineState(FVulkanDevice* const InDevice, const FRayTracingPipelineStateInitializer& Initializer)
	: FDeviceChild(InDevice)
{
	checkf(InDevice->SupportsBindless(), TEXT("Vulkan ray tracing pipelines are only supported in bindless."));

	TArrayView<FRHIRayTracingShader*> InitializerRayGenShaders = Initializer.GetRayGenTable();
	TArrayView<FRHIRayTracingShader*> InitializerMissShaders = Initializer.GetMissTable();
	TArrayView<FRHIRayTracingShader*> InitializerHitGroupShaders = Initializer.GetHitGroupTable();
	TArrayView<FRHIRayTracingShader*> InitializerCallableShaders = Initializer.GetCallableTable();

	TArray<VkPipelineShaderStageCreateInfo> ShaderStages;
	TArray<VkRayTracingShaderGroupCreateInfoKHR> ShaderGroups;
	TArray<ANSICHAR*> EntryPointNames;
	const uint32 EntryPointNameMaxLength = 24;

	RayGen.Shaders.Reserve(InitializerRayGenShaders.Num());
	for (FRHIRayTracingShader* const RayGenShaderRHI : InitializerRayGenShaders)
	{
		checkSlow(RayGenShaderRHI->GetFrequency() == SF_RayGen);
		FVulkanRayTracingShader* const RayGenShader = ResourceCast(RayGenShaderRHI);

		VkPipelineShaderStageCreateInfo ShaderStage;
		ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		ShaderStage.module = RayGenShader->GetOrCreateHandle(FVulkanRayTracingShader::MainModuleIdentifier)->GetVkShaderModule();
		ShaderStage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
			
		ANSICHAR* const EntryPoint = new ANSICHAR[EntryPointNameMaxLength];
		RayGenShader->GetEntryPoint(EntryPoint, EntryPointNameMaxLength);
		EntryPointNames.Add(EntryPoint);
		ShaderStage.pName = EntryPoint;
		ShaderStages.Add(ShaderStage);

		VkRayTracingShaderGroupCreateInfoKHR ShaderGroup;
		ZeroVulkanStruct(ShaderGroup, VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
		ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		ShaderGroup.generalShader = ShaderStages.Num() - 1;
		ShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups.Add(ShaderGroup);

		RayGen.Shaders.Add(RayGenShader);
	}

	Miss.Shaders.Reserve(InitializerMissShaders.Num());
	for (FRHIRayTracingShader* const MissShaderRHI : InitializerMissShaders)
	{
		checkSlow(MissShaderRHI->GetFrequency() == SF_RayMiss);
		FVulkanRayTracingShader* const MissShader = ResourceCast(MissShaderRHI);

		VkPipelineShaderStageCreateInfo ShaderStage;
		ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		ShaderStage.module = MissShader->GetOrCreateHandle(FVulkanRayTracingShader::MainModuleIdentifier)->GetVkShaderModule();
		ShaderStage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;

		ANSICHAR* const EntryPoint = new char[EntryPointNameMaxLength];
		MissShader->GetEntryPoint(EntryPoint, EntryPointNameMaxLength);
		EntryPointNames.Add(EntryPoint);
		ShaderStage.pName = EntryPoint;
		ShaderStages.Add(ShaderStage);

		VkRayTracingShaderGroupCreateInfoKHR ShaderGroup;
		ZeroVulkanStruct(ShaderGroup, VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
		ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		ShaderGroup.generalShader = ShaderStages.Num() - 1;
		ShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups.Add(ShaderGroup);

		Miss.Shaders.Add(MissShader);
	}

	HitGroup.Shaders.Reserve(InitializerHitGroupShaders.Num());
	for (FRHIRayTracingShader* const HitGroupShaderRHI : InitializerHitGroupShaders)
	{
		VkRayTracingShaderGroupCreateInfoKHR ShaderGroup;
		ZeroVulkanStruct(ShaderGroup, VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
		ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		ShaderGroup.generalShader = VK_SHADER_UNUSED_KHR;

		checkSlow(HitGroupShaderRHI->GetFrequency() == SF_RayHitGroup);
		FVulkanRayTracingShader* const HitGroupShader = ResourceCast(HitGroupShaderRHI);

		// Closest Hit, always present
		{
			ANSICHAR* const EntryPoint = new char[EntryPointNameMaxLength];
			HitGroupShader->GetEntryPoint(EntryPoint, EntryPointNameMaxLength);
			EntryPointNames.Add(EntryPoint);

			VkPipelineShaderStageCreateInfo ShaderStage;
			ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
			ShaderStage.module = HitGroupShader->GetOrCreateHandle(FVulkanRayTracingShader::ClosestHitModuleIdentifier)->GetVkShaderModule();
			ShaderStage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
			ShaderStage.pName = EntryPoint;
			ShaderGroup.closestHitShader = ShaderStages.Add(ShaderStage);
		}

		// Any Hit, optional
		if (HitGroupShader->GetCodeHeader().RayGroupAnyHit != FVulkanShaderHeader::ERayHitGroupEntrypoint::NotPresent)
		{
			VkPipelineShaderStageCreateInfo ShaderStage;
			ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
			ShaderStage.module = HitGroupShader->GetOrCreateHandle(FVulkanRayTracingShader::AnyHitModuleIdentifier)->GetVkShaderModule();
			ShaderStage.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
			ShaderStage.pName = "main_00000000_00000000"; // :todo-jn: patch in the size_crc
			ShaderGroup.anyHitShader = ShaderStages.Add(ShaderStage);
		}
		else
		{
			ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		}

		// Intersection, optional
		if (HitGroupShader->GetCodeHeader().RayGroupIntersection != FVulkanShaderHeader::ERayHitGroupEntrypoint::NotPresent)
		{
			VkPipelineShaderStageCreateInfo ShaderStage;
			ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
			ShaderStage.module = HitGroupShader->GetOrCreateHandle(FVulkanRayTracingShader::IntersectionModuleIdentifier)->GetVkShaderModule();
			ShaderStage.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
			ShaderStage.pName = "main_00000000_00000000"; // :todo-jn: patch in the size_crc
			ShaderGroup.intersectionShader = ShaderStages.Add(ShaderStage);

			// Switch the shader group type given the presence of an intersection shader
			ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
		}
		else
		{
			ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		}

		ShaderGroups.Add(ShaderGroup);

		HitGroup.Shaders.Add(HitGroupShader);
	}

	Callable.Shaders.Reserve(InitializerCallableShaders.Num());
	for (FRHIRayTracingShader* const CallableShaderRHI : InitializerCallableShaders)
	{
		checkSlow(CallableShaderRHI->GetFrequency() == SF_RayCallable);
		FVulkanRayTracingShader* const CallableShader = ResourceCast(CallableShaderRHI);

		VkPipelineShaderStageCreateInfo ShaderStage;
		ZeroVulkanStruct(ShaderStage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		ShaderStage.module = CallableShader->GetOrCreateHandle(FVulkanRayTracingShader::MainModuleIdentifier)->GetVkShaderModule();
		ShaderStage.stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;

		ANSICHAR* const EntryPoint = new char[EntryPointNameMaxLength];
		CallableShader->GetEntryPoint(EntryPoint, EntryPointNameMaxLength);
		EntryPointNames.Add(EntryPoint);
		ShaderStage.pName = EntryPoint;
		ShaderStages.Add(ShaderStage);

		VkRayTracingShaderGroupCreateInfoKHR ShaderGroup;
		ZeroVulkanStruct(ShaderGroup, VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
		ShaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		ShaderGroup.generalShader = ShaderStages.Num() - 1;
		ShaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		ShaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		ShaderGroups.Add(ShaderGroup);

		Callable.Shaders.Add(CallableShader);
	}

	VkRayTracingPipelineCreateInfoKHR RayTracingPipelineCreateInfo;
	ZeroVulkanStruct(RayTracingPipelineCreateInfo, VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);
	RayTracingPipelineCreateInfo.stageCount = ShaderStages.Num();
	RayTracingPipelineCreateInfo.pStages = ShaderStages.GetData();
	RayTracingPipelineCreateInfo.groupCount = ShaderGroups.Num();
	RayTracingPipelineCreateInfo.pGroups = ShaderGroups.GetData();
	RayTracingPipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
	RayTracingPipelineCreateInfo.layout = InDevice->GetBindlessDescriptorManager()->GetPipelineLayout();
	RayTracingPipelineCreateInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

	VkDeferredOperationKHR DeferredOp = VK_NULL_HANDLE; // :todo-jn: more speed
	if (GVulkanRayTracingAllowDeferredOperation >= 0)
	{
		VERIFYVULKANRESULT(VulkanRHI::vkCreateDeferredOperationKHR(
			InDevice->GetInstanceHandle(),
			VULKAN_CPU_ALLOCATOR,
			&DeferredOp));
	}

	VERIFYVULKANRESULT_EXPANDED(VulkanDynamicAPI::vkCreateRayTracingPipelinesKHR(
		InDevice->GetInstanceHandle(),
		DeferredOp,
		VK_NULL_HANDLE, // Pipeline Cache 
		1,
		&RayTracingPipelineCreateInfo,
		VULKAN_CPU_ALLOCATOR,
		&Pipeline));

	if (DeferredOp != VK_NULL_HANDLE)
	{
		int32 MaxConcurrency = FMath::Min(
			(int32)VulkanRHI::vkGetDeferredOperationMaxConcurrencyKHR(InDevice->GetInstanceHandle(), DeferredOp),
			FTaskGraphInterface::Get().GetNumWorkerThreads());

		if (GVulkanRayTracingAllowDeferredOperation > 0)
		{
			MaxConcurrency = FMath::Min(MaxConcurrency, GVulkanRayTracingAllowDeferredOperation);
		}
		else if (MaxConcurrency > 3)
		{
			// Prevent automatic detection from completely clogging the machine
			MaxConcurrency -= 2;
		}

		bool bCompleted = false;
		ParallelFor(MaxConcurrency, [DeferredOp, InDevice, &bCompleted](int32 Unused)
			{
				VkResult Result = VulkanRHI::vkDeferredOperationJoinKHR(InDevice->GetInstanceHandle(), DeferredOp);
				while (Result == VK_THREAD_IDLE_KHR)
				{
					FPlatformProcess::Sleep(0.01f);
					Result = VulkanRHI::vkDeferredOperationJoinKHR(InDevice->GetInstanceHandle(), DeferredOp);
				}

				if (Result == VK_SUCCESS)
				{
					bCompleted = true;
				}
			});
		checkf(bCompleted, TEXT("ParallelFor returned but Deferred Operation not complete!"));

		VERIFYVULKANRESULT(VulkanRHI::vkGetDeferredOperationResultKHR(InDevice->GetInstanceHandle(), DeferredOp));

		VulkanRHI::vkDestroyDeferredOperationKHR(InDevice->GetInstanceHandle(), DeferredOp, VULKAN_CPU_ALLOCATOR);
	}

	for (ANSICHAR* const EntryPoint : EntryPointNames)
	{
		delete[] EntryPoint;
	}

	// Grab all shader handles for each stage
	{
		const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& RayTracingPipelineProps = InDevice->GetOptionalExtensionProperties().RayTracingPipelineProps;
		const uint32 HandleSize = RayTracingPipelineProps.shaderGroupHandleSize;

		uint32 HandleOffset = 0;
		auto FetchShaderHandles = [&HandleOffset, HandleSize, InDevice](VkPipeline RTPipeline, uint32 HandleCount) 
		{
			TArray<uint8> OutHandleStorage;

			if (HandleCount)
			{
				const uint32 ShaderHandleStorageSize = HandleCount * HandleSize;
				OutHandleStorage.AddUninitialized(ShaderHandleStorageSize);

				VERIFYVULKANRESULT(VulkanDynamicAPI::vkGetRayTracingShaderGroupHandlesKHR(InDevice->GetInstanceHandle(), RTPipeline, HandleOffset, HandleCount, ShaderHandleStorageSize, OutHandleStorage.GetData()));

				HandleOffset += HandleCount;
			}

			return OutHandleStorage;
		};

		// NOTE: Must be filled in the same order as created above
		RayGen.ShaderHandles = FetchShaderHandles(Pipeline, InitializerRayGenShaders.Num());
		Miss.ShaderHandles = FetchShaderHandles(Pipeline, InitializerMissShaders.Num());
		HitGroup.ShaderHandles = FetchShaderHandles(Pipeline, InitializerHitGroupShaders.Num());
		Callable.ShaderHandles = FetchShaderHandles(Pipeline, InitializerCallableShaders.Num());
	}

	// If no custom hit groups were provided, then disable SBT indexing and force default shader on all primitives
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bAllowHitGroupIndexing = Initializer.GetHitGroupTable().Num() ? Initializer.bAllowHitGroupIndexing : false;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	INC_DWORD_STAT(STAT_VulkanRayTracingCreatedPipelines);
	INC_DWORD_STAT_BY(STAT_VulkanRayTracingCompiledShaders, 1);
}

FVulkanRayTracingPipelineState::~FVulkanRayTracingPipelineState()
{
	if (Pipeline != VK_NULL_HANDLE)
	{
		VulkanRHI::vkDestroyPipeline(Device->GetInstanceHandle(), Pipeline, VULKAN_CPU_ALLOCATOR);
		Pipeline = VK_NULL_HANDLE;
	}
}

const FVulkanRayTracingPipelineState::ShaderData& FVulkanRayTracingPipelineState::GetShaderData(EShaderFrequency Frequency) const
{
	switch (Frequency)
	{
	case SF_RayGen:
		return RayGen;
	case SF_RayMiss:
		return Miss;
	case SF_RayHitGroup:
		return HitGroup;
	case SF_RayCallable:
		return Callable;

	default:
		checkf(false, TEXT("Only usable with RayTracing shaders."));
	};

	static ShaderData EmptyShaderData;
	return EmptyShaderData;
}

int32 FVulkanRayTracingPipelineState::GetShaderIndex(const FVulkanRayTracingShader* Shader) const
{
	const FSHAHash Hash = Shader->GetHash();

	const TArray<TRefCountPtr<FVulkanRayTracingShader>>& ShaderArray = GetShaderData(Shader->GetFrequency()).Shaders;
	for (int32 Index = 0; Index < ShaderArray.Num(); ++Index)
	{
		if (Hash == ShaderArray[Index]->GetHash())
		{
			return Index;
		}
	}

	checkf(false, TEXT("RayTracing shader is not present in the given ray tracing pipeline. "));
	return INDEX_NONE;
}

const FVulkanRayTracingShader* FVulkanRayTracingPipelineState::GetVulkanShader(EShaderFrequency Frequency, int32 ShaderIndex) const
{
	return GetShaderData(Frequency).Shaders[ShaderIndex].GetReference();
}

const TArray<uint8>& FVulkanRayTracingPipelineState::GetShaderHandles(EShaderFrequency Frequency) const
{
	return GetShaderData(Frequency).ShaderHandles;
}

void FVulkanRayTracingCompactedSizeQueryPool::EndBatch(FVulkanCmdBuffer* InCmdBuffer)
{
	check(CmdBuffer == nullptr);
	CmdBuffer = InCmdBuffer;
	FenceSignaledCounter = InCmdBuffer->GetFenceSignaledCounter();
}

void FVulkanRayTracingCompactedSizeQueryPool::Reset(FVulkanCmdBuffer* InCmdBuffer)
{
	VulkanRHI::vkCmdResetQueryPool(InCmdBuffer->GetHandle(), QueryPool, 0, MaxQueries);
	FenceSignaledCounter = 0;
	CmdBuffer = nullptr;
	check(QueryOutput.Num() == MaxQueries);
	FMemory::Memzero(QueryOutput.GetData(), MaxQueries * sizeof(uint64));
}

bool FVulkanRayTracingCompactedSizeQueryPool::TryGetResults(uint32 NumResults)
{
	if (CmdBuffer == nullptr)
	{
		return false;
	}

	const uint64 FenceCurrentSignaledCounter = CmdBuffer->GetFenceSignaledCounter();
	if (FenceSignaledCounter >= FenceCurrentSignaledCounter)
	{
		return false;
	}

	VkResult Result = VulkanRHI::vkGetQueryPoolResults(Device->GetInstanceHandle(), QueryPool, 0, NumResults, NumResults * sizeof(uint64), QueryOutput.GetData(), sizeof(uint64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
	if (Result == VK_SUCCESS)
	{
		return true;
	}
	return false;
}

FVulkanRayTracingCompactionRequestHandler::FVulkanRayTracingCompactionRequestHandler(FVulkanDevice* const InDevice)
	: VulkanRHI::FDeviceChild(InDevice)
{
	QueryPool = new FVulkanRayTracingCompactedSizeQueryPool(InDevice, GVulkanRayTracingMaxBatchedCompaction);

	ActiveRequests.Reserve(GVulkanRayTracingMaxBatchedCompaction);
	ActiveBLASes.Reserve(GVulkanRayTracingMaxBatchedCompaction);
}

void FVulkanRayTracingCompactionRequestHandler::RequestCompact(FVulkanRayTracingGeometry* InRTGeometry)
{
	check(InRTGeometry->AccelerationStructureBuffer);
	ERayTracingAccelerationStructureFlags GeometryBuildFlags = GetRayTracingAccelerationStructureBuildFlags(InRTGeometry->Initializer);
	check(EnumHasAllFlags(GeometryBuildFlags, ERayTracingAccelerationStructureFlags::AllowCompaction) &&
		EnumHasAllFlags(GeometryBuildFlags, ERayTracingAccelerationStructureFlags::FastTrace) &&
		!EnumHasAnyFlags(GeometryBuildFlags, ERayTracingAccelerationStructureFlags::AllowUpdate));

	FScopeLock Lock(&CS);
	PendingRequests.Add(InRTGeometry);
}

bool FVulkanRayTracingCompactionRequestHandler::ReleaseRequest(FVulkanRayTracingGeometry* InRTGeometry)
{
	FScopeLock Lock(&CS);

	// Remove from pending list, not found then try active requests
	if (PendingRequests.Remove(InRTGeometry) <= 0)
	{
		// If currently enqueued, then clear pointer to not handle the compaction request anymore			
		for (int32 BLASIndex = 0; BLASIndex < ActiveBLASes.Num(); ++BLASIndex)
		{
			if (ActiveRequests[BLASIndex] == InRTGeometry)
			{
				ActiveRequests[BLASIndex] = nullptr;
				return true;
			}
		}

		return false;
	}
	else
	{
		return true;
	}
}

void FVulkanRayTracingCompactionRequestHandler::Update(FVulkanCommandListContext& InCommandContext)
{
	LLM_SCOPE_BYNAME(TEXT("FVulkanRT/Compaction"));
	FScopeLock Lock(&CS);

	// If we have an active batch, wait on those queries and launch compaction when the complete
	if (ActiveBLASes.Num() > 0)
	{		
		FVulkanCommandBufferManager& CommandBufferManager = *InCommandContext.GetCommandBufferManager();
		FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();

		if (QueryPool->TryGetResults(ActiveBLASes.Num()))
		{
			// Compact
			for (int32 BLASIndex = 0; BLASIndex < ActiveBLASes.Num(); ++BLASIndex)
			{
				if (ActiveRequests[BLASIndex] != nullptr)
				{
					ActiveRequests[BLASIndex]->CompactAccelerationStructure(*CmdBuffer, QueryPool->GetResultValue(BLASIndex));
				}
			}

			QueryPool->Reset(CmdBuffer);

			ActiveBLASes.Empty(GVulkanRayTracingMaxBatchedCompaction);

			ActiveRequestsCmdBuffer = CmdBuffer;
			ActiveRequestsFenceCounter = CmdBuffer->GetFenceSignaledCounter();
		}

		// Only one active batch at a time (otherwise track the offset for when we launch queries)
		return;
	}
	// If we have an active batch, wait until the compaction went through to launch another batch
	else if (ActiveRequests.Num() > 0)
	{
		if (ActiveRequestsCmdBuffer)
		{
			if (ActiveRequestsFenceCounter >= ActiveRequestsCmdBuffer->GetFenceSignaledCounter())
			{
				return;
			}

			ActiveRequestsCmdBuffer = nullptr;
			ActiveRequestsFenceCounter = MAX_uint64;
		}

		ActiveRequests.Empty(GVulkanRayTracingMaxBatchedCompaction);
	}

	check(ActiveBLASes.Num() == 0);
	check(ActiveRequests.Num() == 0);

	// build a new set of build requests to extract the build data	
	for (FVulkanRayTracingGeometry* RTGeometry : PendingRequests)
	{
		ActiveRequests.Add(RTGeometry);
		ActiveBLASes.Add(RTGeometry->Handle);

		// enqueued enough requests for this update round
		if (ActiveRequests.Num() >= GVulkanRayTracingMaxBatchedCompaction)
		{
			break;
		}
	}

	// Do we have requests?
	if (ActiveRequests.Num() > 0)
	{
		// clear out all of the pending requests, don't allow the array to shrink
		PendingRequests.RemoveAt(0, ActiveRequests.Num(), EAllowShrinking::No);

		FVulkanCommandBufferManager& CommandBufferManager = *InCommandContext.GetCommandBufferManager();
		FVulkanCmdBuffer* const CmdBuffer = CommandBufferManager.GetActiveCmdBuffer();

		// Barrier here is not stricly necessary as it is added after the build.
		// AddAccelerationStructureBuildBarrier(CmdBuffer->GetHandle());

		// Write compacted size info from the selected requests
		VulkanDynamicAPI::vkCmdWriteAccelerationStructuresPropertiesKHR(
			CmdBuffer->GetHandle(),
			ActiveBLASes.Num(), ActiveBLASes.GetData(),
			VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
			QueryPool->GetHandle(),
			0
		);

		QueryPool->EndBatch(CmdBuffer);

		CommandBufferManager.SubmitActiveCmdBuffer();
		CommandBufferManager.PrepareForNewActiveCommandBuffer();
	}
}

bool FVulkanRayTracingCompactionRequestHandler::IsUsingCmdBuffer(FVulkanCmdBuffer* CmdBuffer)
{
	FScopeLock Lock(&CS);
	return (CmdBuffer == ActiveRequestsCmdBuffer) || (CmdBuffer == QueryPool->CmdBuffer);
}


static FVulkanPipelineBarrier SetRayGenResources(FVulkanDevice* Device, FVulkanCmdBuffer* const CmdBuffer, const FRayTracingShaderBindings& InGlobalResourceBindings, FVulkanRayTracingShaderTable* ShaderTable)
{
	TArray<const FVulkanUniformBuffer*> UniformBuffers;
	UniformBuffers.Reserve(UE_ARRAY_COUNT(InGlobalResourceBindings.UniformBuffers));

	// Uniform buffers
	{
		uint32 NumSkippedSlots = 0;
		FVulkanBindlessDescriptorManager::FUniformBufferDescriptorArrays StageUBs;
		const uint32 MaxUniformBuffers = UE_ARRAY_COUNT(InGlobalResourceBindings.UniformBuffers);
		for (uint32 UBIndex = 0; UBIndex < MaxUniformBuffers; ++UBIndex)
		{
			const FVulkanUniformBuffer* UniformBuffer = ResourceCast(InGlobalResourceBindings.UniformBuffers[UBIndex]);
			if (UniformBuffer)
			{
				if (NumSkippedSlots > 0)
				{
					UE_LOG(LogRHI, Warning, TEXT("Skipping %u Uniform Buffer bindings, this isn't normal!"), NumSkippedSlots);

					for (uint32 SkipIndex = 0; SkipIndex < NumSkippedSlots; ++SkipIndex)
					{
						VkDescriptorAddressInfoEXT& DescriptorAddressInfo = StageUBs[ShaderStage::EStage::RayGen].AddZeroed_GetRef();
						DescriptorAddressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
					}

					NumSkippedSlots = 0;
				}
				
				VkDescriptorAddressInfoEXT& DescriptorAddressInfo = StageUBs[ShaderStage::EStage::RayGen].AddZeroed_GetRef();
				DescriptorAddressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
				DescriptorAddressInfo.address = UniformBuffer->GetDeviceAddress();
				DescriptorAddressInfo.range = UniformBuffer->GetSize();

				UniformBuffers.AddUnique(UniformBuffer);
			}
			else
			{
				// :todo-jn: There might be unused indices (see USE_INSTANCE_CULLING_DATA issue), just skip them with a warning for now.
				NumSkippedSlots++;
			}
		}
		Device->GetBindlessDescriptorManager()->RegisterUniformBuffers(CmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, StageUBs);
	}

	// Add all the UBs references by the shader table
	TArrayView<TRefCountPtr<FRHIUniformBuffer>> ShaderTableUBs = ShaderTable->GetUBRefs();
	for (TRefCountPtr<FRHIUniformBuffer>& UniformBuffer : ShaderTableUBs)
	{
		const FVulkanUniformBuffer* VulkanUniformBuffer = ResourceCast(UniformBuffer.GetReference());
		UniformBuffers.AddUnique(VulkanUniformBuffer);
	}

	// Track all the missing transitions for the dispatch to be able to bring it back afterwards (will not touch tracking)
	FVulkanPipelineBarrier PreDispatch, PostDispatch;
	{
		auto TransitionBuffer = [&PreDispatch, &PostDispatch](bool bReadOnly)
		{
			// :todo-jn: tighten these barriers
			const VkAccessFlags RWAccessFlags = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			const VkAccessFlags DesiredAccessFlags = bReadOnly ? VK_ACCESS_MEMORY_READ_BIT : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			PreDispatch.AddMemoryBarrier(RWAccessFlags, DesiredAccessFlags, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
			PostDispatch.AddMemoryBarrier(DesiredAccessFlags, RWAccessFlags, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		};

		// Make sure we only transition textures once, accumulate them in sets
		TSet<FRHITexture*> SRVTransitions;
		TSet<FRHITexture*> UAVTransitions;
		for (const FVulkanUniformBuffer* UniformBuffer : UniformBuffers)
		{
			const TArray<TRefCountPtr<FRHIResource>>& ResourceTable = UniformBuffer->GetResourceTable();
			for (const TRefCountPtr<FRHIResource>& RHIResourceRef : ResourceTable)
			{
				const FRHIResource* RHIResource = RHIResourceRef.GetReference();
				if (!RHIResource)
				{
					continue;
				}

				switch (RHIResource->GetType())
				{
				case RRT_Texture:
				case RRT_Texture2D:
				case RRT_Texture2DArray:
				case RRT_Texture3D:
				case RRT_TextureCube:
					SRVTransitions.Add((FRHITexture*)RHIResource);
					break;

				case RRT_TextureReference:
					SRVTransitions.Add(((FRHITextureReference*)RHIResource)->GetReferencedTexture());
					break;

				case RRT_UnorderedAccessView:
				{
					const FRHIUnorderedAccessView* RHIUnorderedAccessView = (FRHIUnorderedAccessView*)RHIResource;
					if (RHIUnorderedAccessView->IsTexture())
					{
						UAVTransitions.Add(RHIUnorderedAccessView->GetTexture());
					}
					else
					{
						TransitionBuffer(false);
					}
					break;
				}

				case RRT_ShaderResourceView:
				{
					const FRHIShaderResourceView* RHIShaderResourceView = (FRHIShaderResourceView*)RHIResource;
					if (RHIShaderResourceView->IsTexture())
					{
						SRVTransitions.Add(RHIShaderResourceView->GetTexture());
					}
					else
					{
						TransitionBuffer(true);
					}
					break;
				}

				case RRT_RayTracingAccelerationStructure:
				case RRT_StagingBuffer:
				case RRT_Buffer:
					TransitionBuffer(true);
					break;

				case RRT_SamplerState: [[fallthrough]];
				default:
					// Do nothing
					break;
				};
			}
		}

		auto TransitionTexture = [&PreDispatch, &PostDispatch, CmdBuffer](FRHITexture* RHITexture, bool bReadOnly)
		{
			const FVulkanTexture* Texture = ResourceCast(RHITexture);

			// Because Sync2 is a prereq to ray tracing, use the conveniently generic layout
			const VkImageLayout TargetLayout = bReadOnly ? VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
			const FVulkanImageLayout* OriginalLayout = CmdBuffer->GetLayoutManager().GetFullLayout(Texture->Image);
			check(OriginalLayout);

			// If all the subresource are already in a correct layout for the desired RendOnly state, then skip the barrier
			if (!OriginalLayout->AreAllSubresourcesSameLayout() ||
				(
					(bReadOnly && (OriginalLayout->MainLayout != VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL) && (OriginalLayout->MainLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
					|| (!bReadOnly && (OriginalLayout->MainLayout != VK_IMAGE_LAYOUT_GENERAL)) // :todo-jn: prevent overlap?
					))
			{
				PreDispatch.AddImageLayoutTransition(Texture->Image, Texture->GetFullAspectMask(), *OriginalLayout, TargetLayout);

				// Transition back to where it was, leaving any undefined transitions to whatever we set them to
				{
					FVulkanImageLayout FinalLayout = *OriginalLayout;
					if (FinalLayout.AreAllSubresourcesSameLayout())
					{
						if (FinalLayout.MainLayout == VK_IMAGE_LAYOUT_UNDEFINED)
						{
							FinalLayout.MainLayout = TargetLayout;
						}
					}
					else
					{
						for (int32 SubResIndex = 0; SubResIndex < FinalLayout.SubresLayouts.Num(); ++SubResIndex)
						{
							if (FinalLayout.SubresLayouts[SubResIndex] == VK_IMAGE_LAYOUT_UNDEFINED)
							{
								FinalLayout.SubresLayouts[SubResIndex] = TargetLayout;
							}
						}
					}
					PostDispatch.AddImageLayoutTransition(Texture->Image, Texture->GetFullAspectMask(), TargetLayout, FinalLayout);
				}
			}
		};

		for (FRHITexture* RHITexture : UAVTransitions)
		{
			TransitionTexture(RHITexture, false);

			// If a resource shows up as both, use it in VK_IMAGE_LAYOUT_GENERAL
			SRVTransitions.Remove(RHITexture);
		}

		for (FRHITexture* RHITexture : SRVTransitions)
		{
			TransitionTexture(RHITexture, true);
		}
	}

	PreDispatch.Execute(CmdBuffer);
	return PostDispatch;
}

void FVulkanCommandListContext::RHIRayTraceDispatch(
	FRHIRayTracingPipelineState* InRayTracingPipelineState, 
	FRHIRayTracingShader* InRayGenShader,
	FRHIShaderBindingTable* InSBT,
	const FRayTracingShaderBindings& InGlobalResourceBindings, // :todo-jn:
	uint32 InWidth, uint32 InHeight)
{
	const FVulkanRayTracingPipelineState* Pipeline = ResourceCast(InRayTracingPipelineState);
	FVulkanRayTracingShader* RayGenShader = ResourceCast(InRayGenShader);
	FVulkanRayTracingShaderTable* ShaderTable = ResourceCast(InSBT);

	FVulkanCmdBuffer* const CmdBuffer = GetCommandBufferManager()->GetActiveCmdBuffer();
	VulkanRHI::vkCmdBindPipeline(CmdBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Pipeline->GetPipeline());

	ShaderTable->CommitRayGenShader(*this, InRayGenShader->GetFrequency(), Pipeline->GetShaderIndex(RayGenShader), Pipeline->GetShaderHandles(SF_RayGen));

	FVulkanPipelineBarrier PostDispatch = SetRayGenResources(Device, CmdBuffer, InGlobalResourceBindings, ShaderTable);

	VulkanRHI::vkCmdTraceRaysKHR(
		CmdBuffer->GetHandle(),
		ShaderTable->GetRegion(SF_RayGen),
		ShaderTable->GetRegion(SF_RayMiss),
		ShaderTable->GetRegion(SF_RayHitGroup),
		ShaderTable->GetRegion(SF_RayCallable),
		InWidth, InHeight, 1);

	PostDispatch.Execute(CmdBuffer);

	if (GVulkanSubmitOnTraceRays)
	{
		InternalSubmitActiveCmdBuffer();
	}
}

void FVulkanCommandListContext::RHIRayTraceDispatchIndirect(
	FRHIRayTracingPipelineState* InRayTracingPipelineState, 
	FRHIRayTracingShader* InRayGenShader,
	FRHIShaderBindingTable* InSBT,
	const FRayTracingShaderBindings& InGlobalResourceBindings, // :todo-jn:
	FRHIBuffer* InArgumentBuffer, uint32 InArgumentOffset)
{
	checkf(GRHISupportsRayTracingDispatchIndirect, TEXT("RHIRayTraceDispatchIndirect may not be used because it is not supported on this machine."));

	const FVulkanRayTracingPipelineState* Pipeline = ResourceCast(InRayTracingPipelineState);
	FVulkanRayTracingShader* RayGenShader = ResourceCast(InRayGenShader);
	FVulkanRayTracingShaderTable* ShaderTable = ResourceCast(InSBT);

	FVulkanCmdBuffer* const CmdBuffer = GetCommandBufferManager()->GetActiveCmdBuffer();
	VulkanRHI::vkCmdBindPipeline(CmdBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Pipeline->GetPipeline());

	ShaderTable->CommitRayGenShader(*this, InRayGenShader->GetFrequency(), Pipeline->GetShaderIndex(RayGenShader), Pipeline->GetShaderHandles(SF_RayGen));

	FVulkanPipelineBarrier PostDispatch = SetRayGenResources(Device, CmdBuffer, InGlobalResourceBindings, ShaderTable);

	FVulkanResourceMultiBuffer* ArgumentBuffer = ResourceCast(InArgumentBuffer);
	const VkDeviceAddress IndirectDeviceAddress = ArgumentBuffer->GetDeviceAddress() + InArgumentOffset;

	VulkanRHI::vkCmdTraceRaysIndirectKHR(
		CmdBuffer->GetHandle(),
		ShaderTable->GetRegion(SF_RayGen),
		ShaderTable->GetRegion(SF_RayMiss),
		ShaderTable->GetRegion(SF_RayHitGroup),
		ShaderTable->GetRegion(SF_RayCallable),
		IndirectDeviceAddress);

	PostDispatch.Execute(CmdBuffer);

	if (GVulkanSubmitOnTraceRays)
	{
		InternalSubmitActiveCmdBuffer();
	}
}



static void SetSystemParametersUB(FVulkanHitGroupSystemParameters& OutSystemParameters, FVulkanRayTracingShaderTable* ShaderTable, uint32 InNumUniformBuffers, FRHIUniformBuffer* const* InUniformBuffers, const FVulkanRayTracingShader* InShader)
{

	// Plug the shaders in the right slots using LayoutHash comparisons
	check(InShader->GetCodeHeader().UniformBufferInfos.Num() <= (int32)InNumUniformBuffers);
	for (int32 UBIndex = 0; UBIndex < InShader->GetCodeHeader().UniformBufferInfos.Num(); ++UBIndex)
	{
		FVulkanUniformBuffer* UniformBuffer = ResourceCast(InUniformBuffers[UBIndex]);

		const FVulkanShaderHeader::FUniformBufferInfo& UniformBufferInfo = InShader->GetCodeHeader().UniformBufferInfos[UBIndex];

		// :todo-jn: Hack to force in a DummyCullingBuffer in cases where it should have been culled from source (see SPIRV-Tools Issue 4902).
		if (!UniformBuffer)
		{
			UniformBuffer = GetDummyUB(ShaderTable->GetParent(), UniformBufferInfo.LayoutHash);
		}

		check(UniformBuffer);
		check((UniformBufferInfo.LayoutHash == 0) || (UniformBufferInfo.LayoutHash == UniformBuffer->GetLayout().GetHash()));

		const FRHIDescriptorHandle BindlessHandle = UniformBuffer->GetBindlessHandle();
		check(BindlessHandle.IsValid());
		check(UniformBufferInfo.BindlessCBIndex < UE_ARRAY_COUNT(OutSystemParameters.BindlessUniformBuffers));
		OutSystemParameters.BindlessUniformBuffers[UniformBufferInfo.BindlessCBIndex] = BindlessHandle.GetIndex();

		ShaderTable->AddUBRef(UniformBuffer);
	}
}


static void SetRayTracingHitGroup(
	FVulkanRayTracingShaderTable* ShaderTable, uint32 RecordIndex,
	FVulkanRayTracingPipelineState* Pipeline, uint32 HitGroupIndex,
	const FVulkanRayTracingGeometry* Geometry, uint32 GeometrySegmentIndex,
	uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
	uint32 LooseParameterDataSize, const void* LooseParameterData,
	uint32 UserData,
	uint32 WorkerIndex)
{
#if DO_CHECK
	if (Geometry)
	{
		const uint32 NumGeometrySegments = Geometry->GetNumSegments();
		checkf(GeometrySegmentIndex < NumGeometrySegments, TEXT("Segment %d is out of range for ray tracing geometry '%s' that contains %d segments"),
			GeometrySegmentIndex, Geometry->DebugName.IsNone() ? TEXT("UNKNOWN") : *Geometry->DebugName.ToString(), NumGeometrySegments);
	}
#endif // DO_CHECK

	ERayTracingShaderBindingMode ShaderBindingMode = ShaderTable->GetShaderBindingMode();
	ERayTracingHitGroupIndexingMode HitGroupIndexingMode = ShaderTable->GetHitGroupIndexingMode();

	if (HitGroupIndexingMode == ERayTracingHitGroupIndexingMode::Allow && Geometry)
	{		
		if (EnumHasAnyFlags(ShaderBindingMode, ERayTracingShaderBindingMode::RTPSO))
		{
			const FVulkanRayTracingShader* Shader = Pipeline->GetVulkanShader(SF_RayHitGroup, HitGroupIndex);

			FVulkanHitGroupSystemParameters SystemParameters = Geometry->HitGroupSystemParameters[GeometrySegmentIndex];
			SystemParameters.RootConstants.UserData = UserData;
			SetSystemParametersUB(SystemParameters, ShaderTable, NumUniformBuffers, UniformBuffers, Shader);

			ShaderTable->SetLocalShaderParameters(SF_RayHitGroup, RecordIndex, 0, SystemParameters);
			ShaderTable->SetLooseParameterData(SF_RayHitGroup, RecordIndex, LooseParameterData, LooseParameterDataSize);
		}

		if (EnumHasAnyFlags(ShaderBindingMode, ERayTracingShaderBindingMode::Inline))
		{			
			// Only care about shader slot 0 for inline geometry parameters
			uint32 NumShaderSlotsPerGeometrySegment = ShaderTable->GetInitializer().NumShaderSlotsPerGeometrySegment;
			if (RecordIndex % NumShaderSlotsPerGeometrySegment == 0)
			{
				// Setup the inline geometry parameters - can be cached on the geometry as well if needed
				FVulkanRayTracingGeometryParameters SegmentParameters;
				Geometry->SetupInlineGeometryParameters(GeometrySegmentIndex, SegmentParameters);

				// Recompute the geometry segment index from the record index
				uint32 SegmentIndex = RecordIndex / NumShaderSlotsPerGeometrySegment;
				ShaderTable->SetInlineGeometryParameters(SegmentIndex, &SegmentParameters, sizeof(FVulkanRayTracingGeometryParameters));
			}
		}
	}

	if (EnumHasAnyFlags(ShaderBindingMode, ERayTracingShaderBindingMode::RTPSO))
	{
		ShaderTable->SetSlot(SF_RayHitGroup, RecordIndex, HitGroupIndex, Pipeline->GetShaderHandles(SF_RayHitGroup));
	}
}


static void SetGenericSystemParameters(
	FVulkanRayTracingShaderTable* ShaderTable, uint32 RecordIndex,
	FRHIRayTracingPipelineState* InPipeline, uint32 ShaderIndexInPipeline,
	uint32 NumUniformBuffers, FRHIUniformBuffer* const* UniformBuffers,
	uint32 LooseParameterDataSize, const void* LooseParameterData,
	uint32 UserData, const EShaderFrequency ShaderFrequency)
{
	FVulkanRayTracingPipelineState* Pipeline = ResourceCast(InPipeline);
	const FVulkanRayTracingShader* Shader = Pipeline->GetVulkanShader(ShaderFrequency, ShaderIndexInPipeline);

	FVulkanHitGroupSystemParameters SystemParameters;
	FMemory::Memzero(SystemParameters);
	SystemParameters.RootConstants.UserData = UserData;
	SetSystemParametersUB(SystemParameters, ShaderTable, NumUniformBuffers, UniformBuffers, Shader);
	ShaderTable->SetLocalShaderParameters(ShaderFrequency, RecordIndex, 0, SystemParameters);
	ShaderTable->SetLooseParameterData(ShaderFrequency, RecordIndex, LooseParameterData, LooseParameterDataSize);

	ShaderTable->SetSlot(ShaderFrequency, RecordIndex, ShaderIndexInPipeline, Pipeline->GetShaderHandles(ShaderFrequency));
}


void FVulkanCommandListContext::RHISetBindingsOnShaderBindingTable(FRHIShaderBindingTable* InSBT,
	FRHIRayTracingPipelineState* InPipeline,
	uint32 NumBindings, const FRayTracingLocalShaderBindings* Bindings,
	ERayTracingBindingType BindingType)
{
	FVulkanRayTracingPipelineState* Pipeline = ResourceCast(InPipeline);
	FVulkanRayTracingShaderTable* ShaderTable = ResourceCast(InSBT);

	FGraphEventArray TaskList;

	const uint32 NumWorkerThreads = FTaskGraphInterface::Get().GetNumWorkerThreads();
	const uint32 MaxTasks = FApp::ShouldUseThreadingForPerformance() ? FMath::Min<uint32>(NumWorkerThreads, FVulkanRayTracingShaderTable::MaxBindingWorkers) : 1;

	struct FTaskContext
	{
		uint32 WorkerIndex = 0;
	};

	TArray<FTaskContext, TInlineAllocator<FVulkanRayTracingShaderTable::MaxBindingWorkers>> TaskContexts;
	for (uint32 WorkerIndex = 0; WorkerIndex < MaxTasks; ++WorkerIndex)
	{
		TaskContexts.Add(FTaskContext{ WorkerIndex });
	}

	auto BindingTask = [this, Bindings, Pipeline, ShaderTable, BindingType](const FTaskContext& Context, int32 CurrentIndex)
	{
		const FRayTracingLocalShaderBindings& Binding = Bindings[CurrentIndex];

		if (BindingType == ERayTracingBindingType::HitGroup)
		{
			const FVulkanRayTracingGeometry* Geometry = ResourceCast(Binding.Geometry);

			SetRayTracingHitGroup( 
				ShaderTable, Binding.RecordIndex,
				Pipeline, Binding.ShaderIndexInPipeline,
				Geometry, Binding.SegmentIndex,
				Binding.NumUniformBuffers,
				Binding.UniformBuffers,
				Binding.LooseParameterDataSize,
				Binding.LooseParameterData,
				Binding.UserData,
				Context.WorkerIndex);
		}
		else if (BindingType == ERayTracingBindingType::CallableShader)
		{
			SetGenericSystemParameters(
				ShaderTable, Binding.RecordIndex,
				Pipeline, Binding.ShaderIndexInPipeline,
				Binding.NumUniformBuffers, Binding.UniformBuffers,
				Binding.LooseParameterDataSize, Binding.LooseParameterData,
				Binding.UserData,
				SF_RayCallable);
		}
		else if (BindingType == ERayTracingBindingType::MissShader)
		{
			SetGenericSystemParameters(
				ShaderTable, Binding.RecordIndex,
				Pipeline, Binding.ShaderIndexInPipeline,
				Binding.NumUniformBuffers, Binding.UniformBuffers,
				Binding.LooseParameterDataSize, Binding.LooseParameterData,
				Binding.UserData,
				SF_RayMiss);
		}
		else
		{
			checkNoEntry();
		}
	};

	// One helper worker task will be created at most per this many work items, plus one worker for current thread (unless running on a task thread),
	// up to a hard maximum of FVulkanRayTracingShaderTable::MaxBindingWorkers.
	// Internally, parallel for tasks still subdivide the work into smaller chunks and perform fine-grained load-balancing.
	const int32 ItemsPerTask = 1024;

	ParallelForWithExistingTaskContext(TEXT("SetRayTracingBindings"), MakeArrayView(TaskContexts), NumBindings, ItemsPerTask, BindingTask);
}
