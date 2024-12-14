// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/BinaryHeap.h"
#include "Experimental/Containers/RobinHoodHashTable.h"
#include "Lumen/Lumen.h"
#include "Lumen/LumenHeightfields.h"
#include "Lumen/LumenSparseSpanArray.h"
#include "Lumen/LumenSceneGPUDrivenUpdate.h"
#include "Lumen/LumenSurfaceCacheFeedback.h"
#include "Lumen/LumenUniqueList.h"
#include "LumenDefinitions.h"
#include "MeshCardRepresentation.h"
#include "RenderTransform.h"
#include "ShaderParameterMacros.h"
#include "UnifiedBuffer.h"
#include "Tasks/Task.h"
#include "Math/DoubleFloat.h"

enum class ELumenReflectionPass
{
	Opaque,
	SingleLayerWater,
	FrontLayerTranslucency,

	MAX
};

class FDistanceFieldSceneData;
class FLumenCardBuildData;
class FLumenCardPassUniformParameters;
class FLumenCardRenderer;
class FLumenMeshCards;
class FLumenViewState;
class FMeshCardsBuildData;
class FPrimitiveSceneInfo;
class FViewUniformShaderParameters;
struct FLumenPageTableEntry;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardScene, )
	SHADER_PARAMETER(uint32, NumCards)
	SHADER_PARAMETER(uint32, NumMeshCards)
	SHADER_PARAMETER(uint32, NumCardPages)
	SHADER_PARAMETER(uint32, NumHeightfields)
	SHADER_PARAMETER(uint32, NumPrimitiveGroups)
	SHADER_PARAMETER(FVector2f, PhysicalAtlasSize)
	SHADER_PARAMETER(FVector2f, InvPhysicalAtlasSize)
	SHADER_PARAMETER(float, IndirectLightingAtlasDownsampleFactor)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CardData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CardPageData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, MeshCardsData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, HeightfieldData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PrimitiveGroupData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, PageTableBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, SceneInstanceIndexToMeshCardsIndexBuffer)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AlbedoAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NormalAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthAtlas)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

namespace Lumen
{
	constexpr uint32 FeedbackBufferElementStride = 2;

	uint32 GetFeedbackBufferSize(const FViewFamilyInfo& ViewFamily);
	uint32 GetCompactedFeedbackBufferSize();
};

struct FLumenSurfaceMipMap
{
	uint8 SizeInPagesX = 0;
	uint8 SizeInPagesY = 0;
	uint8 ResLevelX = 0;
	uint8 ResLevelY = 0;

	int32 PageTableSpanOffset = -1;
	uint16 PageTableSpanSize = 0;
	bool bLocked = false;

	bool IsAllocated() const
	{
		return PageTableSpanSize > 0;
	}

	FIntPoint GetSizeInPages() const
	{
		return FIntPoint(SizeInPagesX, SizeInPagesY);
	}

	int32 GetPageTableIndex(int32 LocalPageIndex) const
	{
		return PageTableSpanOffset + LocalPageIndex;
	}
};

struct FLumenMipMapDesc
{
	FIntPoint Resolution;
	FIntPoint SizeInPages;
	FIntPoint PageResolution;
	uint16 ResLevelX;
	uint16 ResLevelY;
	bool bSubAllocation;
};

class FLumenCard
{
public:
	FLumenCard();
	~FLumenCard();

	FLumenCardOBBf LocalOBB;
	FLumenCardOBBd WorldOBB;
	FLumenCardOBBf MeshCardsOBB;

	bool bVisible = false;
	bool bHeightfield = false;

	// First and last allocated mip map
	uint8 MinAllocatedResLevel = UINT8_MAX;
	uint8 MaxAllocatedResLevel = 0;

	// Requested res level based on distance. Actual allocated res level may be lower if atlas is out of space.
	uint8 DesiredLockedResLevel = 0;

	// Surface cache allocations per mip map, indexed by [ResLevel - Lumen::MinResLevel]
	FLumenSurfaceMipMap SurfaceMipMaps[Lumen::NumResLevels];

	int32 MeshCardsIndex = -1;
	int32 IndexInMeshCards = -1;
	uint8 IndexInBuildData = UINT8_MAX;
	uint8 AxisAlignedDirectionIndex = UINT8_MAX;
	float ResolutionScale = 1.0f;

	// Initial WorldOBB.Extent.X / WorldOBB.Extent.Y, which can't change during reallocation
	float CardAspect = 1.0f;

	void Initialize(
		float InResolutionScale,
		const FMatrix& LocalToWorld,
		const FLumenMeshCards& InMeshCardsInstance,
		const FLumenCardBuildData& CardBuildData,
		int32 InIndexInMeshCards,
		int32 InMeshCardsIndex,
		uint8 InIndexInBuildData);

	void SetTransform(const FMatrix& LocalToWorld, const FLumenMeshCards& MeshCards);

	void UpdateMinMaxAllocatedLevel();

	bool IsAllocated() const
	{
		return MinAllocatedResLevel <= MaxAllocatedResLevel;
	}

	struct FSurfaceStats
	{
		uint32 NumVirtualTexels = 0;
		uint32 NumLockedVirtualTexels = 0;
		uint32 NumPhysicalTexels = 0;
		uint32 NumLockedPhysicalTexels = 0;
		uint32 DroppedResLevels = 0;
	};

	void GetSurfaceStats(const TSparseSpanArray<FLumenPageTableEntry>& PageTable, FSurfaceStats& Stats) const;

	FLumenSurfaceMipMap& GetMipMap(int32 ResLevel)
	{
		const int32 MipIndex = ResLevel - Lumen::MinResLevel;
		check(MipIndex >= 0 && MipIndex < UE_ARRAY_COUNT(SurfaceMipMaps));
		return SurfaceMipMaps[MipIndex]; 
	}

	FIntPoint ResLevelToResLevelXYBias() const;
	void GetMipMapDesc(int32 ResLevel, FLumenMipMapDesc& Desc) const;

	const FLumenSurfaceMipMap& GetMipMap(int32 ResLevel) const
	{
		const int32 MipIndex = ResLevel - Lumen::MinResLevel;
		check(MipIndex >= 0 && MipIndex < UE_ARRAY_COUNT(SurfaceMipMaps));
		return SurfaceMipMaps[MipIndex];
	}
};

class FLumenPrimitiveGroupRemoveInfo
{
public:
	FLumenPrimitiveGroupRemoveInfo(const FPrimitiveSceneInfo* InPrimitive, int32 InPrimitiveIndex);

	// Must not be dereferenced after creation, the primitive was removed from the scene and deleted
	// Value of the pointer is still useful for map lookups
	const FPrimitiveSceneInfo* Primitive;

	// Need to copy by value as this is a deferred remove and Primitive may be already destroyed
	int32 PrimitiveIndex;
	TArray<int32, TInlineAllocator<1>> LumenPrimitiveGroupIndices;
};

// Defines a group of scene primitives for a given LOD level 
class FLumenPrimitiveGroup
{
public:
	TArray<FPrimitiveSceneInfo*, TInlineAllocator<1>> Primitives;
	int32 PrimitiveInstanceIndex = -1;
	int32 MeshCardsIndex = -1;
	int32 HeightfieldIndex = -1;

	FRenderBounds WorldSpaceBoundingBox; // LWC_TODO
	Experimental::FHashElementId RayTracingGroupMapElementId;
	float CardResolutionScale = 1.0f;

	bool bValidMeshCards = false;
	bool bFarField = false;
	bool bHeightfield = false;
	bool bEmissiveLightSource = false;
	bool bOpaqueOrMasked = true;
	uint32 LightingChannelMask = UINT32_MAX;

	bool HasMergedInstances() const;

	bool HasMergedPrimitives() const
	{
		return RayTracingGroupMapElementId.IsValid();
	}
};

struct FLumenPageTableEntry
{
	// Allocated physical page data
	FIntPoint PhysicalPageCoord = FIntPoint(-1, -1);

	// Allows to point to a sub-allocation inside a shared physical page
	FIntRect PhysicalAtlasRect;

	// Sampling data, can point to a coarser page
	uint32 SamplePageIndex = 0;
	uint16 SampleAtlasBiasX = 0;
	uint16 SampleAtlasBiasY = 0;
	uint16 SampleCardResLevelX = 0;
	uint16 SampleCardResLevelY = 0;

	// CardPage for atlas operations
	int32 CardIndex = -1;
	uint8 ResLevel = 0;
	FVector4f CardUVRect;

	FIntPoint SubAllocationSize = FIntPoint(-1, -1);

	bool IsSubAllocation() const
	{
		return SubAllocationSize.X >= 0 || SubAllocationSize.Y >= 0;
	}

	bool IsMapped() const 
	{ 
		return PhysicalPageCoord.X >= 0 && PhysicalPageCoord.Y >= 0;
	}

	uint32 GetNumVirtualTexels() const
	{
		return IsSubAllocation() ? SubAllocationSize.X * SubAllocationSize.Y : Lumen::VirtualPageSize * Lumen::VirtualPageSize;
	}

	uint32 GetNumPhysicalTexels() const
	{
		return IsMapped() ? PhysicalAtlasRect.Area() : 0;
	}
};

class FSurfaceCacheRequest
{
public:
	int32 CardIndex = -1;
	uint16 ResLevel = 0;
	uint16 LocalPageIndex = UINT16_MAX;
	float Distance = 0.0f;

	bool IsLockedMip() const { return LocalPageIndex == UINT16_MAX; }
};

union FVirtualPageIndex
{
	FVirtualPageIndex() {}
	FVirtualPageIndex(int32 InCardIndex, uint16 InResLevel, uint16 InLocalPageIndex)
		: CardIndex(InCardIndex), ResLevel(InResLevel), LocalPageIndex(InLocalPageIndex)
	{}

	uint64 PackedValue;
	struct
	{
		int32 CardIndex;
		uint16 ResLevel;
		uint16 LocalPageIndex;
	};
};

// Physical page allocator, which routes sub page sized allocations to a bin allocator
class FLumenSurfaceCacheAllocator
{
public:
	struct FAllocation
	{
		// Allocated physical page data
		FIntPoint PhysicalPageCoord = FIntPoint(-1, -1);

		// Allows to point to a sub-allocation inside a shared physical page
		FIntRect PhysicalAtlasRect;
	};

	struct FBinStats
	{
		FIntPoint ElementSize = FIntPoint(0, 0);
		int32 NumAllocations = 0;
		int32 NumPages = 0;
	};

	struct FStats
	{
		uint32 NumFreePages = 0;

		uint32 BinNumPages = 0;
		uint32 BinNumWastedPages = 0;
		uint32 BinPageFreeTexels = 0;

		TArray<FBinStats> Bins;
	};

	void Init(const FIntPoint& InPageAtlasSizeInPages);
	void Allocate(const FLumenPageTableEntry& Page, FAllocation& Allocation);
	void Free(const FLumenPageTableEntry& Page);
	bool IsSpaceAvailable(const FLumenCard& Card, int32 ResLevel, bool bSinglePage) const;
	void GetStats(FStats& Stats) const;

private:

	struct FPageBinAllocation
	{
	public:
		void Init(const FIntPoint& InPageCoord, const FIntPoint& InPageSizeInElements)
		{
			static_assert(Lumen::MinResLevel == 3);
			static_assert(Lumen::PhysicalPageSize == 128);

			PageCoord = InPageCoord;
			PageSizeInElements = InPageSizeInElements;
			SubPageFreeCount = InPageSizeInElements.X * InPageSizeInElements.Y;
			SubPageList.SetNum(SubPageFreeCount, false);
		}

		FIntPoint Add()
		{
			const int32 Index = SubPageList.FindAndSetFirstZeroBit();
			checkSlow(Index != INDEX_NONE);
			--SubPageFreeCount;
			return FIntPoint(Index % PageSizeInElements.X, Index / PageSizeInElements.X);
		}

		void Remove(const FIntPoint& In)
		{
			const int32 Index = In.X + PageSizeInElements.X * In.Y;
			checkSlow(SubPageList.IsValidIndex(Index));
			++SubPageFreeCount;
			SubPageList[Index] = false;
		}

		uint32 GetSubPageFreeCount() const
		{
			return SubPageFreeCount;
		}

		bool HasFreeElements() const
		{
			return SubPageFreeCount > 0;
		}

		FIntPoint PageCoord;
		FIntPoint PageSizeInElements;
	private:
		// 256 bits for storing sub-page elements
		// * MinPage size is 2^Lumen::MinResLevel=8
		// * Physical page is Lumen::PhysicalPageSize=128
		// * Max sub-allocation within a physical page is (128/8)^2 = 16x16 = 256
		// Values -> 0:free 1:used
		TBitArray<TInlineAllocator<8>> SubPageList;
		int32 SubPageFreeCount = 0;
	};

	struct FPageBin
	{
		FPageBin(const FIntPoint& InElementSize);

		int32 GetSubPageCount() const
		{
			return PageSizeInElements.X * PageSizeInElements.Y;
		}

		uint32 GetBinAllocationCount() const
		{
			return BinAllocations.Num();
		}

		uint32 GetSubPageFreeCount() const
		{
			uint32 Count = 0;
			for (auto& BinAllocation : BinAllocations)
			{
				Count += BinAllocation.GetSubPageFreeCount();
			}
			return Count;
		}

		bool HasFreeElements() const
		{
			// Ideally, make a 0(1) lookup for this
			for (auto& BinAllocation : BinAllocations)
			{
				if (BinAllocation.HasFreeElements())
				{
					return true;
				}
			}
			return false;
		}

		FPageBinAllocation* GetBinAllocation()
		{
			// Ideally, make a 0(1) lookup for this
			for (auto& BinAllocation : BinAllocations)
			{
				if (BinAllocation.HasFreeElements())
				{
					return &BinAllocation;
				}
			}
			return nullptr;
		}

		FPageBinAllocation* AddBinAllocation(const FIntPoint& InPageCoord)
		{
			FPageBinAllocation& NewBinAllocation = BinAllocations.AddDefaulted_GetRef();
			NewBinAllocation.Init(InPageCoord, PageSizeInElements);
			return &NewBinAllocation;
		}

		// Return true if the bin is now completely empty (and can be deleted), false otherwise.
		bool RemoveBinAllocation(const FLumenPageTableEntry& Page)
		{
			// Ideally, make a 0(1) lookup for this
			for (uint32 BinAllocIt = 0, BinAllocCount = BinAllocations.Num(); BinAllocIt < BinAllocCount; ++BinAllocIt)
			{
				FPageBinAllocation& BinAllocation = BinAllocations[BinAllocIt];
				if (BinAllocation.PageCoord == Page.PhysicalPageCoord)
				{
					const FIntPoint ElementCoord = (Page.PhysicalAtlasRect.Min - BinAllocation.PageCoord * Lumen::PhysicalPageSize) / ElementSize;
					BinAllocation.Remove(ElementCoord);
					const bool bIsEmpty = !BinAllocation.HasFreeElements();
					if (bIsEmpty)
					{
						BinAllocations.RemoveAtSwap(BinAllocIt);
					}
					return bIsEmpty;
				}
			}
			check(false); // Shouldn't reach this
			return false;
		}

		FIntPoint ElementSize = FIntPoint(0, 0);
		FIntPoint PageSizeInElements = FIntPoint(0, 0);
	private:
		TArray<FPageBinAllocation, TInlineAllocator<16>> BinAllocations;
	};

	// Physical pages
	FIntPoint AllocatePhysicalAtlasPage();
	void FreePhysicalAtlasPage(const FIntPoint& PageCoord);
	// Stored into a bitfield (0:free,1:used)
	// Mapping from page coord to bit is using simple linear remapping
	TBitArray<TInlineAllocator<32>> PhysicalPageList;
	int32 PhysicalPageFreeCount = 0;
	FIntPoint PageAtlasSizeInPages = FIntPoint::ZeroValue;
	
	TArray<FPageBin> PageBins;

	// Bin lookups are stored as 2D mapping (8x8 - [1-128]x[1-128])
	// This mapping indexes PageX dim. and PageY dim.
	// As an example, a 8x16 SubPage allocator will be stored at [3,4] (i.e., [log2(8),log2(16)] )
	//          0 1 2 3 4  5  6   7
	//          --------------------
	//          1 2 4 8 16 32 64 128
	// 0 |   1
	// 1 |   2    X
	// 2 |   4
	// 3 |   8        X
	// 4 |  16
	// 5 |  32      X
	// 6 |  64                X
	// 7 | 128
	static const uint8 InvalidPageBinIndex = 0xFF;
	typedef TStaticArray<uint8, 64u> FPageBinLookup;
	FPageBinLookup PageBinLookup;

	uint8 GetLookupIndex(const FIntPoint& InRes) const
	{
		checkSlow(FMath::IsPowerOfTwo(InRes.X) && FMath::IsPowerOfTwo(InRes.Y));
		checkSlow(InRes.X <= Lumen::PhysicalPageSize && InRes.Y <= Lumen::PhysicalPageSize);
		const uint32 OutIndex = FMath::FloorLog2(InRes.X) + FMath::FloorLog2(InRes.Y) * 8u;
		//check(OutIndex < 64u);
		return (uint8)OutIndex;
	}

	const FPageBin* GetBin(const FIntPoint& InRes) const
	{
		const uint8 LookupIndex = GetLookupIndex(InRes);
		const uint8 BinIndex = PageBinLookup[LookupIndex];
		if (BinIndex != InvalidPageBinIndex)
		{
			return &PageBins[BinIndex];
		}
		return nullptr;
	}

	FPageBin* GetBin(const FIntPoint& InRes)
	{
		const uint8 LookupIndex = GetLookupIndex(InRes);
		const uint8 BinIndex = PageBinLookup[LookupIndex];
		if (BinIndex != InvalidPageBinIndex)
		{
			return &PageBins[BinIndex];
		}
		return nullptr;
	}

	FPageBin* GetOrAddBin(const FIntPoint& InRes)
	{
		const uint8 LookupIndex = GetLookupIndex(InRes);
		const uint8 BinIndex = PageBinLookup[LookupIndex];
		if (BinIndex == InvalidPageBinIndex)
		{
			PageBinLookup[LookupIndex] = PageBins.Num();
			PageBins.Add(FPageBin(InRes));
			return &PageBins.Last();
		}
		else
		{
			return &PageBins[BinIndex];
		}
	}
};

enum class ESurfaceCacheCompression : uint8
{
	Disabled,
	UAVAliasing,
	CopyTextureRegion
};

class FLumenSharedRT
{
public:
	FRDGTextureRef CreateSharedRT(FRDGBuilder& Builder, const FRDGTextureDesc& Desc, FIntPoint VisibleExtent, const TCHAR* Name, ERDGTextureFlags Flags = ERDGTextureFlags::None);
	
	FRDGTextureRef GetRenderTarget() const
	{
		return RenderTarget;
	}

private:
	FRDGTextureRef RenderTarget = nullptr;
};

// Unique view origin.  Typically one per view, but for the case of cube captures, a single view origin is shared.
// The advantage of sharing an origin is that Lumen scene data can be shared and updated once.  In the future, we could
// allow origins to be shared for other use cases, such as nDisplay inner frustums, or imagine a sim with a wide angle
// view across three monitors, where the three views share an origin.
struct FLumenViewOrigin
{
	void Init(const FViewInfo& View);

	bool IsPerspectiveProjection() const
	{
		return OrthoMaxDimension == 0.0f;
	}

	const FSceneViewFamily* Family;

	FVector LumenSceneViewOrigin;
	FVector4f WorldCameraOrigin;
	FDFVector3 PreViewTranslationDF;
	
	// Matrix used for frustum clipping tests in Lumen.  For typical views, this is set to WorldToClip, while cube captures
	// have an omnidirectional projection, and use a trivial matrix that will pass any point as in-frustum.
	FMatrix44f FrustumTranslatedWorldToClip;

	float OrthoMaxDimension;				// If orthographic projection, max dimension, otherwise zero
	float LastEyeAdaptationExposure;		// Shared origin views share exposure
	float MaxTraceDistance;					// Shared origin views share post process settings, which control these values
	float CardMaxDistance;
	float LumenSceneDetail;

	// Ideally this structure would contain a mirror of all view origin specific data, so Lumen scene updates don't end up with
	// dependencies on FViewInfo, but there are still some code paths that pull data from the FViewInfo structure, which are messy
	// to refactor.  So this reference view is included to allow fetching an FViewInfo to send to those code paths.
	// 
	// The first is the "GetDeferredLightParameters" utility function, which uses a bunch of data from the FViewInfo structure, which
	// will be invariant across shared origin views in practice.  This includes fields originally copied from CVars, post process
	// settings, and projection type.  In the future, we could add an API variation that takes those all values as loose parameters.
	//
	// The View uniform buffer is used to access some data assumed to be invariant for views that share an origin:
	//		View.StateFrameIndex			shared origin views are created on same frame and always render together
	//		View.StateFrameIndexMod8		""
	//		View.PreExposure				shared origin views share exposure
	//		View.OneOverPreExposure			""
	//
	// The Substrate global uniform buffer is accessed from FViewInfo, but doesn't include any view dependent data.  Looking at
	// InitialiseSubstrateViewData, it uses SceneTexturesConfig.Extent (as opposed to a view rect), and View.GetShaderPlatform().
	// The Substrate uniforms aren't initialized until mid render, while the view origin is created early in render.  We could
	// copy those into the Lumen view origin later, but it works well enough to grab it from the view when it's needed.
	//
	// Messier are the uses of FViewInfo in FDeferredShadingSceneRenderer::RenderDirectLightingForLumenScene, where view specific
	// forward lighting data, volumetric cloud shadows, ray tracing TLAS, miscellaneous post process settings, shader map, view
	// family, feature level, and FScene are referenced.  Basically a ton of stuff.  To share all that, we probably need to refactor
	// things so there is a formal concept of shared origin views (FViewSharedOrigin?) at a higher level in the scene renderer
	// itself.  Then we could pull all of the above into that structure.  But that goes well beyond the scope of adding Lumen support
	// for cube maps, which is the immediate goal.
	//
	// There may be rendering artifacts with forward lighting, volumetric cloud shadows, and ray tracing, given that the code that
	// generates those may not be completely shared origin view friendly.  Forward lighting pulls in lights from the frustum, so that
	// definitely seems like it should be modified to take into account the frustums of all shared origin views.  It's less clear if
	// volumetric cloud shadows and ray tracing are view direction or just view origin aware (offhand, they look origin aware, but I
	// haven't done a deep dive).
	//
	const FViewInfo* ReferenceView;
};

// Temporaries valid only in a single frame
struct FLumenSceneFrameTemporaries
{
	FLumenSceneFrameTemporaries(const TArray<FViewInfo>& Views);

	// Current frame's buffers for writing feedback
	FLumenSurfaceCacheFeedback::FFeedbackResources SurfaceCacheFeedbackResources;

	FRDGTextureRef AlbedoAtlas = nullptr;
	FRDGTextureRef OpacityAtlas = nullptr;
	FRDGTextureRef NormalAtlas = nullptr;
	FRDGTextureRef EmissiveAtlas = nullptr;
	FRDGTextureRef DepthAtlas = nullptr;

	FRDGTextureRef DirectLightingAtlas = nullptr;
	FRDGTextureRef IndirectLightingAtlas = nullptr;
	FRDGTextureRef RadiosityNumFramesAccumulatedAtlas = nullptr;
	FRDGTextureRef FinalLightingAtlas = nullptr;
	FRDGBufferRef TileShadowDownsampleFactorAtlas = nullptr;
	FRDGTextureRef DiffuseLightingAndSecondMomentHistoryAtlas = nullptr;
	FRDGTextureRef NumFramesAccumulatedHistoryAtlas = nullptr;

	FRDGBufferSRV* CardBufferSRV = nullptr;
	FRDGBufferSRV* MeshCardsBufferSRV = nullptr;
	FRDGBufferSRV* HeightfieldBufferSRV = nullptr;
	FRDGBufferSRV* PrimitiveGroupBufferSRV = nullptr;
	FRDGBufferSRV* SceneInstanceIndexToMeshCardsIndexBufferSRV = nullptr;
	FRDGBufferSRV* PageTableBufferSRV = nullptr;
	FRDGBufferSRV* CardPageBufferSRV = nullptr;
	FRDGBufferUAV* CardPageBufferUAV = nullptr;

	FRDGBufferUAV* CardPageLastUsedBufferUAV = nullptr;
	FRDGBufferSRV* CardPageLastUsedBufferSRV = nullptr;

	FRDGBufferUAV* CardPageHighResLastUsedBufferUAV = nullptr;
	FRDGBufferSRV* CardPageHighResLastUsedBufferSRV = nullptr;

	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer = nullptr;

	FRHIGPUBufferReadback* SceneAddOpsReadbackBuffer = nullptr;
	FRHIGPUBufferReadback* SceneRemoveOpsReadbackBuffer = nullptr;
	FRHIGPUBufferReadback* SurfaceCacheFeedbackBuffer = nullptr;

	UE::Tasks::FTask UpdateSceneTask;
	bool bReallocateAtlas = false;

	TArray<FLumenViewOrigin, TFixedAllocator<LUMEN_MAX_VIEWS>> ViewOrigins;

	FIntPoint ViewExtent;
	
	// Targets shared per view, but can't be shared per pass
	FLumenSharedRT ReflectSpecularIndirect[(uint32)ELumenReflectionPass::MAX];
	FLumenSharedRT ReflectNumHistoryFrames[(uint32)ELumenReflectionPass::MAX];
	FLumenSharedRT ReflectResolveVariance[(uint32)ELumenReflectionPass::MAX];

	FLumenSharedRT DiffuseIndirect;
	FLumenSharedRT BackfaceDiffuseIndirect;
	FLumenSharedRT RoughSpecularIndirect;
	FLumenSharedRT NumHistoryFrames;
	FLumenSharedRT ResolveVariance;
	FLumenSharedRT NewDiffuseIndirect;
	FLumenSharedRT NewBackfaceDiffuseIndirect;
	FLumenSharedRT NewRoughSpecularIndirect;
	FLumenSharedRT NewNumHistoryFrames;
	FLumenSharedRT NewResolveVariance;
	FLumenSharedRT NewHistoryFastUpdateMode;
	FLumenSharedRT DepthHistory;
	FLumenSharedRT NormalHistory;

	FLumenSharedRT ReservoirRayDirection;
	FLumenSharedRT ReservoirTraceRadiance;
	FLumenSharedRT ReservoirTraceHitDistance;
	FLumenSharedRT ReservoirTraceHitNormal;
	FLumenSharedRT ReservoirWeights;
	FLumenSharedRT DownsampledSceneDepth;
	FLumenSharedRT DownsampledWorldNormal;

	// Optional debug data enabled with stats visualization
	// Contains cursor point cards information
	FRDGBufferSRVRef DebugData = nullptr;
};

// Tracks scene-wide lighting state whose changes we should propagate quickly by flushing various lighting caches
class FLumenGlobalLightingState
{
public:
	FLinearColor DirectionalLightColor;
	FLinearColor SkyLightColor;
	bool bDirectionalLightValid;
	bool bSkyLightValid;

	FLumenGlobalLightingState()
	{
		DirectionalLightColor = FLinearColor::Black;
		SkyLightColor = FLinearColor::Black;
		bDirectionalLightValid = false;
		bSkyLightValid = false;
	}
};

class FLumenSceneData
{
public:
	// Clear all cached state like surface cache atlas. Including extra state like final lighting. Used only for debugging.
	bool bDebugClearAllCachedState = false;

	// Whether we should re-upload entire Lumen Scene on next update
	bool bReuploadSceneRequest = false;

	TSparseSpanArray<FLumenCard> Cards;
	FUniqueIndexList CardIndicesToUpdateInBuffer;
	TRefCountPtr<FRDGPooledBuffer> CardBuffer;
	FRDGScatterUploadBuffer CardUploadBuffer;

	// Primitive groups
	FUniqueIndexList PrimitiveGroupIndicesToUpdateInBuffer;
	TSparseSpanArray<FLumenPrimitiveGroup> PrimitiveGroups;
	TRefCountPtr<FRDGPooledBuffer> PrimitiveGroupBuffer;
	FRDGScatterUploadBuffer PrimitiveGroupUploadBuffer;

	// Maps RayTracingGroupId to a specific Primitive Group Index
	Experimental::TRobinHoodHashMap<int32, int32> RayTracingGroups;

	// List of landscape primitives added to the Lumen scene
	TArray<const FPrimitiveSceneInfo*> LandscapePrimitives;

	// Mesh Cards
	FUniqueIndexList MeshCardsIndicesToUpdateInBuffer;
	TSparseSpanArray<FLumenMeshCards> MeshCards;
	TRefCountPtr<FRDGPooledBuffer> MeshCardsBuffer;
	FRDGScatterUploadBuffer MeshCardsUploadBuffer;

	// Heightfields
	FUniqueIndexList HeightfieldIndicesToUpdateInBuffer;
	TSparseSpanArray<FLumenHeightfield> Heightfields;
	TRefCountPtr<FRDGPooledBuffer> HeightfieldBuffer;
	FRDGScatterUploadBuffer HeightfieldUploadBuffer;

	// GPUScene instance index to MeshCards mapping
	FUniqueIndexList PrimitivesToUpdateMeshCards;
	TRefCountPtr<FRDGPooledBuffer> SceneInstanceIndexToMeshCardsIndexBuffer;
	FRDGScatterUploadBuffer SceneInstanceIndexToMeshCardsIndexUploadBuffer;

	// Single card tile per FLumenPageTableEntry. Used for various atlas update operations
	TRefCountPtr<FRDGPooledBuffer> CardPageBuffer;
	FRDGScatterUploadBuffer CardPageUploadBuffer;

	// Last frame index when this page was sampled from. Used to controlling page update rate
	TRefCountPtr<FRDGPooledBuffer> CardPageLastUsedBuffer;
	TRefCountPtr<FRDGPooledBuffer> CardPageHighResLastUsedBuffer;

	// Captured from the triangle scene
	TRefCountPtr<IPooledRenderTarget> AlbedoAtlas;
	TRefCountPtr<IPooledRenderTarget> OpacityAtlas;
	TRefCountPtr<IPooledRenderTarget> NormalAtlas;
	TRefCountPtr<IPooledRenderTarget> EmissiveAtlas;
	TRefCountPtr<IPooledRenderTarget> DepthAtlas;

	// Generated
	TRefCountPtr<IPooledRenderTarget> DirectLightingAtlas;
	TRefCountPtr<IPooledRenderTarget> IndirectLightingAtlas;
	TRefCountPtr<IPooledRenderTarget> RadiosityNumFramesAccumulatedAtlas;
	TRefCountPtr<IPooledRenderTarget> FinalLightingAtlas;
	TRefCountPtr<FRDGPooledBuffer> TileShadowDownsampleFactorAtlas;

	// Radiosity probes
	TRefCountPtr<IPooledRenderTarget> RadiosityTraceRadianceAtlas;
	TRefCountPtr<IPooledRenderTarget> RadiosityTraceHitDistanceAtlas;
	TRefCountPtr<IPooledRenderTarget> RadiosityProbeSHRedAtlas;
	TRefCountPtr<IPooledRenderTarget> RadiosityProbeSHGreenAtlas;
	TRefCountPtr<IPooledRenderTarget> RadiosityProbeSHBlueAtlas;

	// Direct lighting denoising
	TRefCountPtr<IPooledRenderTarget> DiffuseLightingAndSecondMomentHistoryAtlas;
	TRefCountPtr<IPooledRenderTarget> NumFramesAccumulatedHistoryAtlas;

	// Lumen Scene readback for handling GPU driven updates
	FLumenSceneReadback SceneReadback;

	// Virtual surface cache feedback
	FLumenSurfaceCacheFeedback SurfaceCacheFeedback;

	FLumenGlobalLightingState GlobalLightingState;

	bool bFinalLightingAtlasContentsValid;
	int32 NumMeshCardsToAdd = 0;
	int32 NumLockedCardsToUpdate = 0;
	int32 NumHiResPagesToAdd = 0;

	bool bTrackAllPrimitives;
	TSet<FPrimitiveSceneInfo*> PendingAddOperations;
	TSet<FPrimitiveSceneInfo*> PendingUpdateOperations;
	TSet<FPrimitiveSceneInfo*> PendingSurfaceCacheInvalidationOperations;
	TArray<FLumenPrimitiveGroupRemoveInfo> PendingRemoveOperations;

	// Scale factor to adjust atlas size for tuning memory usage
	float SurfaceCacheResolution = 1.0f;

	// Multi-view multi-GPU information
	bool bViewSpecific = false;
#if WITH_MGPU
	bool bViewSpecificMaskInitialized = false;
	FRHIGPUMask ViewSpecificMask;
#endif

	FLumenSceneData(EShaderPlatform ShaderPlatform, EWorldType::Type WorldType);
	FLumenSceneData(bool bInTrackAllPrimitives);
	~FLumenSceneData();

	void UpdatePrimitiveInstanceOffset(int32 PrimitiveIndex);
	void ResetAndConsolidate();

	void AddMeshCards(int32 PrimitiveGroupIndex);
	void UpdateMeshCards(const FMatrix& LocalToWorld, int32 MeshCardsIndex, const FMeshCardsBuildData& MeshCardsBuildData);
	void InvalidateSurfaceCache(FRHIGPUMask GPUMask, int32 MeshCardsIndex);
	void RemoveMeshCards(int32 PrimitiveGroupIndex);

	void RemoveCardFromAtlas(int32 CardIndex);

	bool HasPendingOperations() const
	{
		return PendingAddOperations.Num() > 0 || PendingUpdateOperations.Num() > 0 || PendingRemoveOperations.Num() > 0;
	}

	void DumpStats(const FDistanceFieldSceneData& DistanceFieldSceneData, bool bDumpMeshDistanceFields, bool bDumpPrimitiveGroups);
	bool UpdateAtlasSize();
	void ReleaseAtlas();
	void RemoveAllMeshCards();
	void UploadPageTable(FRDGBuilder& GraphBuilder, FLumenSceneFrameTemporaries& FrameTemporaries);

	void FillFrameTemporaries(FRDGBuilder& GraphBuilder, FLumenSceneFrameTemporaries& FrameTemporaries);

	void AllocateCardAtlases(FRDGBuilder& GraphBuilder, FLumenSceneFrameTemporaries& FrameTemporaries, const FSceneViewFamily* ViewFamily);
	void ReallocVirtualSurface(FLumenCard& Card, int32 CardIndex, int32 ResLevel, bool bLockPages);
	void FreeVirtualSurface(FLumenCard& Card, uint8 FromResLevel, uint8 ToResLevel);

	void UpdateCardMipMapHierarchy(FLumenCard& Card);

	bool IsPhysicalSpaceAvailable(const FLumenCard& Card, int32 ResLevel, bool bSinglePage) const
	{
		return SurfaceCacheAllocator.IsSpaceAvailable(Card, ResLevel, bSinglePage);
	}

	void ForceEvictEntireCache();
	bool EvictOldestAllocation(uint32 MaxFramesSinceLastUsed, TSparseUniqueList<int32, SceneRenderingAllocator>& DirtyCards);

	uint32 GetSurfaceCacheUpdateFrameIndex() const;
	void IncrementSurfaceCacheUpdateFrameIndex();

	const FLumenPageTableEntry& GetPageTableEntry(int32 PageTableIndex) const { return PageTable[PageTableIndex]; }
	FLumenPageTableEntry& GetPageTableEntry(int32 PageTableIndex) { return PageTable[PageTableIndex]; }
	void MapSurfaceCachePage(const FLumenSurfaceMipMap& MipMap, int32 PageTableIndex, FRHIGPUMask GPUMask);
	int32 GetNumCardPages() const { return PageTable.Num(); }
	FIntPoint GetPhysicalAtlasSize() const { return PhysicalAtlasSize; }
	FIntPoint GetRadiosityAtlasSize() const;
	FIntPoint GetCardCaptureAtlasSizeInPages() const;
	FIntPoint GetCardCaptureAtlasSize() const;
	uint32 GetCardCaptureRefreshNumTexels() const;
	uint32 GetCardCaptureRefreshNumPages() const;
	ESurfaceCacheCompression GetPhysicalAtlasCompression() const { return PhysicalAtlasCompression; }

	struct FFeedbackData
	{
		const uint32* Data = nullptr;
		uint32 NumElements = 0;
	};

	void UpdateSurfaceCacheFeedback(FFeedbackData Data, const TArray<FVector, TInlineAllocator<2>>& LumenSceneCameraOrigins, TArray<FSurfaceCacheRequest>& MeshCardsUpdate, const FViewFamilyInfo& ViewFamily, int32 RequestHistogram[Lumen::NumDistanceBuckets]);

	void ProcessLumenSurfaceCacheRequests(
		const FViewInfo& MainView,
		float MaxCardUpdateDistanceFromCamera,
		int32 MaxTileCapturesPerFrame,
		FLumenCardRenderer& LumenCardRenderer,
		FRHIGPUMask GPUMask,
		const TArray<FSurfaceCacheRequest, SceneRenderingAllocator>& SurfaceCacheRequests);

	int32 GetMeshCardsIndex(const FPrimitiveSceneInfo* PrimitiveSceneInfo, int32 InstanceIndex) const;

	// Copy initial data from default Lumen scene data to a view specific Lumen scene data
	void CopyInitialData(const FLumenSceneData& SourceSceneData);
#if WITH_MGPU
	void UpdateGPUMask(FRDGBuilder& GraphBuilder, const FLumenSceneFrameTemporaries& FrameTemporaries, FLumenViewState& LumenViewState, FRHIGPUMask ViewGPUMask);
#endif

	uint64 GetGPUSizeBytes(bool bLogSizes) const;

private:

	void AddMeshCardsFromBuildData(int32 PrimitiveGroupIndex, const FMatrix& LocalToWorld, const FMeshCardsBuildData& MeshCardsBuildData, FLumenPrimitiveGroup& PrimitiveGroup);

	void UnmapSurfaceCachePage(bool bLocked, FLumenPageTableEntry& Page, int32 PageIndex);
	bool RecaptureCardPage(const FViewInfo& MainView, FLumenCardRenderer& LumenCardRenderer, FLumenSurfaceCacheAllocator& CaptureAtlasAllocator, FRHIGPUMask GPUMask, int32 PageTableIndex);

	// Frame index used to time-splice various surface cache update operations
	// 0 is a special value, and means that surface contains default data
	uint32 SurfaceCacheUpdateFrameIndex = 1;

	// Used to detect change in data format
	EPixelFormat CurrentLightingDataFormat = PF_Unknown;

	// Virtual surface cache page table
	FIntPoint PhysicalAtlasSize = FIntPoint(0, 0);
	ESurfaceCacheCompression PhysicalAtlasCompression;
	FLumenSurfaceCacheAllocator SurfaceCacheAllocator;

	TSparseSpanArray<FLumenPageTableEntry> PageTable;
	TArray<int32> PageTableIndicesToUpdateInBuffer;
	TRefCountPtr<FRDGPooledBuffer> PageTableBuffer;
	FRDGScatterUploadBuffer PageTableUploadBuffer;

	// List of high res allocated physical pages which can be deallocated on demand, ordered by last used frame
	// FeedbackFrameIndex, PageTableIndex
	FBinaryHeap<uint32, uint32> UnlockedAllocationHeap;

	// List of pages for forced recapture, ordered by request frame index
	// RequestSurfaceCacheFrameIndex, PageTableIndex
	FBinaryHeap<uint32, uint32> PagesToRecaptureHeap[MAX_NUM_GPUS];

	// List of pages ordered by last captured frame used to periodically recapture pages, or for multi-GPU scenarios,
	// to track that a page is uninitialized on a particular GPU, and needs to be captured for the first time (indicated
	// by a CapturedSurfaceCacheFrameIndex value of zero).
	// CapturedSurfaceCacheFrameIndex, PageTableIndex
	FBinaryHeap<uint32, uint32> LastCapturedPageHeap[MAX_NUM_GPUS];
};