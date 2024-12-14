// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/MotionMatchingInteractionAnimNodeLibrary.h"
#include "PoseSearch/AnimNode_MotionMatchingInteraction.h"

FMotionMatchingInteractionAnimNodeReference UMotionMatchingInteractionAnimNodeLibrary::ConvertToMotionMatchingInteractionNode(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FMotionMatchingInteractionAnimNodeReference>(Node, Result);
}

void UMotionMatchingInteractionAnimNodeLibrary::SetAvailabilities(const FMotionMatchingInteractionAnimNodeReference& MotionMatchingInteractionNode, const TArray<FPoseSearchInteractionAvailability>& Availabilities)
{
	if (FAnimNode_MotionMatchingInteraction* MotionMatchingInteractionNodePtr = MotionMatchingInteractionNode.GetAnimNodePtr<FAnimNode_MotionMatchingInteraction>())
	{
		MotionMatchingInteractionNodePtr->Availabilities = Availabilities;
	}
	else
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingInteractionAnimNodeLibrary::SetAvailabilities called on an invalid context or with an invalid type"));
	}
}

float UMotionMatchingInteractionAnimNodeLibrary::GetTranslationWarpLerp(const FMotionMatchingInteractionAnimNodeReference& MotionMatchingInteractionNode)
{
	if (const FAnimNode_MotionMatchingInteraction* MotionMatchingInteractionNodePtr = MotionMatchingInteractionNode.GetAnimNodePtr<FAnimNode_MotionMatchingInteraction>())
	{
		return MotionMatchingInteractionNodePtr->GetTranslationWarpLerp();
	}

	UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingInteractionAnimNodeLibrary::GetTranslationWarpLerp called on an invalid context or with an invalid type"));
	return 0.f;
}

float UMotionMatchingInteractionAnimNodeLibrary::GetRotationWarpLerp(const FMotionMatchingInteractionAnimNodeReference& MotionMatchingInteractionNode)
{
	if (FAnimNode_MotionMatchingInteraction* MotionMatchingInteractionNodePtr = MotionMatchingInteractionNode.GetAnimNodePtr<FAnimNode_MotionMatchingInteraction>())
	{
		return MotionMatchingInteractionNodePtr->GetRotationWarpLerp();
	}
	
	UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingInteractionAnimNodeLibrary::GetRotationWarpLerp called on an invalid context or with an invalid type"));
	return 0.f;
}