// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/NaniteResources.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/Engine.h"
#include "EngineLogs.h"
#include "EngineModule.h"
#include "HAL/LowLevelMemStats.h"
#include "Rendering/NaniteStreamingManager.h"
#include "Rendering/RayTracingGeometryManager.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/InstancedStaticMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "SkeletalRenderPublic.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "CommonRenderResources.h"
#include "DistanceFieldAtlas.h"
#include "NaniteSceneProxy.h"
#include "NaniteVertexFactory.h"
#include "Rendering/NaniteCoarseMeshStreamingManager.h"
#include "Elements/SMInstance/SMInstanceElementData.h" // For SMInstanceElementDataUtil::SMInstanceElementsEnabled
#include "MaterialCachedData.h"
#include "MaterialDomain.h"
#include "MeshMaterialShader.h"
#include "PrimitiveSceneInfo.h"
#include "SceneInterface.h"
#include "StaticMeshComponentLODInfo.h"
#include "Stats/StatsTrace.h"
#include "SkinningDefinitions.h"

#include "ComponentRecreateRenderStateContext.h"
#include "StaticMeshSceneProxyDesc.h"
#include "InstancedStaticMeshSceneProxyDesc.h"
#include "GPUSkinCacheVisualizationData.h"
#include "VT/MeshPaintVirtualTexture.h"

#include "AnimationRuntime.h"

#if WITH_EDITOR
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "Rendering/StaticLightingSystemInterface.h"
#endif

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "SkeletalDebugRendering.h"
#endif

#if WITH_EDITORONLY_DATA
#include "UObject/Package.h"
#endif

#if NANITE_ENABLE_DEBUG_RENDERING
#include "AI/Navigation/NavCollisionBase.h"
#include "PhysicsEngine/BodySetup.h"
#endif

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

DEFINE_GPU_STAT(NaniteStreaming);
DEFINE_GPU_STAT(NaniteReadback);

DECLARE_LLM_MEMORY_STAT(TEXT("Nanite"), STAT_NaniteLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Nanite"), STAT_NaniteSummaryLLM, STATGROUP_LLM);
LLM_DEFINE_TAG(Nanite, NAME_None, NAME_None, GET_STATFNAME(STAT_NaniteLLM), GET_STATFNAME(STAT_NaniteSummaryLLM));

static TAutoConsoleVariable<int32> CVarNaniteAllowWorkGraphMaterials(
	TEXT("r.Nanite.AllowWorkGraphMaterials"),
	0,
	TEXT("Whether to enable support for Nanite work graph materials"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarNaniteAllowSplineMeshes(
	TEXT("r.Nanite.AllowSplineMeshes"),
	1,
	TEXT("Whether to enable support for Nanite spline meshes"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarNaniteAllowSkinnedMeshes(
	TEXT("r.Nanite.AllowSkinnedMeshes"),
	1,
	TEXT("Whether to enable support for Nanite skinned meshes"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

int32 GNaniteAllowMaskedMaterials = 1;
FAutoConsoleVariableRef CVarNaniteAllowMaskedMaterials(
	TEXT("r.Nanite.AllowMaskedMaterials"),
	GNaniteAllowMaskedMaterials,
	TEXT("Whether to allow meshes using masked materials to render using Nanite."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingNaniteProxyMeshes(
	TEXT("r.RayTracing.Geometry.NaniteProxies"),
	1,
	TEXT("Include Nanite proxy meshes in ray tracing effects (default = 1 (Nanite proxy meshes enabled in ray tracing))"));

static TAutoConsoleVariable<int32> CVarRayTracingNaniteSkinnedProxyMeshes(
	TEXT("r.RayTracing.Geometry.NaniteSkinnedProxies"),
	1,
	TEXT("Include Nanite skinned proxy meshes in ray tracing effects (default = 1 (Nanite proxy meshes enabled in ray tracing))"));

static int32 GNaniteRayTracingMode = 0;
static FAutoConsoleVariableRef CVarNaniteRayTracingMode(
	TEXT("r.RayTracing.Nanite.Mode"),
	GNaniteRayTracingMode,
	TEXT("0 - fallback mesh (default);\n")
	TEXT("1 - streamed out mesh;"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
		{
			FGlobalComponentRecreateRenderStateContext Context;
		}),
	ECVF_RenderThreadSafe
);

int32 GNaniteCustomDepthEnabled = 1;
static FAutoConsoleVariableRef CVarNaniteCustomDepthStencil(
	TEXT("r.Nanite.CustomDepth"),
	GNaniteCustomDepthEnabled,
	TEXT("Whether to allow Nanite to render in the CustomDepth pass"),
	ECVF_RenderThreadSafe
);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

extern TAutoConsoleVariable<int32> CVarDebugDrawSimpleBones;
extern TAutoConsoleVariable<int32> CVarDebugDrawBoneAxes;

#endif

namespace Nanite
{
ERayTracingMode GetRayTracingMode()
{
	return (ERayTracingMode)GNaniteRayTracingMode;
}

bool GetSupportsCustomDepthRendering()
{
	return GNaniteCustomDepthEnabled != 0;
}

static_assert(sizeof(FPackedCluster) == NANITE_NUM_PACKED_CLUSTER_FLOAT4S * 16, "NANITE_NUM_PACKED_CLUSTER_FLOAT4S out of sync with sizeof(FPackedCluster)");

FArchive& operator<<(FArchive& Ar, FPackedHierarchyNode& Node)
{
	for (uint32 i = 0; i < NANITE_MAX_BVH_NODE_FANOUT; i++)
	{
		Ar << Node.LODBounds[ i ];
		Ar << Node.Misc0[ i ].BoxBoundsCenter;
		Ar << Node.Misc0[ i ].MinLODError_MaxParentLODError;
		Ar << Node.Misc1[ i ].BoxBoundsExtent;
		Ar << Node.Misc1[ i ].ChildStartReference;
		Ar << Node.Misc2[ i ].ResourcePageIndex_NumPages_GroupPartSize;
	}
	
	return Ar;
}

FArchive& operator<<( FArchive& Ar, FPageStreamingState& PageStreamingState )
{
	Ar << PageStreamingState.BulkOffset;
	Ar << PageStreamingState.BulkSize;
	Ar << PageStreamingState.PageSize;
	Ar << PageStreamingState.DependenciesStart;
	Ar << PageStreamingState.DependenciesNum;
	Ar << PageStreamingState.MaxHierarchyDepth;
	Ar << PageStreamingState.Flags;
	return Ar;
}

void FResources::InitResources(const UObject* Owner)
{
	// TODO: Should remove bulk data from built data if platform cannot run Nanite in any capacity
	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

	if (PageStreamingStates.Num() == 0)
	{
		// Skip resources that have their render data stripped
		return;
	}
	
	// Root pages should be available here. If they aren't, this resource has probably already been initialized and added to the streamer. Investigate!
	check(RootData.Num() > 0);
	PersistentHash = FMath::Max(FCrc::StrCrc32<TCHAR>(*Owner->GetName()), 1u);
#if WITH_EDITOR
	ResourceName = Owner->GetPathName();
#endif
	
	ENQUEUE_RENDER_COMMAND(InitNaniteResources)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			GStreamingManager.Add(this);
		}
	);
}

bool FResources::ReleaseResources()
{
	// TODO: Should remove bulk data from built data if platform cannot run Nanite in any capacity
	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return false;
	}

	if (PageStreamingStates.Num() == 0)
	{
		return false;
	}

	ENQUEUE_RENDER_COMMAND(ReleaseNaniteResources)(
		[this]( FRHICommandListImmediate& RHICmdList)
		{
			GStreamingManager.Remove(this);
		}
	);
	return true;
}

void FResources::Serialize(FArchive& Ar, UObject* Owner, bool bCooked)
{
	LLM_SCOPE_BYTAG(Nanite);

	// Note: this is all derived data, native versioning is not needed, but be sure to bump NANITE_DERIVEDDATA_VER when modifying!
	FStripDataFlags StripFlags( Ar, 0 );
	if( !StripFlags.IsAudioVisualDataStripped() )
	{
		const ITargetPlatform* CookingTarget = (Ar.IsSaving() && bCooked) ? Ar.CookingTarget() : nullptr;
		if (PageStreamingStates.Num() > 0 && CookingTarget != nullptr && !DoesTargetPlatformSupportNanite(CookingTarget))
		{
			// Cook out the Nanite resources for platforms that don't support it.
			FResources Dummy;
			Dummy.SerializeInternal(Ar, Owner, bCooked);
		}
		else
		{
			SerializeInternal(Ar, Owner, bCooked);
		}
	}
}

void FResources::SerializeInternal(FArchive& Ar, UObject* Owner, bool bCooked)
{
	uint32 StoredResourceFlags;
	if (Ar.IsSaving() && bCooked)
	{
		// Disable DDC store when saving out a cooked build
		StoredResourceFlags = ResourceFlags & ~NANITE_RESOURCE_FLAG_STREAMING_DATA_IN_DDC;
		Ar << StoredResourceFlags;
	}
	else
	{
		Ar << ResourceFlags;
		StoredResourceFlags = ResourceFlags;
	}
		
	if (StoredResourceFlags & NANITE_RESOURCE_FLAG_STREAMING_DATA_IN_DDC)
	{
#if !WITH_EDITOR
		checkf(false, TEXT("DDC streaming should only happen in editor"));
#endif
	}
	else
	{
		StreamablePages.Serialize(Ar, Owner, 0);
	}

	Ar << RootData;
	Ar << PageStreamingStates;
	Ar << HierarchyNodes;
	Ar << HierarchyRootOffsets;
	Ar << PageDependencies;
	Ar << ImposterAtlas;
	Ar << NumRootPages;
	Ar << PositionPrecision;
	Ar << NormalPrecision;
	Ar << NumInputTriangles;
	Ar << NumInputVertices;
	Ar << NumInputMeshes;
	Ar << NumInputTexCoords;
	Ar << NumClusters;

#if !WITH_EDITOR
	check(!HasStreamingData() || StreamablePages.GetBulkDataSize() > 0);
#endif
}

bool FResources::HasStreamingData() const
{
	return (uint32)PageStreamingStates.Num() > NumRootPages;
}

#if WITH_EDITOR
void FResources::DropBulkData()
{
	if (!HasStreamingData())
	{
		return;
	}

	if(ResourceFlags & NANITE_RESOURCE_FLAG_STREAMING_DATA_IN_DDC)
	{
		StreamablePages.RemoveBulkData();
	}
}

bool FResources::HasBuildFromDDCError() const
{
	return DDCRebuildState.State.load() == EDDCRebuildState::InitialAfterFailed;
}

void FResources::SetHasBuildFromDDCError(bool bHasError)
{
	if (bHasError)
	{
		EDDCRebuildState ExpectedState = EDDCRebuildState::Initial;
		DDCRebuildState.State.compare_exchange_strong(ExpectedState, EDDCRebuildState::InitialAfterFailed);
	}
	else
	{
		EDDCRebuildState ExpectedState = EDDCRebuildState::InitialAfterFailed;
		DDCRebuildState.State.compare_exchange_strong(ExpectedState, EDDCRebuildState::Initial);
	}
}

void FResources::RebuildBulkDataFromDDC(const UObject* Owner)
{
	BeginRebuildBulkDataFromCache(Owner);
	EndRebuildBulkDataFromCache();
}

void FResources::BeginRebuildBulkDataFromCache(const UObject* Owner)
{
	check(IsInitialState(DDCRebuildState.State.load()));
	if (!HasStreamingData() || (ResourceFlags & NANITE_RESOURCE_FLAG_STREAMING_DATA_IN_DDC) == 0u)
	{
		return;
	}

	using namespace UE::DerivedData;

	FCacheKey Key;
	Key.Bucket = FCacheBucket(TEXT("StaticMesh"));
	Key.Hash = DDCKeyHash;
	check(!DDCKeyHash.IsZero());

	FCacheGetChunkRequest Request;
	Request.Name = Owner->GetPathName();
	Request.Id = FValueId::FromName("NaniteStreamingData");
	Request.Key = Key;
	Request.RawHash = DDCRawHash;
	check(!DDCRawHash.IsZero());

	FSharedBuffer SharedBuffer;
	*DDCRequestOwner = MakePimpl<FRequestOwner>(EPriority::Normal);
	DDCRebuildState.State.store(EDDCRebuildState::Pending);

	GetCache().GetChunks(MakeArrayView(&Request, 1), **DDCRequestOwner,
		[this](FCacheGetChunkResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				StreamablePages.Lock(LOCK_READ_WRITE);
				uint8* Ptr = (uint8*)StreamablePages.Realloc(Response.RawData.GetSize());
				FMemory::Memcpy(Ptr, Response.RawData.GetData(), Response.RawData.GetSize());
				StreamablePages.Unlock();
				StreamablePages.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
				DDCRebuildState.State.store(EDDCRebuildState::Succeeded);
			}
			else
			{
				DDCRebuildState.State.store(EDDCRebuildState::Failed);
			}
		});
}

void FResources::EndRebuildBulkDataFromCache()
{
	if (*DDCRequestOwner)
	{
		(*DDCRequestOwner)->Wait();
		(*DDCRequestOwner).Reset();
	}
	EDDCRebuildState NewState = DDCRebuildState.State.load() != EDDCRebuildState::Failed ?
		EDDCRebuildState::Initial : EDDCRebuildState::InitialAfterFailed;
	DDCRebuildState.State.store(NewState);
}

bool FResources::RebuildBulkDataFromCacheAsync(const UObject* Owner, bool& bFailed)
{
	bFailed = false;

	if (!HasStreamingData() || (ResourceFlags & NANITE_RESOURCE_FLAG_STREAMING_DATA_IN_DDC) == 0u)
	{
		return true;
	}

	if (IsInitialState(DDCRebuildState.State.load()))
	{
		if (StreamablePages.IsBulkDataLoaded())
		{
			return true;
		}

		// Handle Initial state first so we can transition directly to Succeeded/Failed if the data was immediately available from the cache.
		check(!(*DDCRequestOwner).IsValid());
		BeginRebuildBulkDataFromCache(Owner);
	}

	switch (DDCRebuildState.State.load())
	{
	case EDDCRebuildState::Pending:
		return false;
	case EDDCRebuildState::Succeeded:
		check(StreamablePages.GetBulkDataSize() > 0);
		EndRebuildBulkDataFromCache();
		return true;
	case EDDCRebuildState::Failed:
		bFailed = true;
		EndRebuildBulkDataFromCache();
		return true;
	default:
		check(false);
		return true;
	}
}
#endif

void FResources::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(RootData.GetAllocatedSize());
	if (StreamablePages.IsBulkDataLoaded())
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(StreamablePages.GetBulkDataSize());
	}
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ImposterAtlas.GetAllocatedSize());
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(HierarchyNodes.GetAllocatedSize());
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(HierarchyRootOffsets.GetAllocatedSize());
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(PageStreamingStates.GetAllocatedSize());
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(PageDependencies.GetAllocatedSize());
}

void FSceneProxyBase::FMaterialSection::ResetToDefaultMaterial(bool bShading, bool bRaster)
{
	UMaterialInterface* ShadingMaterial = bHidden ? GEngine->NaniteHiddenSectionMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
	FMaterialRenderProxy* DefaultRP = ShadingMaterial->GetRenderProxy();
	if (bShading)
	{
		ShadingMaterialProxy = DefaultRP;
	}
	if (bRaster)
	{
		RasterMaterialProxy = DefaultRP;
	}
}

#if WITH_EDITOR
HHitProxy* FSceneProxyBase::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies)
{
	return FSceneProxyBase::CreateHitProxies(Component->GetPrimitiveComponentInterface(), OutHitProxies);
}

HHitProxy* FSceneProxyBase::CreateHitProxies(IPrimitiveComponent* ComponentInterface,TArray<TRefCountPtr<HHitProxy> >& OutHitProxies)
{	
	// Subclasses will have populated OutHitProxies already - update the hit proxy ID before used by GPUScene
	HitProxyIds.SetNumUninitialized(OutHitProxies.Num());
	for (int32 HitProxyId = 0; HitProxyId < HitProxyIds.Num(); ++HitProxyId)
	{
		HitProxyIds[HitProxyId] = OutHitProxies[HitProxyId]->Id;
	}

	// Create a default hit proxy, but don't add it to our internal list (needed for proper collision mesh selection)
	return FPrimitiveSceneProxy::CreateHitProxies(ComponentInterface, OutHitProxies);
}
#endif

float FSceneProxyBase::GetMaterialDisplacementFadeOutSize() const
{
	static const auto CVarNaniteMaxPixelsPerEdge = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Nanite.MaxPixelsPerEdge"));
	const float PixelsPerEdge = CVarNaniteMaxPixelsPerEdge ? CVarNaniteMaxPixelsPerEdge->GetValueOnAnyThread() : 1.0f;
	return MaterialDisplacementFadeOutSize / PixelsPerEdge;
}

void FSceneProxyBase::DrawStaticElementsInternal(FStaticPrimitiveDrawInterface* PDI, const FLightCacheInterface* LCI)
{
}

void FSceneProxyBase::OnMaterialsUpdated(bool bOverrideMaterialRelevance)
{
	CombinedMaterialRelevance = FMaterialRelevance();
	MaxWPOExtent = 0.0f;
	MinMaxMaterialDisplacement = FVector2f::Zero();
	MaterialDisplacementFadeOutSize = UE_MAX_FLT;
	bHasVertexProgrammableRaster = false;
	bHasPixelProgrammableRaster = false;
	bHasDynamicDisplacement = false;
	bAnyMaterialAlwaysEvaluatesWorldPositionOffset = false;
	bAnyMaterialHasPixelAnimation = false;

	const bool bUseTessellation = UseNaniteTessellation();

	EShaderPlatform ShaderPlatform = GetScene().GetShaderPlatform();
	bool bVelocityEncodeHasPixelAnimation = VelocityEncodeHasPixelAnimation(ShaderPlatform);

	for (auto& MaterialSection : MaterialSections)
	{
		const UMaterialInterface* ShadingMaterial = MaterialSection.ShadingMaterialProxy->GetMaterialInterface();

		// Update section relevance and combined material relevance
		if (!bOverrideMaterialRelevance)
		{
			MaterialSection.MaterialRelevance = ShadingMaterial->GetRelevance_Concurrent(GetScene().GetFeatureLevel());
		}
		CombinedMaterialRelevance |= MaterialSection.MaterialRelevance;

		// Now that the material relevance is updated, determine if any material has programmable raster
		const bool bVertexProgrammableRaster = MaterialSection.IsVertexProgrammableRaster(bEvaluateWorldPositionOffset);
		const bool bPixelProgrammableRaster = MaterialSection.IsPixelProgrammableRaster();
		bHasVertexProgrammableRaster |= bVertexProgrammableRaster;
		bHasPixelProgrammableRaster |= bPixelProgrammableRaster;
		
		// Update the RasterMaterialProxy, which is dependent on hidden status and programmable rasterization
		if (MaterialSection.bHidden)
		{
			MaterialSection.RasterMaterialProxy = GEngine->NaniteHiddenSectionMaterial.Get()->GetRenderProxy();
		}
		else if (bVertexProgrammableRaster || bPixelProgrammableRaster)
		{
			MaterialSection.RasterMaterialProxy = MaterialSection.ShadingMaterialProxy;
		}
		else
		{
			MaterialSection.RasterMaterialProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		}

		// Determine if we need to always evaluate WPO for this material slot.
		const bool bHasWPO = MaterialSection.MaterialRelevance.bUsesWorldPositionOffset;
		MaterialSection.bAlwaysEvaluateWPO = bHasWPO && ShadingMaterial->ShouldAlwaysEvaluateWorldPositionOffset();
		bAnyMaterialAlwaysEvaluatesWorldPositionOffset |= MaterialSection.bAlwaysEvaluateWPO;

		// Determine if has any pixel animation.
		bAnyMaterialHasPixelAnimation |= ShadingMaterial->HasPixelAnimation() && bVelocityEncodeHasPixelAnimation && IsOpaqueOrMaskedBlendMode(ShadingMaterial->GetBlendMode());

		// Determine max extent of WPO
		if (MaterialSection.bAlwaysEvaluateWPO || (bEvaluateWorldPositionOffset && bHasWPO))
		{
			MaterialSection.MaxWPOExtent = ShadingMaterial->GetMaxWorldPositionOffsetDisplacement();
			MaxWPOExtent = FMath::Max(MaxWPOExtent, MaterialSection.MaxWPOExtent);
		}
		else
		{
			MaterialSection.MaxWPOExtent = 0.0f;
		}

		// Determine min/max tessellation displacement
		if (bUseTessellation && MaterialSection.MaterialRelevance.bUsesDisplacement)
		{
			MaterialSection.DisplacementScaling = ShadingMaterial->GetDisplacementScaling();
			if (ShadingMaterial->IsDisplacementFadeEnabled())
			{
				MaterialSection.DisplacementFadeRange = ShadingMaterial->GetDisplacementFadeRange();

				// Determine the smallest pixel size of the maximum amount of displacement before it has entirely faded out
				// NOTE: If the material is ALSO masked, we can't disable it based on tessellation fade (must be manually set
				// to be disabled by PixelProgrammableDistance otherwise non-obvious side effects could occur)
				MaterialDisplacementFadeOutSize = FMath::Min3(
					MaterialSection.MaterialRelevance.bMasked ? 0.0f : MaterialDisplacementFadeOutSize,
					MaterialSection.DisplacementFadeRange.StartSizePixels,
					MaterialSection.DisplacementFadeRange.EndSizePixels
				);
			}
			else
			{
				MaterialSection.DisplacementFadeRange = FDisplacementFadeRange::Invalid();
				MaterialDisplacementFadeOutSize = 0.0f; // never disable pixel programmable rasterization
			}
			
			const float MinDisplacement = (0.0f - MaterialSection.DisplacementScaling.Center) * MaterialSection.DisplacementScaling.Magnitude;
			const float MaxDisplacement = (1.0f - MaterialSection.DisplacementScaling.Center) * MaterialSection.DisplacementScaling.Magnitude;

			MinMaxMaterialDisplacement.X = FMath::Min(MinMaxMaterialDisplacement.X, MinDisplacement);
			MinMaxMaterialDisplacement.Y = FMath::Max(MinMaxMaterialDisplacement.Y, MaxDisplacement);

			bHasDynamicDisplacement = true;
		}
		else
		{
			MaterialSection.DisplacementScaling = FDisplacementScaling();
			MaterialSection.DisplacementFadeRange = FDisplacementFadeRange::Invalid();

			// If we have a material that is pixel programmable but not using tessellation, we can never disable pixel programmable
			// rasterization due to displacement fade (though note we still might disable it due to PixelProgrammableDistance)
			if (bPixelProgrammableRaster)
			{
				MaterialDisplacementFadeOutSize = 0.0f;
			}
		}
	}

	if (!bHasDynamicDisplacement)
	{
		MaterialDisplacementFadeOutSize = 0.0f;
	}
}

bool FSceneProxyBase::SupportsAlwaysVisible() const
{
#if WITH_EDITOR
	// Right now we never use the always visible optimization
	// in editor builds due to dynamic relevance, hit proxies, etc..
	return false;
#else
	if (Nanite::GetSupportsCustomDepthRendering() && ShouldRenderCustomDepth())
	{
		// Custom depth/stencil is not supported yet.
		return false;
	}

	if (GetLightingChannelMask() != GetDefaultLightingChannelMask())
	{
		// Lighting channels are not supported yet.
		return false;
	}

	static bool bAllowStaticLighting = FReadOnlyCVARCache::AllowStaticLighting();
	if (bAllowStaticLighting)
	{
		// Static lighting is not supported
		return false;
	}

	if (bSkinnedMesh)
	{
		// Disallow optimization for skinned meshes (need proper CPU LOD calculation and RecentlyRendered to function)
		return false;
	}

	// Always visible
	return true;
#endif
}

FSceneProxy::FSceneProxy(const FMaterialAudit& MaterialAudit, const FStaticMeshSceneProxyDesc& ProxyDesc, const TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe>& InInstanceDataSceneProxy)
: FSceneProxyBase(ProxyDesc)
, MeshInfo(ProxyDesc)
, RenderData(ProxyDesc.GetStaticMesh()->GetRenderData())
, StaticMesh(ProxyDesc.GetStaticMesh())
#if NANITE_ENABLE_DEBUG_RENDERING
, Owner(ProxyDesc.GetOwner())
, LightMapResolution(ProxyDesc.GetStaticLightMapResolution())
, BodySetup(ProxyDesc.GetBodySetup())
, CollisionTraceFlag(ECollisionTraceFlag::CTF_UseSimpleAndComplex)
, CollisionResponse(ProxyDesc.GetCollisionResponseToChannels())
, ForcedLodModel(ProxyDesc.ForcedLodModel)
, LODForCollision(ProxyDesc.GetStaticMesh()->LODForCollision)
, bDrawMeshCollisionIfComplex(ProxyDesc.bDrawMeshCollisionIfComplex)
, bDrawMeshCollisionIfSimple(ProxyDesc.bDrawMeshCollisionIfSimple)
#endif
{
	LLM_SCOPE_BYTAG(Nanite);

	const bool bIsInstancedMesh = InInstanceDataSceneProxy.IsValid();
	if (bIsInstancedMesh)
	{
		// Nanite supports the GPUScene instance data buffer.
		InstanceDataSceneProxy = InInstanceDataSceneProxy;
		SetupInstanceSceneDataBuffers(InstanceDataSceneProxy->GeInstanceSceneDataBuffers());
	}

	Resources = ProxyDesc.GetNaniteResources();

	// This should always be valid.
	checkSlow(Resources && Resources->PageStreamingStates.Num() > 0);

	DistanceFieldSelfShadowBias = FMath::Max(ProxyDesc.bOverrideDistanceFieldSelfShadowBias ? ProxyDesc.DistanceFieldSelfShadowBias : ProxyDesc.GetStaticMesh()->DistanceFieldSelfShadowBias, 0.0f);

	// Use fast path that does not update static draw lists.
	bStaticElementsAlwaysUseProxyPrimitiveUniformBuffer = true;

	// Nanite always uses GPUScene, so we can skip expensive primitive uniform buffer updates.
	bVFRequiresPrimitiveUniformBuffer = false;

	// Indicates if 1 or more materials contain settings not supported by Nanite.
	bHasMaterialErrors = false;

	InstanceWPODisableDistance = ProxyDesc.WorldPositionOffsetDisableDistance;
	PixelProgrammableDistance = ProxyDesc.NanitePixelProgrammableDistance;

	SetWireframeColor(ProxyDesc.GetWireframeColor());

	const bool bHasSurfaceStaticLighting = MeshInfo.GetLightMap() != nullptr || MeshInfo.GetShadowMap() != nullptr;

	const uint32 FirstLODIndex = 0; // Only data from LOD0 is used.
	const FStaticMeshLODResources& MeshResources = RenderData->LODResources[FirstLODIndex];
	const FStaticMeshSectionArray& MeshSections = MeshResources.Sections;

	// Copy the pointer to the volume data, async building of the data may modify the one on FStaticMeshLODResources while we are rendering
	DistanceFieldData = MeshResources.DistanceFieldData;
	CardRepresentationData = MeshResources.CardRepresentationData;

	bEvaluateWorldPositionOffset = ProxyDesc.bEvaluateWorldPositionOffset;
	
	MaterialSections.SetNum(MeshSections.Num());

	for (int32 SectionIndex = 0; SectionIndex < MeshSections.Num(); ++SectionIndex)
	{
		const FStaticMeshSection& MeshSection = MeshSections[SectionIndex];
		FMaterialSection& MaterialSection = MaterialSections[SectionIndex];
		MaterialSection.MaterialIndex = MeshSection.MaterialIndex;
		MaterialSection.bHidden = false;
		MaterialSection.bCastShadow = MeshSection.bCastShadow;
	#if WITH_EDITORONLY_DATA
		MaterialSection.bSelected = false;
		if (GIsEditor)
		{
			if (ProxyDesc.SelectedEditorMaterial != INDEX_NONE)
			{
				MaterialSection.bSelected = (ProxyDesc.SelectedEditorMaterial == MaterialSection.MaterialIndex);
			}
			else if (ProxyDesc.SelectedEditorSection != INDEX_NONE)
			{
				MaterialSection.bSelected = (ProxyDesc.SelectedEditorSection == SectionIndex);
			}

			// If material is hidden, then skip the raster
			if ((ProxyDesc.MaterialIndexPreview != INDEX_NONE) && (ProxyDesc.MaterialIndexPreview != MaterialSection.MaterialIndex))
			{
				MaterialSection.bHidden = true;
			}

			// If section is hidden, then skip the raster
			if ((ProxyDesc.SectionIndexPreview != INDEX_NONE) && (ProxyDesc.SectionIndexPreview != SectionIndex))
			{
				MaterialSection.bHidden = true;
			}
		}
	#endif

		// Keep track of highest observed material index.
		MaterialMaxIndex = FMath::Max(MaterialSection.MaterialIndex, MaterialMaxIndex);

		UMaterialInterface* ShadingMaterial = nullptr;
		if (!MaterialSection.bHidden)
		{
			// Get the shading material
			ShadingMaterial = MaterialAudit.GetMaterial(MaterialSection.MaterialIndex);

			MaterialSection.LocalUVDensities = MaterialAudit.GetLocalUVDensities(MaterialSection.MaterialIndex);

			// Copy over per-instance material flags for this section
			MaterialSection.bHasPerInstanceRandomID = MaterialAudit.HasPerInstanceRandomID(MaterialSection.MaterialIndex);
			MaterialSection.bHasPerInstanceCustomData = MaterialAudit.HasPerInstanceCustomData(MaterialSection.MaterialIndex);

			// Set the IsUsedWithInstancedStaticMeshes usage so per instance random and custom data get compiled
			// in by the HLSL translator in cases where only Nanite scene proxies have rendered with this material
			// which would result in this usage not being set by FInstancedStaticMeshSceneProxy::SetupProxy()
			if (bIsInstancedMesh && ShadingMaterial && !ShadingMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_InstancedStaticMeshes))
			{
				ShadingMaterial = nullptr;
			}

			if (bHasSurfaceStaticLighting && ShadingMaterial && !ShadingMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_StaticLighting))
			{
				ShadingMaterial = nullptr;
			}
		}

		if (ShadingMaterial == nullptr || ProxyDesc.ShouldRenderProxyFallbackToDefaultMaterial())
		{
			ShadingMaterial = MaterialSection.bHidden ? GEngine->NaniteHiddenSectionMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
		}

		MaterialSection.ShadingMaterialProxy = ShadingMaterial->GetRenderProxy();
	}

	// Now that the material sections are initialized, we can make material-dependent calculations
	OnMaterialsUpdated();

	// Nanite supports distance field representation for fully opaque meshes.
	bSupportsDistanceFieldRepresentation = CombinedMaterialRelevance.bOpaque && DistanceFieldData && DistanceFieldData->IsValid();;

	// Find the first LOD with any vertices (ie that haven't been stripped)
	int32 FirstAvailableLOD = 0;
	for (; FirstAvailableLOD < RenderData->LODResources.Num(); FirstAvailableLOD++)
	{
		if (RenderData->LODResources[FirstAvailableLOD].GetNumVertices() > 0)
		{
			break;
		}
	}

	const int32 SMCurrentMinLOD = ProxyDesc.GetStaticMesh()->GetMinLODIdx();
	int32 EffectiveMinLOD = ProxyDesc.bOverrideMinLOD ? ProxyDesc.MinLOD : SMCurrentMinLOD;
	ClampedMinLOD = FMath::Clamp(EffectiveMinLOD, FirstAvailableLOD, RenderData->LODResources.Num() - 1);

#if RHI_RAYTRACING
	if (IsRayTracingAllowed() && ProxyDesc.GetStaticMesh()->bSupportRayTracing && RenderData->LODResources[ClampedMinLOD].GetNumVertices())
	{
		bHasRayTracingInstances = true;

		CoarseMeshStreamingHandle = (Nanite::CoarseMeshStreamingHandle)ProxyDesc.GetStaticMesh()->GetStreamingIndex();
	}
#endif

#if RHI_RAYTRACING || NANITE_ENABLE_DEBUG_RENDERING
	bool bInitializeFallBackLODs = false;
#	if RHI_RAYTRACING
		bInitializeFallBackLODs |= bHasRayTracingInstances;
#	endif
#	if NANITE_ENABLE_DEBUG_RENDERING
		bInitializeFallBackLODs |= true;
#	endif

	if (bInitializeFallBackLODs)
	{
		// Pre-allocate FallbackLODs. Dynamic resize is unsafe as the FFallbackLODInfo constructor queues up a rendering command with a reference to itself.
		FallbackLODs.SetNumUninitialized(RenderData->LODResources.Num());

		for (int32 LODIndex = 0; LODIndex < RenderData->LODResources.Num(); LODIndex++)
		{
			FFallbackLODInfo* NewLODInfo = new (&FallbackLODs[LODIndex]) FFallbackLODInfo(&ProxyDesc, RenderData->LODVertexFactories, LODIndex, ClampedMinLOD);
		}
	}
#endif

#if NANITE_ENABLE_DEBUG_RENDERING
	if (BodySetup)
	{
		CollisionTraceFlag = BodySetup->GetCollisionTraceFlag();
	}
#endif

	FilterFlags = bIsInstancedMesh ? EFilterFlags::InstancedStaticMesh : EFilterFlags::StaticMesh;
	FilterFlags |= ProxyDesc.Mobility == EComponentMobility::Static ? EFilterFlags::StaticMobility : EFilterFlags::NonStaticMobility;

	bReverseCulling = ProxyDesc.bReverseCulling;

	bOpaqueOrMasked = true; // Nanite only supports opaque
	UpdateVisibleInLumenScene();

	MeshPaintTextureResource = ProxyDesc.GetMeshPaintTextureResource();
	MeshPaintTextureCoordinateIndex = ProxyDesc.MeshPaintTextureCoordinateIndex;
}

FSceneProxy::FSceneProxy(const FMaterialAudit& MaterialAudit, const FInstancedStaticMeshSceneProxyDesc& InProxyDesc)
	: FSceneProxy(MaterialAudit, InProxyDesc, InProxyDesc.InstanceDataSceneProxy)
{
	LLM_SCOPE_BYTAG(Nanite);

	// Nanite meshes do not deform internally
	bHasDeformableMesh = false;

#if WITH_EDITOR
	const bool bSupportInstancePicking = HasPerInstanceHitProxies() && SMInstanceElementDataUtil::SMInstanceElementsEnabled();
	HitProxyMode = bSupportInstancePicking ? EHitProxyMode::PerInstance : EHitProxyMode::MaterialSection;

	if (HitProxyMode == EHitProxyMode::PerInstance)
	{
		bHasSelectedInstances = InProxyDesc.bHasSelectedInstances;

		if (bHasSelectedInstances)
		{
			// If we have selected indices, mark scene proxy as selected.
			SetSelection_GameThread(true);
		}
	}
#endif

	EndCullDistance = InProxyDesc.InstanceEndCullDistance;
}

FSceneProxy::FSceneProxy(const FMaterialAudit& MaterialAudit, UStaticMeshComponent* Component, const TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe>& InInstanceDataSceneProxy)
	: FSceneProxy(MaterialAudit, FStaticMeshSceneProxyDesc(Component), InInstanceDataSceneProxy)
{
}

FSceneProxy::FSceneProxy(const FMaterialAudit& MaterialAudit, UInstancedStaticMeshComponent* Component)
	: FSceneProxy(MaterialAudit, FInstancedStaticMeshSceneProxyDesc(Component))
{
}

FSceneProxy::FSceneProxy(const FMaterialAudit& MaterialAudit, UHierarchicalInstancedStaticMeshComponent* Component)
: FSceneProxy(MaterialAudit, static_cast<UInstancedStaticMeshComponent*>(Component))
{
	bIsHierarchicalInstancedStaticMesh = true;

	switch (Component->GetViewRelevanceType())
	{
	case EHISMViewRelevanceType::Grass:
		FilterFlags = EFilterFlags::Grass;
		bIsLandscapeGrass = true;
		break;
	case EHISMViewRelevanceType::Foliage:
		FilterFlags = EFilterFlags::Foliage;
		break;
	default:
		FilterFlags = EFilterFlags::InstancedStaticMesh;
		break;
	}
	FilterFlags |= Component->Mobility == EComponentMobility::Static ? EFilterFlags::StaticMobility : EFilterFlags::NonStaticMobility;
}

FSceneProxy::~FSceneProxy()
{
#if RHI_RAYTRACING
	ReleaseDynamicRayTracingGeometries();
#endif
}

void FSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	check(Resources->RuntimeResourceID != INDEX_NONE && Resources->HierarchyOffset != INDEX_NONE);

#if RHI_RAYTRACING
	if (IsRayTracingAllowed())
	{
		// copy RayTracingGeometryGroupHandle from FStaticMeshRenderData since UStaticMesh can be released before the proxy is destroyed
		RayTracingGeometryGroupHandle = RenderData->RayTracingGeometryGroupHandle;
	}

	if (IsRayTracingEnabled() && bNeedsDynamicRayTracingGeometries)
	{
		CreateDynamicRayTracingGeometries(RHICmdList);
	}
#endif

	MeshPaintTextureDescriptor = MeshPaintVirtualTexture::GetTextureDescriptor(MeshPaintTextureResource, MeshPaintTextureCoordinateIndex);
}

void FSceneProxy::OnEvaluateWorldPositionOffsetChanged_RenderThread()
{
	bHasVertexProgrammableRaster = false;
	for (FMaterialSection& MaterialSection : MaterialSections)
	{
		if (MaterialSection.IsVertexProgrammableRaster(bEvaluateWorldPositionOffset))
		{
			MaterialSection.RasterMaterialProxy = MaterialSection.ShadingMaterialProxy;
			bHasVertexProgrammableRaster = true;
		}
		else
		{
			MaterialSection.ResetToDefaultMaterial(false, true);
		}
	}

	GetRendererModule().RequestStaticMeshUpdate(GetPrimitiveSceneInfo());
}

SIZE_T FSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FPrimitiveViewRelevance FSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	LLM_SCOPE_BYTAG(Nanite);

#if WITH_EDITOR
	const bool bOptimizedRelevance = false;
#else
	const bool bOptimizedRelevance = true;
#endif

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && !!View->Family->EngineShowFlags.NaniteMeshes;
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bRenderCustomDepth = Nanite::GetSupportsCustomDepthRendering() && ShouldRenderCustomDepth();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();

	// Always render the Nanite mesh data with static relevance.
	Result.bStaticRelevance = true;

	// Should always be covered by constructor of Nanite scene proxy.
	Result.bRenderInMainPass = true;

	if (bOptimizedRelevance) // No dynamic relevance if optimized.
	{
		CombinedMaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity();
	}
	else
	{
	#if WITH_EDITOR
		//only check these in the editor
		Result.bEditorVisualizeLevelInstanceRelevance = IsEditingLevelInstanceChild();
		Result.bEditorStaticSelectionRelevance = (WantsEditorEffects() || IsSelected() || IsHovered());
	#endif

	#if NANITE_ENABLE_DEBUG_RENDERING
		bool bDrawSimpleCollision = false, bDrawComplexCollision = false;
		const bool bInCollisionView = IsCollisionView(View->Family->EngineShowFlags, bDrawSimpleCollision, bDrawComplexCollision);
	#else
		bool bInCollisionView = false;
	#endif

		// Set dynamic relevance for overlays like collision and bounds.
		bool bSetDynamicRelevance = false;
	#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
		bSetDynamicRelevance |= (
			// Nanite doesn't respect rich view enabling dynamic relevancy.
			//IsRichView(*View->Family) ||
			View->Family->EngineShowFlags.Collision ||
			bInCollisionView ||
			View->Family->EngineShowFlags.Bounds ||
			View->Family->EngineShowFlags.VisualizeInstanceUpdates
		);
	#endif
	#if NANITE_ENABLE_DEBUG_RENDERING
		bSetDynamicRelevance |= bDrawMeshCollisionIfComplex || bDrawMeshCollisionIfSimple;
	#endif

		if (bSetDynamicRelevance)
		{
			Result.bDynamicRelevance = true;

		#if NANITE_ENABLE_DEBUG_RENDERING
			// If we want to draw collision, needs to make sure we are considered relevant even if hidden
			if (View->Family->EngineShowFlags.Collision || bInCollisionView)
			{
				Result.bDrawRelevance = true;
			}
		#endif
		}

		if (!View->Family->EngineShowFlags.Materials
		#if NANITE_ENABLE_DEBUG_RENDERING
			|| bInCollisionView
		#endif
			)
		{
			Result.bOpaque = true;
		}

		CombinedMaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = Result.bOpaque && Result.bRenderInMainPass && DrawsVelocity();
	}

	return Result;
}

void FSceneProxy::GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const
{
	// Attach the light to the primitive's static meshes.
	const ELightInteractionType InteractionType = MeshInfo.GetInteraction(LightSceneProxy).GetType();
	bRelevant     = (InteractionType != LIT_CachedIrrelevant);
	bDynamic      = (InteractionType == LIT_Dynamic);
	bLightMapped  = (InteractionType == LIT_CachedLightMap || InteractionType == LIT_CachedIrrelevant);
	bShadowMapped = (InteractionType == LIT_CachedSignedDistanceFieldShadowMap2D);
}

#if WITH_EDITOR

FORCENOINLINE HHitProxy* FSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies)
{
	return FSceneProxy::CreateHitProxies(Component->GetPrimitiveComponentInterface(), OutHitProxies);
}

FORCENOINLINE HHitProxy* FSceneProxy::CreateHitProxies(IPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies)
{
	LLM_SCOPE_BYTAG(Nanite);

	switch (HitProxyMode)
	{
		case FSceneProxyBase::EHitProxyMode::MaterialSection:
		{
			if (Component->GetOwner())
			{
				// Generate separate hit proxies for each material section, so that we can perform hit tests against each one.
				for (int32 SectionIndex = 0; SectionIndex < MaterialSections.Num(); ++SectionIndex)
				{
					FMaterialSection& Section = MaterialSections[SectionIndex];
					HHitProxy* ActorHitProxy = Component->CreateMeshHitProxy(SectionIndex, SectionIndex);

					if (ActorHitProxy)
					{
						check(!Section.HitProxy);
						Section.HitProxy = ActorHitProxy;
						OutHitProxies.Add(ActorHitProxy);
					}
				}
			}
			break;
		}

		case FSceneProxyBase::EHitProxyMode::PerInstance:
		{
			// Note: the instance data proxy handles the hitproxy lifetimes internally as the update cadence does not match FPrimitiveSceneInfo ctor cadence
			break;
		}

		default:
			break;
	}

	return Super::CreateHitProxies(Component, OutHitProxies);
}

#endif

FSceneProxy::FMeshInfo::FMeshInfo(const FStaticMeshSceneProxyDesc& InProxyDesc)
{
	LLM_SCOPE_BYTAG(Nanite);

	// StaticLighting only supported by UStaticMeshComponents & derived classes for the moment
	const UStaticMeshComponent* Component =  InProxyDesc.GetUStaticMeshComponent();
	if (!Component)
	{
		return;
	}

	if (Component->GetLightmapType() == ELightmapType::ForceVolumetric)
	{
		SetGlobalVolumeLightmap(true);
	}
#if WITH_EDITOR
	else if (Component && FStaticLightingSystemInterface::GetPrimitiveMeshMapBuildData(Component, 0))
	{
		const FMeshMapBuildData* MeshMapBuildData = FStaticLightingSystemInterface::GetPrimitiveMeshMapBuildData(Component, 0);
		if (MeshMapBuildData)
		{
			SetLightMap(MeshMapBuildData->LightMap);
			SetShadowMap(MeshMapBuildData->ShadowMap);
			SetResourceCluster(MeshMapBuildData->ResourceCluster);
			bCanUsePrecomputedLightingParametersFromGPUScene = true;
			IrrelevantLights = MeshMapBuildData->IrrelevantLights;
		}
	}
#endif
	else if (InProxyDesc.LODData.Num() > 0)
	{
		const FStaticMeshComponentLODInfo& ComponentLODInfo = InProxyDesc.LODData[0];

		const FMeshMapBuildData* MeshMapBuildData = Component->GetMeshMapBuildData(ComponentLODInfo);
		if (MeshMapBuildData)
		{
			SetLightMap(MeshMapBuildData->LightMap);
			SetShadowMap(MeshMapBuildData->ShadowMap);
			SetResourceCluster(MeshMapBuildData->ResourceCluster);
			bCanUsePrecomputedLightingParametersFromGPUScene = true;
			IrrelevantLights = MeshMapBuildData->IrrelevantLights;
		}
	}
}

FLightInteraction FSceneProxy::FMeshInfo::GetInteraction(const FLightSceneProxy* LightSceneProxy) const
{
	// Ask base class
	ELightInteractionType LightInteraction = GetStaticInteraction(LightSceneProxy, IrrelevantLights);

	if (LightInteraction != LIT_MAX)
	{
		return FLightInteraction(LightInteraction);
	}

	// Use dynamic lighting if the light doesn't have static lighting.
	return FLightInteraction::Dynamic();
}

#if RHI_RAYTRACING || NANITE_ENABLE_DEBUG_RENDERING

// Loosely copied from FStaticMeshSceneProxy::FLODInfo::FLODInfo and modified for Nanite fallback
// TODO: Refactor all this to share common code with Nanite and regular SM scene proxy
FSceneProxy::FFallbackLODInfo::FFallbackLODInfo(
	const FStaticMeshSceneProxyDesc* InProxyDesc,
	const FStaticMeshVertexFactoriesArray& InLODVertexFactories,
	int32 LODIndex,
	int32 InClampedMinLOD
)
{
	const auto FeatureLevel = InProxyDesc->GetScene()->GetFeatureLevel();

	FStaticMeshRenderData* MeshRenderData = InProxyDesc->GetStaticMesh()->GetRenderData();
	FStaticMeshLODResources& LODModel = MeshRenderData->LODResources[LODIndex];
	const FStaticMeshVertexFactories& VFs = InLODVertexFactories[LODIndex];

	if (LODIndex < InProxyDesc->LODData.Num() && LODIndex >= InClampedMinLOD)
	{
		const FStaticMeshComponentLODInfo& ComponentLODInfo = InProxyDesc->LODData[LODIndex];

		// Initialize this LOD's overridden vertex colors, if it has any
		if (ComponentLODInfo.OverrideVertexColors)
		{
			bool bBroken = false;
			for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
			{
				const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
				if (Section.MaxVertexIndex >= ComponentLODInfo.OverrideVertexColors->GetNumVertices())
				{
					bBroken = true;
					break;
				}
			}
			if (!bBroken)
			{
				// the instance should point to the loaded data to avoid copy and memory waste
				OverrideColorVertexBuffer = ComponentLODInfo.OverrideVertexColors;
				check(OverrideColorVertexBuffer->GetStride() == sizeof(FColor)); //assumed when we set up the stream

				if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
				{
					TUniformBufferRef<FLocalVertexFactoryUniformShaderParameters>* UniformBufferPtr = &OverrideColorVFUniformBuffer;
					const FLocalVertexFactory* LocalVF = &VFs.VertexFactoryOverrideColorVertexBuffer;
					FColorVertexBuffer* VertexBuffer = OverrideColorVertexBuffer;

					//temp measure to identify nullptr crashes deep in the renderer
					FString ComponentPathName = InProxyDesc->GetPathName();
					checkf(LODModel.VertexBuffers.PositionVertexBuffer.GetNumVertices() > 0, TEXT("LOD: %i of PathName: %s has an empty position stream."), LODIndex, *ComponentPathName);

					ENQUEUE_RENDER_COMMAND(FLocalVertexFactoryCopyData)(
						[UniformBufferPtr, LocalVF, LODIndex, VertexBuffer, ComponentPathName] (FRHICommandListBase&)
						{
							checkf(LocalVF->GetTangentsSRV(), TEXT("LOD: %i of PathName: %s has a null tangents srv."), LODIndex, *ComponentPathName);
							checkf(LocalVF->GetTextureCoordinatesSRV(), TEXT("LOD: %i of PathName: %s has a null texcoord srv."), LODIndex, *ComponentPathName);
							*UniformBufferPtr = CreateLocalVFUniformBuffer(LocalVF, LODIndex, VertexBuffer, 0, 0);
						});
				}
			}
		}
	}

	// Gather the materials applied to the LOD.
	Sections.Empty(MeshRenderData->LODResources[LODIndex].Sections.Num());
	for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
	{
		const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
		FSectionInfo SectionInfo;

		// Determine the material applied to this element of the LOD.
		UMaterialInterface* Material = InProxyDesc->GetMaterial(Section.MaterialIndex, /*bDoingNaniteMaterialAudit*/ false, /*bIgnoreNaniteOverrideMaterials*/ true);
#if WITH_EDITORONLY_DATA
		SectionInfo.MaterialIndex = Section.MaterialIndex;
#endif

		if (Material == nullptr)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		SectionInfo.MaterialProxy = Material->GetRenderProxy();

		// Per-section selection for the editor.
#if WITH_EDITORONLY_DATA
		if (GIsEditor)
		{
			if (InProxyDesc->SelectedEditorMaterial >= 0)
			{
				SectionInfo.bSelected = (InProxyDesc->SelectedEditorMaterial == Section.MaterialIndex);
			}
			else
			{
				SectionInfo.bSelected = (InProxyDesc->SelectedEditorSection == SectionIndex);
			}
		}
#endif

		// Store the element info.
		Sections.Add(SectionInfo);
	}
}

#endif

void FSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	const FLightCacheInterface* LCI = &MeshInfo;
	DrawStaticElementsInternal(PDI, LCI);
}

// Loosely copied from FStaticMeshSceneProxy::GetDynamicMeshElements and modified for Nanite fallback
// TODO: Refactor all this to share common code with Nanite and regular SM scene proxy
void FSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	// Nanite only has dynamic relevance in the editor for certain debug modes
#if WITH_EDITOR
	LLM_SCOPE_BYTAG(Nanite);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_NaniteSceneProxy_GetMeshElements);

	const bool bIsLightmapSettingError = HasStaticLighting() && !HasValidSettingsForStaticLighting();
	const bool bProxyIsSelected = WantsEditorEffects() || IsSelected();
	const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;

	bool bDrawSimpleCollision = false, bDrawComplexCollision = false;
	const bool bInCollisionView = IsCollisionView(EngineShowFlags, bDrawSimpleCollision, bDrawComplexCollision);

#if NANITE_ENABLE_DEBUG_RENDERING
	// Collision and bounds drawing
	FColor SimpleCollisionColor = FColor(157, 149, 223, 255);
	FColor ComplexCollisionColor = FColor(0, 255, 255, 255);


	// Make material for drawing complex collision mesh
	UMaterial* ComplexCollisionMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	FLinearColor DrawCollisionColor = GetWireframeColor();

	// Collision view modes draw collision mesh as solid
	if (bInCollisionView)
	{
		ComplexCollisionMaterial = GEngine->ShadedLevelColorationUnlitMaterial;
	}
	// Wireframe, choose color based on complex or simple
	else
	{
		ComplexCollisionMaterial = GEngine->WireframeMaterial;
		DrawCollisionColor = (CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple) ? SimpleCollisionColor : ComplexCollisionColor;
	}

	// Create colored proxy
	FColoredMaterialRenderProxy* ComplexCollisionMaterialInstance = new FColoredMaterialRenderProxy(ComplexCollisionMaterial->GetRenderProxy(), DrawCollisionColor);
	Collector.RegisterOneFrameMaterialProxy(ComplexCollisionMaterialInstance);


	// Make a material for drawing simple solid collision stuff
	auto SimpleCollisionMaterialInstance = new FColoredMaterialRenderProxy(
		GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(),
		GetWireframeColor()
	);

	Collector.RegisterOneFrameMaterialProxy(SimpleCollisionMaterialInstance);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			if (AllowDebugViewmodes())
			{
				// Should we draw the mesh wireframe to indicate we are using the mesh as collision
				bool bDrawComplexWireframeCollision = (EngineShowFlags.Collision && IsCollisionEnabled() && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);
				
				// Requested drawing complex in wireframe, but check that we are not using simple as complex
				bDrawComplexWireframeCollision |= (bDrawMeshCollisionIfComplex && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseSimpleAsComplex);
				
				// Requested drawing simple in wireframe, and we are using complex as simple
				bDrawComplexWireframeCollision |= (bDrawMeshCollisionIfSimple && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);

				// If drawing complex collision as solid or wireframe
				if (bDrawComplexWireframeCollision || (bInCollisionView && bDrawComplexCollision))
				{
					// If we have at least one valid LOD to draw
					if (RenderData->LODResources.Num() > 0)
					{
						// Get LOD used for collision
						int32 DrawLOD = FMath::Clamp(LODForCollision, 0, RenderData->LODResources.Num() - 1);
						const FStaticMeshLODResources& LODModel = RenderData->LODResources[DrawLOD];

						// Iterate over sections of that LOD
						for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
						{
							// If this section has collision enabled
							if (LODModel.Sections[SectionIndex].bEnableCollision)
							{
							#if WITH_EDITOR
								// See if we are selected
								const bool bSectionIsSelected = FallbackLODs[DrawLOD].Sections[SectionIndex].bSelected;
							#else
								const bool bSectionIsSelected = false;
							#endif

								// Iterate over batches
								const int32 NumMeshBatches = 1; // TODO: GetNumMeshBatches()
								for (int32 BatchIndex = 0; BatchIndex < NumMeshBatches; BatchIndex++)
								{
									FMeshBatch& CollisionElement = Collector.AllocateMesh();
									if (GetCollisionMeshElement(DrawLOD, BatchIndex, SectionIndex, SDPG_World, ComplexCollisionMaterialInstance, CollisionElement))
									{
										Collector.AddMesh(ViewIndex, CollisionElement);
										INC_DWORD_STAT_BY(STAT_StaticMeshTriangles, CollisionElement.GetNumPrimitives());
									}
								}
							}
						}
					}
				}
			}

			// Draw simple collision as wireframe if 'show collision', collision is enabled, and we are not using the complex as the simple
			const bool bDrawSimpleWireframeCollision = (EngineShowFlags.Collision && IsCollisionEnabled() && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple); 

			const FInstanceSceneDataBuffers *InstanceSceneDataBuffers = GetInstanceSceneDataBuffers();

			int32 InstanceCount = 1;
			if (InstanceSceneDataBuffers)
			{
				InstanceCount = InstanceSceneDataBuffers->IsInstanceDataGPUOnly() ? 0 : InstanceSceneDataBuffers->GetNumInstances();
			}

			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; InstanceIndex++)
			{
				FMatrix InstanceToWorld = InstanceSceneDataBuffers ? InstanceSceneDataBuffers->GetInstanceToWorld(InstanceIndex) : GetLocalToWorld();

				if ((bDrawSimpleCollision || bDrawSimpleWireframeCollision) && BodySetup)
				{
					if (FMath::Abs(InstanceToWorld.Determinant()) < UE_SMALL_NUMBER)
					{
						// Catch this here or otherwise GeomTransform below will assert
						// This spams so commented out
						//UE_LOG(LogNanite, Log, TEXT("Zero scaling not supported (%s)"), *StaticMesh->GetPathName());
					}
					else
					{
						const bool bDrawSolid = !bDrawSimpleWireframeCollision;

						if (AllowDebugViewmodes() && bDrawSolid)
						{
							FTransform GeomTransform(InstanceToWorld);
							BodySetup->AggGeom.GetAggGeom(GeomTransform, GetWireframeColor().ToFColor(true), SimpleCollisionMaterialInstance, false, true, AlwaysHasVelocity(), ViewIndex, Collector);
						}
						// wireframe
						else
						{
							FTransform GeomTransform(InstanceToWorld);
							BodySetup->AggGeom.GetAggGeom(GeomTransform, GetSelectionColor(SimpleCollisionColor, bProxyIsSelected, IsHovered()).ToFColor(true), nullptr, (Owner == nullptr), false, AlwaysHasVelocity(), ViewIndex, Collector);
						}

						// The simple nav geometry is only used by dynamic obstacles for now
						if (StaticMesh->GetNavCollision() && StaticMesh->GetNavCollision()->IsDynamicObstacle())
						{
							// Draw the static mesh's body setup (simple collision)
							FTransform GeomTransform(InstanceToWorld);
							FColor NavCollisionColor = FColor(118, 84, 255, 255);
							StaticMesh->GetNavCollision()->DrawSimpleGeom(Collector.GetPDI(ViewIndex), GeomTransform, GetSelectionColor(NavCollisionColor, bProxyIsSelected, IsHovered()).ToFColor(true));
						}
					}
				}

				if (EngineShowFlags.MassProperties && DebugMassData.Num() > 0)
				{
					DebugMassData[0].DrawDebugMass(Collector.GetPDI(ViewIndex), FTransform(InstanceToWorld));
				}

				if (EngineShowFlags.StaticMeshes)
				{
					RenderBounds(Collector.GetPDI(ViewIndex), EngineShowFlags, GetBounds(), !Owner || IsSelected());
				}
			}
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (EngineShowFlags.VisualizeInstanceUpdates && InstanceDataSceneProxy)
			{
				InstanceDataSceneProxy->DebugDrawInstanceChanges(Collector.GetPDI(ViewIndex), EngineShowFlags.Game ? SDPG_World : SDPG_Foreground);
			}
#endif
		}
	}
#endif // NANITE_ENABLE_DEBUG_RENDERING
#endif // WITH_EDITOR
}

#if NANITE_ENABLE_DEBUG_RENDERING

// Loosely copied from FStaticMeshSceneProxy::GetCollisionMeshElement and modified for Nanite fallback
// TODO: Refactor all this to share common code with Nanite and regular SM scene proxy
bool FSceneProxy::GetCollisionMeshElement(
	int32 LODIndex,
	int32 BatchIndex,
	int32 SectionIndex,
	uint8 InDepthPriorityGroup,
	const FMaterialRenderProxy* RenderProxy,
	FMeshBatch& OutMeshBatch) const
{
	const FStaticMeshLODResources& LOD = RenderData->LODResources[LODIndex];
	const FStaticMeshVertexFactories& VFs = RenderData->LODVertexFactories[LODIndex];
	const FStaticMeshSection& Section = LOD.Sections[SectionIndex];

	if (Section.NumTriangles == 0)
	{
		return false;
	}

	const ::FVertexFactory* VertexFactory = nullptr;

	const FFallbackLODInfo& ProxyLODInfo = FallbackLODs[LODIndex];

	const bool bWireframe = false;
	const bool bUseReversedIndices = false;
	const bool bDitheredLODTransition = false;

	SetMeshElementGeometrySource(LODIndex, SectionIndex, bWireframe, bUseReversedIndices, VertexFactory, OutMeshBatch);

	FMeshBatchElement& OutMeshBatchElement = OutMeshBatch.Elements[0];

	if (ProxyLODInfo.OverrideColorVertexBuffer)
	{
		VertexFactory = &VFs.VertexFactoryOverrideColorVertexBuffer;
	
		OutMeshBatchElement.VertexFactoryUserData = ProxyLODInfo.OverrideColorVFUniformBuffer.GetReference();
	}
	else
	{
		VertexFactory = &VFs.VertexFactory;

		OutMeshBatchElement.VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer();
	}

	if (OutMeshBatchElement.NumPrimitives > 0)
	{
		OutMeshBatch.LODIndex = LODIndex;
		OutMeshBatch.VisualizeLODIndex = LODIndex;
		OutMeshBatch.VisualizeHLODIndex = 0;// HierarchicalLODIndex;
		OutMeshBatch.ReverseCulling = IsReversedCullingNeeded(bUseReversedIndices);
		OutMeshBatch.CastShadow = false;
		OutMeshBatch.DepthPriorityGroup = (ESceneDepthPriorityGroup)InDepthPriorityGroup;
		OutMeshBatch.LCI = &MeshInfo;// &ProxyLODInfo;
		OutMeshBatch.VertexFactory = VertexFactory;
		OutMeshBatch.MaterialRenderProxy = RenderProxy;
		OutMeshBatchElement.MinVertexIndex = Section.MinVertexIndex;
		OutMeshBatchElement.MaxVertexIndex = Section.MaxVertexIndex;
		OutMeshBatchElement.VisualizeElementIndex = SectionIndex;

		if (ForcedLodModel > 0)
		{
			OutMeshBatch.bDitheredLODTransition = false;

			OutMeshBatchElement.MaxScreenSize = 0.0f;
			OutMeshBatchElement.MinScreenSize = -1.0f;
		}
		else
		{
			OutMeshBatch.bDitheredLODTransition = bDitheredLODTransition;

			OutMeshBatchElement.MaxScreenSize = RenderData->ScreenSize[LODIndex].GetValue();
			OutMeshBatchElement.MinScreenSize = 0.0f;
			if (LODIndex < MAX_STATIC_MESH_LODS - 1)
			{
				OutMeshBatchElement.MinScreenSize = RenderData->ScreenSize[LODIndex + 1].GetValue();
			}
		}

		return true;
	}
	else
	{
		return false;
	}
}

#endif

bool FSceneProxy::GetInstanceDrawDistanceMinMax(FVector2f& OutDistanceMinMax) const
{
	if (EndCullDistance > 0)
	{
		OutDistanceMinMax = FVector2f(0.0f, float(EndCullDistance));
		return true;
	}
	else
	{
		OutDistanceMinMax = FVector2f(0.0f);
		return false;
	}
}

bool FSceneProxy::GetInstanceWorldPositionOffsetDisableDistance(float& OutWPODisableDistance) const
{
	OutWPODisableDistance = float(InstanceWPODisableDistance);
	return InstanceWPODisableDistance != 0;
}

void FSceneProxy::SetWorldPositionOffsetDisableDistance_GameThread(int32 NewValue)
{
	ENQUEUE_RENDER_COMMAND(CmdSetWPODisableDistance)(
		[this, NewValue](FRHICommandList&)
		{
			const bool bUpdatePrimitiveData = InstanceWPODisableDistance != NewValue;
			const bool bUpdateDrawCmds = bUpdatePrimitiveData && (InstanceWPODisableDistance == 0 || NewValue == 0);

			if (bUpdatePrimitiveData)
			{
				InstanceWPODisableDistance = NewValue;
				GetScene().RequestUniformBufferUpdate(*GetPrimitiveSceneInfo());
				GetScene().RequestGPUSceneUpdate(*GetPrimitiveSceneInfo(), EPrimitiveDirtyState::ChangedOther);
				if (bUpdateDrawCmds)
				{
					GetRendererModule().RequestStaticMeshUpdate(GetPrimitiveSceneInfo());
				}
			}
		});
}

void FSceneProxy::SetInstanceCullDistance_RenderThread(float InStartCullDistance, float InEndCullDistance)
{
	EndCullDistance = InEndCullDistance;
}

FInstanceDataUpdateTaskInfo *FSceneProxy::GetInstanceDataUpdateTaskInfo() const
{
	return InstanceDataSceneProxy ? InstanceDataSceneProxy->GetUpdateTaskInfo() : nullptr;
}

#if RHI_RAYTRACING
bool FSceneProxy::HasRayTracingRepresentation() const
{
	return bHasRayTracingInstances;
}

int32 FSceneProxy::GetFirstValidRaytracingGeometryLODIndex() const
{
	if (GetRayTracingMode() != ERayTracingMode::Fallback)
	{
		// NaniteRayTracing always uses LOD0
		return 0;
	}

	FStaticMeshRayTracingProxyLODArray& RayTracingLODs = RenderData->RayTracingProxy->LODs;

	const int32 NumLODs = RayTracingLODs.Num();

	int32 RayTracingMinLOD = RenderData->RayTracingProxy->bUsingRenderingLODs ? RenderData->GetCurrentFirstLODIdx(ClampedMinLOD) : 0;

#if WITH_EDITOR
	// If coarse mesh streaming mode is set to 2 then we force use the lowest LOD to visualize streamed out coarse meshes
	if (Nanite::FCoarseMeshStreamingManager::GetStreamingMode() == 2)
	{
		RayTracingMinLOD = NumLODs - 1;
	}
#endif // WITH_EDITOR

	// find the first valid RT geometry index
	for (int32 LODIndex = RayTracingMinLOD; LODIndex < NumLODs; ++LODIndex)
	{
		const FRayTracingGeometry& RayTracingGeometry = *RayTracingLODs[LODIndex].RayTracingGeometry;
		if (RayTracingGeometry.IsValid() && !RayTracingGeometry.IsEvicted() && !RayTracingGeometry.HasPendingBuildRequest())
		{
			return LODIndex;
		}
	}

	return INDEX_NONE;
}

void FSceneProxy::SetupRayTracingMaterials(int32 LODIndex, TArray<FMeshBatch>& OutMaterials) const
{
	OutMaterials.SetNum(MaterialSections.Num());

	for (int32 SectionIndex = 0; SectionIndex < OutMaterials.Num(); ++SectionIndex)
	{
		const FMaterialSection& MaterialSection = MaterialSections[SectionIndex];

		const bool bWireframe = false;
		const bool bUseReversedIndices = false;

		FMeshBatch& MeshBatch = OutMaterials[SectionIndex];
		FMeshBatchElement& MeshBatchElement = MeshBatch.Elements[0];

		MeshBatch.VertexFactory = GVertexFactoryResource.GetVertexFactory();
		MeshBatch.MaterialRenderProxy = MaterialSection.ShadingMaterialProxy;
		MeshBatch.bWireframe = bWireframe;
		MeshBatch.SegmentIndex = SectionIndex;
		MeshBatch.LODIndex = 0;
		MeshBatch.CastRayTracedShadow = MaterialSection.bCastShadow && CastsDynamicShadow(); // Relying on BuildInstanceMaskAndFlags(...) to check Material.CastsRayTracedShadows()

		MeshBatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;
	}
}

void FSceneProxy::SetupFallbackRayTracingMaterials(int32 LODIndex, TArray<FMeshBatch>& OutMaterials) const
{
	const FStaticMeshRayTracingProxyLOD& LOD = RenderData->RayTracingProxy->LODs[LODIndex];
	const FStaticMeshVertexFactories& VFs = (*RenderData->RayTracingProxy->LODVertexFactories)[LODIndex];

	const FFallbackLODInfo& FallbackLODInfo = FallbackLODs[LODIndex]; // todo: use RayTracingProxy section info etc

	OutMaterials.SetNum(FallbackLODInfo.Sections.Num());

	for (int32 SectionIndex = 0; SectionIndex < OutMaterials.Num(); ++SectionIndex)
	{
		const FStaticMeshSection& Section = (*LOD.Sections)[SectionIndex];
		const FFallbackLODInfo::FSectionInfo& SectionInfo = FallbackLODInfo.Sections[SectionIndex];

		FMeshBatch& MeshBatch = OutMaterials[SectionIndex];
		FMeshBatchElement& MeshBatchElement = MeshBatch.Elements[0];

		const bool bWireframe = false;
		const bool bUseReversedIndices = false;

		SetMeshElementGeometrySource(LODIndex, SectionIndex, bWireframe, bUseReversedIndices, &VFs.VertexFactory, MeshBatch);

		MeshBatch.VertexFactory = &VFs.VertexFactory;
		MeshBatchElement.VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer();

		MeshBatchElement.MinVertexIndex = Section.MinVertexIndex;
		MeshBatchElement.MaxVertexIndex = Section.MaxVertexIndex;

		MeshBatch.MaterialRenderProxy = SectionInfo.MaterialProxy;
		MeshBatch.bWireframe = bWireframe;
		MeshBatch.SegmentIndex = SectionIndex;
		MeshBatch.LODIndex = 0; // CacheRayTracingPrimitive(...) currently assumes that primitives with CacheInstances flag only cache mesh commands for one LOD
		MeshBatch.CastRayTracedShadow = Section.bCastShadow && CastsDynamicShadow(); // Relying on BuildInstanceMaskAndFlags(...) to check Material.CastsRayTracedShadows()

		MeshBatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;
	}
}

void FSceneProxy::CreateDynamicRayTracingGeometries(FRHICommandListBase& RHICmdList)
{
	check(bNeedsDynamicRayTracingGeometries);
	check(DynamicRayTracingGeometries.IsEmpty());

	FStaticMeshRayTracingProxyLODArray& RayTracingLODs = RenderData->RayTracingProxy->LODs;

	DynamicRayTracingGeometries.AddDefaulted(RayTracingLODs.Num());

	const int32 RayTracingMinLOD = RenderData->RayTracingProxy->bUsingRenderingLODs ? ClampedMinLOD : 0;

	for (int32 LODIndex = RayTracingMinLOD; LODIndex < RayTracingLODs.Num(); LODIndex++)
	{
		FRayTracingGeometryInitializer Initializer = RayTracingLODs[LODIndex].RayTracingGeometry->Initializer;
		for (FRayTracingGeometrySegment& Segment : Initializer.Segments)
		{
			Segment.VertexBuffer = nullptr;
		}
		Initializer.bAllowUpdate = true;
		Initializer.bFastBuild = true;
		Initializer.Type = ERayTracingGeometryInitializerType::Rendering;

		DynamicRayTracingGeometries[LODIndex].SetInitializer(MoveTemp(Initializer));
		DynamicRayTracingGeometries[LODIndex].InitResource(RHICmdList);
	}
}

void FSceneProxy::ReleaseDynamicRayTracingGeometries()
{
	for (auto& Geometry : DynamicRayTracingGeometries)
	{
		Geometry.ReleaseResource();
	}

	DynamicRayTracingGeometries.Empty();
}

void FSceneProxy::GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector)
{
	check(!IsRayTracingStaticRelevant());

	if (CVarRayTracingNaniteProxyMeshes.GetValueOnRenderThread() == 0 || !bHasRayTracingInstances)
	{
		return;
	}

	if (GetRayTracingMode() != ERayTracingMode::Fallback)
	{
		// We don't currently support non-fallback dynamic instances
		return;
	}

	// try and find the first valid RT geometry index
	int32 ValidLODIndex = GetFirstValidRaytracingGeometryLODIndex();
	if (ValidLODIndex == INDEX_NONE)
	{
		return;
	}

	if (!ensure(DynamicRayTracingGeometries.IsValidIndex(ValidLODIndex)))
	{
		return;
	}

	const FStaticMeshLODResources& LODData = RenderData->LODResources[ValidLODIndex];
	FRayTracingGeometry* DynamicGeometry = &DynamicRayTracingGeometries[ValidLODIndex];

	// Setup a new instance
	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = DynamicGeometry;

	const FInstanceSceneDataBuffers* InstanceSceneDataBuffers = GetInstanceSceneDataBuffers();
	const int32 InstanceCount = InstanceSceneDataBuffers ? InstanceSceneDataBuffers->GetNumInstances() : 1;
	
	// NOTE: For now, only single-instance dynamic ray tracing is supported
	checkf(
		InstanceCount == 1,
		TEXT("GetDynamicRayTracingInstances called for a Nanite scene proxy with multiple instances. ")
		TEXT("This isn't currently supported.")
	);
	RayTracingInstance.InstanceTransformsView = MakeArrayView(&GetLocalToWorld(), 1);
	RayTracingInstance.NumTransforms = 1;

	const int32 NumRayTracingMaterialEntries = RenderData->LODResources[ValidLODIndex].Sections.Num();

	// Setup the cached materials again when the LOD changes
	if (NumRayTracingMaterialEntries != CachedRayTracingMaterials.Num() || ValidLODIndex != CachedRayTracingMaterialsLODIndex)
	{
		CachedRayTracingMaterials.Reset();

		SetupFallbackRayTracingMaterials(ValidLODIndex, CachedRayTracingMaterials);
		CachedRayTracingMaterialsLODIndex = ValidLODIndex;
	}
	else
	{
		// Skip computing the mask and flags in the renderer since material didn't change
		RayTracingInstance.bInstanceMaskAndFlagsDirty = false;
	}

	RayTracingInstance.MaterialsView = CachedRayTracingMaterials;

	Collector.AddRayTracingInstance(MoveTemp(RayTracingInstance));

	// Use the shared vertex buffer - needs to be updated every frame
	FRWBuffer* VertexBuffer = nullptr;

	Collector.AddRayTracingGeometryUpdate(
		FRayTracingDynamicGeometryUpdateParams
		{
			CachedRayTracingMaterials,
			false,
			(uint32)LODData.GetNumVertices(),
			(uint32)LODData.GetNumVertices() * (uint32)sizeof(FVector3f),
			DynamicGeometry->Initializer.TotalPrimitiveCount,
			DynamicGeometry,
			VertexBuffer,
			true
		}
	);
}

ERayTracingPrimitiveFlags FSceneProxy::GetCachedRayTracingInstance(FRayTracingInstance& RayTracingInstance)
{
	if (!(IsVisibleInRayTracing() && ShouldRenderInMainPass() && (IsDrawnInGame() || AffectsIndirectLightingWhileHidden()|| CastsHiddenShadow())) && !IsRayTracingFarField())
	{
		return ERayTracingPrimitiveFlags::Exclude;
	}

	if (CVarRayTracingNaniteProxyMeshes.GetValueOnRenderThread() == 0 || !bHasRayTracingInstances)
	{
		return ERayTracingPrimitiveFlags::Exclude;
	}

	static const auto RayTracingStaticMeshesCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.RayTracing.Geometry.StaticMeshes"));

	if (RayTracingStaticMeshesCVar && RayTracingStaticMeshesCVar->GetValueOnRenderThread() <= 0)
	{
		return ERayTracingPrimitiveFlags::Exclude;
	}

	static const auto RayTracingHISMCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.RayTracing.Geometry.HierarchicalInstancedStaticMesh"));

	if (bIsHierarchicalInstancedStaticMesh && RayTracingHISMCVar && RayTracingHISMCVar->GetValueOnRenderThread() <= 0)
	{
		return ERayTracingPrimitiveFlags::Exclude;
	}

	static const auto RayTracingLandscapeGrassCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.RayTracing.Geometry.LandscapeGrass"));

	if (bIsLandscapeGrass && RayTracingLandscapeGrassCVar && RayTracingLandscapeGrassCVar->GetValueOnRenderThread() <= 0)
	{
		return ERayTracingPrimitiveFlags::Exclude;
	}

	const bool bUsingNaniteRayTracing = GetRayTracingMode() != ERayTracingMode::Fallback;
	const bool bIsRayTracingFarField = IsRayTracingFarField();

	// try and find the first valid RT geometry index
	int32 ValidLODIndex = GetFirstValidRaytracingGeometryLODIndex();
	if (ValidLODIndex == INDEX_NONE)
	{
		// Use Skip flag here since Excluded primitives don't get cached ray tracing state updated even if it's marked dirty.
		// ERayTracingPrimitiveFlags::Exclude should only be used for conditions that will cause proxy to be recreated when they change.
		ERayTracingPrimitiveFlags ResultFlags = ERayTracingPrimitiveFlags::Skip;

		if (CoarseMeshStreamingHandle != INDEX_NONE)
		{
			// If there is a streaming handle (but no valid LOD available), then give the streaming flag to make sure it's not excluded
			// It's still needs to be processed during TLAS build because this will drive the streaming of these resources.
			ResultFlags |= ERayTracingPrimitiveFlags::Streaming;
		}

		if (bIsRayTracingFarField)
		{
			ResultFlags |= ERayTracingPrimitiveFlags::FarField;
		}

		return ResultFlags;
	}

	FStaticMeshRayTracingProxyLODArray& RayTracingLODs = RenderData->RayTracingProxy->LODs;

	if (bUsingNaniteRayTracing)
	{
		RayTracingInstance.Geometry = nullptr;
		RayTracingInstance.bApplyLocalBoundsTransform = false;
	}
	else
	{
		RayTracingInstance.Geometry = RenderData->RayTracingProxy->LODs[ValidLODIndex].RayTracingGeometry;
		RayTracingInstance.bApplyLocalBoundsTransform = false;
	}

	//checkf(SupportsInstanceDataBuffer() && InstanceSceneData.Num() <= GetPrimitiveSceneInfo()->GetNumInstanceSceneDataEntries(),
	//	TEXT("Primitives using ERayTracingPrimitiveFlags::CacheInstances require instance transforms available in GPUScene"));

	RayTracingInstance.NumTransforms = GetPrimitiveSceneInfo()->GetNumInstanceSceneDataEntries();
	// When ERayTracingPrimitiveFlags::CacheInstances is used, instance transforms are copied from GPUScene while building ray tracing instance buffer.

	if (bUsingNaniteRayTracing)
	{
		SetupRayTracingMaterials(ValidLODIndex, RayTracingInstance.Materials);
	}
	else
	{
		SetupFallbackRayTracingMaterials(ValidLODIndex, RayTracingInstance.Materials);
	}

	RayTracingInstance.InstanceLayer = bIsRayTracingFarField ? ERayTracingInstanceLayer::FarField : ERayTracingInstanceLayer::NearField;

	// setup the flags
	ERayTracingPrimitiveFlags ResultFlags = ERayTracingPrimitiveFlags::CacheInstances;

	if (CoarseMeshStreamingHandle != INDEX_NONE)
	{
		ResultFlags |= ERayTracingPrimitiveFlags::Streaming;
	}

	if (bIsRayTracingFarField)
	{
		ResultFlags |= ERayTracingPrimitiveFlags::FarField;
	}

	return ResultFlags;
}

RayTracing::GeometryGroupHandle FSceneProxy::GetRayTracingGeometryGroupHandle() const
{
	check(IsInRenderingThread() || IsInParallelRenderingThread());
	return RayTracingGeometryGroupHandle;
}

#endif // RHI_RAYTRACING

#if RHI_RAYTRACING || NANITE_ENABLE_DEBUG_RENDERING

// Loosely copied from FStaticMeshSceneProxy::SetMeshElementGeometrySource and modified for Nanite fallback
// TODO: Refactor all this to share common code with Nanite and regular SM scene proxy
uint32 FSceneProxy::SetMeshElementGeometrySource(
	int32 LODIndex,
	int32 SectionIndex,
	bool bWireframe,
	bool bUseReversedIndices,
	const ::FVertexFactory* VertexFactory,
	FMeshBatch& OutMeshElement) const
{
	const FStaticMeshLODResources& LODModel = RenderData->LODResources[LODIndex];

	const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
	if (Section.NumTriangles == 0)
	{
		return 0;
	}

	const FFallbackLODInfo& LODInfo = FallbackLODs[LODIndex];
	const FFallbackLODInfo::FSectionInfo& SectionInfo = LODInfo.Sections[SectionIndex];

	FMeshBatchElement& OutMeshBatchElement = OutMeshElement.Elements[0];
	uint32 NumPrimitives = 0;

	if (bWireframe)
	{
		if (LODModel.AdditionalIndexBuffers && LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.IsInitialized())
		{
			OutMeshElement.Type = PT_LineList;
			OutMeshBatchElement.FirstIndex = 0;
			OutMeshBatchElement.IndexBuffer = &LODModel.AdditionalIndexBuffers->WireframeIndexBuffer;
			NumPrimitives = LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.GetNumIndices() / 2;
		}
		else
		{
			OutMeshBatchElement.FirstIndex = 0;
			OutMeshBatchElement.IndexBuffer = &LODModel.IndexBuffer;
			NumPrimitives = LODModel.IndexBuffer.GetNumIndices() / 3;

			OutMeshElement.Type = PT_TriangleList;
			OutMeshElement.bWireframe = true;
			OutMeshElement.bDisableBackfaceCulling = true;
		}
	}
	else
	{
		OutMeshElement.Type = PT_TriangleList;

		OutMeshBatchElement.IndexBuffer = bUseReversedIndices ? &LODModel.AdditionalIndexBuffers->ReversedIndexBuffer : &LODModel.IndexBuffer;
		OutMeshBatchElement.FirstIndex = Section.FirstIndex;
		NumPrimitives = Section.NumTriangles;
	}

	OutMeshBatchElement.NumPrimitives = NumPrimitives;
	OutMeshElement.VertexFactory = VertexFactory;

	return NumPrimitives;
}

bool FSceneProxy::IsReversedCullingNeeded(bool bUseReversedIndices) const
{
	// Use != to ensure consistent face directions between negatively and positively scaled primitives
	// NOTE: This is only used debug draw mesh elements
	// (Nanite determines cull mode on the GPU. See ReverseWindingOrder() in NaniteRasterizer.usf)
	const bool bReverseNeeded = IsCullingReversedByComponent() != IsLocalToWorldDeterminantNegative();
	return bReverseNeeded && !bUseReversedIndices;
}

#endif

FResourceMeshInfo FSceneProxy::GetResourceMeshInfo() const
{
	FResourceMeshInfo OutInfo;

	OutInfo.NumClusters = Resources->NumClusters;
	OutInfo.NumNodes = Resources->NumHierarchyNodes;
	OutInfo.NumVertices = Resources->NumInputVertices;
	OutInfo.NumTriangles = Resources->NumInputTriangles;
	OutInfo.NumMaterials = MaterialMaxIndex + 1;
	OutInfo.DebugName = StaticMesh->GetFName();

	OutInfo.NumResidentClusters = Resources->NumResidentClusters;

	{
		const uint32 FirstLODIndex = 0; // Only data from LOD0 is used.
		const FStaticMeshLODResources& MeshResources = RenderData->LODResources[FirstLODIndex];
		const FStaticMeshSectionArray& MeshSections = MeshResources.Sections;

		OutInfo.NumSegments = MeshSections.Num();

		OutInfo.SegmentMapping.Init(INDEX_NONE, MaterialMaxIndex + 1);

		for (int32 SectionIndex = 0; SectionIndex < MeshSections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& MeshSection = MeshSections[SectionIndex];
			OutInfo.SegmentMapping[MeshSection.MaterialIndex] = SectionIndex;
		}
	}

	return MoveTemp(OutInfo);
}

const FCardRepresentationData* FSceneProxy::GetMeshCardRepresentation() const
{
	return CardRepresentationData;
}

void FSceneProxy::GetDistanceFieldAtlasData(const FDistanceFieldVolumeData*& OutDistanceFieldData, float& SelfShadowBias) const
{
	OutDistanceFieldData = DistanceFieldData;
	SelfShadowBias = DistanceFieldSelfShadowBias;
}

bool FSceneProxy::HasDistanceFieldRepresentation() const
{
	return CastsDynamicShadow() && AffectsDistanceFieldLighting() && DistanceFieldData;
}

int32 FSceneProxy::GetLightMapCoordinateIndex() const
{
	const int32 LightMapCoordinateIndex = StaticMesh != nullptr ? StaticMesh->GetLightMapCoordinateIndex() : INDEX_NONE;
	return LightMapCoordinateIndex;
}

bool FSceneProxy::IsCollisionView(const FEngineShowFlags& EngineShowFlags, bool& bDrawSimpleCollision, bool& bDrawComplexCollision) const
{
	bDrawSimpleCollision = bDrawComplexCollision = false;

	const bool bInCollisionView = EngineShowFlags.CollisionVisibility || EngineShowFlags.CollisionPawn;

#if NANITE_ENABLE_DEBUG_RENDERING
	// If in a 'collision view' and collision is enabled
	if (bInCollisionView && IsCollisionEnabled())
	{
		// See if we have a response to the interested channel
		bool bHasResponse = EngineShowFlags.CollisionPawn && CollisionResponse.GetResponse(ECC_Pawn) != ECR_Ignore;
		bHasResponse |= EngineShowFlags.CollisionVisibility && CollisionResponse.GetResponse(ECC_Visibility) != ECR_Ignore;

		if (bHasResponse)
		{
			// Visibility uses complex and pawn uses simple. However, if UseSimpleAsComplex or UseComplexAsSimple is used we need to adjust accordingly
			bDrawComplexCollision = (EngineShowFlags.CollisionVisibility && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseSimpleAsComplex) || (EngineShowFlags.CollisionPawn && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);
			bDrawSimpleCollision = (EngineShowFlags.CollisionPawn && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple) || (EngineShowFlags.CollisionVisibility && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseSimpleAsComplex);
		}
	}
#endif
	return bInCollisionView;
}

uint32 FSceneProxy::GetMemoryFootprint() const
{
	return sizeof( *this ) + GetAllocatedSize();
}

static FGuid AnimRuntimeId(ANIM_RUNTIME_TRANSFORM_PROVIDER_GUID);

FSkinnedSceneProxy::FSkinnedSceneProxy(
	const FMaterialAudit& MaterialAudit,
	USkinnedMeshComponent* InComponent,
	FSkeletalMeshRenderData* InRenderData,
	bool bAllowScaling
)
: FSceneProxyBase(InComponent)
, SkinnedAsset(InComponent->GetSkinnedAsset())
, Resources(InComponent->GetNaniteResources())
, RenderData(InRenderData)
, MeshObject(InComponent->MeshObject)
, TransformProviderId(AnimRuntimeId)
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
, DebugDrawColor(InComponent->GetDebugDrawColor())
, bDrawDebugSkeleton(InComponent->ShouldDrawDebugSkeleton())
#endif
{
	LLM_SCOPE_BYTAG(Nanite);

	// TODO: Nanite-Skinning
	//Nanite::FMaterialAudit MaterialAudit{};

	check(InComponent->MeshObject->IsNaniteMesh());

	// This should always be valid.
	checkSlow(Resources && Resources->PageStreamingStates.Num() > 0);

	// Skinning is supported by this proxy
	bSkinnedMesh = true;

	// TODO: Temp until proper GPU driven shadow cache invalidation is implemented, as well as accurate cluster bounds
	bHasDeformableMesh = true;
	ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Always;

	// Use fast path that does not update static draw lists.
	bStaticElementsAlwaysUseProxyPrimitiveUniformBuffer = true;

	// Nanite always uses GPUScene, so we can skip expensive primitive uniform buffer updates.
	bVFRequiresPrimitiveUniformBuffer = false;

	// Indicates if 1 or more materials contain settings not supported by Nanite.
	//bHasMaterialErrors = false;

	// Get the pre-skinned local bounds
	//InComponent->GetPreSkinnedLocalBounds(PreSkinnedLocalBounds);

	const USkinnedMeshComponent* SkinnedMeshComponent = Cast<const USkinnedMeshComponent>(InComponent);
	if (SkinnedMeshComponent && SkinnedMeshComponent->bPerBoneMotionBlur)
	{
		bAlwaysHasVelocity = true;
	}

	const FReferenceSkeleton& RefSkeleton = SkinnedAsset->GetRefSkeleton();
	const TArray<FTransform>& RefBonePose = RefSkeleton.GetRawRefBonePose();

	TArray<FTransform> ComponentTransforms;
	FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, RefBonePose, ComponentTransforms);

	MaxBoneTransformCount = uint16(RefSkeleton.GetRawBoneNum());
	MaxBoneInfluenceCount = RenderData->GetNumBoneInfluences();

	BoneHierarchy.SetNumUninitialized(MaxBoneTransformCount);

	bHasScale = false;

	const bool bRemoveScale = !bAllowScaling;

	for (int32 BoneIndex = 0; BoneIndex < MaxBoneTransformCount; ++BoneIndex)
	{
		struct FPackedBone
		{
			uint32 BoneParent : 16;
			uint32 BoneDepth : 16;
		}
		Packed;

		const int32 ParentBoneIndex	= RefSkeleton.GetRawParentIndex(BoneIndex);
		const int32 BoneDepth		= RefSkeleton.GetDepthBetweenBones(BoneIndex, 0);
		Packed.BoneParent			= uint16(ParentBoneIndex);
		Packed.BoneDepth			= uint16(BoneDepth);
		BoneHierarchy[BoneIndex]	= *reinterpret_cast<uint32*>(&Packed);

		if (bRemoveScale)
		{
			ComponentTransforms[BoneIndex].RemoveScaling();
		}
		else if (!bHasScale && !FMath::IsNearlyEqual((float)ComponentTransforms[BoneIndex].GetDeterminant(), 1.0f, UE_KINDA_SMALL_NUMBER))
		{
			bHasScale = true;
		}
	}

	// TODO: Shrink/compress representation further
	// Drop one of the rotation components (largest value) and store index in 4 bits to reconstruct
	// 16b fixed point? Variable rate?
	const uint32 FloatCount = GetObjectSpaceFloatCount();
	BoneObjectSpace.SetNumUninitialized(MaxBoneTransformCount * FloatCount);
	float* WritePtr = BoneObjectSpace.GetData();
	for (int32 BoneIndex = 0; BoneIndex < MaxBoneTransformCount; ++BoneIndex)
	{
		const FTransform& Transform = ComponentTransforms[BoneIndex];
		const FQuat& Rotation = Transform.GetRotation();
		const FVector& Translation = Transform.GetTranslation();

		WritePtr[0] = (float)Rotation.X;
		WritePtr[1] = (float)Rotation.Y;
		WritePtr[2] = (float)Rotation.Z;
		WritePtr[3] = (float)Rotation.W;

		WritePtr[4] = (float)Translation.X;
		WritePtr[5] = (float)Translation.Y;
		WritePtr[6] = (float)Translation.Z;

		if (bHasScale)
		{
			const FVector& Scale = Transform.GetScale3D();
			WritePtr[7] = (float)Scale.X;
			WritePtr[8] = (float)Scale.Y;
			WritePtr[9] = (float)Scale.Z;
		}
			
		WritePtr += FloatCount;
	}

	const uint32 FirstLODIndex = 0; // Only data from LOD0 is used.
	const FSkeletalMeshLODRenderData& MeshResources = RenderData->LODRenderData[FirstLODIndex];
	const FSkeletalMeshLODInfo& MeshInfo = *(SkinnedAsset->GetLODInfo(FirstLODIndex));

	const TArray<FSkelMeshRenderSection>& MeshSections = MeshResources.RenderSections;

	MaterialSections.SetNum(MeshSections.Num());

	for (int32 SectionIndex = 0; SectionIndex < MeshSections.Num(); ++SectionIndex)
	{
		const FSkelMeshRenderSection& MeshSection = MeshSections[SectionIndex];
		FMaterialSection& MaterialSection = MaterialSections[SectionIndex];
		MaterialSection.MaterialIndex = MeshSection.MaterialIndex;
		MaterialSection.bCastShadow = MeshSection.bCastShadow;
	#if WITH_EDITORONLY_DATA
		MaterialSection.bSelected = false;
	#endif

		// If we are at a dropped LOD, route material index through the LODMaterialMap in the LODInfo struct.
		{
			if (SectionIndex < MeshInfo.LODMaterialMap.Num() && SkinnedAsset->IsValidMaterialIndex(MeshInfo.LODMaterialMap[SectionIndex]))
			{
				MaterialSection.MaterialIndex = MeshInfo.LODMaterialMap[SectionIndex];
				MaterialSection.MaterialIndex = FMath::Clamp(MaterialSection.MaterialIndex, 0, SkinnedAsset->GetNumMaterials());
			}
		}

		// Keep track of highest observed material index.
		MaterialMaxIndex = FMath::Max(MaterialSection.MaterialIndex, MaterialMaxIndex);

		// If Section is hidden, do not cast shadow
		MaterialSection.bHidden = InComponent->MeshObject->IsMaterialHidden(FirstLODIndex, MaterialSection.MaterialIndex);

		// If the material is NULL, or isn't flagged for use with skeletal meshes, it will be replaced by the default material.
		UMaterialInterface* ShadingMaterial = InComponent->GetMaterial(MaterialSection.MaterialIndex);
		//check(ShadingMaterial);
		/*if (bForceDefaultMaterial || (GForceDefaultMaterial && Material && !IsTranslucentBlendMode(*Material)))
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
			MaterialRelevance |= Material->GetRelevance(FeatureLevel);
		}*/

		bool bValidUsage = ShadingMaterial && ShadingMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh) && ShadingMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_Nanite);

		if (ShadingMaterial == nullptr || !bValidUsage)// || ProxyDesc.ShouldRenderProxyFallbackToDefaultMaterial())
		{
			ShadingMaterial = MaterialSection.bHidden ? GEngine->NaniteHiddenSectionMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
		}

		MaterialSection.ShadingMaterialProxy = ShadingMaterial->GetRenderProxy();

		//MaterialsInUse_GameThread.Add(ShadingMaterial);
	}

	// Now that the material sections are initialized, we can make material-dependent calculations
	OnMaterialsUpdated();

	// Nanite supports distance field representation for fully opaque meshes.
	bSupportsDistanceFieldRepresentation = false;// CombinedMaterialRelevance.bOpaque&& DistanceFieldData&& DistanceFieldData->IsValid();;

#if RHI_RAYTRACING
	//bHasRayTracingInstances = false;
#endif

	FilterFlags = EFilterFlags::SkeletalMesh;
	FilterFlags |= InComponent->Mobility == EComponentMobility::Static ? EFilterFlags::StaticMobility : EFilterFlags::NonStaticMobility;

	bReverseCulling = false;// InComponent->bReverseCulling;

	bOpaqueOrMasked = true; // Nanite only supports opaque
	UpdateVisibleInLumenScene();
}

FSkinnedSceneProxy::~FSkinnedSceneProxy()
{
}

void FSkinnedSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	check(Resources->RuntimeResourceID != INDEX_NONE && Resources->HierarchyOffset != INDEX_NONE);
}

SIZE_T FSkinnedSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FPrimitiveViewRelevance	FSkinnedSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	LLM_SCOPE_BYTAG(Nanite);

	// View relevance is updated once per frame per view across all views in the frame (including shadows) so we update the LOD level for next frame here.
	MeshObject->UpdateMinDesiredLODLevel(View, GetBounds());

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && !!View->Family->EngineShowFlags.NaniteMeshes;
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bRenderCustomDepth = Nanite::GetSupportsCustomDepthRendering() && ShouldRenderCustomDepth();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();

	// Always render the Nanite mesh data with static relevance.
	Result.bStaticRelevance = true;

	// Should always be covered by constructor of Nanite scene proxy.
	Result.bRenderInMainPass = true;

	const auto& EngineShowFlags = View->Family->EngineShowFlags;

	const auto IsDynamic = [&]
	{
	#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
		return IsRichView(*View->Family)
			|| EngineShowFlags.Bones
			|| EngineShowFlags.Collision
			|| EngineShowFlags.Bounds
			|| IsSelected()
		#if WITH_EDITORONLY_DATA
			|| MeshObject->SelectedEditorMaterial != -1
			|| MeshObject->SelectedEditorSection != -1
		#endif
			|| GetGPUSkinCacheVisualizationData().IsActive();
	#else
		return false;
	#endif
	};

	Result.bDynamicRelevance = IsDynamic();

	CombinedMaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = DrawsVelocity();

	return Result;
}

#if WITH_EDITOR

HHitProxy* FSkinnedSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies)
{
	LLM_SCOPE_BYTAG(Nanite);

	switch (HitProxyMode)
	{
	case FSceneProxyBase::EHitProxyMode::MaterialSection:
	{
		if (Component->GetOwner())
		{
			// Generate separate hit proxies for each material section, so that we can perform hit tests against each one.
			for (int32 SectionIndex = 0; SectionIndex < MaterialSections.Num(); ++SectionIndex)
			{
				FMaterialSection& Section = MaterialSections[SectionIndex];

				HHitProxy* ActorHitProxy = nullptr;
				if (Component->GetOwner())
				{
					ActorHitProxy = new HActor(Component->GetOwner(), Component, Component->HitProxyPriority, SectionIndex, SectionIndex);
				}

				if (ActorHitProxy)
				{
					check(!Section.HitProxy);
					Section.HitProxy = ActorHitProxy;
					OutHitProxies.Add(ActorHitProxy);
				}
			}
		}
		break;
	}

	default:
		break;
	}

	return Super::CreateHitProxies(Component, OutHitProxies);
}

#endif

void FSkinnedSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	const FLightCacheInterface* LCI = nullptr;
	DrawStaticElementsInternal(PDI, LCI);
}

void FSkinnedSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!MeshObject)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalMesh);

	const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			if (EngineShowFlags.MassProperties && DebugMassData.Num() > 0)
			{
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				if (MeshObject->GetComponentSpaceTransforms())
				{
					const TArray<FTransform>& ComponentSpaceTransforms = *MeshObject->GetComponentSpaceTransforms();

					for (const FDebugMassData& DebugMass : DebugMassData)
					{
						if (ComponentSpaceTransforms.IsValidIndex(DebugMass.BoneIndex))
						{
							const FTransform BoneToWorld = ComponentSpaceTransforms[DebugMass.BoneIndex] * FTransform(GetLocalToWorld());
							DebugMass.DrawDebugMass(PDI, BoneToWorld);
						}
					}
				}
			}

			if (ViewFamily.EngineShowFlags.SkeletalMeshes)
			{
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}

			if (ViewFamily.EngineShowFlags.Bones || bDrawDebugSkeleton)
			{
				DebugDrawSkeleton(ViewIndex, Collector, ViewFamily.EngineShowFlags);
			}
		}
	}
#endif
}

void FSkinnedSceneProxy::DebugDrawSkeleton(int32 ViewIndex, FMeshElementCollector& Collector, const FEngineShowFlags& EngineShowFlags) const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!MeshObject->GetComponentSpaceTransforms())
	{
		return;
	}

	FMatrix ProxyLocalToWorld = GetLocalToWorld();

	if (ProxyLocalToWorld.GetScaledAxis(EAxis::X).IsNearlyZero(UE_SMALL_NUMBER) &&
		ProxyLocalToWorld.GetScaledAxis(EAxis::Y).IsNearlyZero(UE_SMALL_NUMBER) &&
		ProxyLocalToWorld.GetScaledAxis(EAxis::Z).IsNearlyZero(UE_SMALL_NUMBER))
	{
		// Cannot draw this, world matrix not valid
		return;
	}

	FMatrix WorldToLocal = GetLocalToWorld().InverseFast();
	FTransform LocalToWorldTransform(ProxyLocalToWorld);

	auto MakeRandomColorForSkeleton = [](uint32 InUID)
	{
		FRandomStream Stream((int32)InUID);
		const uint8 Hue = (uint8)(Stream.FRand() * 255.f);
		return FLinearColor::MakeFromHSV8(Hue, 255, 255);
	};

	FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
	TArray<FTransform>& ComponentSpaceTransforms = *MeshObject->GetComponentSpaceTransforms();

	for (int32 Index = 0; Index < ComponentSpaceTransforms.Num(); ++Index)
	{
		const int32 ParentIndex = SkinnedAsset->GetRefSkeleton().GetParentIndex(Index);
		FVector Start, End;

		FLinearColor LineColor = DebugDrawColor.Get(MakeRandomColorForSkeleton(GetPrimitiveComponentId().PrimIDValue));
		const FTransform Transform = ComponentSpaceTransforms[Index] * LocalToWorldTransform;

		if (ParentIndex >= 0)
		{
			Start = (ComponentSpaceTransforms[ParentIndex] * LocalToWorldTransform).GetLocation();
			End = Transform.GetLocation();
		}
		else
		{
			Start = LocalToWorldTransform.GetLocation();
			End = Transform.GetLocation();
		}

		if (EngineShowFlags.Bones || bDrawDebugSkeleton)
		{
			if (CVarDebugDrawSimpleBones.GetValueOnRenderThread() != 0)
			{
				PDI->DrawLine(Start, End, LineColor, SDPG_Foreground, 0.0f, 1.0f);
			}
			else
			{
				SkeletalDebugRendering::DrawWireBone(PDI, Start, End, LineColor, SDPG_Foreground);
			}

			if (CVarDebugDrawBoneAxes.GetValueOnRenderThread() != 0)
			{
				SkeletalDebugRendering::DrawAxes(PDI, Transform, SDPG_Foreground);
			}
		}
	}
#endif
}

#if RHI_RAYTRACING
void FSkinnedSceneProxy::GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector)
{
	if (!CVarRayTracingNaniteSkinnedProxyMeshes.GetValueOnRenderThread())
	{
		return;
	}

	if (MeshObject->GetRayTracingLOD() < RenderData->CurrentFirstLODIdx)
	{
		return;
	}

	MeshObject->QueuePendingRayTracingGeometryUpdate(Collector.GetRHICommandList());

	FRayTracingGeometry* RayTracingGeometry = MeshObject->GetRayTracingGeometry();

	if (RayTracingGeometry && RayTracingGeometry->IsValid())
	{
		// Setup materials for each segment
		const int32 LODIndex = MeshObject->GetRayTracingLOD();
		check(LODIndex < RenderData->LODRenderData.Num());
		const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];		

		check(LODData.RenderSections.Num() > 0);		
		check(LODData.RenderSections.Num() == RayTracingGeometry->Initializer.Segments.Num());

		FRayTracingInstance RayTracingInstance;
		RayTracingInstance.Geometry = RayTracingGeometry;
		RayTracingInstance.InstanceTransformsView = MakeArrayView(&GetLocalToWorld(), 1);
		RayTracingInstance.NumTransforms = 1;

		for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
		{
			const FSkelMeshRenderSection& RenderSection = LODData.RenderSections[SectionIndex];
			FMaterialSection& MaterialSection = MaterialSections[SectionIndex];

			FMeshBatch MeshBatch;				
			MeshBatch.Type = PT_TriangleList;
			MeshBatch.VertexFactory = MeshObject->GetSkinVertexFactory(nullptr, LODIndex, SectionIndex, ESkinVertexFactoryMode::RayTracing);

			MeshBatch.MaterialRenderProxy = MaterialSection.ShadingMaterialProxy;
			MeshBatch.bWireframe = false;
			MeshBatch.SegmentIndex = SectionIndex;
			MeshBatch.LODIndex = LODIndex;
			MeshBatch.CastRayTracedShadow = CastsDynamicShadow(); // Relying on BuildInstanceMaskAndFlags(...) to check Material.CastsRayTracedShadows()

			FMeshBatchElement& MeshBatchElement = MeshBatch.Elements[0];
			MeshBatchElement.IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
			MeshBatchElement.FirstIndex = RenderSection.BaseIndex;
			MeshBatchElement.MinVertexIndex = RenderSection.GetVertexBufferIndex();
			MeshBatchElement.MaxVertexIndex = RenderSection.GetVertexBufferIndex() + RenderSection.GetNumVertices() - 1;
			MeshBatchElement.NumPrimitives = RenderSection.NumTriangles;
			MeshBatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

			RayTracingInstance.Materials.Add(MeshBatch);
		}

		/*
		TODO: Support WPO

		Collector.AddRayTracingGeometryUpdate(
			FRayTracingDynamicGeometryUpdateParams
			{
				RayTracingInstance.Materials,
				false,
				LODData.GetNumVertices(),
				LODData.GetNumVertices() * (uint32)sizeof(FVector3f),
				RayTracingGeometry->Initializer.TotalPrimitiveCount,
				RayTracingGeometry,
				nullptr,
				true
			}
		);*/

		Collector.AddRayTracingInstance(MoveTemp(RayTracingInstance));
	}
}
#endif

uint32 FSkinnedSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

FResourceMeshInfo FSkinnedSceneProxy::GetResourceMeshInfo() const
{
	FResourceMeshInfo OutInfo;

	OutInfo.NumClusters = Resources->NumClusters;
	OutInfo.NumNodes = Resources->NumHierarchyNodes;
	OutInfo.NumVertices = Resources->NumInputVertices;
	OutInfo.NumTriangles = Resources->NumInputTriangles;
	OutInfo.NumMaterials = MaterialMaxIndex + 1;
	OutInfo.DebugName = SkinnedAsset->GetFName();

	OutInfo.NumResidentClusters = Resources->NumResidentClusters;

#if 0 // TODO: Nanite-Skinning
	SkinnedAsset->GetResourceForRendering()

	{
		const uint32 FirstLODIndex = 0; // Only data from LOD0 is used.
		const FStaticMeshLODResources& MeshResources = RenderData->LODResources[FirstLODIndex];
		const FStaticMeshSectionArray& MeshSections = MeshResources.Sections;

		OutInfo.NumSegments = MeshSections.Num();

		OutInfo.SegmentMapping.Init(INDEX_NONE, MaterialMaxIndex + 1);

		for (int32 SectionIndex = 0; SectionIndex < MeshSections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& MeshSection = MeshSections[SectionIndex];
			OutInfo.SegmentMapping[MeshSection.MaterialIndex] = SectionIndex;
		}
	}
#endif

	return MoveTemp(OutInfo);
}

uint32 FSkinnedSceneProxy::GetMaxBoneTransformCount() const
{
	return MaxBoneTransformCount;
}

uint32 FSkinnedSceneProxy::GetMaxBoneInfluenceCount() const
{
	return MaxBoneInfluenceCount;
}

uint32 FSkinnedSceneProxy::GetUniqueAnimationCount() const
{
	return UniqueAnimationCount;
}

const FGuid& FSkinnedSceneProxy::GetTransformProviderId() const
{
	// If the proxy is current in an invalid state, use the
	// reference pose transform provider
	if (TransformProviderId.IsValid())
	{
		bool bIsValid = false;
		GetAnimationProviderData(bIsValid);
		if (!bIsValid)
		{
			static FGuid RefPoseProviderId(REF_POSE_TRANSFORM_PROVIDER_GUID);
			return RefPoseProviderId;
		}
	}

	return TransformProviderId;
}

FDesiredLODLevel FSkinnedSceneProxy::GetDesiredLODLevel_RenderThread(const FSceneView* View) const
{
	return FDesiredLODLevel::CreateFixed(MeshObject->GetLOD());
}

uint8 FSkinnedSceneProxy::GetCurrentFirstLODIdx_RenderThread() const
{
	return RenderData->CurrentFirstLODIdx;
}

struct FAuditMaterialSlotInfo
{
	UMaterialInterface* Material;
	FName SlotName;
	FMeshUVChannelInfo UVChannelData;
};

template<class T>
TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> GetMaterialSlotInfos(const T& Object)
{
	TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> Infos;

	if (UStaticMesh* StaticMesh = Object.GetStaticMesh())
	{
		TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();

		uint32 Index = 0;
		for (FStaticMaterial& Material : StaticMaterials)
		{
			Infos.Add({Object.GetNaniteAuditMaterial(Index), Material.MaterialSlotName, Material.UVChannelData});
			Index++;
		}
	}

	return Infos;
}

template<>
TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> GetMaterialSlotInfos<USkinnedMeshComponent>(const USkinnedMeshComponent& Object)
{
	TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> Infos;

	if (const USkinnedAsset* SkinnedAsset = Object.GetSkinnedAsset())
	{
		const TArray<FSkeletalMaterial>& Materials = SkinnedAsset->GetMaterials();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			const FSkeletalMaterial& Material = Materials[Index];
			Infos.Add({ Material.MaterialInterface, Material.MaterialSlotName, Material.UVChannelData });
		}
	}

	return Infos;
}

template<class T>
FString GetMaterialMeshName(const T& Object)
{
	return Object.GetStaticMesh()->GetName();
}

template<>
FString GetMaterialMeshName<USkinnedMeshComponent>(const USkinnedMeshComponent& Object)
{
	return Object.GetSkinnedAsset()->GetName();
}

template<class T>
bool IsMaterialSkeletalMesh(const T& Object)
{
	return false;
}

template<>
bool IsMaterialSkeletalMesh<USkinnedMeshComponent>(const USkinnedMeshComponent& Object)
{
	return true;
}

template<class T> 
FMaterialAudit& AuditMaterialsImp(const T* InProxyDesc, FMaterialAudit& Audit, bool bSetMaterialUsage)
{
	static const auto NaniteForceEnableMeshesCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Nanite.ForceEnableMeshes"));
	static const bool bNaniteForceEnableMeshes = NaniteForceEnableMeshesCvar && NaniteForceEnableMeshesCvar->GetValueOnAnyThread() != 0;

	Audit.bHasAnyError = false;
	Audit.Entries.Reset();

	if (InProxyDesc != nullptr)
	{
		TArray<FAuditMaterialSlotInfo, TInlineAllocator<32>> Slots = Nanite::GetMaterialSlotInfos(*InProxyDesc);

		uint32 Index = 0;
		for (const FAuditMaterialSlotInfo& SlotInfo : Slots)
		{
			FMaterialAuditEntry& Entry = Audit.Entries.AddDefaulted_GetRef();
			Entry.MaterialSlotName = SlotInfo.SlotName;
			Entry.MaterialIndex = Index;
			Index++;
			Entry.Material = SlotInfo.Material;
			Entry.bHasNullMaterial = Entry.Material == nullptr;
			Entry.LocalUVDensities = FVector4f(
				SlotInfo.UVChannelData.LocalUVDensities[0],
				SlotInfo.UVChannelData.LocalUVDensities[1],
				SlotInfo.UVChannelData.LocalUVDensities[2],
				SlotInfo.UVChannelData.LocalUVDensities[3]
			);

			if (Entry.bHasNullMaterial)
			{
				// Never allow null materials, assign default instead
				Entry.Material = UMaterial::GetDefaultMaterial(MD_Surface);
			}

			const UMaterial* Material = Entry.Material->GetMaterial_Concurrent();
			check(Material != nullptr); // Should always be valid here

			const EBlendMode BlendMode = Entry.Material->GetBlendMode();

			bool bUsingCookedEditorData = false;
		#if WITH_EDITORONLY_DATA
			bUsingCookedEditorData = Material->GetOutermost()->bIsCookedForEditor;
		#endif
			bool bUsageSetSuccessfully = false;

			const FMaterialCachedExpressionData& CachedMaterialData = Material->GetCachedExpressionData();
			Entry.bHasVertexInterpolator		= CachedMaterialData.bHasVertexInterpolator;
			Entry.bHasPerInstanceRandomID		= CachedMaterialData.bHasPerInstanceRandom;
			Entry.bHasPerInstanceCustomData		= CachedMaterialData.bHasPerInstanceCustomData;
			Entry.bHasVertexUVs					= CachedMaterialData.bHasCustomizedUVs;
			Entry.bHasPixelDepthOffset			= Material->HasPixelDepthOffsetConnected();
			Entry.bHasWorldPositionOffset		= Material->HasVertexPositionOffsetConnected();
			Entry.bHasTessellationEnabled		= Material->IsTessellationEnabled();
			Entry.bHasUnsupportedBlendMode		= !IsSupportedBlendMode(BlendMode);
			Entry.bHasUnsupportedShadingModel	= !IsSupportedShadingModel(Material->GetShadingModels());
			Entry.bHasInvalidUsage				= (bUsingCookedEditorData || !bSetMaterialUsage) ? Material->NeedsSetMaterialUsage_Concurrent(bUsageSetSuccessfully, MATUSAGE_Nanite) : !Material->CheckMaterialUsage_Concurrent(MATUSAGE_Nanite);

			if (IsMaterialSkeletalMesh(*InProxyDesc))
			{
				Entry.bHasInvalidUsage |= (bUsingCookedEditorData || !bSetMaterialUsage) ? Material->NeedsSetMaterialUsage_Concurrent(bUsageSetSuccessfully, MATUSAGE_SkeletalMesh) : !Material->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh);
			}

			if (BlendMode == BLEND_Masked)
			{
				Audit.bHasMasked = true;
			}

			if (Material->bIsSky)
			{
				// Sky material is a special case we want to skip
				Audit.bHasSky = true;
			}

			Entry.bHasAnyError =
				Entry.bHasUnsupportedBlendMode |
				Entry.bHasUnsupportedShadingModel |
				Entry.bHasInvalidUsage;

			if (!bUsingCookedEditorData && Entry.bHasAnyError && !Audit.bHasAnyError)
			{
				// Only populate on error for performance/memory reasons
				Audit.AssetName = GetMaterialMeshName(*InProxyDesc);
				Audit.FallbackMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
			}

			Audit.bHasAnyError |= Entry.bHasAnyError;

		#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
			if (!bUsingCookedEditorData && !bNaniteForceEnableMeshes)
			{
				if (Entry.bHasUnsupportedBlendMode)
				{
					const FString BlendModeName = GetBlendModeString(Entry.Material->GetBlendMode());
					if (IsMaterialSkeletalMesh(*InProxyDesc))
					{
						UE_LOG
						(
							LogSkeletalMesh, Warning,
							TEXT("Invalid material [%s] used on Nanite skeletal mesh [%s]. Only opaque or masked blend modes are currently supported, [%s] blend mode was specified."),
							*Entry.Material->GetName(),
							*Audit.AssetName,
							*BlendModeName
						);
					}
					else
					{
						UE_LOG
						(
							LogStaticMesh, Warning,
							TEXT("Invalid material [%s] used on Nanite static mesh [%s]. Only opaque or masked blend modes are currently supported, [%s] blend mode was specified. (NOTE: \"Disallow Nanite\" on static mesh components can be used to suppress this warning and forcibly render the object as non-Nanite.)"),
							*Entry.Material->GetName(),
							*Audit.AssetName,
							*BlendModeName
						);
					}
				}
				if (Entry.bHasUnsupportedShadingModel)
				{
					const FString ShadingModelString = GetShadingModelFieldString(Entry.Material->GetShadingModels());
					if (IsMaterialSkeletalMesh(*InProxyDesc))
					{
						UE_LOG
						(
							LogSkeletalMesh, Warning,
							TEXT("Invalid material [%s] used on Nanite skeletal mesh [%s]. The SingleLayerWater shading model is currently not supported, [%s] shading model was specified."),
							*Entry.Material->GetName(),
							*Audit.AssetName,
							*ShadingModelString
						);
					}
					else
					{
						UE_LOG
						(
							LogStaticMesh, Warning,
							TEXT("Invalid material [%s] used on Nanite static mesh [%s]. The SingleLayerWater shading model is currently not supported, [%s] shading model was specified. (NOTE: \"Disallow Nanite\" on static mesh components can be used to suppress this warning and forcibly render the object as non-Nanite.)"),
							*Entry.Material->GetName(),
							*Audit.AssetName,
							*ShadingModelString
						);
					}
				}
			}
		#endif
		}
	}

	return Audit;
}

void AuditMaterials(const USkinnedMeshComponent* Component, FMaterialAudit& Audit, bool bSetMaterialUsage)
{
	AuditMaterialsImp(Component, Audit, bSetMaterialUsage);
}

void AuditMaterials(const UStaticMeshComponent* Component, FMaterialAudit& Audit, bool bSetMaterialUsage)
{
	AuditMaterialsImp(Component, Audit, bSetMaterialUsage);
}

void AuditMaterials(const FStaticMeshSceneProxyDesc* ProxyDesc, FMaterialAudit& Audit, bool bSetMaterialUsage)
{
	AuditMaterialsImp(ProxyDesc, Audit, bSetMaterialUsage);
}

bool IsSupportedBlendMode(EBlendMode BlendMode)
{
	return IsOpaqueOrMaskedBlendMode(BlendMode);
}
bool IsSupportedBlendMode(const FMaterialShaderParameters& In)	{ return IsSupportedBlendMode(In.BlendMode); }
bool IsSupportedBlendMode(const FMaterial& In)					{ return IsSupportedBlendMode(In.GetBlendMode()); }
bool IsSupportedBlendMode(const UMaterialInterface& In)			{ return IsSupportedBlendMode(In.GetBlendMode()); }

bool IsSupportedMaterialDomain(EMaterialDomain Domain)
{
	return Domain == EMaterialDomain::MD_Surface;
}

bool IsSupportedShadingModel(FMaterialShadingModelField ShadingModelField)
{
	return !ShadingModelField.HasShadingModel(MSM_SingleLayerWater);
}

bool IsMaskingAllowed(UWorld* World, bool bForceNaniteForMasked)
{
	bool bAllowedByWorld = true;

	if (World)
	{
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			bAllowedByWorld = WorldSettings->NaniteSettings.bAllowMaskedMaterials;
		}
	}
	
	return (GNaniteAllowMaskedMaterials != 0) && (bAllowedByWorld || bForceNaniteForMasked);
}

void FVertexFactoryResource::InitRHI(FRHICommandListBase& RHICmdList)
{
	if (DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		LLM_SCOPE_BYTAG(Nanite);
		VertexFactory = new FNaniteVertexFactory(ERHIFeatureLevel::SM5);
		VertexFactory->InitResource(RHICmdList);
	}
}

void FVertexFactoryResource::ReleaseRHI()
{
	if (DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		LLM_SCOPE_BYTAG(Nanite);
		delete VertexFactory;
		VertexFactory = nullptr;
	}
}

TGlobalResource< FVertexFactoryResource > GVertexFactoryResource;

} // namespace Nanite

FNaniteVertexFactory::FNaniteVertexFactory(ERHIFeatureLevel::Type FeatureLevel) : ::FVertexFactory(FeatureLevel)
{
	// We do not want a vertex declaration since this factory is pure compute
	bNeedsDeclaration = false;
}

FNaniteVertexFactory::~FNaniteVertexFactory()
{
	ReleaseResource();
}

void FNaniteVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	LLM_SCOPE_BYTAG(Nanite);
}

bool FNaniteVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	bool bShouldCompile =
		(Parameters.ShaderType->GetFrequency() == SF_Compute || Parameters.ShaderType->GetFrequency() == SF_RayHitGroup || (Parameters.ShaderType->GetFrequency() == SF_WorkGraphComputeNode && NaniteWorkGraphMaterialsSupported() && RHISupportsWorkGraphs(Parameters.Platform))) &&
		(Parameters.MaterialParameters.bIsUsedWithNanite || Parameters.MaterialParameters.bIsSpecialEngineMaterial) &&
		Nanite::IsSupportedMaterialDomain(Parameters.MaterialParameters.MaterialDomain) &&
		Nanite::IsSupportedBlendMode(Parameters.MaterialParameters) &&
		DoesPlatformSupportNanite(Parameters.Platform);

	return bShouldCompile;
}

void FNaniteVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	const bool bUseNaniteUniformBuffers = Parameters.ShaderType->GetFrequency() != SF_RayHitGroup;

	OutEnvironment.SetDefine(TEXT("IS_NANITE_SHADING_PASS"), 1);
	OutEnvironment.SetDefine(TEXT("IS_NANITE_PASS"), 1);
	OutEnvironment.SetDefine(TEXT("USE_ANALYTIC_DERIVATIVES"), 1);
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	OutEnvironment.SetDefine(TEXT("NANITE_USE_RASTER_UNIFORM_BUFFER"), bUseNaniteUniformBuffers);
	OutEnvironment.SetDefine(TEXT("NANITE_USE_SHADING_UNIFORM_BUFFER"), bUseNaniteUniformBuffers);
	OutEnvironment.SetDefine(TEXT("NANITE_USE_RAYTRACING_UNIFORM_BUFFER"), !bUseNaniteUniformBuffers);
	OutEnvironment.SetDefine(TEXT("NANITE_USE_VIEW_UNIFORM_BUFFER"), 1);
	OutEnvironment.SetDefine(TEXT("NANITE_COMPUTE_SHADE"), 1);
	OutEnvironment.SetDefine(TEXT("ALWAYS_EVALUATE_WORLD_POSITION_OFFSET"),
		Parameters.MaterialParameters.bAlwaysEvaluateWorldPositionOffset ? 1 : 0);

	if (NaniteSplineMeshesSupported())
	{
		if (Parameters.MaterialParameters.bIsUsedWithSplineMeshes || Parameters.MaterialParameters.bIsDefaultMaterial)
		{
			// NOTE: This effectively means the logic to deform vertices will be added to the barycentrics calculation in the
			// Nanite shading CS, but will be branched over on instances that do not supply spline mesh parameters. If that
			// frequently causes occupancy issues, we may want to consider ways to split the spline meshes into their own
			// shading bin and permute the CS.
			OutEnvironment.SetDefine(TEXT("USE_SPLINEDEFORM"), 1);
			OutEnvironment.SetDefine(TEXT("USE_SPLINE_MESH_SCENE_RESOURCES"), UseSplineMeshSceneResources(Parameters.Platform));
		}
	}

	if (NaniteSkinnedMeshesSupported())
	{
		if (Parameters.MaterialParameters.bIsUsedWithSkeletalMesh || Parameters.MaterialParameters.bIsDefaultMaterial)
		{
			OutEnvironment.SetDefine(TEXT("USE_SKINNING"), 1);
		}
	}

	OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	OutEnvironment.CompilerFlags.Add(CFLAG_RootConstants);
	OutEnvironment.CompilerFlags.Add(CFLAG_ShaderBundle);
	OutEnvironment.CompilerFlags.Add(CFLAG_CheckForDerivativeOps);
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FNaniteVertexFactory, "/Engine/Private/Nanite/NaniteVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsStaticLighting
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
	| EVertexFactoryFlags::SupportsNaniteRendering
	| EVertexFactoryFlags::SupportsComputeShading
	| EVertexFactoryFlags::SupportsManualVertexFetch
	| EVertexFactoryFlags::SupportsRayTracing
	| EVertexFactoryFlags::SupportsLumenMeshCards
	| EVertexFactoryFlags::SupportsLandscape
	| EVertexFactoryFlags::SupportsPSOPrecaching
);

void ClearNaniteResources(Nanite::FResources& InResources)
{
	InResources = {};
}

void ClearNaniteResources(TPimplPtr<Nanite::FResources>& InResources)
{
	InitNaniteResources(InResources, false /* recreate */);
	ClearNaniteResources(*InResources);
}

void InitNaniteResources(TPimplPtr<Nanite::FResources>& InResources, bool bRecreate)
{
	if (!InResources.IsValid() || bRecreate)
	{
		InResources = MakePimpl<Nanite::FResources>();
	}
}

uint64 GetNaniteResourcesSize(const TPimplPtr<Nanite::FResources>& InResources)
{
	if (InResources.IsValid())
	{
		GetNaniteResourcesSize(*InResources);
	}

	return 0;
}

uint64 GetNaniteResourcesSize(const Nanite::FResources& InResources)
{
	uint64 ResourcesSize = 0;
	ResourcesSize += InResources.RootData.GetAllocatedSize();
	ResourcesSize += InResources.ImposterAtlas.GetAllocatedSize();
	ResourcesSize += InResources.HierarchyNodes.GetAllocatedSize();
	ResourcesSize += InResources.HierarchyRootOffsets.GetAllocatedSize();
	ResourcesSize += InResources.PageStreamingStates.GetAllocatedSize();
	ResourcesSize += InResources.PageDependencies.GetAllocatedSize();
	return ResourcesSize;
}

void GetNaniteResourcesSizeEx(const TPimplPtr<Nanite::FResources>& InResources, FResourceSizeEx& CumulativeResourceSize)
{
	if (InResources.IsValid())
	{
		GetNaniteResourcesSizeEx(*InResources.Get(), CumulativeResourceSize);
	}
}

void GetNaniteResourcesSizeEx(const Nanite::FResources& InResources, FResourceSizeEx& CumulativeResourceSize)
{
	InResources.GetResourceSizeEx(CumulativeResourceSize);
}
