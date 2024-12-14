// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayAnim/PlayAnimEvents.h"

#include "TraitCore/TraitEventList.h"

namespace UE::AnimNext
{
	void FPlayAnim_PlayEvent::OnExpired(FTraitEventList& OutputEventList)
	{
		auto ActionEvent = MakeTraitEvent<FAnimNextModule_ActionEvent>();
		ActionEvent->ActionFunction = [Request = Request]()
			{
				Request->OnStatusUpdate(EPlayAnimStatus::Expired);
			};

		OutputEventList.Push(MoveTemp(ActionEvent));
	}

	void FPlayAnim_StatusUpdateEvent::Execute() const
	{
		Request->OnStatusUpdate(Status);
	}

	void FPlayAnim_TimelineUpdateEvent::Execute() const
	{
		Request->OnTimelineUpdate(TimelineProgress);
	}
}
