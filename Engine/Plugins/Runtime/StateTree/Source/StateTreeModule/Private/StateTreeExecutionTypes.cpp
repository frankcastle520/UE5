// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeExecutionTypes.h"

const FStateTreeExternalDataHandle FStateTreeExternalDataHandle::Invalid = FStateTreeExternalDataHandle();

#if WITH_STATETREE_TRACE
const FStateTreeInstanceDebugId FStateTreeInstanceDebugId::Invalid = FStateTreeInstanceDebugId();
#endif // WITH_STATETREE_TRACE

FStateTreeTransitionResult::FStateTreeTransitionResult(const FRecordedStateTreeTransitionResult& RecordedTransition)
{
	for (int32 RecordedFrameIndex = 0; RecordedFrameIndex < RecordedTransition.NextActiveFrames.Num(); RecordedFrameIndex++)
	{
		const FRecordedStateTreeExecutionFrame& RecordedExecutionFrame = RecordedTransition.NextActiveFrames[RecordedFrameIndex];
		NextActiveFrames.Add(RecordedExecutionFrame);

		FStateTreeFrameStateSelectionEvents StateTreeFrameStateSelectionEvents;

		for (int32 EventIdx = 0; EventIdx < RecordedExecutionFrame.EventIndices.Num(); EventIdx++)
		{
			if (RecordedTransition.NextActiveFrameEvents.IsValidIndex(EventIdx))
			{
				const FStateTreeEvent& RecordedStateTreeEvent = RecordedTransition.NextActiveFrameEvents[EventIdx];
				StateTreeFrameStateSelectionEvents.Events[EventIdx] = FStateTreeSharedEvent(RecordedStateTreeEvent);
			}
		}

		NextActiveFrameEvents.Add(MoveTemp(StateTreeFrameStateSelectionEvents));
	}

	SourceState = RecordedTransition.SourceState;
	TargetState = RecordedTransition.SourceState;
	Priority = RecordedTransition.Priority;
	SourceStateTree = RecordedTransition.SourceStateTree;
	SourceRootState = RecordedTransition.SourceRootState;
}

FRecordedStateTreeTransitionResult::FRecordedStateTreeTransitionResult(const FStateTreeTransitionResult& Transition)
{
	check(Transition.NextActiveFrames.Num() == Transition.NextActiveFrameEvents.Num());

	for (int32 FrameIndex = 0; FrameIndex < Transition.NextActiveFrames.Num(); FrameIndex++)
	{
		const FStateTreeExecutionFrame& ExecutionFrame = Transition.NextActiveFrames[FrameIndex];
		const FStateTreeFrameStateSelectionEvents& StateSelectionEvents = Transition.NextActiveFrameEvents[FrameIndex];

		FRecordedStateTreeExecutionFrame& RecordedFrame = NextActiveFrames.Emplace_GetRef(ExecutionFrame);

		for (int32 StateIndex = 0; StateIndex < ExecutionFrame.ActiveStates.Num(); StateIndex++)
		{
			const FStateTreeEvent* Event = StateSelectionEvents.Events[StateIndex].Get();
			if (Event)
			{
				const int32 EventIndex = NextActiveFrameEvents.Add(*Event);
				RecordedFrame.EventIndices[StateIndex] = static_cast<uint8>(EventIndex);
			}
		}
	}

	SourceState = Transition.SourceState;
	TargetState = Transition.SourceState;
	Priority = Transition.Priority;
	SourceStateTree = Transition.SourceStateTree;
	SourceRootState = Transition.SourceRootState;
}

FStateTreeExecutionFrame::FStateTreeExecutionFrame(const FRecordedStateTreeExecutionFrame& RecordedExecutionFrame)
{
	StateTree = RecordedExecutionFrame.StateTree;
	RootState = RecordedExecutionFrame.RootState;
	ActiveStates = RecordedExecutionFrame.ActiveStates;
	bIsGlobalFrame = RecordedExecutionFrame.bIsGlobalFrame;
}

FRecordedStateTreeExecutionFrame::FRecordedStateTreeExecutionFrame(const FStateTreeExecutionFrame& ExecutionFrame)
{
	StateTree = ExecutionFrame.StateTree;
	RootState = ExecutionFrame.RootState;
	ActiveStates = ExecutionFrame.ActiveStates;
	bIsGlobalFrame = ExecutionFrame.bIsGlobalFrame;
}
