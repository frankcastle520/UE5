// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigLogicTask.h"

#include "EvaluationVM/EvaluationVM.h"
#include "EvaluationVM/KeyframeState.h"
#include "TransformArrayOperations.h"

#include "RigLogicAnimNext.h"
#include "DNAIndexMapping.h"
#include "RigLogic.h"
#include "RigInstance.h"
#include "SharedRigRuntimeContext.h"
#include "RigLogicInstanceDataPool.h"

#include <tdm/TDM.h>

DEFINE_STAT(STAT_AnimNext_Task_RigLogic);

FAnimNextRigLogicTask FAnimNextRigLogicTask::Make()
{
	return {};
}

void FAnimNextRigLogicTask::Execute(UE::AnimNext::FEvaluationVM& VM) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_Task_RigLogic);

	// Pop pose, we'll re-use it for our result
	TUniquePtr<UE::AnimNext::FKeyframeState> Keyframe;
	if (!VM.PopValue(UE::AnimNext::KEYFRAME_STACK_NAME, Keyframe))
	{
		// We have no inputs, nothing to do.
		return;
	}

	// Lazy-init instance data.
	FRigLogicAnimNextModule& Module = FModuleManager::GetModuleChecked<FRigLogicAnimNextModule>("RigLogicAnimNext");
	TSharedPtr<UE::AnimNext::FRigLogicAnimNextInstanceData> NewInstanceData = Module.DataPool.RequestData(Keyframe->Pose.RefPose);

	FRigInstance* RigInstance = NewInstanceData->RigInstance.Get();
	const FSharedRigRuntimeContext* RigRuntimeContext = NewInstanceData->CachedRigRuntimeContext.Get();
	const FDNAIndexMapping* DNAIndexMapping = NewInstanceData->CachedDNAIndexMapping.Get();
	if (RigInstance && RigRuntimeContext && DNAIndexMapping)
	{
		if (const FRigLogic* RigLogic = RigRuntimeContext->RigLogic.Get())
		{
			// 1. Feed input curves to RigLogic.
			FBlendedCurve& Curves = Keyframe->Curves;
			const TArrayView<const float> NeutralJointValues = NewInstanceData->CachedRigRuntimeContext->RigLogic->GetNeutralJointValues();

			UpdateControlCurves(RigInstance,
				Curves,
				DNAIndexMapping->ControlAttributeCurves,
				DNAIndexMapping->NeuralNetworkMaskCurves,
				NeutralJointValues,
				NewInstanceData->SparseDriverJointsToControlAttributesMap,
				NewInstanceData->DenseDriverJointsToControlAttributesMap,
				Keyframe->Pose);

			// 2. Evaluate RigLogic.
			const int32 LODLevel = Keyframe->Pose.LODLevel;
			RigInstance->SetLOD(LODLevel);
			RigLogic->Calculate(RigInstance);

			// 3. Update AnimNext bone transforms and curves with RigLogic output.
			TArrayView<const uint16> VariableJointIndices = NewInstanceData->CachedRigRuntimeContext->VariableJointIndicesPerLOD[LODLevel].Values;
			const TArrayView<const UE::AnimNext::FRigLogicBoneMapping> RigLogicIndexToMeshIndexMapping = NewInstanceData->RigLogicToSkeletonBoneIndexMappingPerLOD[LODLevel];
			UpdateJoints(RigLogicIndexToMeshIndexMapping,
				VariableJointIndices,
				NeutralJointValues,
				RigInstance->GetJointOutputs(),
				Keyframe->Pose);

			UpdateBlendShapeCurves(DNAIndexMapping->MorphTargetCurvesPerLOD[LODLevel], RigInstance->GetBlendShapeOutputs(), Curves);
			UpdateAnimMapCurves(DNAIndexMapping->MaskMultiplierCurvesPerLOD[LODLevel], RigInstance->GetAnimatedMapOutputs(), Curves);
		}
	}

	Module.DataPool.FreeData(Keyframe->Pose.RefPose->SkeletalMesh, NewInstanceData);

	// Push our result back
	VM.PushValue(UE::AnimNext::KEYFRAME_STACK_NAME, MoveTemp(Keyframe));
}

void FAnimNextRigLogicTask::UpdateControlCurves(FRigInstance* InRigInstance,
	const FBlendedCurve& Curves,
	const FCachedIndexedCurve& ControlAttributeCurves,
	const FCachedIndexedCurve& NeuralNetworkMaskCurves,
	const TArrayView<const float> NeutralJointValues,
	const TArrayView<const UE::AnimNext::FPoseBoneControlAttributeMapping> SparseDriverJointsToControlAttributesMap,
	const TArrayView<const UE::AnimNext::FPoseBoneControlAttributeMapping> DenseDriverJointsToControlAttributesMap,
	const UE::AnimNext::FLODPoseStack& InputPose) const
{
	// Combine control attribute curve with input curve to get indexed curve to apply to rig
	// Curve elements that dont have a control mapping will have INDEX_NONE as their index
	UE::Anim::FNamedValueArrayUtils::Union(Curves, ControlAttributeCurves,
		[this, InRigInstance](const UE::Anim::FCurveElement& InCurveElement, const UE::Anim::FCurveElementIndexed& InControlAttributeCurveElement, UE::Anim::ENamedValueUnionFlags InFlags)
		{
			if (InControlAttributeCurveElement.Index != INDEX_NONE)
			{
				InRigInstance->SetRawControl(InControlAttributeCurveElement.Index, InCurveElement.Value);
			}
		});

	// Combine control attribute curve with input curve to get indexed curve to apply to rig
	// Curve elements that dont have a control mapping will have INDEX_NONE as their index
	UE::Anim::FNamedValueArrayUtils::Union(Curves, ControlAttributeCurves,
		[this, InRigInstance](const UE::Anim::FCurveElement& InCurveElement, const UE::Anim::FCurveElementIndexed& InControlAttributeCurveElement, UE::Anim::ENamedValueUnionFlags InFlags)
		{
			if (InControlAttributeCurveElement.Index != INDEX_NONE)
			{
				InRigInstance->SetRawControl(InControlAttributeCurveElement.Index, InCurveElement.Value);
			}
		});

	const uint16 LODLevel = InRigInstance->GetLOD();
	const TArrayView<const FBoneIndexType> SkeletonIndexToPoseIndex = InputPose.GetSkeletonBoneIndexToLODBoneIndexMap(); // skeleton -> pose

	// The sparse mapping is NOT guaranteed to supply all quaternion attributes, so checks for each attribute mapping are present
	for (int32 MappingIndex = 0; MappingIndex < SparseDriverJointsToControlAttributesMap.Num(); ++MappingIndex)
	{
		const UE::AnimNext::FPoseBoneControlAttributeMapping& Mapping = SparseDriverJointsToControlAttributesMap[MappingIndex];
		const int32 SkeletonBoneIndex = Mapping.SkeletonBoneIndex;

		// Translate from skeleton bone to LOD pose bone index as the LOD pose bone indices might vary across the skel mesh instances.
		const int32 PoseBoneIndex = SkeletonIndexToPoseIndex[SkeletonBoneIndex];

		const UE::AnimNext::FTransformSoAAdapterConst TransformAdapter = InputPose.LocalTransforms[PoseBoneIndex];

		// Translation and Scale is currently not used here, so to avoid the overhead of checking them, they are simply ignored.
		// Should the need arise to use them as well, this code will need adjustment.
		const FQuat Rotation = TransformAdapter.GetRotation();
		const int32 AttrIndex = Mapping.DNAJointIndex * ATTR_COUNT_PER_JOINT;

		const tdm::fquat NeutralRotation{ NeutralJointValues[AttrIndex + 3], NeutralJointValues[AttrIndex + 4], NeutralJointValues[AttrIndex + 5], NeutralJointValues[AttrIndex + 6] };
		const tdm::fquat AbsPoseRotation{ static_cast<float>(Rotation.X), static_cast<float>(Rotation.Y), static_cast<float>(Rotation.Z), static_cast<float>(Rotation.W) };
		const tdm::fquat DeltaPoseRotation = tdm::inverse(NeutralRotation) * AbsPoseRotation;

		if (Mapping.RotationX != INDEX_NONE)
		{
			InRigInstance->SetRawControl(Mapping.RotationX, DeltaPoseRotation.x);
		}
		if (Mapping.RotationY != INDEX_NONE)
		{
			InRigInstance->SetRawControl(Mapping.RotationY, DeltaPoseRotation.y);
		}
		if (Mapping.RotationZ != INDEX_NONE)
		{
			InRigInstance->SetRawControl(Mapping.RotationZ, DeltaPoseRotation.z);
		}
		if (Mapping.RotationW != INDEX_NONE)
		{
			InRigInstance->SetRawControl(Mapping.RotationW, DeltaPoseRotation.w);
		}
	}

	// The dense mapping is guaranteed to supply all quaternion attributes, so NO checks for each attribute mapping are present
	for (int32 MappingIndex = 0; MappingIndex < DenseDriverJointsToControlAttributesMap.Num(); ++MappingIndex)
	{
		const UE::AnimNext::FPoseBoneControlAttributeMapping& Mapping = SparseDriverJointsToControlAttributesMap[MappingIndex];
		const int32 SkeletonBoneIndex = Mapping.SkeletonBoneIndex;

		// Translate from skeleton bone to LOD pose bone index as the LOD pose bone indices might vary across the skel mesh instances.
		const int32 PoseBoneIndex = SkeletonIndexToPoseIndex[SkeletonBoneIndex];

		const UE::AnimNext::FTransformSoAAdapterConst TransformAdapter = InputPose.LocalTransforms[PoseBoneIndex];

		// Translation and Scale is currently not used here, so to avoid the overhead of checking them, they are simply ignored.
		// Should the need arise to use them as well, this code will need adjustment.
		const FQuat Rotation = TransformAdapter.GetRotation();
		const int32 AttrIndex = Mapping.DNAJointIndex * ATTR_COUNT_PER_JOINT;

		const tdm::fquat NeutralRotation{ NeutralJointValues[AttrIndex + 3], NeutralJointValues[AttrIndex + 4], NeutralJointValues[AttrIndex + 5], NeutralJointValues[AttrIndex + 6] };
		const tdm::fquat AbsPoseRotation{ static_cast<float>(Rotation.X), static_cast<float>(Rotation.Y), static_cast<float>(Rotation.Z), static_cast<float>(Rotation.W) };
		const tdm::fquat DeltaPoseRotation = tdm::inverse(NeutralRotation) * AbsPoseRotation;

		InRigInstance->SetRawControl(Mapping.RotationX, DeltaPoseRotation.x);
		InRigInstance->SetRawControl(Mapping.RotationY, DeltaPoseRotation.y);
		InRigInstance->SetRawControl(Mapping.RotationZ, DeltaPoseRotation.z);
		InRigInstance->SetRawControl(Mapping.RotationW, DeltaPoseRotation.w);
	}

	if (InRigInstance->GetNeuralNetworkCount() != 0)
	{
		UE::Anim::FNamedValueArrayUtils::Union(Curves, NeuralNetworkMaskCurves,
			[this, InRigInstance](const UE::Anim::FCurveElement& InCurveElement, const UE::Anim::FCurveElementIndexed& InControlAttributeCurveElement, UE::Anim::ENamedValueUnionFlags InFlags)
			{
				if (InControlAttributeCurveElement.Index != INDEX_NONE)
				{
					InRigInstance->SetNeuralNetworkMask(InControlAttributeCurveElement.Index, InCurveElement.Value);
				}
			});
	}
}

void FAnimNextRigLogicTask::UpdateJoints(const TArrayView<const UE::AnimNext::FRigLogicBoneMapping> RigLogicIndexToMeshIndexMapping,
	const TArrayView<const uint16> VariableJointIndices,
	const TArrayView<const float> NeutralJointValues,
	const TArrayView<const float> DeltaJointValues,
	UE::AnimNext::FLODPoseStack& OutputPose) const
{
	const TArrayView<const FBoneIndexType> SkeletonIndexToPoseIndex = OutputPose.GetSkeletonBoneIndexToLODBoneIndexMap(); // skeleton -> pose

	const float* N = NeutralJointValues.GetData();
	const float* D = DeltaJointValues.GetData();
	for (const UE::AnimNext::FRigLogicBoneMapping& Mapping : RigLogicIndexToMeshIndexMapping)
	{
		const uint16 RigLogicJointIndex = Mapping.RigLogicJointIndex;
		const int32 SkeletonBoneIndex = Mapping.SkeletonBoneIndex;
		const uint16 AttrIndex = RigLogicJointIndex * ATTR_COUNT_PER_JOINT;

		// Translate from skeleton bone to LOD pose bone index as the LOD pose bone indices might vary across the skel mesh instances.
		const int32 PoseBoneIndex = SkeletonIndexToPoseIndex[SkeletonBoneIndex];

		UE::AnimNext::FTransformSoAAdapter TransformAdapter = OutputPose.LocalTransforms[PoseBoneIndex];

		TransformAdapter.SetTranslation(FVector((N[AttrIndex + 0] + D[AttrIndex + 0]), (N[AttrIndex + 1] + D[AttrIndex + 1]), (N[AttrIndex + 2] + D[AttrIndex + 2])));
		TransformAdapter.SetRotation(FQuat(N[AttrIndex + 3], N[AttrIndex + 4], N[AttrIndex + 5], N[AttrIndex + 6]) * FQuat(D[AttrIndex + 3], D[AttrIndex + 4], D[AttrIndex + 5], D[AttrIndex + 6]));
		TransformAdapter.SetScale3D(FVector((N[AttrIndex + 7] + D[AttrIndex + 7]), (N[AttrIndex + 8] + D[AttrIndex + 8]), (N[AttrIndex + 9] + D[AttrIndex + 9])));
	}
}

void FAnimNextRigLogicTask::UpdateBlendShapeCurves(const FCachedIndexedCurve& MorphTargetCurves, const TArrayView<const float> BlendShapeValues, FBlendedCurve& OutputCurves) const
{
	UE::Anim::FNamedValueArrayUtils::Union(OutputCurves, MorphTargetCurves, [&BlendShapeValues](UE::Anim::FCurveElement& InOutResult, const UE::Anim::FCurveElementIndexed& InSource, UE::Anim::ENamedValueUnionFlags InFlags)
		{
			if (BlendShapeValues.IsValidIndex(InSource.Index))
			{
				InOutResult.Value = BlendShapeValues[InSource.Index];
				InOutResult.Flags |= UE::Anim::ECurveElementFlags::MorphTarget;
			}
		});
}

void FAnimNextRigLogicTask::UpdateAnimMapCurves(const FCachedIndexedCurve& MaskMultiplierCurves, const TArrayView<const float> AnimMapOutputs, FBlendedCurve& OutputCurves) const
{
	UE::Anim::FNamedValueArrayUtils::Union(OutputCurves, MaskMultiplierCurves, [&AnimMapOutputs](UE::Anim::FCurveElement& InOutResult, const UE::Anim::FCurveElementIndexed& InSource, UE::Anim::ENamedValueUnionFlags InFlags)
		{
			if (AnimMapOutputs.IsValidIndex(InSource.Index))
			{
				InOutResult.Value = AnimMapOutputs[InSource.Index];
				InOutResult.Flags |= UE::Anim::ECurveElementFlags::Material;
			}
		});
}