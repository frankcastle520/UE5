// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "RenderResource.h"
#include "RayTracingGeometry.h"
#include "ShaderParameters.h"
#include "Components/ExternalMorphSet.h"
#include "Components/SkinnedMeshComponent.h"
#include "GlobalShader.h"
#include "SkeletalRenderPublic.h"
#include "ClothingSystemRuntimeTypes.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Animation/MeshDeformerGeometry.h"
#include "NaniteSceneProxy.h"

class FPrimitiveDrawInterface;
class UMorphTarget;
class FRayTracingSkinnedGeometryUpdateQueue;
class FSkeletalMeshObjectNanite;

/** 
* Stores the updated matrices needed to skin the verts.
* Created by the game thread and sent to the rendering thread as an update 
*/
class FDynamicSkelMeshObjectDataNanite
{
public:
	ENGINE_API FDynamicSkelMeshObjectDataNanite(
		USkinnedMeshComponent* InComponent,
		FSkeletalMeshRenderData* InRenderData,
		int32 InLODIndex,
		EPreviousBoneTransformUpdateMode InPreviousBoneTransformUpdateMode,
		FSkeletalMeshObjectNanite* InMeshObject
	);

	ENGINE_API virtual ~FDynamicSkelMeshObjectDataNanite();

	// Current reference pose to local space transforms
	TArray<FMatrix44f> ReferenceToLocal;
	TArray<FMatrix44f> ReferenceToLocalForRayTracing;

	// Previous reference pose to local space transforms
	TArray<FMatrix44f> PrevReferenceToLocal;
	TArray<FMatrix44f> PrevReferenceToLocalForRayTracing;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) 
	// Component space bone transforms
	TArray<FTransform> ComponentSpaceTransforms;
#endif

	TArray<FMatrix3x4> CurrentBoneTransforms;
	TArray<FMatrix3x4> PreviousBoneTransforms;

	// Current LOD for bones being updated
	int32 LODIndex;
	int32 RayTracingLODIndex;

	// Returns the size of memory allocated by render data
	ENGINE_API void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);

private:

	enum class ETransformsToUpdate
	{
		Current,
		Previous,
	};
	
	void UpdateBonesRemovedByLOD(
		TArray<FMatrix44f>& PoseBuffer,
		USkinnedMeshComponent* InComponent,
		ETransformsToUpdate TransformsToUpdate) const;
};

class FSkeletalMeshObjectNanite : public FSkeletalMeshObject
{
public:
	ENGINE_API FSkeletalMeshObjectNanite(USkinnedMeshComponent* InComponent, FSkeletalMeshRenderData* InRenderData, ERHIFeatureLevel::Type InFeatureLevel);
	ENGINE_API virtual ~FSkeletalMeshObjectNanite();

	ENGINE_API virtual void InitResources(USkinnedMeshComponent* InComponent) override;
	ENGINE_API virtual void ReleaseResources() override;
	
	virtual void Update(
		int32 LODIndex,
		USkinnedMeshComponent* InComponent,
		const FMorphTargetWeightMap& InActiveMorphTargets,
		const TArray<float>& MorphTargetWeights,
		EPreviousBoneTransformUpdateMode PreviousBoneTransformUpdateMode,
		const FExternalMorphWeightData& InExternalMorphWeightData) override;

	ENGINE_API void UpdateDynamicData_RenderThread(
		FRHICommandList& RHICmdList,
		FDynamicSkelMeshObjectDataNanite* InDynamicData,
		uint64 FrameNumberToPrepare,
		uint32 RevisionNumber,
		uint32 PreviousRevisionNumber,
		FGPUSkinCache* GPUSkinCache,
		int32 LODIndex,
		bool bRecreating
	);

	ENGINE_API virtual const FVertexFactory* GetSkinVertexFactory(const FSceneView* View, int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode = ESkinVertexFactoryMode::Default) const override;
	ENGINE_API virtual const FVertexFactory* GetStaticSkinVertexFactory(int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode = ESkinVertexFactoryMode::Default) const override;
	ENGINE_API virtual TArray<FTransform>* GetComponentSpaceTransforms() const override;
	ENGINE_API virtual const TArray<FMatrix44f>& GetReferenceToLocalMatrices() const override;
	ENGINE_API virtual const TArray<FMatrix44f>& GetPrevReferenceToLocalMatrices() const override;
	ENGINE_API virtual const TArray<FMatrix3x4>* GetCurrentBoneTransforms() const override;
	ENGINE_API virtual const TArray<FMatrix3x4>* GetPreviousBoneTransforms() const override;

	virtual int32 GetLOD() const override;

	virtual bool HaveValidDynamicData() const override;

	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;

	virtual void UpdateSkinWeightBuffer(USkinnedMeshComponent* InMeshComponent) override;

	virtual bool IsNaniteMesh() const override { return true; }

	virtual FSkinWeightVertexBuffer* GetSkinWeightVertexBuffer(int32 LODIndex) const;

#if RHI_RAYTRACING	
	FRayTracingGeometry RayTracingGeometry;
	FRayTracingSkinnedGeometryUpdateQueue* RayTracingUpdateQueue = nullptr;

	virtual void UpdateRayTracingGeometry(FRHICommandListBase& RHICmdList, FSkeletalMeshLODRenderData& LODModel, uint32 LODIndex, TArray<FBufferRHIRef>& VertexBuffers) override;

	virtual void QueuePendingRayTracingGeometryUpdate(FRHICommandListBase& RHICmdList) override;

	virtual FRayTracingGeometry* GetRayTracingGeometry() { check(!bRayTracingGeometryRequiresUpdate);  return &RayTracingGeometry; }
	virtual const FRayTracingGeometry* GetRayTracingGeometry() const { check(!bRayTracingGeometryRequiresUpdate);  return &RayTracingGeometry; }

	virtual int32 GetRayTracingLOD() const override
	{
		if (DynamicData)
		{
			return DynamicData->RayTracingLODIndex;
		}
		else
		{
			return 0;
		}
	}
#endif

	inline bool HasValidMaterials() const
	{
		return bHasValidMaterials;
	}

	inline const Nanite::FMaterialAudit& GetMaterials() const
	{
		return NaniteMaterials;
	}

private:
	FDynamicSkelMeshObjectDataNanite* DynamicData = nullptr;

	struct FSkeletalMeshObjectLOD
	{
		FSkeletalMeshRenderData* RenderData;
		int32 LODIndex;
		bool bInitialized;
		
		// Needed for skin cache update for ray tracing
		TArray<TUniquePtr<FGPUBaseSkinVertexFactory>> VertexFactories;
		TArray<TUniquePtr<FGPUSkinPassthroughVertexFactory>> PassthroughVertexFactories;

		FSkinWeightVertexBuffer* MeshObjectWeightBuffer = nullptr;

		FSkeletalMeshObjectLOD(ERHIFeatureLevel::Type InFeatureLevel, FSkeletalMeshRenderData* InRenderData, int32 InLOD)
		: RenderData(InRenderData)
		, LODIndex(InLOD)
		, bInitialized(false)
		{
		}

		void InitResources(FSkelMeshComponentLODInfo* LODInfo, ERHIFeatureLevel::Type FeatureLevel);
		void ReleaseResources();
		void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);
		void UpdateSkinWeights(FSkelMeshComponentLODInfo* LODInfo);
	};

	TArray<FSkeletalMeshObjectLOD> LODs;

	Nanite::FMaterialAudit NaniteMaterials;
	bool bHasValidMaterials = false;

	mutable int32 CachedLOD;
};