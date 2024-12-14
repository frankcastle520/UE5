// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextSkeletalMeshComponentLibrary.h"
#include "Components/SkeletalMeshComponent.h"

FAnimNextGraphReferencePose UAnimNextSkeletalMeshComponentLibrary::GetReferencePose(USkeletalMeshComponent* InComponent)
{
	UE::AnimNext::FDataHandle RefPoseHandle = UE::AnimNext::FDataRegistry::Get()->GetOrGenerateReferencePose(InComponent);
	return FAnimNextGraphReferencePose(RefPoseHandle);
}
