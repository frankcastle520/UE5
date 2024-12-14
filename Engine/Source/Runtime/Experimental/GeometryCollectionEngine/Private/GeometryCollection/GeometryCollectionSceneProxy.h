// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DynamicMeshBuilder.h"
#include "EngineGlobals.h"
#include "HAL/CriticalSection.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "GeometryCollectionRendering.h"
#include "GeometryCollection/GeometryCollectionEditorSelection.h"
#include "HitProxies.h"
#include "EngineUtils.h"
#include "NaniteSceneProxy.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionRenderData.h"
#include "GeometryCollection/GeometryCollectionHitProxy.h"
#include "InstanceDataSceneProxy.h"

class UGeometryCollection;
class UGeometryCollectionComponent;
struct FGeometryCollectionSection;

namespace Nanite
{
	struct FResources;
}


/** Vertex Buffer for transform data */
class FGeometryCollectionTransformBuffer : public FVertexBuffer
{
public:
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("FGeometryCollectionTransformBuffer"));

		// #note: This differs from instanced static mesh in that we are storing the entire transform in the buffer rather than
		// splitting out the translation.  This is to simplify transferring data at runtime as a memcopy
		VertexBufferRHI = RHICmdList.CreateVertexBuffer(NumTransforms * sizeof(FVector4f) * 4, BUF_Dynamic | BUF_ShaderResource, CreateInfo);		
		VertexBufferSRV = RHICmdList.CreateShaderResourceView(VertexBufferRHI, 16, PF_A32B32G32R32F);
	}

	void UpdateDynamicData(FRHICommandListBase& RHICmdList, const TArray<FMatrix44f>& Transforms, EResourceLockMode LockMode);

	int32 NumTransforms;

	FShaderResourceViewRHIRef VertexBufferSRV;
};

inline void CopyTransformsWithConversionWhenNeeded(TArray<FMatrix44f>& DstTransforms, const TArray<FMatrix>& SrcTransforms)
{
	// LWC_TODO : we have no choice but to convert each element at this point to avoid changing GeometryCollectionAlgo::GlobalMatrices that is used all over the place
	DstTransforms.SetNumUninitialized(SrcTransforms.Num());
	for (int TransformIndex = 0; TransformIndex < SrcTransforms.Num(); ++TransformIndex)
	{
		DstTransforms[TransformIndex] = FMatrix44f(SrcTransforms[TransformIndex]); // LWC_TODO: Perf pessimization
	}
}

inline void CopyTransformsWithConversionWhenNeeded(TArray<FMatrix44f>& DstTransforms, const TArray<FTransform>& SrcTransforms)
{
	// LWC_TODO : we have no choice but to convert each element at this point to avoid changing GeometryCollectionAlgo::GlobalMatrices that is used all over the place
	DstTransforms.SetNumUninitialized(SrcTransforms.Num());
	for (int TransformIndex = 0; TransformIndex < SrcTransforms.Num(); ++TransformIndex)
	{
		DstTransforms[TransformIndex] = FTransform3f(SrcTransforms[TransformIndex]).ToMatrixWithScale(); // LWC_TODO: Perf pessimization
	}
}

inline void CopyTransformsWithConversionWhenNeeded(TArray<FMatrix44f>& DstTransforms, const TArray<FTransform3f>& SrcTransforms)
{
	DstTransforms.SetNumUninitialized(SrcTransforms.Num());
	for (int TransformIndex = 0; TransformIndex < SrcTransforms.Num(); ++TransformIndex)
	{
		DstTransforms[TransformIndex] = SrcTransforms[TransformIndex].ToMatrixWithScale();
	}
}

/** Mutable rendering data */
struct FGeometryCollectionDynamicData
{
	TArray<FMatrix44f> Transforms;
	uint64 FrameIndex = 0;

	FGeometryCollectionDynamicData()
	{
		Reset();
	}

	void Reset()
	{
		Transforms.Reset();
		FrameIndex = GFrameCounter;
	}

	void SetTransforms(const TArray<FTransform>& InTransforms)
	{
		// use for LWC as FMatrix and FMatrix44f are different when LWC is on 
		CopyTransformsWithConversionWhenNeeded(Transforms, InTransforms);
	}

	void SetTransforms(const TArray<FTransform3f>& InTransforms)
	{
		CopyTransformsWithConversionWhenNeeded(Transforms, InTransforms);
	}
};


class FGeometryCollectionDynamicDataPool
{
public:
	FGeometryCollectionDynamicDataPool();
	~FGeometryCollectionDynamicDataPool();

	FGeometryCollectionDynamicData* Allocate();
	void Release(FGeometryCollectionDynamicData* DynamicData);

private:
	TArray<FGeometryCollectionDynamicData*> UsedList;
	TArray<FGeometryCollectionDynamicData*> FreeList;

	FCriticalSection ListLock;
};


/***
*   FGeometryCollectionSceneProxy
*    
*	The FGeometryCollectionSceneProxy manages the interaction between the GeometryCollectionComponent
*   on the game thread and the vertex buffers on the render thread.
*
*   NOTE : This class is still in flux, and has a few pending todos. Your comments and 
*   thoughts are appreciated though. The remaining items to address involve:
*   - @todo double buffer - The double buffering of the FGeometryCollectionDynamicData.
*   - @todo GPU skin : Make the skinning use the GpuVertexShader
*/
class FGeometryCollectionSceneProxy final : public FPrimitiveSceneProxy
{
	TArray<UMaterialInterface*> Materials;

	FMaterialRelevance MaterialRelevance;

	FGeometryCollectionMeshResources const& MeshResource;
	FGeometryCollectionMeshDescription MeshDescription;

	int32 NumTransforms = 0;
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollection;

	FCollisionResponseContainer CollisionResponse;

	FBoxSphereBounds PreSkinnedBounds;

	FGeometryCollectionVertexFactory VertexFactory;
	
	bool bSupportsManualVertexFetch;
	FPositionVertexBuffer SkinnedPositionVertexBuffer;

	int32 CurrentTransformBufferIndex = 0;
	bool bSupportsTripleBufferVertexUpload = false;
	bool bRenderResourcesCreated = false;
	TArray<FGeometryCollectionTransformBuffer, TInlineAllocator<3>> TransformBuffers;

	FGeometryCollectionDynamicData* DynamicData = nullptr;

#if WITH_EDITOR
	bool bShowBoneColors = false;
	bool bSuppressSelectionMaterial = false;
	TArray<FColor> BoneColors;
	FColorVertexBuffer ColorVertexBuffer;
	FGeometryCollectionVertexFactory VertexFactoryDebugColor;
	UMaterialInterface* BoneSelectedMaterial = nullptr;
	#endif

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	bool bUsesSubSections = false;
	bool bEnableBoneSelection = false;
	TArray<TRefCountPtr<HHitProxy>> HitProxies;
	FColorVertexBuffer HitProxyIdBuffer;
#endif

#if RHI_RAYTRACING
	bool bGeometryResourceUpdated = false;
	FRayTracingGeometry RayTracingGeometry;
	FRWBuffer RayTracingDynamicVertexBuffer;
#endif

public:
	FGeometryCollectionSceneProxy(UGeometryCollectionComponent* Component);
	virtual ~FGeometryCollectionSceneProxy();

	/** Called on render thread to setup dynamic geometry for rendering */
	void SetDynamicData_RenderThread(FRHICommandListBase& RHICmdList, FGeometryCollectionDynamicData* NewDynamicData);

	uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
	uint32 GetAllocatedSize() const;

	SIZE_T GetTypeHash() const override;
	void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	void DestroyRenderThreadResources() override;
	void GetPreSkinnedLocalBounds(FBoxSphereBounds& OutBounds) const override;
	FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	virtual bool AllowInstanceCullingOcclusionQueries() const override { return true; }

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
 	virtual const FColorVertexBuffer* GetCustomHitProxyIdBuffer() const override
 	{
		return (bEnableBoneSelection || bUsesSubSections) ? &HitProxyIdBuffer : nullptr;
 	}
#endif

#if RHI_RAYTRACING
	bool IsRayTracingRelevant() const override { return true; }
	bool IsRayTracingStaticRelevant() const override { return false; }
	void GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector) override;
#endif

protected:
	/** Setup a geometry collection vertex factory. */
	void SetupVertexFactory(FRHICommandListBase& RHICmdList, FGeometryCollectionVertexFactory& GeometryCollectionVertexFactory, FColorVertexBuffer* ColorOverride = nullptr) const;
	/** Update skinned position buffer used by mobile CPU skinning path. */
	void UpdateSkinnedPositions(FRHICommandListBase& RHICmdList, TArray<FMatrix44f> const& Transforms);
	/** Get material proxy from material ID */
	FMaterialRenderProxy* GetMaterial(FMeshElementCollector& Collector, int32 MaterialIndex) const;
	/** Get the standard or debug vertex factory dependent on current state. */
	FVertexFactory const* GetVertexFactory() const;

	FGeometryCollectionTransformBuffer& GetCurrentTransformBuffer()
	{
		return TransformBuffers[CurrentTransformBufferIndex];
	}
	FGeometryCollectionTransformBuffer& GetCurrentPrevTransformBuffer()
	{
		const int32 NumBuffers = TransformBuffers.Num();
		const int32 PreviousIndex = (CurrentTransformBufferIndex + NumBuffers - 1) % NumBuffers;
		return TransformBuffers[PreviousIndex];
	}

	void CycleTransformBuffers(bool bCycle)
	{
		if (bCycle)
		{
			CurrentTransformBufferIndex = (CurrentTransformBufferIndex + 1) % TransformBuffers.Num();
		}
	}

#if RHI_RAYTRACING
	void UpdatingRayTracingGeometry_RenderingThread(TArray<FGeometryCollectionMeshElement> const& InSectionArray);
#endif

private:
	bool ShowCollisionMeshes(const FEngineShowFlags& EngineShowFlags) const;
};


class FNaniteGeometryCollectionSceneProxy : public Nanite::FSceneProxyBase
{
public:
	using Super = Nanite::FSceneProxyBase;
	
	FNaniteGeometryCollectionSceneProxy(UGeometryCollectionComponent* Component);
	virtual ~FNaniteGeometryCollectionSceneProxy();

public:
	// FPrimitiveSceneProxy interface.
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	virtual SIZE_T GetTypeHash() const override;
	virtual FPrimitiveViewRelevance	GetViewRelevance(const FSceneView* View) const override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
#endif
	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;

	virtual uint32 GetMemoryFootprint() const override;

	// FSceneProxyBase interface.
	virtual void GetNaniteResourceInfo(uint32& ResourceID, uint32& HierarchyOffset, uint32& ImposterIndex) const override;

	virtual Nanite::FResourceMeshInfo GetResourceMeshInfo() const override;

	/** Called on render thread to setup dynamic geometry for rendering */
	void SetDynamicData_RenderThread(FGeometryCollectionDynamicData* NewDynamicData, const FMatrix &PrimitiveLocalToWorld);

	void ResetPreviousTransforms_RenderThread();

	void FlushGPUSceneUpdate_GameThread();

	FORCEINLINE void SetRequiresGPUSceneUpdate_RenderThread(bool bRequireUpdate)
	{
		bRequiresGPUSceneUpdate = bRequireUpdate;
	}

	FORCEINLINE bool GetRequiresGPUSceneUpdate_RenderThread() const
	{
		return bRequiresGPUSceneUpdate;
	}

	inline virtual void GetLCIs(FLCIArray& LCIs) override
	{
		FLightCacheInterface* LCI = &EmptyLightCacheInfo;
		LCIs.Add(LCI);
	}

protected:
	// TODO : Copy required data from UObject instead of using unsafe object pointer.
	const UGeometryCollection* GeometryCollection = nullptr;
	FCollisionResponseContainer CollisionResponse;

	struct FGeometryNaniteData
	{
		FBoxSphereBounds LocalBounds;
		uint32 HierarchyOffset;
	};
	TArray<FGeometryNaniteData> GeometryNaniteData;

	uint32 NaniteResourceID = INDEX_NONE;
	uint32 NaniteHierarchyOffset = INDEX_NONE;

	// TODO: Should probably calculate this on the materials array above instead of on the component
	//       Null and !Opaque are assigned default material unlike the component material relevance.
	FMaterialRelevance MaterialRelevance;

	uint32 bCastShadow : 1;
	uint32 bReverseCulling : 1;
	uint32 bHasMaterialErrors : 1;
	uint32 bRequiresGPUSceneUpdate : 1;
	uint32 bEnableBoneSelection : 1;
	
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	TArray<TRefCountPtr<HHitProxy>> HitProxies;
#endif

	FInstanceSceneDataBuffers InstanceSceneDataBuffersImpl;

	FGeometryCollectionDynamicData* DynamicData = nullptr;

	// Geometry collection doesn't currently support baked light maps, so we use this simple empty light cache info for all nanite geometry collection proxies
	class FEmptyLightCacheInfo : public FLightCacheInterface
	{
	public:

		// FLightCacheInterface.
		GEOMETRYCOLLECTIONENGINE_API virtual FLightInteraction GetInteraction(const FLightSceneProxy* LightSceneProxy) const override;
	};

private:
	bool ShowCollisionMeshes(const FEngineShowFlags& EngineShowFlags) const;

private:
	static FEmptyLightCacheInfo EmptyLightCacheInfo;
};
