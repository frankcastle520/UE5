// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimationAsset.h"
#include "TraitCore/ITraitInterface.h"
#include "TraitCore/TraitBinding.h"
#include "TraitInterfaces/ITimeline.h"

namespace UE::AnimNext
{
	/**
	 * IGroupSynchronization
	 *
	 * This interface exposes group synchronization related information and behavior.
	 */
	struct ANIMNEXTANIMGRAPH_API IGroupSynchronization : ITraitInterface
	{
		DECLARE_ANIM_TRAIT_INTERFACE(IGroupSynchronization, 0xf607d0fd)

		// Returns the group name used for synchronization
		virtual FName GetGroupName(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding) const;

		// Returns the group role used for synchronization
		virtual EAnimGroupRole::Type GetGroupRole(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding) const;

		// Called by the sync group graph instance component once a group has been synchronized to advance time on the leader
		// Returns the progress ratio of playback: 0.0 = start of animation, 1.0 = end of animation
		virtual FTimelineProgress AdvanceBy(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding, float DeltaTime) const;

		// Called by the sync group graph instance component once a group has been synchronized to advance time on each follower
		// Progress ratio must be between [0.0, 1.0]
		virtual void AdvanceToRatio(FExecutionContext& Context, const TTraitBinding<IGroupSynchronization>& Binding, float ProgressRatio) const;
	
#if WITH_EDITOR
		virtual const FText& GetDisplayName() const override;
		virtual const FText& GetDisplayShortName() const override;
#endif // WITH_EDITOR
	};

	/**
	 * Specialization for trait binding.
	 */
	template<>
	struct TTraitBinding<IGroupSynchronization> : FTraitBinding
	{
		// @see IGroupSynchronization::GetGroupName
		FName GetGroupName(FExecutionContext& Context) const
		{
			return GetInterface()->GetGroupName(Context, *this);
		}

		// @see IGroupSynchronization::GetGroupRole
		EAnimGroupRole::Type GetGroupRole(FExecutionContext& Context) const
		{
			return GetInterface()->GetGroupRole(Context, *this);
		}

		// @see IGroupSynchronization::AdvanceBy
		FTimelineProgress AdvanceBy(FExecutionContext& Context, float DeltaTime) const
		{
			return GetInterface()->AdvanceBy(Context, *this, DeltaTime);
		}

		// @see IGroupSynchronization::AdvanceToRatio
		void AdvanceToRatio(FExecutionContext& Context, float ProgressRatio) const
		{
			GetInterface()->AdvanceToRatio(Context, *this, ProgressRatio);
		}

	protected:
		const IGroupSynchronization* GetInterface() const { return GetInterfaceTyped<IGroupSynchronization>(); }
	};
}
