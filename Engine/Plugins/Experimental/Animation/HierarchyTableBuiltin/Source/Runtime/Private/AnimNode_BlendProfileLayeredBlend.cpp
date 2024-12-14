// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_BlendProfileLayeredBlend.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimTrace.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimCurveUtils.h"
#include "HierarchyTable.h"
#include "MaskProfile/HierarchyTableTypeMask.h"
#include "Animation/AttributeTypes.h"
#include "Animation/IAttributeBlendOperator.h"
#include "Hash/CityHash.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_BlendProfileLayeredBlend)

void FAnimNode_BlendProfileLayeredBlend::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_Base::Initialize_AnyThread(Context);

	BasePose.Initialize(Context);
	BlendPose.Initialize(Context);
}


void FAnimNode_BlendProfileLayeredBlend::RebuildPerBoneBlendWeights(const USkeleton* InSkeleton)
{
	if (InSkeleton)
	{
		CreateMaskWeights(InSkeleton);

		SkeletonGuid = InSkeleton->GetGuid();
		VirtualBoneGuid = InSkeleton->GetVirtualBoneGuid();
	}
}

bool FAnimNode_BlendProfileLayeredBlend::ArePerBoneBlendWeightsValid(const USkeleton* InSkeleton) const
{
	return (InSkeleton != nullptr && InSkeleton->GetGuid() == SkeletonGuid && InSkeleton->GetVirtualBoneGuid() == VirtualBoneGuid);
}

void FAnimNode_BlendProfileLayeredBlend::UpdateCachedBoneData(const FBoneContainer& RequiredBones, const USkeleton* Skeleton)
{
	if (RequiredBones.GetSerialNumber() == RequiredBonesSerialNumber)
	{
		return;
	}
	
	// Update cached mask weights
	{
		CachedCurveMaskWeights.Empty();
		//CachedAttributeMaskWeights.Empty();

		for (const FHierarchyTableEntryData& Entry : BlendMask->TableData)
		{
			if (Entry.EntryType == EHierarchyTableEntryType::Curve)
			{
				const float EntryWeight = Entry.GetValue<FHierarchyTableType_Mask>()->Value;
				CachedCurveMaskWeights.Add<UE::Anim::FCurveElement>({ Entry.Identifier, EntryWeight });
			}
			/*
			else if (Entry.EntryType == EHierarchyTableEntryType::Attribute)
			{
				const float EntryWeight = Entry.GetValue<FHierarchyTableType_Mask>()->Value;
				CachedAttributeMaskWeights.Add(FNamedFloat(Entry.Identifier, EntryWeight));
			}
			*/
		}
	}

	if (!ArePerBoneBlendWeightsValid(Skeleton))
	{
		RebuildPerBoneBlendWeights(Skeleton);
	}

	// build desired bone weights
	const TArray<FBoneIndexType>& RequiredBoneIndices = RequiredBones.GetBoneIndicesArray();
	const int32 NumRequiredBones = RequiredBoneIndices.Num();
	DesiredBoneBlendWeights.SetNumZeroed(NumRequiredBones);
	for (int32 RequiredBoneIndex = 0; RequiredBoneIndex < NumRequiredBones; RequiredBoneIndex++)
	{
		const int32 SkeletonBoneIndex = RequiredBones.GetSkeletonIndex(FCompactPoseBoneIndex(RequiredBoneIndex));
		if (ensure(SkeletonBoneIndex != INDEX_NONE))
		{
			DesiredBoneBlendWeights[RequiredBoneIndex] = PerBoneBlendWeights[SkeletonBoneIndex];
		}
	}

	CurrentBoneBlendWeights.Reset(DesiredBoneBlendWeights.Num());
	CurrentBoneBlendWeights.AddZeroed(DesiredBoneBlendWeights.Num());

	//Reinitialize bone blend weights now that we have cleared them
	UpdateDesiredBoneWeight();

	RequiredBonesSerialNumber = RequiredBones.GetSerialNumber();
}

void FAnimNode_BlendProfileLayeredBlend::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	
	BasePose.CacheBones(Context);
	BlendPose.CacheBones(Context);

	UpdateCachedBoneData(Context.AnimInstanceProxy->GetRequiredBones(), Context.AnimInstanceProxy->GetSkeleton());
}

void FAnimNode_BlendProfileLayeredBlend::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
	bool RootMotionBlendPose = false;
	float RootMotionWeight = 0.f;
	const float RootMotionClearWeight = bBlendRootMotionBasedOnRootBone ? 0.f : 1.f;

	if (IsLODEnabled(Context.AnimInstanceProxy))
	{
		GetEvaluateGraphExposedInputs().Execute(Context);

		if (FAnimWeight::IsRelevant(BlendWeight))
		{
			UpdateCachedBoneData(Context.AnimInstanceProxy->GetRequiredBones(), Context.AnimInstanceProxy->GetSkeleton());
			UpdateDesiredBoneWeight();

			if (bBlendRootMotionBasedOnRootBone && !CurrentBoneBlendWeights.IsEmpty())
			{
				const float NewRootMotionWeight = CurrentBoneBlendWeights[0];
				if (NewRootMotionWeight > ZERO_ANIMWEIGHT_THRESH)
				{
					RootMotionWeight = NewRootMotionWeight;
					RootMotionBlendPose = true;
				}
			}

			const float ThisPoseRootMotionWeight = RootMotionBlendPose ? RootMotionWeight : RootMotionClearWeight;
			BlendPose.Update(Context.FractionalWeightAndRootMotion(BlendWeight, ThisPoseRootMotionWeight));
		}
	}

	// initialize children
	const float BaseRootMotionWeight = 1.f - RootMotionWeight;

	if (BaseRootMotionWeight < ZERO_ANIMWEIGHT_THRESH)
	{
		BasePose.Update(Context.FractionalWeightAndRootMotion(1.f, BaseRootMotionWeight));
	}
	else
	{
		BasePose.Update(Context);
	}
}

void FAnimNode_BlendProfileLayeredBlend::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	ANIM_MT_SCOPE_CYCLE_COUNTER(BlendPosesInGraph, !IsInGameThread());

	FPoseContext BasePoseContext(Output);
	BasePose.Evaluate(BasePoseContext);

	FCompactPose TargetPose;
	FBlendedCurve TargetCurve;
	UE::Anim::FStackAttributeContainer TargetAttributes;

	if (FAnimWeight::IsRelevant(BlendWeight))
	{
		FPoseContext CurrentPoseContext(Output);
		BlendPose.Evaluate(CurrentPoseContext);

		TargetPose.MoveBonesFrom(CurrentPoseContext.Pose);
		TargetCurve.MoveFrom(CurrentPoseContext.Curve);
		TargetAttributes.MoveFrom(CurrentPoseContext.CustomAttributes);
	}
	else
	{
		TargetPose.ResetToRefPose(BasePoseContext.Pose.GetBoneContainer());
		TargetCurve.InitFrom(Output.Curve);
		TargetAttributes.CopyFrom(Output.CustomAttributes);
	}

	FAnimationPoseData OutputPoseData(Output);
	
	// Blend poses
	FAnimationRuntime::BlendTwoPosesTogetherPerBone(BasePoseContext.Pose, TargetPose, CurrentBoneBlendWeights, OutputPoseData.GetPose());

	// Blend curves
	{
		FBlendedCurve& OutCurve = OutputPoseData.GetCurve();
		OutCurve.CopyFrom(BasePoseContext.Curve);

		if (FAnimWeight::IsRelevant(BlendWeight))
		{
			FBlendedCurve FilteredCurves;

			// Multiply per-curve blend weights by matching blend pose curves
			UE::Anim::FNamedValueArrayUtils::Intersection(
				TargetCurve,
				CachedCurveMaskWeights,
				[this, &FilteredCurves](const UE::Anim::FCurveElement& InBlendElement, const UE::Anim::FCurveElement& InMaskElement) mutable
					{
						FilteredCurves.Add(InBlendElement.Name, InBlendElement.Value * InMaskElement.Value);
					});

			// Override blend curve values with premultipled curves
			TargetCurve.Combine(FilteredCurves);

			// Remove curves that have been filtered by the mask, curves with no mask value defined remain, even with 0.0 value
			UE::Anim::FNamedValueArrayUtils::RemoveByPredicate(
				TargetCurve,
				CachedCurveMaskWeights,
				[](const UE::Anim::FCurveElement& InBaseElement, const UE::Anim::FCurveElement& InMaskElement)
				{	
					const bool bKeep = InMaskElement.Value == 0.0;
					return bKeep;
				}
			);

			// Combine base and filtered pre-multiplied blend curves
			UE::Anim::FNamedValueArrayUtils::Union(
				OutCurve,
				TargetCurve,
				[this](UE::Anim::FCurveElement& InOutBaseElement, const UE::Anim::FCurveElement& InBlendElement, UE::Anim::ENamedValueUnionFlags InFlags)
					{
						InOutBaseElement.Value = FMath::Lerp(InOutBaseElement.Value, InBlendElement.Value, BlendWeight);
						InOutBaseElement.Flags |= InBlendElement.Flags; // Should this only apply when the weight is relevant?
					});
		}
	}

	// Blend attributes
	{
		using namespace UE::Anim;

		FStackAttributeContainer& BaseAttributes = BasePoseContext.CustomAttributes;
		FStackAttributeContainer& BlendAttributes = TargetAttributes;
		FStackAttributeContainer& OutAttributes = OutputPoseData.GetAttributes();
		OutAttributes.CopyFrom(BaseAttributes);

		// Premultiply masked blend attributes
		const TArray<TWeakObjectPtr<UScriptStruct>>& BlendUniqueTypes = BlendAttributes.GetUniqueTypes();

		//CachedAttributeMaskWeights.ForEachElement([&BlendUniqueTypes, &BlendAttributes](const FNamedFloat& AttributeMask)
		for (const FNamedFloat& AttributeMask : CachedAttributeMaskWeights)
		{
			for (int32 BlendUniqueTypeIndex = 0; BlendUniqueTypeIndex < BlendUniqueTypes.Num(); ++BlendUniqueTypeIndex)
			{
				const TArray<FAttributeId>& TypedAttributeIdentifiers = BlendAttributes.GetKeys(BlendUniqueTypeIndex);
				const TArray<UE::Anim::TWrappedAttribute<FAnimStackAllocator>>& TypedAttributeValues = BlendAttributes.GetValues(BlendUniqueTypeIndex);

				const TWeakObjectPtr<UScriptStruct> AttributeType = BlendUniqueTypes[BlendUniqueTypeIndex];
				const IAttributeBlendOperator* Operator = UE::Anim::AttributeTypes::GetTypeOperator(AttributeType);

				TWrappedAttribute<FAnimStackAllocator> DefaultData;
				AttributeType->InitializeStruct(DefaultData.GetPtr<void>());

				const int32 AttributeIndex = TypedAttributeIdentifiers.IndexOfByPredicate([&AttributeMask](const FAttributeId& InAttribute)
					{
						return InAttribute.GetName() == AttributeMask.Name;
					});

				if (AttributeIndex != INDEX_NONE)
				{
					const FAttributeId& AttributeId = TypedAttributeIdentifiers[AttributeIndex];

					if (AttributeMask.Value == 0.0f)
					{
						BlendAttributes.Remove(AttributeType.Get(), AttributeId);
					}
					else
					{
						uint8* Value = BlendAttributes.Find(AttributeType.Get(), AttributeId);
						check(Value != nullptr);

						Operator->Interpolate(DefaultData.GetPtr<void>(), Value, AttributeMask.Value, Value);
					}
					break;
				}
			}
		}

		const TArray<const UE::Anim::FStackAttributeContainer> SourceAttributes { BaseAttributes, BlendAttributes };
		const TArray<const float> SourceWeights { 1.0f - BlendWeight, BlendWeight };
		static const TArray<const int32> WeightView { 0, 1 };

		UE::Anim::Attributes::BlendAttributes(SourceAttributes, SourceWeights, WeightView, BaseAttributes);
	}
}

void FAnimNode_BlendProfileLayeredBlend::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)

	BasePose.GatherDebugData(DebugData.BranchFlow(1.f));
	BlendPose.GatherDebugData(DebugData.BranchFlow(BlendWeight));
}

void FAnimNode_BlendProfileLayeredBlend::CreateMaskWeights(const USkeleton* Skeleton)
{
	if (!Skeleton)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();

	const int32 NumBones = RefSkeleton.GetNum();
	PerBoneBlendWeights.Reset(NumBones);
	PerBoneBlendWeights.AddZeroed(NumBones);

	check(BlendMask->Skeleton == Skeleton && BlendMask->TableType == FHierarchyTableType_Mask::StaticStruct());

	for (int32 EntryIndex = 0; EntryIndex < BlendMask->TableData.Num(); ++EntryIndex)
	{
		const FHierarchyTableEntryData& EntryData = BlendMask->TableData[EntryIndex];
		if (EntryData.EntryType != EHierarchyTableEntryType::Bone)
		{
			break;
		}

		const FHierarchyTableType_Mask* MaskValue = EntryData.GetValue<FHierarchyTableType_Mask>();
		check(MaskValue);
			
		PerBoneBlendWeights[EntryIndex] = MaskValue->Value;
	}
}

void FAnimNode_BlendProfileLayeredBlend::UpdateDesiredBoneWeight()
{
	check(CurrentBoneBlendWeights.Num() == DesiredBoneBlendWeights.Num());

	CurrentBoneBlendWeights.Init(0, CurrentBoneBlendWeights.Num());

	for (int32 BoneIndex = 0; BoneIndex < DesiredBoneBlendWeights.Num(); ++BoneIndex)
	{
		float TargetBlendWeight = BlendWeight * DesiredBoneBlendWeights[BoneIndex];
		
		if (FAnimWeight::IsRelevant(TargetBlendWeight))
		{
			CurrentBoneBlendWeights[BoneIndex] = TargetBlendWeight;
		}
	}
}
