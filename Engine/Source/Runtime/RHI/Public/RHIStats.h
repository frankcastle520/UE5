// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StaticArray.h"
#include "MultiGPU.h"
#include "RHIDefinitions.h"
#include "Stats/Stats.h"

struct FTextureMemoryStats
{
	// Hardware state (never change after device creation):

	// -1 if unknown, in bytes
	int64 DedicatedVideoMemory = -1;

	// -1 if unknown, in bytes
	int64 DedicatedSystemMemory = -1;

	// -1 if unknown, in bytes
	int64 SharedSystemMemory = -1;

	// Total amount of "graphics memory" that we think we can use for all our graphics resources, in bytes. -1 if unknown.
	int64 TotalGraphicsMemory = -1;

	// Size of memory allocated to streaming textures, in bytes
	uint64 StreamingMemorySize = 0;

	// Size of memory allocated to non-streaming textures, in bytes
	uint64 NonStreamingMemorySize = 0;

	// Size of the largest memory fragment, in bytes
	int64 LargestContiguousAllocation = 0;
	
	// 0 if streaming pool size limitation is disabled, in bytes
	int64 TexturePoolSize = 0;

	bool AreHardwareStatsValid() const
	{
		// pardon the redundancy, have a broken compiler (__EMSCRIPTEN__) that needs these types spelled out...
		return ((int64)DedicatedVideoMemory >= 0 && (int64)DedicatedSystemMemory >= 0 && (int64)SharedSystemMemory >= 0);
	}

	bool IsUsingLimitedPoolSize() const
	{
		return TexturePoolSize > 0;
	}

	int64 ComputeAvailableMemorySize() const
	{
		return FMath::Max<int64>(TexturePoolSize - StreamingMemorySize, 0);
	}
};


// GPU stats

extern RHI_API int32 GNumDrawCallsRHI[MAX_NUM_GPUS];
extern RHI_API int32 GNumPrimitivesDrawnRHI[MAX_NUM_GPUS];

#if HAS_GPU_STATS

struct FRHIDrawStatsCategory
{
	RHI_API FRHIDrawStatsCategory();
	RHI_API FRHIDrawStatsCategory(FName InName);

	bool ShouldCountDraws() const { return Index != -1; }

	FName  const Name;
	uint32 const Index;

	static constexpr int32 MAX_DRAWCALL_CATEGORY = 31;

	struct FManager
	{
		TStaticArray<FRHIDrawStatsCategory*, MAX_DRAWCALL_CATEGORY> Array;

		// A backup of the counts that can be used to display on screen to avoid flickering.
		TStaticArray<TStaticArray<int32, MAX_NUM_GPUS>, MAX_DRAWCALL_CATEGORY> DisplayCounts;

		int32 NumCategory;

		RHI_API FManager();
	};

	RHI_API static FManager& GetManager();
};

// RHI counter stats.
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("DrawPrimitive calls"), STAT_RHIDrawPrimitiveCalls, STATGROUP_RHI, RHI_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Triangles drawn"), STAT_RHITriangles, STATGROUP_RHI, RHI_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Lines drawn"), STAT_RHILines, STATGROUP_RHI, RHI_API);

#else

struct FRHIDrawStatsCategory
{
	static constexpr uint32 Index = 0;
};

#endif

// Macros for use inside RHI context Draw functions.
// Updates the Stats structure on the executing RHI command list
#if RHI_NEW_GPU_PROFILER
	#define RHI_DRAW_CALL_INC()             do { StatEvent.NumDraws++; GetExecutingCommandList().Stats_AddDraw(); } while (false)
	#define RHI_DRAW_CALL_STATS(Type,Prims) do { StatEvent.NumDraws++; StatEvent.NumPrimitives += Prims; GetExecutingCommandList().Stats_AddDrawAndPrimitives(Type, Prims); } while (false)
#else
	#define RHI_DRAW_CALL_INC()             do { GetExecutingCommandList().Stats_AddDraw(); } while (false)
	#define RHI_DRAW_CALL_STATS(Type,Prims) do { GetExecutingCommandList().Stats_AddDrawAndPrimitives(Type, Prims); } while (false)
#endif

struct FRHIDrawStats
{
#if HAS_GPU_STATS
	// The +1 is for "uncategorised"
	static constexpr int32 NumCategories = FRHIDrawStatsCategory::MAX_DRAWCALL_CATEGORY + 1;
#else
	static constexpr int32 NumCategories = 1;
#endif

	static constexpr int32 NoCategory = NumCategories - 1;

	struct FPerCategory
	{
		uint32 Draws;
		uint32 Triangles;
		uint32 Lines;
		uint32 Quads;
		uint32 Points;
		uint32 Rectangles;

		uint32 GetTotalPrimitives() const
		{
			return Triangles
				+ Lines
				+ Quads
				+ Points
				+ Rectangles;
		}

		FPerCategory& operator += (FPerCategory const& RHS)
		{
			Draws      += RHS.Draws;
			Triangles  += RHS.Triangles;
			Lines      += RHS.Lines;
			Quads      += RHS.Quads;
			Points     += RHS.Points;
			Rectangles += RHS.Rectangles;
			return *this;
		}
	};

	struct FPerGPU
	{
		friend struct FRHIDrawStats;
		FPerCategory Categories[NumCategories];
	};

	FPerGPU& GetGPU(uint32 GPUIndex)
	{
		checkSlow(GPUIndex < UE_ARRAY_COUNT(GPUs));
		return GPUs[GPUIndex];
	}

	FRHIDrawStats()
	{
		Reset();
	}

	void Reset()
	{
		FMemory::Memzero(*this);
	}

	void AddDraw(FRHIGPUMask GPUMask, FRHIDrawStatsCategory const* Category)
	{
		uint32 CategoryIndex = Category ? Category->Index : NoCategory;
		for (uint32 GPUIndex : GPUMask)
		{
			FPerCategory& Stats = GPUs[GPUIndex].Categories[CategoryIndex];
			Stats.Draws++;
		}
	}

	void AddDrawAndPrimitives(FRHIGPUMask GPUMask, FRHIDrawStatsCategory const* Category, EPrimitiveType PrimitiveType, uint32 NumPrimitives)
	{
		uint32 CategoryIndex = Category ? Category->Index : NoCategory;
		for (uint32 GPUIndex : GPUMask)
		{
			FPerCategory& Stats = GPUs[GPUIndex].Categories[CategoryIndex];
			Stats.Draws++;

			switch (PrimitiveType)
			{
			case PT_TriangleList : Stats.Triangles  += NumPrimitives; break;
			case PT_TriangleStrip: Stats.Triangles  += NumPrimitives; break;
			case PT_LineList     : Stats.Lines      += NumPrimitives; break;
			case PT_QuadList     : Stats.Quads      += NumPrimitives; break;
			case PT_PointList    : Stats.Points     += NumPrimitives; break;
			case PT_RectList     : Stats.Rectangles += NumPrimitives; break;
			}
		}
	}

	RHI_API void Accumulate(FRHIDrawStats& RHS);
	RHI_API void ProcessAsFrameStats();

private:
	FPerGPU GPUs[MAX_NUM_GPUS];
};

// RHI memory stats.
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Render Target 2D Memory"), STAT_RenderTargetMemory2D, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Render Target 3D Memory"), STAT_RenderTargetMemory3D, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Render Target Cube Memory"), STAT_RenderTargetMemoryCube, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);

DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("UAV Texture Memory"), STAT_UAVTextureMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);

DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Texture 2D Memory"), STAT_TextureMemory2D, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Texture 3D Memory"), STAT_TextureMemory3D, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Texture Cube Memory"), STAT_TextureMemoryCube, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);

DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Uniform Buffer Memory"), STAT_UniformBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Index Buffer Memory"), STAT_IndexBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Vertex Buffer Memory"), STAT_VertexBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("RayTracing Acceleration Structure Memory"), STAT_RTAccelerationStructureMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Structured Buffer Memory"), STAT_StructuredBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Byte Address Buffer Memory"), STAT_ByteAddressBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Draw Indirect Buffer Memory"), STAT_DrawIndirectBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Misc Buffer Memory"), STAT_MiscBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);

DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Reserved Buffer Memory (Uncommitted)"), STAT_ReservedUncommittedBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Reserved Buffer Memory (Committed)"), STAT_ReservedCommittedBufferMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Reserved Texture Memory (Uncommitted)"), STAT_ReservedUncommittedTextureMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Reserved Texture Memory (Committed)"), STAT_ReservedCommittedTextureMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Sampler Descriptors Allocated"), STAT_SamplerDescriptorsAllocated, STATGROUP_RHI, RHI_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Resource Descriptors Allocated"), STAT_ResourceDescriptorsAllocated, STATGROUP_RHI, RHI_API);

DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Bindless Sampler Heap"), STAT_BindlessSamplerHeapMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Bindless Resource Heap"), STAT_BindlessResourceHeapMemory, STATGROUP_RHI, FPlatformMemory::MCR_GPU, RHI_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Bindless Sampler Descriptors Allocated"), STAT_BindlessSamplerDescriptorsAllocated, STATGROUP_RHI, RHI_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Bindless Resource Descriptors Allocated"), STAT_BindlessResourceDescriptorsAllocated, STATGROUP_RHI, RHI_API);

#if PLATFORM_MICROSOFT

// D3D memory stats.
struct FD3DMemoryStats
{
	// Budget assigned by the OS. This can be considered the total memory
	// the application should use, but an application can also go over-budget.
	uint64 BudgetLocal = 0;
	uint64 BudgetSystem = 0;

	// Used memory.
	uint64 UsedLocal = 0;
	uint64 UsedSystem = 0;

	// Over-budget memory. This is Budget - Used if Used > Budget.
	uint64 DemotedLocal = 0;
	uint64 DemotedSystem = 0;

	// Available memory within budget. This is Budget - Used clamped to 0 if over-budget.
	uint64 AvailableLocal = 0;
	uint64 AvailableSystem = 0;

	bool IsOverBudget() const
	{
		return DemotedLocal > 0 || DemotedSystem > 0;
	}
};

DECLARE_STATS_GROUP(TEXT("D3D Video Memory"), STATGROUP_D3DMemory, STATCAT_Advanced);
DECLARE_MEMORY_STAT_EXTERN(TEXT("Total Video Memory (Budget)"), STAT_D3DTotalVideoMemory, STATGROUP_D3DMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Total System Memory (Budget)"), STAT_D3DTotalSystemMemory, STATGROUP_D3DMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Available Video Memory"), STAT_D3DAvailableVideoMemory, STATGROUP_D3DMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Available System Memory"), STAT_D3DAvailableSystemMemory, STATGROUP_D3DMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Used Video Memory"), STAT_D3DUsedVideoMemory, STATGROUP_D3DMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Used System Memory"), STAT_D3DUsedSystemMemory, STATGROUP_D3DMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Demoted Video Memory"), STAT_D3DDemotedVideoMemory, STATGROUP_D3DMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Demoted System Memory"), STAT_D3DDemotedSystemMemory, STATGROUP_D3DMemory, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Video Memory stats update time"), STAT_D3DUpdateVideoMemoryStats, STATGROUP_D3DMemory, RHI_API);

// Update D3D memory stat counters and CSV profiler stats, if enabled.
RHI_API void UpdateD3DMemoryStatsAndCSV(const FD3DMemoryStats& MemoryStats, bool bUpdateCSV);

#endif // PLATFORM_MICROSOFT