// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalRenderNanite.h"
#include "Animation/MeshDeformerInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "RenderUtils.h"
#include "SkeletalRender.h"
#include "GPUSkinCache.h"
#include "RayTracingSkinnedGeometry.h"
#include "Rendering/RenderCommandPipes.h"
#include "ShaderParameterUtils.h"
#include "SceneInterface.h"
#include "SkeletalMeshSceneProxy.h"
#include "RenderGraphUtils.h"
#include "RenderCore.h"
#include "Engine/SkinnedAssetCommon.h"
#include "SkeletalRenderGPUSkin.h"

FDynamicSkelMeshObjectDataNanite::FDynamicSkelMeshObjectDataNanite(
	USkinnedMeshComponent* InComponent,
	FSkeletalMeshRenderData* InRenderData,
	int32 InLODIndex,
	EPreviousBoneTransformUpdateMode InPreviousBoneTransformUpdateMode,
	FSkeletalMeshObjectNanite* InMeshObject
)
:	LODIndex(InLODIndex)
{
#if RHI_RAYTRACING
	RayTracingLODIndex = FMath::Clamp(FMath::Max(LODIndex, InMeshObject->RayTracingMinLOD), LODIndex, InRenderData->LODRenderData.Num() - 1);
#endif

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ComponentSpaceTransforms = InComponent->GetComponentSpaceTransforms();

	const bool bCalculateComponentSpaceTransformsFromLeader = ComponentSpaceTransforms.IsEmpty(); // This will be empty for follower components.
	TArray<FTransform>* const LeaderBoneMappedMeshComponentSpaceTransforms = bCalculateComponentSpaceTransformsFromLeader ? &ComponentSpaceTransforms : nullptr;
#else
	TArray<FTransform>* const LeaderBoneMappedMeshComponentSpaceTransforms = nullptr;
#endif

	UpdateRefToLocalMatrices(ReferenceToLocal, InComponent, InRenderData, LODIndex, nullptr, LeaderBoneMappedMeshComponentSpaceTransforms);
#if RHI_RAYTRACING
	if (RayTracingLODIndex != LODIndex)
	{
		UpdateRefToLocalMatrices(ReferenceToLocalForRayTracing, InComponent, InRenderData, RayTracingLODIndex, nullptr);
	}
#endif
	UpdateBonesRemovedByLOD(ReferenceToLocal, InComponent, ETransformsToUpdate::Current);

	CurrentBoneTransforms.SetNumUninitialized(ReferenceToLocal.Num());

	const int64 ReferenceToLocalCount = int64(ReferenceToLocal.Num());
	const FMatrix44f* ReferenceToLocalPtr = ReferenceToLocal.GetData();
	FMatrix3x4* CurrentBoneTransformsPtr = CurrentBoneTransforms.GetData();

	TransposeTransforms(CurrentBoneTransformsPtr, ReferenceToLocalPtr, ReferenceToLocalCount);

	bool bUpdatePrevious = false;

	switch (InPreviousBoneTransformUpdateMode)
	{
	case EPreviousBoneTransformUpdateMode::None:
		// Use previously uploaded buffer
		// TODO: Nanite-Skinning, optimize scene extension upload to keep cached GPU representation using PreviousBoneTransformRevisionNumber
		// For now we'll just redundantly update and upload previous transforms
		UpdatePreviousRefToLocalMatrices(PrevReferenceToLocal, InComponent, InRenderData, LODIndex);
#if RHI_RAYTRACING
		if (RayTracingLODIndex != LODIndex)
		{
			UpdatePreviousRefToLocalMatrices(PrevReferenceToLocalForRayTracing, InComponent, InRenderData, RayTracingLODIndex);
		}
#endif
		UpdateBonesRemovedByLOD(PrevReferenceToLocal, InComponent, ETransformsToUpdate::Previous);
		bUpdatePrevious = true;
		break;

	case EPreviousBoneTransformUpdateMode::UpdatePrevious:
		UpdatePreviousRefToLocalMatrices(PrevReferenceToLocal, InComponent, InRenderData, LODIndex);
#if RHI_RAYTRACING
		if (RayTracingLODIndex != LODIndex)
		{
			UpdatePreviousRefToLocalMatrices(PrevReferenceToLocalForRayTracing, InComponent, InRenderData, RayTracingLODIndex);
		}
#endif
		UpdateBonesRemovedByLOD(PrevReferenceToLocal, InComponent, ETransformsToUpdate::Previous);
		bUpdatePrevious = true;
		break;

	case EPreviousBoneTransformUpdateMode::DuplicateCurrentToPrevious:
		// TODO: Nanite-Skinning likely possible we can just return ReferenceToLocal here rather than cloning it into previous
		// Need to make sure it's safe when next update mode = None
		PrevReferenceToLocal = ReferenceToLocal;
#if RHI_RAYTRACING
		if (RayTracingLODIndex != LODIndex)
		{
			PrevReferenceToLocalForRayTracing = ReferenceToLocalForRayTracing;
		}
#endif
		PreviousBoneTransforms = CurrentBoneTransforms;
		break;
	}

	if (bUpdatePrevious)
	{
		PreviousBoneTransforms.SetNumUninitialized(PrevReferenceToLocal.Num());
		const FMatrix44f* PrevReferenceToLocalPtr = PrevReferenceToLocal.GetData();

		const int64 PrevReferenceToLocalCount = int64(PrevReferenceToLocal.Num());
		FMatrix3x4* PreviousBoneTransformsPtr = PreviousBoneTransforms.GetData();

		TransposeTransforms(PreviousBoneTransformsPtr, PrevReferenceToLocalPtr, PrevReferenceToLocalCount);
	}
}

FDynamicSkelMeshObjectDataNanite::~FDynamicSkelMeshObjectDataNanite() = default;

void FDynamicSkelMeshObjectDataNanite::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ReferenceToLocal.GetAllocatedSize());
}

void FDynamicSkelMeshObjectDataNanite::UpdateBonesRemovedByLOD(
	TArray<FMatrix44f>& PoseBuffer,
	USkinnedMeshComponent* InComponent,
	ETransformsToUpdate CurrentOrPrevious) const
{
	// Why is this necessary?
	//
	// When the animation system removes bones at higher LODs, the pose in USkinnedMeshComponent::GetComponentSpaceTransforms()
	// will leave the LOD'd bone transforms at their last updated position/rotation. This is not a problem for GPU skinning
	// because the actual weight for those bones is pushed up the hierarchy onto the next non-LOD'd parent; making the transform irrelevant.
	//
	// But Nanite skinning only ever uses the LOD-0 weights (it dynamically interpolates weights for higher-LOD clusters)
	// This means that these "frozen" bone transforms actually affect the skin. Which is bad.
	//
	// So we do an FK update here of the frozen branch of transforms...

	const USkinnedAsset* SkinnedAsset = InComponent->GetSkinnedAsset();
	const TArray<FBoneReference>& BonesToRemove = SkinnedAsset->GetLODInfo(LODIndex)->BonesToRemove;
	if (BonesToRemove.IsEmpty())
	{
		return; // no bones removed in this LOD
	}
	
	// get current OR previous component space pose (possibly from a leader component)
	// any LOD'd out bones in this pose are "frozen" since their last update
	const TArray<FTransform>& ComponentSpacePose = [InComponent, CurrentOrPrevious, SkinnedAsset]
	{
		const USkinnedMeshComponent* const LeaderComp = InComponent->LeaderPoseComponent.Get();
		const bool bIsLeaderCompValid = LeaderComp && InComponent->GetLeaderBoneMap().Num() == SkinnedAsset->GetRefSkeleton().GetNum();
		switch (CurrentOrPrevious)
		{
		case ETransformsToUpdate::Current:
			return bIsLeaderCompValid ? LeaderComp->GetComponentSpaceTransforms() : InComponent->GetComponentSpaceTransforms();
		case ETransformsToUpdate::Previous:
			return bIsLeaderCompValid ? LeaderComp->GetPreviousComponentTransformsArray() : InComponent->GetPreviousComponentTransformsArray();
		default:
			checkNoEntry();
			return TArray<FTransform>();
		}
	}();
	
	// these are inverted ref pose matrices
	const TArray<FMatrix44f>* RefBasesInvMatrix = &SkinnedAsset->GetRefBasesInvMatrix();
	TArray<int32> AllChildrenBones;
	const FReferenceSkeleton& RefSkeleton = SkinnedAsset->GetRefSkeleton();
	for (const FBoneReference& RemovedBone : BonesToRemove)
	{
		AllChildrenBones.Reset();
		// can't use FBoneReference::GetMeshPoseIndex() because rendering operates at lower-level (on USkinnedMeshComponent)
		// but this call to FindBoneIndex is probably not so bad since there's typically only the parent bone of a branch in "BonesToRemove"
		const FBoneIndexType BoneIndex = RefSkeleton.FindBoneIndex(RemovedBone.BoneName);
		AllChildrenBones.Add(BoneIndex);
		RefSkeleton.GetRawChildrenIndicesRecursiveCached(BoneIndex, AllChildrenBones);

		// first pass to generate component space transforms
		for (int32 ChildIndex = 0; ChildIndex<AllChildrenBones.Num(); ++ChildIndex)
		{
			const FBoneIndexType ChildBoneIndex = AllChildrenBones[ChildIndex];
			const FBoneIndexType ParentIndex = RefSkeleton.GetParentIndex(ChildBoneIndex);

			FMatrix44f ParentComponentTransform;
			if (ParentIndex == INDEX_NONE)
			{
				ParentComponentTransform = FMatrix44f::Identity; // root bone transform is always component space
			}
			else if (ChildIndex == 0)
			{
				ParentComponentTransform = static_cast<FMatrix44f>(ComponentSpacePose[ParentIndex].ToMatrixWithScale());
			}
			else
			{
				ParentComponentTransform = PoseBuffer[ParentIndex];
			}

			const FMatrix44f RefLocalTransform = static_cast<FMatrix44f>(RefSkeleton.GetRefBonePose()[ChildBoneIndex].ToMatrixWithScale());
			PoseBuffer[ChildBoneIndex] = RefLocalTransform * ParentComponentTransform;
		}

		// second pass to make relative to ref pose
		for (const FBoneIndexType ChildBoneIndex : AllChildrenBones)
		{
			PoseBuffer[ChildBoneIndex] = (*RefBasesInvMatrix)[ChildBoneIndex] * PoseBuffer[ChildBoneIndex];
		}
	}
}

FSkeletalMeshObjectNanite::FSkeletalMeshObjectNanite(USkinnedMeshComponent* InComponent, FSkeletalMeshRenderData* InRenderData, ERHIFeatureLevel::Type InFeatureLevel)
: FSkeletalMeshObject(InComponent, InRenderData, InFeatureLevel)
, DynamicData(nullptr)
, CachedLOD(INDEX_NONE)
{
#if RHI_RAYTRACING
	FSkeletalMeshObjectNanite* PreviousMeshObject = nullptr;
	if (InComponent->PreviousMeshObject && InComponent->PreviousMeshObject->IsNaniteMesh())
	{
		PreviousMeshObject = (FSkeletalMeshObjectNanite*)InComponent->PreviousMeshObject;

		// Don't use re-create data if the mesh or feature level changed
		if (PreviousMeshObject->SkeletalMeshRenderData != InRenderData || PreviousMeshObject->FeatureLevel != InFeatureLevel)
		{
			PreviousMeshObject = nullptr;
		}
	}

	if (PreviousMeshObject)
	{
		// Transfer GPU skin cache from PreviousMeshObject -- needs to happen on render thread.  PreviousMeshObject is defer deleted, so it's safe to access it there.
		ENQUEUE_RENDER_COMMAND(ReleaseSkeletalMeshSkinCacheResources)(UE::RenderCommandPipe::SkeletalMesh,
			[this, PreviousMeshObject](FRHICommandList& RHICmdList)
			{
				SkinCacheEntryForRayTracing = PreviousMeshObject->SkinCacheEntryForRayTracing;

				// patch entries to point to new GPUSkin
				FGPUSkinCache::SetEntryGPUSkin(SkinCacheEntryForRayTracing, this);

				PreviousMeshObject->SkinCacheEntryForRayTracing = nullptr;
			}
		);
	}

	RayTracingUpdateQueue = InComponent->GetScene()->GetRayTracingSkinnedGeometryUpdateQueue();
#endif

	for (int32 LODIndex = 0; LODIndex < InRenderData->LODRenderData.Num(); ++LODIndex)
	{
		new(LODs) FSkeletalMeshObjectLOD(InFeatureLevel, InRenderData, LODIndex);
	}

	InitResources(InComponent);

	AuditMaterials(InComponent, NaniteMaterials, true /* Set material usage flags */);

	const bool bIsMaskingAllowed = Nanite::IsMaskingAllowed(InComponent->GetWorld(), false /* force Nanite for masked */);
	bHasValidMaterials = NaniteMaterials.IsValid(bIsMaskingAllowed);
}

FSkeletalMeshObjectNanite::~FSkeletalMeshObjectNanite()
{
	delete DynamicData;
}

void FSkeletalMeshObjectNanite::InitResources(USkinnedMeshComponent* InComponent)
{
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); ++LODIndex)
	{
		FSkeletalMeshObjectLOD& LOD = LODs[LODIndex];

		// Skip LODs that have their render data stripped
		if (LOD.RenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			FSkelMeshComponentLODInfo* InitLODInfo = nullptr;
			if (InComponent->LODInfo.IsValidIndex(LODIndex))
			{
				InitLODInfo = &InComponent->LODInfo[LODIndex];
			}

			LOD.InitResources(InitLODInfo, FeatureLevel);
		}
	}

#if RHI_RAYTRACING
	if (IsRayTracingAllowed() && bSupportRayTracing)
	{
		BeginInitResource(&RayTracingGeometry, &UE::RenderCommandPipe::SkeletalMesh);
	}	
#endif
}

void FSkeletalMeshObjectNanite::ReleaseResources()
{
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); ++LODIndex)
	{
		FSkeletalMeshObjectLOD& LOD = LODs[LODIndex];
		LOD.ReleaseResources();
	}

#if RHI_RAYTRACING
	BeginReleaseResource(&RayTracingGeometry, &UE::RenderCommandPipe::SkeletalMesh);

	FSkeletalMeshObjectNanite* MeshObject = this;
	FGPUSkinCacheEntry** PtrSkinCacheEntry = &SkinCacheEntryForRayTracing;
	ENQUEUE_RENDER_COMMAND(ReleaseSkeletalMeshSkinCacheResources)(UE::RenderCommandPipe::SkeletalMesh,
		[MeshObject, PtrSkinCacheEntry](FRHICommandList& RHICmdList)
		{
			FGPUSkinCacheEntry*& LocalSkinCacheEntry = *PtrSkinCacheEntry;
			FGPUSkinCache::Release(LocalSkinCacheEntry);

			*PtrSkinCacheEntry = nullptr;
		}
	);

	if (RayTracingUpdateQueue != nullptr)
	{
		ENQUEUE_RENDER_COMMAND(ReleaseRayTracingDynamicVertexBuffer)(UE::RenderCommandPipe::SkeletalMesh,
			[RayTracingUpdateQueue = RayTracingUpdateQueue, RayTracingGeometryPtr = &RayTracingGeometry](FRHICommandList& RHICmdList) mutable
			{
				if (RayTracingUpdateQueue != nullptr)
				{
					RayTracingUpdateQueue->Remove(RayTracingGeometryPtr);
				}
			});
	}
#endif
}

void FSkeletalMeshObjectNanite::Update(
	int32 LODIndex,
	USkinnedMeshComponent* InComponent,
	const FMorphTargetWeightMap& InActiveMorphTargets,
	const TArray<float>& MorphTargetWeights,
	EPreviousBoneTransformUpdateMode PreviousBoneTransformUpdateMode,
	const FExternalMorphWeightData& InExternalMorphWeightData)
{
	if (InComponent)
	{
		// Create the new dynamic data for use by the rendering thread
		// this data is only deleted when another update is sent
		FDynamicSkelMeshObjectDataNanite* NewDynamicData = new FDynamicSkelMeshObjectDataNanite(InComponent, SkeletalMeshRenderData, LODIndex, PreviousBoneTransformUpdateMode, this);

		uint64 FrameNumberToPrepare = GFrameCounter;
		uint32 RevisionNumber = 0;
		uint32 PreviousRevisionNumber = 0;

		if (InComponent->SceneProxy)
		{
			RevisionNumber = InComponent->GetBoneTransformRevisionNumber();
			PreviousRevisionNumber = InComponent->GetPreviousBoneTransformRevisionNumber();
		}

		// Queue a call to update this data
		{

			FGPUSkinCache* GPUSkinCache = nullptr;
			if (InComponent && InComponent->GetScene())
			{
				FSceneInterface* Scene = InComponent->GetScene();
				GPUSkinCache = Scene->GetGPUSkinCache();
			}			
			
			const bool bRecreating = InComponent->IsRenderStateRecreating();
			FSkeletalMeshObjectNanite* MeshObject = this;
			ENQUEUE_RENDER_COMMAND(SkelMeshObjectUpdateDataCommand)(UE::RenderCommandPipe::SkeletalMesh,
				[MeshObject, FrameNumberToPrepare, RevisionNumber, PreviousRevisionNumber, NewDynamicData, GPUSkinCache, LODIndex, bRecreating](FRHICommandList& RHICmdList)
				{
					FScopeCycleCounter Context(MeshObject->GetStatId());
					MeshObject->UpdateDynamicData_RenderThread(RHICmdList, NewDynamicData, FrameNumberToPrepare, RevisionNumber, PreviousRevisionNumber, GPUSkinCache, LODIndex, bRecreating);
				}
			);
		}
	}
}

void FSkeletalMeshObjectNanite::UpdateDynamicData_RenderThread(FRHICommandList& RHICmdList, FDynamicSkelMeshObjectDataNanite* InDynamicData, uint64 FrameNumberToPrepare, uint32 RevisionNumber, uint32 PreviousRevisionNumber, FGPUSkinCache* GPUSkinCache, int32 LODIndex, bool bRecreating)
{
	// We should be done with the old data at this point
	delete DynamicData;

	// Update with new data
	DynamicData = InDynamicData;
	check(DynamicData);

	check(IsInParallelRenderingThread());

#if RHI_RAYTRACING	
	const bool bGPUSkinCacheEnabled = FGPUSkinCache::IsGPUSkinCacheRayTracingSupported() && GPUSkinCache && GEnableGPUSkinCache && IsRayTracingEnabled();

	if (bGPUSkinCacheEnabled && SkeletalMeshRenderData->bSupportRayTracing)
	{
		const bool bShouldUseSeparateMatricesForRayTracing = DynamicData->RayTracingLODIndex != DynamicData->LODIndex;

		const int32 RayTracingLODIndex = DynamicData->RayTracingLODIndex;
		FSkeletalMeshObjectLOD& LOD = LODs[RayTracingLODIndex];

		const FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData->LODRenderData[RayTracingLODIndex];
		const TArray<FSkelMeshRenderSection>& Sections = GetRenderSections(RayTracingLODIndex);
		const FName OwnerName = GetAssetPathName(RayTracingLODIndex);

		for (int32 SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
		{
			FGPUBaseSkinVertexFactory* VertexFactory = LOD.VertexFactories[SectionIdx].Get();
			FGPUBaseSkinVertexFactory::FShaderDataType& ShaderData = VertexFactory->GetShaderData();

			const FSkelMeshRenderSection& Section = Sections[SectionIdx];			

			if (DynamicData->PrevReferenceToLocal.Num() > 0)
			{
				TArray<FMatrix44f>& PreviousReferenceToLocalMatrices = bShouldUseSeparateMatricesForRayTracing ? DynamicData->PrevReferenceToLocalForRayTracing : DynamicData->PrevReferenceToLocal;
				ShaderData.UpdateBoneData(RHICmdList, PreviousReferenceToLocalMatrices, Section.BoneMap, PreviousRevisionNumber, FeatureLevel, OwnerName);
			}

			// Create a uniform buffer from the bone transforms.
			{
				TArray<FMatrix44f>& ReferenceToLocalMatrices = bShouldUseSeparateMatricesForRayTracing ? DynamicData->ReferenceToLocalForRayTracing : DynamicData->ReferenceToLocal;
				ShaderData.UpdateBoneData(RHICmdList, ReferenceToLocalMatrices, Section.BoneMap, RevisionNumber, FeatureLevel, OwnerName);
				ShaderData.UpdatedFrameNumber = FrameNumberToPrepare;
			}

			bool bSectionUsingSkinCache = Section.MaxBoneInfluences != 0;

			if (bSectionUsingSkinCache)
			{
				bSectionUsingSkinCache = GPUSkinCache->ProcessEntry(
					EGPUSkinCacheEntryMode::RayTracing,
					RHICmdList,
					LOD.VertexFactories[SectionIdx].Get(),
					LOD.PassthroughVertexFactories[SectionIdx].Get(),
					Section,
					this,
					nullptr, // MorphVertexBuffer,
					nullptr, // ClothSimulationData != nullptr ? &LODData.ClothVertexBuffer : 0,
					nullptr, // ClothSimulationData,
					FMatrix44f::Identity, // ClothToLocal,
					0.0f, // DynamicData->ClothBlendWeight,
					(FVector3f)FVector::OneVector, // (FVector3f)WorldScale,
					RevisionNumber,
					SectionIdx,
					RayTracingLODIndex,
					bRecreating,
					SkinCacheEntryForRayTracing);
			}			
		}
	}
#endif
}

const FVertexFactory* FSkeletalMeshObjectNanite::GetSkinVertexFactory(const FSceneView* View, int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode) const
{
	check(LODs.IsValidIndex(LODIndex));

	if (VFMode == ESkinVertexFactoryMode::RayTracing)
	{
		return LODs[LODIndex].PassthroughVertexFactories[ChunkIdx].Get();
	}

	return LODs[LODIndex].VertexFactories[ChunkIdx].Get();
}

const FVertexFactory* FSkeletalMeshObjectNanite::GetStaticSkinVertexFactory(int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode) const
{
	check(LODs.IsValidIndex(LODIndex));

	if (VFMode == ESkinVertexFactoryMode::RayTracing)
	{
		return LODs[LODIndex].PassthroughVertexFactories[ChunkIdx].Get();
	}

	return LODs[LODIndex].VertexFactories[ChunkIdx].Get();
}

TArray<FTransform>* FSkeletalMeshObjectNanite::GetComponentSpaceTransforms() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (DynamicData)
	{
		return &(DynamicData->ComponentSpaceTransforms);
	}
	else
#endif
	{
		return nullptr;
	}
}

const TArray<FMatrix44f>& FSkeletalMeshObjectNanite::GetReferenceToLocalMatrices() const
{
	return DynamicData->ReferenceToLocal;
}

const TArray<FMatrix44f>& FSkeletalMeshObjectNanite::GetPrevReferenceToLocalMatrices() const
{
	return DynamicData->PrevReferenceToLocal;
}

const TArray<FMatrix3x4>* FSkeletalMeshObjectNanite::GetCurrentBoneTransforms() const
{
	return &DynamicData->CurrentBoneTransforms;
}

const TArray<FMatrix3x4>* FSkeletalMeshObjectNanite::GetPreviousBoneTransforms() const
{
	return &DynamicData->PreviousBoneTransforms;
}

int32 FSkeletalMeshObjectNanite::GetLOD() const
{
	// WorkingMinDesiredLODLevel can be a LOD that's not loaded, so need to clamp it to the first loaded LOD
	return FMath::Max<int32>(WorkingMinDesiredLODLevel, SkeletalMeshRenderData->CurrentFirstLODIdx);
	/*if (DynamicData)
	{
		return DynamicData->LODIndex;
	}
	else
	{
		return 0;
	}*/
}

bool FSkeletalMeshObjectNanite::HaveValidDynamicData() const
{
	return (DynamicData != nullptr);
}

void FSkeletalMeshObjectNanite::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));

	if (DynamicData)
	{
		DynamicData->GetResourceSizeEx(CumulativeResourceSize);
	}

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(LODs.GetAllocatedSize());

	for (int32 Index = 0; Index < LODs.Num(); ++Index)
	{
		LODs[Index].GetResourceSizeEx(CumulativeResourceSize);
	}
}

void FSkeletalMeshObjectNanite::UpdateSkinWeightBuffer(USkinnedMeshComponent* InComponent)
{
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); ++LODIndex)
	{
		FSkeletalMeshObjectLOD& LOD = LODs[LODIndex];

		// Skip LODs that have their render data stripped
		if (InComponent && LOD.RenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			FSkelMeshComponentLODInfo* UpdateLODInfo = nullptr;
			if (InComponent->LODInfo.IsValidIndex(LODIndex))
			{
				UpdateLODInfo = &InComponent->LODInfo[LODIndex];
			}

			LOD.UpdateSkinWeights(UpdateLODInfo);

			if (InComponent && InComponent->SceneProxy)
			{
				if (SkinCacheEntryForRayTracing)
				{
					ENQUEUE_RENDER_COMMAND(UpdateSkinCacheSkinWeightBuffer)(UE::RenderCommandPipe::SkeletalMesh,
						[SkinCacheEntryForRayTracing = SkinCacheEntryForRayTracing](FRHICommandList& RHICmdList)
						{
							FGPUSkinCache::UpdateSkinWeightBuffer(SkinCacheEntryForRayTracing);
						});
				}
			}
		}
	}
}

void FSkeletalMeshObjectNanite::FSkeletalMeshObjectLOD::InitResources(FSkelMeshComponentLODInfo* InLODInfo, ERHIFeatureLevel::Type InFeatureLevel)
{
	check(RenderData);
	check(RenderData->LODRenderData.IsValidIndex(LODIndex));

	FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];

	// Init vertex factories for ray tracing entry in skin cache
	if (IsRayTracingAllowed())
	{
		MeshObjectWeightBuffer = FSkeletalMeshObject::GetSkinWeightVertexBuffer(LODData, InLODInfo);

		FSkeletalMeshObjectGPUSkin::FVertexFactoryBuffers VertexBuffers;
		VertexBuffers.StaticVertexBuffers = &LODData.StaticVertexBuffers;
		VertexBuffers.ColorVertexBuffer = FSkeletalMeshObject::GetColorVertexBuffer(LODData, InLODInfo);
		VertexBuffers.SkinWeightVertexBuffer = MeshObjectWeightBuffer;
		VertexBuffers.MorphVertexBufferPool = nullptr; // MorphVertexBufferPool;
		VertexBuffers.APEXClothVertexBuffer = &LODData.ClothVertexBuffer;
		VertexBuffers.NumVertices = LODData.GetNumVertices();

		const bool bUsedForPassthroughVertexFactory = true;
		FGPUSkinPassthroughVertexFactory::EVertexAttributeFlags VertexAttributeMask = FGPUSkinPassthroughVertexFactory::EVertexAttributeFlags::Position | FGPUSkinPassthroughVertexFactory::EVertexAttributeFlags::Tangent;

		VertexFactories.Empty(LODData.RenderSections.Num());
		PassthroughVertexFactories.Empty(LODData.RenderSections.Num());

		for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
		{
			FSkeletalMeshObjectGPUSkin::CreateVertexFactory(VertexFactories,
				&PassthroughVertexFactories,
				VertexBuffers,
				InFeatureLevel,
				VertexAttributeMask,
				Section.BaseVertexIndex,
				bUsedForPassthroughVertexFactory);
		}
	}

	bInitialized = true;
}

void FSkeletalMeshObjectNanite::FSkeletalMeshObjectLOD::ReleaseResources()
{
	bInitialized = false;

	for (int32 FactoryIdx = 0; FactoryIdx < VertexFactories.Num(); FactoryIdx++)
	{
		BeginReleaseResource(VertexFactories[FactoryIdx].Get(), &UE::RenderCommandPipe::SkeletalMesh);
	}

	for (int32 FactoryIdx = 0; FactoryIdx < PassthroughVertexFactories.Num(); FactoryIdx++)
	{
		BeginReleaseResource(PassthroughVertexFactories[FactoryIdx].Get(), &UE::RenderCommandPipe::SkeletalMesh);
	}
}

#if RHI_RAYTRACING
void FSkeletalMeshObjectNanite::UpdateRayTracingGeometry(FRHICommandListBase& RHICmdList, FSkeletalMeshLODRenderData& LODModel, uint32 LODIndex, TArray<FBufferRHIRef>& VertexBuffers)
{
	// TODO: Support WPO
	const bool bAnySegmentUsesWorldPositionOffset = false;

	FSkeletalMeshObjectGPUSkin::UpdateRayTracingGeometry_Internal(LODModel, LODIndex, VertexBuffers, RayTracingGeometry, bAnySegmentUsesWorldPositionOffset, this, RayTracingUpdateQueue);
}

void FSkeletalMeshObjectNanite::QueuePendingRayTracingGeometryUpdate(FRHICommandListBase& RHICmdList)
{
	if (IsRayTracingEnabled() && bSupportRayTracing)
	{
		// TODO: Support WPO
		//const bool bAnySegmentUsesWorldPositionOffset = false;

		if (!RayTracingGeometry.IsValid() || RayTracingGeometry.IsEvicted())
		{
			// Only create RHI object but enqueue actual BLAS creation so they can be accumulated
			RayTracingGeometry.CreateRayTracingGeometry(RHICmdList, ERTAccelerationStructureBuildPriority::Skip);

			bRayTracingGeometryRequiresUpdate = /*!bAnySegmentUsesWorldPositionOffset &&*/ RayTracingGeometry.IsValid();
		}

		if (bRayTracingGeometryRequiresUpdate)
		{
			RayTracingUpdateQueue->Add(&RayTracingGeometry, RHICalcRayTracingGeometrySize(RayTracingGeometry.Initializer));
			bRayTracingGeometryRequiresUpdate = false;
		}
	}
}
#endif

FSkinWeightVertexBuffer* FSkeletalMeshObjectNanite::GetSkinWeightVertexBuffer(int32 LODIndex) const
{
	checkSlow(LODs.IsValidIndex(LODIndex));
	return LODs[LODIndex].MeshObjectWeightBuffer;
}

void FSkeletalMeshObjectNanite::FSkeletalMeshObjectLOD::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
}

void FSkeletalMeshObjectNanite::FSkeletalMeshObjectLOD::UpdateSkinWeights(FSkelMeshComponentLODInfo* InLODInfo)
{
	check(RenderData);
	check(RenderData->LODRenderData.IsValidIndex(LODIndex));

	FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
	MeshObjectWeightBuffer = FSkeletalMeshObject::GetSkinWeightVertexBuffer(LODData, InLODInfo);
}