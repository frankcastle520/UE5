// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/ITraitInterface.h"
#include "TraitCore/TraitBinding.h"

namespace UE::AnimNext
{
	/**
	 * Timeline Progress
	 * 
	 * Encapsulates the progress along a timeline.
	 */
	struct FTimelineProgress
	{
		// Default construct with no progress
		FTimelineProgress() = default;

		// Construct with a specific position and duration
		FTimelineProgress(float InPosition, float InDuration)
			: Position(InPosition)
			, Duration(InDuration)
		{
		}

		// Resets the timeline progress to its initial state
		void Reset()
		{
			Position = Duration = 0.0f;
		}

		// Returns the timeline duration in seconds
		float GetDuration() const { return Duration; }

		// Returns the timeline position in seconds
		float GetPosition() const { return Position; }

		// Returns the timeline position as a ratio (0.0 = start of timeline, 1.0 = end of timeline)
		float GetPositionRatio() const { return Duration != 0.0f ? FMath::Clamp(Position / Duration, 0.0f, 1.0f) : 0.0f; }

		// Returns the time left to play in the timeline
		float GetTimeLeft() const { return Duration - Position; }

	private:
		// Timeline position in seconds
		float Position = 0.0f;

		// Timeline duration in seconds
		float Duration = 0.0f;
	};

	/**
	 * ITimeline
	 *
	 * This interface exposes timeline related information.
	 */
	struct ANIMNEXTANIMGRAPH_API ITimeline : ITraitInterface
	{
		DECLARE_ANIM_TRAIT_INTERFACE(ITimeline, 0x53760727)

		// Returns the play rate of this timeline
		virtual float GetPlayRate(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding) const;

		// Returns the progress of this timeline
		virtual FTimelineProgress GetProgress(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding) const;

		// Simulates the advance of time by the provided delta time (positive or negative) on this timeline
		// Returns the progress of playback
		virtual FTimelineProgress SimulateAdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const;

		// Advances time by the provided delta time (positive or negative) on this timeline
		// Returns the progress of playback
		virtual FTimelineProgress AdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const;

		// Advances time to the specified progress ratio on this timeline
		// Progress ratio must be between [0.0, 1.0]
		virtual void AdvanceToRatio(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float ProgressRatio) const;

#if WITH_EDITOR
		virtual const FText& GetDisplayName() const override;
		virtual const FText& GetDisplayShortName() const override;
#endif // WITH_EDITOR
	};

	/**
	 * Specialization for trait binding.
	 */
	template<>
	struct TTraitBinding<ITimeline> : FTraitBinding
	{
		// @see ITimeline::GetPlayRate
		float GetPlayRate(FExecutionContext& Context) const
		{
			return GetInterface()->GetPlayRate(Context, *this);
		}

		// @see ITimeline::GetProgress
		FTimelineProgress GetProgress(FExecutionContext& Context) const
		{
			return GetInterface()->GetProgress(Context, *this);
		}

		// @see ITimeline::SimulateAdvanceBy
		FTimelineProgress SimulateAdvanceBy(FExecutionContext& Context, float DeltaTime) const
		{
			return GetInterface()->SimulateAdvanceBy(Context, *this, DeltaTime);
		}

		// @see ITimeline::AdvanceBy
		FTimelineProgress AdvanceBy(FExecutionContext& Context, float DeltaTime) const
		{
			return GetInterface()->AdvanceBy(Context, *this, DeltaTime);
		}

		// @see ITimeline::AdvanceToRatio
		void AdvanceToRatio(FExecutionContext& Context, float ProgressRatio) const
		{
			GetInterface()->AdvanceToRatio(Context, *this, ProgressRatio);
		}

	protected:
		const ITimeline* GetInterface() const { return GetInterfaceTyped<ITimeline>(); }
	};
}
