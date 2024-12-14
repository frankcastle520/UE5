// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimationAsset.h"
#include "Curves/CurveFloat.h"
#include "TraitCore/Trait.h"
#include "TraitInterfaces/IDiscreteBlend.h"
#include "TraitInterfaces/IEvaluate.h"
#include "TraitInterfaces/IUpdate.h"

#include "BlendSmootherPerBone.generated.h"

class UBlendProfile;

USTRUCT(meta = (DisplayName = "Blend Smoother Per Bone"))
struct FAnimNextBlendSmootherPerBoneTraitSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

	/** Blend profile that configures how fast to blend each bone. */
	// TODO: Can't show list of blend profiles, we need to find a skeleton to perform the lookup with
	UPROPERTY(EditAnywhere, Category = "Default", meta = (Inline, UseAsBlendProfile=true))
	TObjectPtr<UBlendProfile> BlendProfile = nullptr;
};

namespace UE::AnimNext
{
	/**
	 * FBlendSmootherTrait
	 * 
	 * A trait that smoothly blends between discrete states over time.
	 */
	struct FBlendSmootherPerBoneTrait : FAdditiveTrait, IEvaluate, IUpdate, IDiscreteBlend
	{
		DECLARE_ANIM_TRAIT(FBlendSmootherPerBoneTrait, 0xb97ffc16, FAdditiveTrait)

		using FSharedData = FAnimNextBlendSmootherPerBoneTraitSharedData;

		// Struct for tracking blends for each pose
		struct FBlendData
		{
			// Which blend alpha we started the blend with
			float StartAlpha = 0.0f;
		};

		struct FInstanceData : FTrait::FInstanceData
		{
			// Blend state per child
			TArray<FBlendData> PerChildBlendData;

			// Per-bone blending data for each child
			TArray<FBlendSampleData> PerBoneSampleData;
		};

		// IEvaluate impl
		virtual void PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const override;

		// IUpdate impl
		virtual void PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const override;

		// IDiscreteBlend impl
		virtual void OnBlendTransition(FExecutionContext& Context, const TTraitBinding<IDiscreteBlend>& Binding, int32 OldChildIndex, int32 NewChildIndex) const override;

		// Internal impl
		static void InitializeInstanceData(const FExecutionContext& Context, const FTraitBinding& Binding, const FSharedData* SharedData, FInstanceData* InstanceData);
	};
}
