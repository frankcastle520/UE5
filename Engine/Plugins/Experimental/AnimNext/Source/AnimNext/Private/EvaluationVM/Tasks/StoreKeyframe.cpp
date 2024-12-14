// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvaluationVM/Tasks/StoreKeyframe.h"

#include "EvaluationVM/EvaluationVM.h"
#include "EvaluationVM/KeyframeState.h"
#include "TransformArrayOperations.h"

FAnimNextSwapTransformsTask FAnimNextSwapTransformsTask::Make(UE::AnimNext::FTransformArraySoAHeap* A, UE::AnimNext::FTransformArraySoAHeap* B)
{
	FAnimNextSwapTransformsTask Task;
	Task.A = A;
	Task.B = B;
	return Task;
}

void FAnimNextSwapTransformsTask::Execute(UE::AnimNext::FEvaluationVM& VM) const
{
	Swap(*A, *B);
}

FAnimNextStoreKeyframeTransformsTask FAnimNextStoreKeyframeTransformsTask::Make(UE::AnimNext::FTransformArraySoAHeap* Dest)
{
	FAnimNextStoreKeyframeTransformsTask Task;
	Task.Dest = Dest;
	return Task;
}

void FAnimNextStoreKeyframeTransformsTask::Execute(UE::AnimNext::FEvaluationVM& VM) const
{
	using namespace UE::AnimNext;

	if (EnumHasAnyFlags(VM.GetFlags(), EEvaluationFlags::Bones))
	{
		if (const TUniquePtr<FKeyframeState>* Keyframe = VM.PeekValue<TUniquePtr<FKeyframeState>>(KEYFRAME_STACK_NAME, 0))
		{
			Dest->SetNumUninitialized((*Keyframe)->Pose.LocalTransforms.Num());
			CopyTransforms(Dest->GetView(), (*Keyframe)->Pose.LocalTransforms.GetConstView());
		}
	}
}
