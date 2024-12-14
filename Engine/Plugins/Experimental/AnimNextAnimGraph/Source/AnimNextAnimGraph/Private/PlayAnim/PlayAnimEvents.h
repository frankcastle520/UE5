// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PlayAnim/PlayAnimRequest.h"
#include "PlayAnim/PlayAnimStatus.h"
#include "Module/ModuleEvents.h"
#include "TraitCore/TraitEvent.h"
#include "TraitInterfaces/ITimeline.h"

namespace UE::AnimNext
{
	/**
	 * PlayAnim Play Event
	 *
	 * Event raised when a Play request is made.
	 * It encapsulates everything needed to service an animation request.
	 *
	 * If no sub-graph is provided, this event will request that the input source plays instead.
	 */
	struct FPlayAnim_PlayEvent : public FAnimNextTraitEvent
	{
		DECLARE_ANIM_TRAIT_EVENT(FPlayAnim_PlayEvent, FAnimNextTraitEvent)

		FPlayAnimRequestPtr Request;

	protected:
		// FAnimNextTraitEvent impl
		virtual void OnExpired(FTraitEventList& OutputEventList) override;
	};

	/**
	 * PlayAnim Stop Event
	 *
	 * Event raised when a Stop request is made.
	 * It encapsulates everything needed to service an animation request.
	 */
	struct FPlayAnim_StopEvent : public FAnimNextTraitEvent
	{
		DECLARE_ANIM_TRAIT_EVENT(FPlayAnim_StopEvent, FAnimNextTraitEvent)

		FPlayAnimRequestPtr Request;
	};

	/**
	 * PlayAnim Status Update
	 *
	 * Event raised when the status of a request changes.
	 */
	struct FPlayAnim_StatusUpdateEvent : public FAnimNextModule_ActionEvent
	{
		DECLARE_ANIM_TRAIT_EVENT(FPlayAnim_StatusUpdateEvent, FAnimNextModule_ActionEvent)

		// FAnimNextSchedule_ActionEvent impl
		virtual void Execute() const override;

		// The request to update
		FPlayAnimRequestPtr Request;

		// The current request status
		EPlayAnimStatus Status = EPlayAnimStatus::None;
	};

	/**
	 * PlayAnim Timeline Update
	 *
	 * Event raised when a request is playing with its updated timeline progress.
	 */
	struct FPlayAnim_TimelineUpdateEvent : public FAnimNextModule_ActionEvent
	{
		DECLARE_ANIM_TRAIT_EVENT(FPlayAnim_TimelineUpdateEvent, FAnimNextModule_ActionEvent)

		// FAnimNextSchedule_ActionEvent impl
		virtual void Execute() const override;

		// The request to update
		FPlayAnimRequestPtr Request;

		// The current request timeline progress
		FTimelineProgress TimelineProgress;
	};
}
