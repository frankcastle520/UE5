// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimationAsset.h"
#include "TraitCore/Trait.h"
#include "TraitInterfaces/IGroupSynchronization.h"
#include "TraitInterfaces/IUpdate.h"
#include "TraitInterfaces/ITimeline.h"

#include "SynchronizeUsingGroups.generated.h"

USTRUCT(meta = (DisplayName = "Synchronize Using Groups"))
struct FAnimNextSynchronizeUsingGroupsTraitSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

	// The group name
	// If no name is provided, this trait is inactive
	UPROPERTY(EditAnywhere, Category = "Default", meta = (Inline))
	FName GroupName;

	// The role this player can assume within the group
	UPROPERTY(EditAnywhere, Category = "Default", meta = (Inline))
	TEnumAsByte<EAnimGroupRole::Type> GroupRole = EAnimGroupRole::CanBeLeader;
};

namespace UE::AnimNext
{
	/**
	 * FSynchronizeUsingGroupsTrait
	 * 
	 * A trait that synchronizes animation sequence playback using named groups.
	 */
	struct FSynchronizeUsingGroupsTrait : FAdditiveTrait, IUpdate, IGroupSynchronization, ITimeline
	{
		DECLARE_ANIM_TRAIT(FSynchronizeUsingGroupsTrait, 0x6d318931, FAdditiveTrait)

		using FSharedData = FAnimNextSynchronizeUsingGroupsTraitSharedData;

		struct FInstanceData : FTraitInstanceData
		{
			bool bFreezeTimeline = false;
		};

		// IUpdate impl
		virtual void PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const override;

		// IGroupSynchronization impl
		virtual FName GetGroupName(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding) const override;
		virtual EAnimGroupRole::Type GetGroupRole(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding) const override;
		virtual FTimelineProgress AdvanceBy(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding, float DeltaTime) const override;
		virtual void AdvanceToRatio(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding, float ProgressRatio) const override;

		// ITimeline impl
		virtual FTimelineProgress SimulateAdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const override;
		virtual FTimelineProgress AdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const override;
		virtual void AdvanceToRatio(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float ProgressRatio) const override;
	};
}
