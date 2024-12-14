// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DNAIndexMapping.h"
#include "RigInstance.h"
#include "LODPose.h"
#include "Engine/SkeletalMesh.h"

struct FSharedRigRuntimeContext;
struct FDNAIndexMapping;

namespace UE::AnimNext
{
	struct FRigLogicBoneMapping
	{
		uint16 RigLogicJointIndex;
		int32 SkeletonBoneIndex;
	};

	struct FPoseBoneControlAttributeMapping
	{
		int32 SkeletonBoneIndex;
		int32 DNAJointIndex;
		int32 RotationX;
		int32 RotationY;
		int32 RotationZ;
		int32 RotationW;
	};

	// Instance data not per anim graph node, skeleton pair instance. TODO: Better call it PoolData?
	struct FRigLogicAnimNextInstanceData
	{
		/** Cached pointer to the shared RigLogic runtime context originally owned by UDNAAsset. */
		TSharedPtr<FSharedRigRuntimeContext> CachedRigRuntimeContext;

		/** Cached pointer to the DNA index mapping which is originally owned by UDNAAsset. */
		TSharedPtr<FDNAIndexMapping> CachedDNAIndexMapping;

		/** Actually cloned RigLogic instance owned by this class. */
		TUniquePtr<FRigInstance> RigInstance;

		/** Bone index mapping from a RigLogic joint index to the reference skeleton bone index, one per LOD level. */
		TArray<TArray<FRigLogicBoneMapping>> RigLogicToSkeletonBoneIndexMappingPerLOD;

		TArray<FPoseBoneControlAttributeMapping> SparseDriverJointsToControlAttributesMap;
		TArray<FPoseBoneControlAttributeMapping> DenseDriverJointsToControlAttributesMap;

		void Init(USkeletalMesh* SkeletalMesh);

	private:
		void InitBoneIndexMapping();
		void InitSparseAndDenseDriverJointMapping();
	};
} // namespace UE::AnimNext