// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigLogicInstanceData.h"
#include "RigLogicAnimNext.h"

#include "Engine/SkeletalMesh.h"
#include "DNAAsset.h"
#include "LODPose.h"
#include "SharedRigRuntimeContext.h"

namespace UE::AnimNext
{
	void FRigLogicAnimNextInstanceData::Init(USkeletalMesh* SkeletalMesh)
	{
		const USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
		if (!Skeleton)
		{
			UE_LOG(LogRigLogicAnimNext, Warning, TEXT("No skeleton assigned to the skeletal mesh."));
			return;
		}

		UDNAAsset* DNAAsset = Cast<UDNAAsset>(SkeletalMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass()));
		if (!DNAAsset)
		{
			UE_LOG(LogRigLogicAnimNext, Warning, TEXT("No DNA asset assigned to the skeletal mesh."));
			return;
		}

		TSharedPtr<FSharedRigRuntimeContext> SharedRigRuntimeContext = DNAAsset->GetRigRuntimeContext();
		if (!SharedRigRuntimeContext.IsValid())
		{
			UE_LOG(LogRigLogicAnimNext, Warning, TEXT("Can't get the shared rig runtime context."));
			return;
		}

		if (CachedRigRuntimeContext != SharedRigRuntimeContext)
		{
			CachedRigRuntimeContext = SharedRigRuntimeContext;
			RigInstance = TUniquePtr<FRigInstance>(new FRigInstance(CachedRigRuntimeContext->RigLogic.Get()));
		}

		CachedDNAIndexMapping = DNAAsset->GetDNAIndexMapping(Skeleton, SkeletalMesh);

		InitBoneIndexMapping();
		InitSparseAndDenseDriverJointMapping();
	}

	void FRigLogicAnimNextInstanceData::InitBoneIndexMapping()
	{
		const uint32 NumLODs = CachedRigRuntimeContext->VariableJointIndicesPerLOD.Num();
		RigLogicToSkeletonBoneIndexMappingPerLOD.Empty();
		RigLogicToSkeletonBoneIndexMappingPerLOD.SetNum(NumLODs);

		for (uint32 LODLevel = 0; LODLevel < NumLODs; ++LODLevel)
		{
			const TArray<uint16>& VariableJointIndices = CachedRigRuntimeContext->VariableJointIndicesPerLOD[LODLevel].Values;
			RigLogicToSkeletonBoneIndexMappingPerLOD[LODLevel].Reserve(VariableJointIndices.Num());

			for (const uint16 RigLogicJointIndex : VariableJointIndices)
			{
				const FMeshPoseBoneIndex MeshPoseBoneIndex = CachedDNAIndexMapping->JointsMapDNAIndicesToMeshPoseBoneIndices[RigLogicJointIndex];
				if (MeshPoseBoneIndex != INDEX_NONE)
				{
					const int32 SkeletonBoneIndex = MeshPoseBoneIndex.GetInt();
					RigLogicToSkeletonBoneIndexMappingPerLOD[LODLevel].Add({ RigLogicJointIndex, SkeletonBoneIndex });
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Could not find bone in skeleton for RigLogic joint with index %i."), RigLogicJointIndex);
				}
			}
		}
	}

	void FRigLogicAnimNextInstanceData::InitSparseAndDenseDriverJointMapping()
	{
		// Populate driver joint to raw control attribute mapping (used to feed RigLogic with inputs from the joint hierarchy)
		SparseDriverJointsToControlAttributesMap.Empty();
		DenseDriverJointsToControlAttributesMap.Empty();
		DenseDriverJointsToControlAttributesMap.Reserve(CachedDNAIndexMapping->DriverJointsToControlAttributesMap.Num());

		// Sparse mapping will likely remain empty so no reservation happens
		for (const auto& Mapping : CachedDNAIndexMapping->DriverJointsToControlAttributesMap)
		{
			const FMeshPoseBoneIndex MeshPoseBoneIndex = Mapping.MeshPoseBoneIndex;

			if (MeshPoseBoneIndex != INDEX_NONE)
			{
				const int32 SkeletonBoneIndex = MeshPoseBoneIndex.GetInt();
				if ((Mapping.RotationX != INDEX_NONE) && (Mapping.RotationY != INDEX_NONE) && (Mapping.RotationZ != INDEX_NONE) && (Mapping.RotationW != INDEX_NONE))
				{
					DenseDriverJointsToControlAttributesMap.Add({ SkeletonBoneIndex, Mapping.DNAJointIndex, Mapping.RotationX, Mapping.RotationY, Mapping.RotationZ, Mapping.RotationW });
				}
				else
				{
					SparseDriverJointsToControlAttributesMap.Add({ SkeletonBoneIndex, Mapping.DNAJointIndex, Mapping.RotationX, Mapping.RotationY, Mapping.RotationZ, Mapping.RotationW });
				}
			}
		}
	}
} // namespace UE::AnimNext