// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_RigLogic.h"

#include "Components/SkeletalMeshComponent.h"
#include "DNAAsset.h"
#include "DNAIndexMapping.h"
#include "DNAReader.h"
#include "Engine/SkeletalMesh.h"
#include "RigLogic.h"
#include "RigInstance.h"
#include "SharedRigRuntimeContext.h"
#include "Animation/AnimInstanceProxy.h"
#include "HAL/LowLevelMemTracker.h"
#include "Animation/MorphTarget.h"

#include <tdm/TDM.h>

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_RigLogic)

LLM_DEFINE_TAG(Animation_RigLogic);
DEFINE_LOG_CATEGORY(LogRigLogicAnimNode);

static constexpr uint16 ATTR_COUNT_PER_JOINT = 10;

FAnimNode_RigLogic::FAnimNode_RigLogic() : RigInstance(nullptr)
{
}

FAnimNode_RigLogic::~FAnimNode_RigLogic()
{
	if (RigInstance != nullptr)
	{
		delete RigInstance;
		RigInstance = nullptr;
	}
}

void FAnimNode_RigLogic::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_Initialize_AnyThread);
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));

	AnimSequence.Initialize(Context);
}

void FAnimNode_RigLogic::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_CacheBones_AnyThread);
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));

	AnimSequence.CacheBones(Context);

	USkeletalMeshComponent* SkeletalMeshComponent = Context.AnimInstanceProxy->GetSkelMeshComponent();
	if (SkeletalMeshComponent == nullptr)
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	if (SkeletalMesh == nullptr)
	{
		return;
	}

	USkeleton* Skeleton = Context.AnimInstanceProxy->GetSkeleton();
	if (Skeleton == nullptr)
	{
		return;
	}

	UDNAAsset* DNAAsset = Cast<UDNAAsset>(SkeletalMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass()));
	if (DNAAsset == nullptr)
	{
		return;
	}

	TSharedPtr<FSharedRigRuntimeContext> SharedRigRuntimeContext = DNAAsset->GetRigRuntimeContext();
	if (!SharedRigRuntimeContext.IsValid())
	{
		return;
	}

	if (LocalRigRuntimeContext != SharedRigRuntimeContext)
	{
		LocalRigRuntimeContext = SharedRigRuntimeContext;
		if (RigInstance != nullptr)
		{
			delete RigInstance;
		}
		RigInstance = new FRigInstance(LocalRigRuntimeContext->RigLogic.Get());
	}

	LocalDNAIndexMapping = DNAAsset->GetDNAIndexMapping(Skeleton, SkeletalMesh);
	// CacheBones is called on LOD switches as well, in which case compact pose bone indices must be remapped
	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	if (RequiredBones.IsValid())
	{
		// Set current LOD
		RigInstance->SetLOD(Context.AnimInstanceProxy->GetLODLevel());
		const uint16 CurrentLOD = RigInstance->GetLOD();

		// Populate mapping of DNA joint indices to CompactPoseBoneIndex entries (used in updating joints with results from RigLogic)
		const TArray<uint16>& VariableJointIndices = LocalRigRuntimeContext->VariableJointIndicesPerLOD[CurrentLOD].Values;
		JointsMapDNAIndicesToCompactPoseBoneIndices.Empty();
		JointsMapDNAIndicesToCompactPoseBoneIndices.Reserve(VariableJointIndices.Num());
		for (const uint16 JointIndex : VariableJointIndices)
		{
			const FMeshPoseBoneIndex MeshPoseBoneIndex = LocalDNAIndexMapping->JointsMapDNAIndicesToMeshPoseBoneIndices[JointIndex];
			const FCompactPoseBoneIndex CompactPoseBoneIndex = RequiredBones.MakeCompactPoseIndex(MeshPoseBoneIndex);
			if (CompactPoseBoneIndex != INDEX_NONE) {
				JointsMapDNAIndicesToCompactPoseBoneIndices.Add({JointIndex, CompactPoseBoneIndex});
			}
		}

		// Populate driver joint to raw control attribute mapping (used to feed RigLogic with inputs from the joint hierarchy)
		SparseDriverJointsToControlAttributesMap.Empty();
		DenseDriverJointsToControlAttributesMap.Empty();
		DenseDriverJointsToControlAttributesMap.Reserve(LocalDNAIndexMapping->DriverJointsToControlAttributesMap.Num());
		// Sparse mapping will likely remain empty so no reservation happens
		for (const auto& Mapping : LocalDNAIndexMapping->DriverJointsToControlAttributesMap)
		{
			const FCompactPoseBoneIndex CompactPoseBoneIndex = RequiredBones.MakeCompactPoseIndex(Mapping.MeshPoseBoneIndex);
			if (CompactPoseBoneIndex != INDEX_NONE) {
				if ((Mapping.RotationX != INDEX_NONE) && (Mapping.RotationY != INDEX_NONE) && (Mapping.RotationZ != INDEX_NONE) && (Mapping.RotationW != INDEX_NONE))
				{
					DenseDriverJointsToControlAttributesMap.Add({CompactPoseBoneIndex, Mapping.DNAJointIndex, Mapping.RotationX, Mapping.RotationY, Mapping.RotationZ, Mapping.RotationW});
				}
				else
				{
					SparseDriverJointsToControlAttributesMap.Add({CompactPoseBoneIndex, Mapping.DNAJointIndex, Mapping.RotationX, Mapping.RotationY, Mapping.RotationZ, Mapping.RotationW});
				}
			}
		}
	}
}

void FAnimNode_RigLogic::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_Update_AnyThread);

	GetEvaluateGraphExposedInputs().Execute(Context);
	AnimSequence.Update(Context);
}

void FAnimNode_RigLogic::Evaluate_AnyThread(FPoseContext& OutputContext)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_Evaluate_AnyThread);

	AnimSequence.Evaluate(OutputContext);

	if (!LocalRigRuntimeContext.IsValid() || !LocalDNAIndexMapping.IsValid())
	{
		return;
	}

	if (IsLODEnabled(OutputContext.AnimInstanceProxy))
	{
		UpdateControlCurves(OutputContext, LocalDNAIndexMapping.Get(), LocalRigRuntimeContext->RigLogic->GetNeutralJointValues());
		CalculateRigLogic(LocalRigRuntimeContext->RigLogic.Get());
		const uint16 CurrentLOD = RigInstance->GetLOD();
		UpdateJoints(LocalRigRuntimeContext->RigLogic->GetNeutralJointValues(), RigInstance->GetJointOutputs(), OutputContext);
		UpdateBlendShapeCurves(LocalDNAIndexMapping.Get(), RigInstance->GetBlendShapeOutputs(), OutputContext);
		UpdateAnimMapCurves(LocalDNAIndexMapping.Get(), RigInstance->GetAnimatedMapOutputs(), OutputContext);
	}
}

void FAnimNode_RigLogic::GatherDebugData(FNodeDebugData& DebugData)
{
	AnimSequence.GatherDebugData(DebugData);
}

void FAnimNode_RigLogic::UpdateControlCurves(const FPoseContext& InputContext, const FDNAIndexMapping* DNAIndexMapping, TArrayView<const float> NeutralJointValues)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_UpdateControlCurves);

	// Combine control attribute curve with input curve to get indexed curve to apply to rig
	// Curve elements that dont have a control mapping will have INDEX_NONE as their index
	UE::Anim::FNamedValueArrayUtils::Union(InputContext.Curve, DNAIndexMapping->ControlAttributeCurves,
		[this](const UE::Anim::FCurveElement& InCurveElement, const UE::Anim::FCurveElementIndexed& InControlAttributeCurveElement, UE::Anim::ENamedValueUnionFlags InFlags)
		{
			if (InControlAttributeCurveElement.Index != INDEX_NONE)
 			{
				RigInstance->SetRawControl(InControlAttributeCurveElement.Index, FMath::Clamp(InCurveElement.Value, 0.0, 1.0));
			}
		});

	// The sparse mapping is NOT guaranteed to supply all quaternion attributes, so checks for each attribute mapping are present
	for (int32 MappingIndex = 0; MappingIndex < SparseDriverJointsToControlAttributesMap.Num(); ++MappingIndex)
	{
		const FCompactPoseBoneControlAttributeMapping& Mapping = SparseDriverJointsToControlAttributesMap[MappingIndex];
		const FTransform& CompactPose = InputContext.Pose[Mapping.CompactPoseBoneIndex];
		// Translation and Scale is currently not used here, so to avoid the overhead of checking them, they are simply ignored.
		// Should the need arise to use them as well, this code will need adjustment.
		const FQuat Rotation = CompactPose.GetRotation();
		const int32 AttrIndex = Mapping.DNAJointIndex * ATTR_COUNT_PER_JOINT;
		const tdm::fquat NeutralRotation{NeutralJointValues[AttrIndex + 3], NeutralJointValues[AttrIndex + 4], NeutralJointValues[AttrIndex + 5], NeutralJointValues[AttrIndex + 6]};
		const tdm::fquat AbsPoseRotation{static_cast<float>(Rotation.X), static_cast<float>(Rotation.Y), static_cast<float>(Rotation.Z), static_cast<float>(Rotation.W)};
		const tdm::fquat DeltaPoseRotation = tdm::inverse(NeutralRotation) * AbsPoseRotation;

		if (Mapping.RotationX != INDEX_NONE)
		{
			RigInstance->SetRawControl(Mapping.RotationX, DeltaPoseRotation.x);
		}
		if (Mapping.RotationY != INDEX_NONE)
		{
			RigInstance->SetRawControl(Mapping.RotationY, DeltaPoseRotation.y);
		}
		if (Mapping.RotationZ != INDEX_NONE)
		{
			RigInstance->SetRawControl(Mapping.RotationZ, DeltaPoseRotation.z);
		}
		if (Mapping.RotationW != INDEX_NONE)
		{
			RigInstance->SetRawControl(Mapping.RotationW, DeltaPoseRotation.w);
		}
	}

	// The dense mapping is guaranteed to supply all quaternion attributes, so NO checks for each attribute mapping are present
	for (int32 MappingIndex = 0; MappingIndex < DenseDriverJointsToControlAttributesMap.Num(); ++MappingIndex)
	{
		const FCompactPoseBoneControlAttributeMapping& Mapping = DenseDriverJointsToControlAttributesMap[MappingIndex];
		const FTransform& CompactPose = InputContext.Pose[Mapping.CompactPoseBoneIndex];
		// Translation and Scale is currently not used here, so to avoid the overhead of checking them, they are simply ignored.
		// Should the need arise to use them as well, this code will need adjustment.
		const FQuat Rotation = CompactPose.GetRotation();
		const int32 AttrIndex = Mapping.DNAJointIndex * ATTR_COUNT_PER_JOINT;
		const tdm::fquat NeutralRotation{ NeutralJointValues[AttrIndex + 3], NeutralJointValues[AttrIndex + 4], NeutralJointValues[AttrIndex + 5], NeutralJointValues[AttrIndex + 6] };
		const tdm::fquat AbsPoseRotation{ static_cast<float>(Rotation.X), static_cast<float>(Rotation.Y), static_cast<float>(Rotation.Z), static_cast<float>(Rotation.W) };
		const tdm::fquat DeltaPoseRotation = tdm::inverse(NeutralRotation) * AbsPoseRotation;
		RigInstance->SetRawControl(Mapping.RotationX, DeltaPoseRotation.x);
		RigInstance->SetRawControl(Mapping.RotationY, DeltaPoseRotation.y);
		RigInstance->SetRawControl(Mapping.RotationZ, DeltaPoseRotation.z);
		RigInstance->SetRawControl(Mapping.RotationW, DeltaPoseRotation.w);
	}

	if (RigInstance->GetNeuralNetworkCount() != 0) {
		UE::Anim::FNamedValueArrayUtils::Union(InputContext.Curve, DNAIndexMapping->NeuralNetworkMaskCurves,
			[this](const UE::Anim::FCurveElement& InCurveElement, const UE::Anim::FCurveElementIndexed& InControlAttributeCurveElement, UE::Anim::ENamedValueUnionFlags InFlags)
			{
				if (InControlAttributeCurveElement.Index != INDEX_NONE)
 				{
					RigInstance->SetNeuralNetworkMask(InControlAttributeCurveElement.Index, InCurveElement.Value);
 				}
			});
	}
}

void FAnimNode_RigLogic::CalculateRigLogic(FRigLogic* RigLogic)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_CalculateRigLogic);
	RigLogic->Calculate(RigInstance);
}

void FAnimNode_RigLogic::UpdateJoints(TArrayView<const float> NeutralJointValues, TArrayView<const float> DeltaJointValues, FPoseContext& OutputContext)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_UpdateJoints);

	const float* N = NeutralJointValues.GetData();
	const float* D = DeltaJointValues.GetData();
	for (const FJointCompactPoseBoneMapping& Mapping : JointsMapDNAIndicesToCompactPoseBoneIndices)
	{
		const uint16 AttrIndex = Mapping.JointIndex * ATTR_COUNT_PER_JOINT;
		FTransform& CompactPose = OutputContext.Pose[Mapping.CompactPoseBoneIndex];
		CompactPose.SetTranslation(FVector((N[AttrIndex + 0] + D[AttrIndex + 0]), (N[AttrIndex + 1] + D[AttrIndex + 1]), (N[AttrIndex + 2] + D[AttrIndex + 2])));
		CompactPose.SetRotation(FQuat(N[AttrIndex + 3], N[AttrIndex + 4], N[AttrIndex + 5], N[AttrIndex + 6]) * FQuat(D[AttrIndex + 3], D[AttrIndex + 4], D[AttrIndex + 5], D[AttrIndex + 6]));
		CompactPose.SetScale3D(FVector((N[AttrIndex + 7] + D[AttrIndex + 7]), (N[AttrIndex + 8] + D[AttrIndex + 8]), (N[AttrIndex + 9] + D[AttrIndex + 9])));
	}
}

void FAnimNode_RigLogic::UpdateBlendShapeCurves(const FDNAIndexMapping* DNAIndexMapping, TArrayView<const float> BlendShapeValues, FPoseContext& OutputContext)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_UpdateBlendShapeCurves);
	
	const uint16 LOD = RigInstance->GetLOD();
	const FDNAIndexMapping::FCachedIndexedCurve& MorphTargetCurve = DNAIndexMapping->MorphTargetCurvesPerLOD[LOD];
	UE::Anim::FNamedValueArrayUtils::Union(OutputContext.Curve, MorphTargetCurve, [&BlendShapeValues](UE::Anim::FCurveElement& InOutResult, const UE::Anim::FCurveElementIndexed& InSource, UE::Anim::ENamedValueUnionFlags InFlags)
	{
		if (BlendShapeValues.IsValidIndex(InSource.Index))
		{
			InOutResult.Value = BlendShapeValues[InSource.Index];
			InOutResult.Flags |= UE::Anim::ECurveElementFlags::MorphTarget;
		}
	});
}

void FAnimNode_RigLogic::UpdateAnimMapCurves(const FDNAIndexMapping* DNAIndexMapping, TArrayView<const float> AnimMapOutputs, FPoseContext& OutputContext)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	QUICK_SCOPE_CYCLE_COUNTER(STAT_AnimNode_RigLogic_UpdateAnimMapCurves);

	const uint16 LOD = RigInstance->GetLOD();
	const FDNAIndexMapping::FCachedIndexedCurve& MaskMultiplierCurve = DNAIndexMapping->MaskMultiplierCurvesPerLOD[LOD];
	UE::Anim::FNamedValueArrayUtils::Union(OutputContext.Curve, MaskMultiplierCurve, [&AnimMapOutputs](UE::Anim::FCurveElement& InOutResult, const UE::Anim::FCurveElementIndexed& InSource, UE::Anim::ENamedValueUnionFlags InFlags)
	{
		if (AnimMapOutputs.IsValidIndex(InSource.Index))
		{
			InOutResult.Value = AnimMapOutputs[InSource.Index];
			InOutResult.Flags |= UE::Anim::ECurveElementFlags::Material;
		}
	});
}
