// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_AnimNextWriteSkeletalMeshComponentPose.h"

#include "Units/RigUnitContext.h"
#include "AnimNextStats.h"
#include "GenerationTools.h"
#include "Component/SkinnedMeshComponentExtensions.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_AnimNextWriteSkeletalMeshComponentPose)

DEFINE_STAT(STAT_AnimNext_Write_Pose);

FRigUnit_AnimNextWriteSkeletalMeshComponentPose_Execute()
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_Write_Pose);

	using namespace UE::AnimNext;

	if(SkeletalMeshComponent == nullptr)
	{
		return;
	}
	
	if(!Pose.LODPose.IsValid())
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	if(SkeletalMesh == nullptr)
	{
		return;
	}

	FDataHandle RefPoseHandle = FDataRegistry::Get()->GetOrGenerateReferencePose(SkeletalMeshComponent);
	const UE::AnimNext::FReferencePose& RefPose = RefPoseHandle.GetRef<UE::AnimNext::FReferencePose>();

	FMemMark MemMark(FMemStack::Get());

	TArray<FTransform, TMemStackAllocator<>> LocalSpaceTransforms;
	LocalSpaceTransforms.SetNumUninitialized(SkeletalMesh->GetRefSkeleton().GetNum());

	// Map LOD pose into local-space scratch buffer
	FGenerationTools::RemapPose(Pose.LODPose, LocalSpaceTransforms);

	SkeletalMeshComponent->AnimCurves.CopyFrom(Pose.Curves);

	// Attributes require remapping since the indices are LOD indices and we want mesh indices
	FGenerationTools::RemapAttributes(Pose.LODPose, Pose.Attributes, SkeletalMeshComponent->CustomAttributes);

	// Convert and dispatch to renderer
	UE::Anim::FSkinnedMeshComponentExtensions::CompleteAndDispatch(
		SkeletalMeshComponent,
		RefPose.GetMeshBoneIndexToParentMeshBoneIndexMap(),
		RefPose.GetLODBoneIndexToMeshBoneIndexMap(Pose.LODPose.LODLevel),
		LocalSpaceTransforms);
}
