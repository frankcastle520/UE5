// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvaluationVM/Tasks/PushAnimSequenceKeyframe.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimRootMotionProvider.h"
#include "BonePose.h"
#include "DecompressionTools.h"
#include "EvaluationVM/EvaluationVM.h"
#include "EvaluationVM/KeyframeState.h"

FAnimNextAnimSequenceKeyframeTask FAnimNextAnimSequenceKeyframeTask::MakeFromSampleTime(TWeakObjectPtr<UAnimSequence> AnimSequence, double SampleTime, bool bInterpolate)
{
	FAnimNextAnimSequenceKeyframeTask Task;
	Task.AnimSequence = AnimSequence;
	Task.SampleTime = SampleTime;
	Task.bInterpolate = bInterpolate;

	return Task;
}

FAnimNextAnimSequenceKeyframeTask FAnimNextAnimSequenceKeyframeTask::MakeFromKeyframeIndex(TWeakObjectPtr<UAnimSequence> AnimSequence, uint32 KeyframeIndex)
{
	FAnimNextAnimSequenceKeyframeTask Task;
	Task.AnimSequence = AnimSequence;
	Task.KeyframeIndex = KeyframeIndex;

	return Task;
}

void FAnimNextAnimSequenceKeyframeTask::Execute(UE::AnimNext::FEvaluationVM& VM) const
{
	using namespace UE::AnimNext;
	
	if(const UAnimSequence* AnimSequencePtr = AnimSequence.Get())
	{
		const bool bIsAdditive = AnimSequencePtr->IsValidAdditive();

		FDeltaTimeRecord DeltaTimeRecord;	// Not needed
		const bool bExtractRootMotion = bExtractTrajectory;
		const bool bLooping = false;		// Not needed

		const FAnimExtractContext ExtractionContext(SampleTime, bExtractRootMotion, DeltaTimeRecord, bLooping);

		FKeyframeState Keyframe = VM.MakeUninitializedKeyframe(bIsAdditive);

		const bool bUseRawData = FDecompressionTools::ShouldUseRawData(AnimSequencePtr, Keyframe.Pose);

		if (EnumHasAnyFlags(VM.GetFlags(), EEvaluationFlags::Bones))
		{
			FDecompressionTools::GetAnimationPose(AnimSequencePtr, ExtractionContext, Keyframe.Pose, bUseRawData);
		}

		if (EnumHasAnyFlags(VM.GetFlags(), EEvaluationFlags::Curves))
		{
			FDecompressionTools::GetAnimationCurves(AnimSequencePtr, ExtractionContext, Keyframe.Curves, bUseRawData);
		}

		if (EnumHasAnyFlags(VM.GetFlags(), EEvaluationFlags::Attributes))
		{
			FDecompressionTools::GetAnimationAttributes(AnimSequencePtr, ExtractionContext, Keyframe.Pose.GetRefPose(), Keyframe.Attributes, bUseRawData);
		}

		// Trajectory is currently held as an attribute
		if (EnumHasAnyFlags(VM.GetFlags(), EEvaluationFlags::Attributes | EEvaluationFlags::Trajectory))
		{
			// If the sequence has root motion enabled, allow sampling of a root motion delta into the custom attribute container of the outgoing pose
			if (AnimSequence->HasRootMotion())
			{
				// TODO: We should cache the provider in the VM
				// We have to grab two locks to get it and it won't change during graph evaluation
				if (const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get())
				{
					RootMotionProvider->SampleRootMotion(ExtractionContext.DeltaTimeRecord, *AnimSequence, ExtractionContext.bLooping, Keyframe.Attributes);
				}
			}
		}

		VM.PushValue(KEYFRAME_STACK_NAME, MakeUnique<FKeyframeState>(MoveTemp(Keyframe)));
	}
	else
	{
		constexpr bool bIsAdditive = false;
		FKeyframeState Keyframe = VM.MakeReferenceKeyframe(bIsAdditive);
		VM.PushValue(KEYFRAME_STACK_NAME, MakeUnique<FKeyframeState>(MoveTemp(Keyframe)));
	}
}
