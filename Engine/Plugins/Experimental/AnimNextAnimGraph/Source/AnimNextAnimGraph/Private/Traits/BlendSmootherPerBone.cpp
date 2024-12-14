// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traits/BlendSmootherPerBone.h"

#include "AlphaBlend.h"
#include "Animation/BlendProfile.h"
#include "TraitCore/ExecutionContext.h"
#include "TraitInterfaces/IHierarchy.h"
#include "EvaluationVM/Tasks/BlendKeyframesPerBone.h"
#include "EvaluationVM/Tasks/NormalizeRotations.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT(FBlendSmootherPerBoneTrait)

	// Trait implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(IDiscreteBlend) \
		GeneratorMacro(IEvaluate) \
		GeneratorMacro(IUpdate) \

	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FBlendSmootherPerBoneTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR

	void FBlendSmootherPerBoneTrait::PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		if (!SharedData->BlendProfile)
		{
			// No blend profile set, default smooth blend behavior
			IEvaluate::PostEvaluate(Context, Binding);
			return;
		}

		// We override the default behavior since we need to blend per bone

		const int32 NumChildren = InstanceData->PerChildBlendData.Num();
		if (NumChildren < 2)
		{
			return;	// If we don't have at least 2 children, there is nothing to do
		}

		// Children are visited depth first, in the order returned
		// As such, when we evaluate the task program, the keyframe of the last child will be
		// on top of the keyframe stack
		// We thus process children in reverse order

		// The last child override the top keyframe and scales it
		{
			const int32 ChildIndex = NumChildren - 1;
			const FBlendSampleData& PoseSampleData = InstanceData->PerBoneSampleData[ChildIndex];

			Context.AppendTask(FAnimNextBlendOverwriteKeyframePerBoneWithScaleTask::Make(SharedData->BlendProfile, PoseSampleData, PoseSampleData.TotalWeight));
		}

		// Other children accumulate with scale
		for (int32 ChildIndex = NumChildren - 2; ChildIndex >= 0; --ChildIndex)
		{
			const FBlendSampleData& PoseSampleDataA = InstanceData->PerBoneSampleData[ChildIndex];
			const FBlendSampleData& PoseSampleDataB = InstanceData->PerBoneSampleData[ChildIndex + 1];	// Above on the keyframe stack

			Context.AppendTask(FAnimNextBlendAddKeyframePerBoneWithScaleTask::Make(SharedData->BlendProfile, PoseSampleDataA, PoseSampleDataB, PoseSampleDataA.TotalWeight));
		}

		// Once we are done, we normalize rotations
		Context.AppendTask(FAnimNextNormalizeKeyframeRotationsTask());
	}

	void FBlendSmootherPerBoneTrait::PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		// If this is our first update, allocate our blend data
		if (InstanceData->PerChildBlendData.IsEmpty())
		{
			InitializeInstanceData(Context, Binding, SharedData, InstanceData);
		}

		// Update the traits below us, they might trigger a transition
		IUpdate::PreUpdate(Context, Binding, TraitState);

		if (!SharedData->BlendProfile)
		{
			return;	// No blend profile set, nothing to do
		}

		TTraitBinding<IDiscreteBlend> DiscreteBlendTrait;
		Binding.GetStackInterface(DiscreteBlendTrait);

		const int32 DestinationChildIndex = DiscreteBlendTrait.GetBlendDestinationChildIndex(Context);

		// If we're using a blend profile, extract the scales and build blend sample data
		const int32 NumChildren = InstanceData->PerChildBlendData.Num();
		for (int32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			const float BlendWeight = DiscreteBlendTrait.GetBlendWeight(Context, ChildIndex);
			const FAlphaBlend* BlendState = DiscreteBlendTrait.GetBlendState(Context, ChildIndex);

			FBlendSampleData& PoseSampleData = InstanceData->PerBoneSampleData[ChildIndex];
			PoseSampleData.TotalWeight = BlendWeight;

			const FBlendData& BlendData = InstanceData->PerChildBlendData[ChildIndex];
			const bool bInverse = SharedData->BlendProfile->Mode == EBlendProfileMode::WeightFactor ? (DestinationChildIndex != ChildIndex) : false;
			SharedData->BlendProfile->UpdateBoneWeights(PoseSampleData, *BlendState, BlendData.StartAlpha, BlendWeight, bInverse);
		}

		FBlendSampleData::NormalizeDataWeight(InstanceData->PerBoneSampleData);
	}

	void FBlendSmootherPerBoneTrait::OnBlendTransition(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 OldChildIndex, int32 NewChildIndex) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		// Trigger the new transition
		IDiscreteBlend::OnBlendTransition(Context, Binding, OldChildIndex, NewChildIndex);

		if (!SharedData->BlendProfile)
		{
			return;	// No blend profile set, nothing to do
		}

		const int32 NumChildren = InstanceData->PerChildBlendData.Num();
		if (NewChildIndex >= NumChildren)
		{
			// We have a new child
			check(NewChildIndex == NumChildren + 1);

			InstanceData->PerChildBlendData.AddDefaulted();
			FBlendSampleData& SampleData = InstanceData->PerBoneSampleData.AddDefaulted_GetRef();

			const uint32 NumBlendEntries = SharedData->BlendProfile->GetNumBlendEntries();
			SampleData.SampleDataIndex = NewChildIndex;
			SampleData.PerBoneBlendData.AddZeroed(NumBlendEntries);
		}

		TTraitBinding<IDiscreteBlend> DiscreteBlendTrait;
		Binding.GetStackInterface(DiscreteBlendTrait);

		for (int32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			FBlendData& ChildBlendData = InstanceData->PerChildBlendData[ChildIndex];

			const FAlphaBlend* BlendState = DiscreteBlendTrait.GetBlendState(Context, ChildIndex);
			ChildBlendData.StartAlpha = BlendState->GetAlpha();
		}
	}

	void FBlendSmootherPerBoneTrait::InitializeInstanceData(const FExecutionContext& Context, const FTraitBinding& Binding, const FSharedData* SharedData, FInstanceData* InstanceData)
	{
		check(InstanceData->PerChildBlendData.IsEmpty());

		if (!SharedData->BlendProfile)
		{
			return;	// No blend profile set, nothing to do
		}

		const uint32 NumChildren = IHierarchy::GetNumStackChildren(Context, Binding);

		InstanceData->PerChildBlendData.SetNum(NumChildren);
		InstanceData->PerBoneSampleData.SetNum(NumChildren);

		const uint32 NumBlendEntries = SharedData->BlendProfile->GetNumBlendEntries();
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			FBlendSampleData& SampleData = InstanceData->PerBoneSampleData[ChildIndex];
			SampleData.SampleDataIndex = ChildIndex;
			SampleData.PerBoneBlendData.AddZeroed(NumBlendEntries);
		}
	}
}
