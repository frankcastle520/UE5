// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_MakeReferencePoseFromSkeletalMeshComponent.h"

#include "Units/RigUnitContext.h"
#include "GenerationTools.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_MakeReferencePoseFromSkeletalMeshComponent)

FRigUnit_MakeReferencePoseFromSkeletalMeshComponent_Execute()
{
	using namespace UE::AnimNext;

	if(SkeletalMeshComponent == nullptr)
	{
		return;
	}

	ReferencePose.ReferencePose = FDataRegistry::Get()->GetOrGenerateReferencePose(SkeletalMeshComponent);
}
