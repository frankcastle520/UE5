// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNodeReference.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MotionMatchingInteractionAnimNodeLibrary.generated.h"

struct FAnimNode_MotionMatchingInteraction;

USTRUCT(Experimental, BlueprintType)
struct FMotionMatchingInteractionAnimNodeReference : public FAnimNodeReference
{
	GENERATED_BODY()

	typedef FAnimNode_MotionMatchingInteraction FInternalNodeType;
};

UCLASS(Experimental)
class POSESEARCH_API UMotionMatchingInteractionAnimNodeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe, ExpandEnumAsExecs = "Result"))
	static FMotionMatchingInteractionAnimNodeReference ConvertToMotionMatchingInteractionNode(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result);

	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe, DisplayName = "Convert to Motion Matching Interaction Node"))
	static void ConvertToMotionMatchingInteractionNodePure(const FAnimNodeReference& Node, FMotionMatchingInteractionAnimNodeReference& MotionMatchingInteractionNode, bool& Result)
	{
		EAnimNodeReferenceConversionResult ConversionResult;
		MotionMatchingInteractionNode = ConvertToMotionMatchingInteractionNode(Node, ConversionResult);
		Result = (ConversionResult == EAnimNodeReferenceConversionResult::Succeeded);
	}

	UFUNCTION(BlueprintCallable, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe))
	static void SetAvailabilities(const FMotionMatchingInteractionAnimNodeReference& MotionMatchingInteractionNode, const TArray<FPoseSearchInteractionAvailability>& Availabilities);

	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe))
	static float GetTranslationWarpLerp(const FMotionMatchingInteractionAnimNodeReference& MotionMatchingInteractionNode);

	UFUNCTION(BlueprintPure, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe))
	static float GetRotationWarpLerp(const FMotionMatchingInteractionAnimNodeReference& MotionMatchingInteractionNode);
};