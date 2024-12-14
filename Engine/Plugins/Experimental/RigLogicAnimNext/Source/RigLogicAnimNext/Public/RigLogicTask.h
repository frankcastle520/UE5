// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvaluationVM/EvaluationTask.h"
#include "Animation/AnimCurveTypes.h"
#include "LODPose.h"
#include "AnimNextStats.h"

#include "RigInstance.h"
#include "RigLogic.h"
#include "RigLogicTrait.h"
#include "RigLogicInstanceData.h"

#include "RigLogicTask.generated.h"

struct FSharedRigRuntimeContext;
struct FDNAIndexMapping;

DECLARE_CYCLE_STAT_EXTERN(TEXT("AnimNext Task: RigLogic"), STAT_AnimNext_Task_RigLogic, STATGROUP_AnimNext, RIGLOGICANIMNEXT_API);

USTRUCT()
struct RIGLOGICANIMNEXT_API FAnimNextRigLogicTask : public FAnimNextEvaluationTask
{
	GENERATED_BODY()

	DECLARE_ANIM_EVALUATION_TASK(FAnimNextRigLogicTask)

	static FAnimNextRigLogicTask Make();

	// Task entry point
	virtual void Execute(UE::AnimNext::FEvaluationVM& VM) const override;

	using FCachedIndexedCurve = TBaseBlendedCurve<FDefaultAllocator, UE::Anim::FCurveElementIndexed>;
	static constexpr uint16 ATTR_COUNT_PER_JOINT = 10;

	void UpdateControlCurves(FRigInstance* InRigInstance,
		const FBlendedCurve& Curves,
		const FCachedIndexedCurve& ControlAttributeCurves,
		const FCachedIndexedCurve& NeuralNetworkMaskCurves,
		const TArrayView<const float> NeutralJointValues,
		const TArrayView<const UE::AnimNext::FPoseBoneControlAttributeMapping> SparseDriverJointsToControlAttributesMap,
		const TArrayView<const UE::AnimNext::FPoseBoneControlAttributeMapping> DenseDriverJointsToControlAttributesMap,
		const UE::AnimNext::FLODPoseStack& InputPose) const;

	void UpdateJoints(const TArrayView<const UE::AnimNext::FRigLogicBoneMapping> RigLogicIndexToMeshIndexMapping,
		const TArrayView<const uint16> VariableJointIndices,
		const TArrayView<const float> NeutralJointValues,
		const TArrayView<const float> DeltaJointValues,
		UE::AnimNext::FLODPoseStack& OutputPose) const;

	void UpdateBlendShapeCurves(const FCachedIndexedCurve& MorphTargetCurves, const TArrayView<const float> BlendShapeValues, FBlendedCurve& OutputCurves) const;
	void UpdateAnimMapCurves(const FCachedIndexedCurve& MaskMultiplierCurves, const TArrayView<const float> AnimMapOutputs, FBlendedCurve& OutputCurves) const;
};