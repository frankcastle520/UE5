// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvaluationVM/EvaluationTask.h"
#include "TransformArray.h"

#include "StoreKeyframe.generated.h"

/**
 * Swap the two given Transform Arrays
 */
USTRUCT()
struct ANIMNEXT_API FAnimNextSwapTransformsTask : public FAnimNextEvaluationTask
{
	GENERATED_BODY()

	DECLARE_ANIM_EVALUATION_TASK(FAnimNextSwapTransformsTask)

	static FAnimNextSwapTransformsTask Make(UE::AnimNext::FTransformArraySoAHeap* A, UE::AnimNext::FTransformArraySoAHeap* B);

	// Task entry point
	virtual void Execute(UE::AnimNext::FEvaluationVM& VM) const override;

	UE::AnimNext::FTransformArraySoAHeap* A = nullptr;
	UE::AnimNext::FTransformArraySoAHeap* B = nullptr;
};

/*
 * Store the pose on top of the stack in the given Transform Array
 */
USTRUCT()
struct ANIMNEXT_API FAnimNextStoreKeyframeTransformsTask : public FAnimNextEvaluationTask
{
	GENERATED_BODY()

	DECLARE_ANIM_EVALUATION_TASK(FAnimNextStoreKeyframeTransformsTask)

	static FAnimNextStoreKeyframeTransformsTask Make(UE::AnimNext::FTransformArraySoAHeap* Dest);

	// Task entry point
	virtual void Execute(UE::AnimNext::FEvaluationVM& VM) const override;

	UE::AnimNext::FTransformArraySoAHeap* Dest = nullptr;
};
