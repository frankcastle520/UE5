// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlendStack/AnimNode_BlendStack.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "AnimNode_MotionMatchingInteraction.generated.h"

struct FAnimationInitializeContext;
struct FNodeDebugData;

UENUM(Experimental)
enum class EMotionMatchingInteractionEvaluationMode
{
	// Node will continuously provide its availabilities and eventually blend to newly selected animations
	ContinuousReselection,

	// Node will continuously provide its availabilities to keep the interaction alive, but will play only the first selected animation
	// the idea is to let the animation play until the end and allow the evenutual state machine playing this node to be able to perform an automatic transition
	SingleSelection,

	// @todo: is this needed?
	// Node will stop providing its availabilities and consequently kill the interaction, when the first selected animation stop playing as continuing pose
	// the idea is to let the animation play until its valid as continuing pose and allow the evenutual state machine playing this node to be able to perform an automatic transition
	// UntilContinuingPoseIsValid,
};

USTRUCT(Experimental, BlueprintInternalUseOnly)
struct POSESEARCH_API FAnimNode_MotionMatchingInteraction : public FAnimNode_BlendStack
{
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, Category = Settings)
	EMotionMatchingInteractionEvaluationMode EvaluationMode = EMotionMatchingInteractionEvaluationMode::ContinuousReselection;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	TArray<FPoseSearchInteractionAvailability> Availabilities;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	bool bValidateResultAgainstAvailabilities = true;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	bool bEnableWarping = true;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	bool bUseAnimRootMotionProvider = true;
	
	// time from the beginning of the interaction to warp to full translation alignment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Warping, meta = (PinHiddenByDefault, ClampMin = "0"))
	float InitialTranslationWarpTime = 0.2f;

	// time from the beginning of the interaction to warp to full rotation alignment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Warping, meta = (PinHiddenByDefault, ClampMin = "0"))
	float InitialRotationWarpTime = 0.2f;

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;

	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	// End of FAnimNode_Base interface

	virtual void Reset() override;

	float GetTranslationWarpLerp() const { return TranslationWarpLerp; }
	float GetRotationWarpLerp() const { return RotationWarpLerp; }

protected:
	float TranslationWarpLerp = 0.f;
	float RotationWarpLerp = 0.f;
	// if a search is successful InteractingRolesNum > 0. 
	// if InteractingRolesNum == 1 it means that the search is a regular single character motion matching search
	// if InteractingRolesNum > 1 it means this node is interacting with other actors via the Interaction Subsystem
	int32 InteractingRolesNum = 0;

	// Update Counter for detecting being relevant
	FGraphTraversalCounter UpdateCounter;

	// wanted world transform for full aligment interaction
	FTransform FullAlignedActorRootBoneTransform = FTransform::Identity;
	FTransform WarpRootMotionTransform = FTransform::Identity;
};
