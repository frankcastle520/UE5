// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_TRACE_DEBUGGER

#include "Debugger/StateTreeDebuggerTrack.h"
#include "Debugger/StateTreeDebugger.h"
#include "SStateTreeDebuggerEventTimelineView.h"

//----------------------------------------------------------------------//
// FStateTreeDebuggerInstanceTrack
//----------------------------------------------------------------------//
FStateTreeDebuggerInstanceTrack::FStateTreeDebuggerInstanceTrack(
	const TSharedPtr<FStateTreeDebugger>& InDebugger,
	const FStateTreeInstanceDebugId InInstanceId,
	const FText& InName,
	const TRange<double>& InViewRange
	)
	: FStateTreeDebuggerBaseTrack(FSlateIcon("StateTreeEditorStyle", "StateTreeEditor.Debugger.InstanceTrack", "StateTreeEditor.Debugger.InstanceTrack"), InName)
	, StateTreeDebugger(InDebugger)
	, InstanceId(InInstanceId)
	, ViewRange(InViewRange)
{
	EventData = MakeShared<SStateTreeDebuggerEventTimelineView::FTimelineEventData>();
}

void FStateTreeDebuggerInstanceTrack::OnSelected()
{
	if (FStateTreeDebugger* Debugger = StateTreeDebugger.Get())
	{
		Debugger->SelectInstance(InstanceId);
	}
}

bool FStateTreeDebuggerInstanceTrack::UpdateInternal()
{
	const int32 PrevNumPoints = EventData->Points.Num();
	const int32 PrevNumWindows = EventData->Windows.Num();

	EventData->Points.SetNum(0, EAllowShrinking::No);
	EventData->Windows.SetNum(0);
	
	const FStateTreeDebugger* Debugger = StateTreeDebugger.Get();
	check(Debugger);
	const UStateTree* StateTree = Debugger->GetAsset();
	const UE::StateTreeDebugger::FInstanceEventCollection& EventCollection = Debugger->GetEventCollection(InstanceId);
	const double RecordingDuration = Debugger->GetRecordingDuration();
	
	if (StateTree != nullptr && EventCollection.IsValid())
	{
		auto MakeRandomColor = [bActive = !bIsStale](const uint32 InSeed)->FLinearColor
		{
			const FRandomStream Stream(InSeed);
			const uint8 Hue = static_cast<uint8>(Stream.FRand() * 255.0f);
			const uint8 SatVal = bActive ? 196 : 128;
			return FLinearColor::MakeFromHSV8(Hue, SatVal, SatVal);
		};
		
		const TConstArrayView<UE::StateTreeDebugger::FFrameSpan> Spans = EventCollection.FrameSpans;
		const TConstArrayView<FStateTreeTraceEventVariantType> Events = EventCollection.Events;
		const uint32 NumStateChanges = EventCollection.ActiveStatesChanges.Num();

		TArray<UE::StateTreeDebugger::FInstanceEventCollection::FContiguousTraceInfo> TracesInfo = EventCollection.ContiguousTracesData;
		// Append the on going trace info to "stopped" previous trace
		if (NumStateChanges > 0)
		{
			TracesInfo.Emplace(EventCollection.ActiveStatesChanges.Last().SpanIndex);
		}

		int32 StateChangeEndIndex = INDEX_NONE;
		for (int32 TraceIndex = 0; TraceIndex < TracesInfo.Num(); TraceIndex++)
		{
			UE::StateTreeDebugger::FInstanceEventCollection::FContiguousTraceInfo TraceInfo = TracesInfo[TraceIndex];
			// Start at first event for the first trace or from the end index of the previous trace 
			const int32 StateChangeBeginIndex = (StateChangeEndIndex == INDEX_NONE) ? 0 : StateChangeEndIndex;

			// Find the starting index of the next trace to stop our iteration
			StateChangeEndIndex = EventCollection.ActiveStatesChanges.IndexOfByPredicate(
				[LastSpanIndex = TraceInfo.LastSpanIndex](const UE::StateTreeDebugger::FInstanceEventCollection::FActiveStatesChangePair& Pair)
				{
					return Pair.SpanIndex > LastSpanIndex;
				});

			// When not found means we are processing the last (or the only) trace
			if (StateChangeEndIndex == INDEX_NONE)
			{
				StateChangeEndIndex = NumStateChanges;
			}

			for (int32 StateChangeIndex = StateChangeBeginIndex; StateChangeIndex < StateChangeEndIndex; ++StateChangeIndex)
			{
				const uint32 SpanIndex = EventCollection.ActiveStatesChanges[StateChangeIndex].SpanIndex;
				const uint32 EventIndex = EventCollection.ActiveStatesChanges[StateChangeIndex].EventIndex;
				const FStateTreeTraceActiveStatesEvent& Event = Events[EventIndex].Get<FStateTreeTraceActiveStatesEvent>();
				
				FString StatePath = Event.GetValueString(*StateTree);
				UE::StateTreeDebugger::FFrameSpan Span = EventCollection.FrameSpans[SpanIndex];
				SStateTreeDebuggerEventTimelineView::FTimelineEventData::EventWindow& Window = EventData->Windows.AddDefaulted_GetRef();
				Window.Color = MakeRandomColor(GetTypeHash(StatePath));
				Window.Description = FText::FromString(StatePath);
				Window.TimeStart = Span.GetWorldTimeStart();

				// For the last received event we use either the current recording duration if the instance is still active
				if (StateChangeIndex == (NumStateChanges - 1))
				{
					// or the last recorded frame time.
					Window.TimeEnd = Debugger->IsActiveInstance(RecordingDuration, InstanceId)
						? RecordingDuration
						: EventCollection.FrameSpans.Last().GetWorldTimeEnd();
				}
				else
				{
					// When there is another state change after the current one in the list we use it to close the window.
					// If the event is not the last of that specific trace then we use the start time of the next span.
					// Otherwise, we use the end time of the last frame that was part of that trace.
					const int32 NextStateChangeSpanIndex = EventCollection.ActiveStatesChanges[StateChangeIndex+1].SpanIndex;
					Window.TimeEnd = (StateChangeIndex < (StateChangeEndIndex - 1))
						? EventCollection.FrameSpans[NextStateChangeSpanIndex].GetWorldTimeStart()
						: EventCollection.FrameSpans[NextStateChangeSpanIndex - 1].GetWorldTimeEnd(); //TraceInfo.LastRecordingTime;
				}
			}
		}

		for (int32 SpanIndex = 0; SpanIndex < Spans.Num(); SpanIndex++)
		{
			const UE::StateTreeDebugger::FFrameSpan& Span = Spans[SpanIndex];

			const int32 StartIndex = Span.EventIdx;
			const int32 MaxIndex = (SpanIndex + 1 < Spans.Num()) ? Spans[SpanIndex+1].EventIdx : Events.Num();
			for (int EventIndex = StartIndex; EventIndex < MaxIndex; ++EventIndex)
			{
				if (Events[EventIndex].IsType<FStateTreeTraceLogEvent>())
				{
					SStateTreeDebuggerEventTimelineView::FTimelineEventData::EventPoint Point;
					Point.Time = Span.GetWorldTimeStart();
					Point.Color = FColorList::Salmon;
					EventData->Points.Add(Point);
				}
			}
		}
	}

	const bool bChanged = (PrevNumPoints != EventData->Points.Num() || PrevNumWindows != EventData->Windows.Num());

	// Tracks can be reactivated when multiple recordings are made in a single PIE session.
	if (bChanged && IsStale())
	{
		bIsStale = false;
	}

	return bChanged;
}

TSharedPtr<SWidget> FStateTreeDebuggerInstanceTrack::GetTimelineViewInternal()
{
	return SNew(SStateTreeDebuggerEventTimelineView)
		.ViewRange_Lambda([this](){ return ViewRange; })
		.EventData_Lambda([this](){ return EventData; });
}


//----------------------------------------------------------------------//
// FStateTreeDebuggerOwnerTrack
//----------------------------------------------------------------------//
FStateTreeDebuggerOwnerTrack::FStateTreeDebuggerOwnerTrack(const FText& InInstanceName)
	: FStateTreeDebuggerBaseTrack(FSlateIcon("StateTreeEditorStyle", "StateTreeEditor.Debugger.OwnerTrack", "StateTreeEditor.Debugger.OwnerTrack"), InInstanceName)
{
}

bool FStateTreeDebuggerOwnerTrack::UpdateInternal()
{
	bool bChanged = false;
	for (const TSharedPtr<FStateTreeDebuggerInstanceTrack>& Track : SubTracks)
	{
		bChanged = Track->Update() || bChanged;
	}

	return bChanged;
}

void FStateTreeDebuggerOwnerTrack::IterateSubTracksInternal(TFunction<void(TSharedPtr<FRewindDebuggerTrack> SubTrack)> IteratorFunction)
{
	for (TSharedPtr<FStateTreeDebuggerInstanceTrack>& Track : SubTracks)
	{
		IteratorFunction(Track);
	}
}

void FStateTreeDebuggerOwnerTrack::MarkAsStale()
{
	for (TSharedPtr<FStateTreeDebuggerInstanceTrack>& Track : SubTracks)
	{
		if (FStateTreeDebuggerBaseTrack* InstanceTrack = Track.Get())
		{
			InstanceTrack->MarkAsStale();
		}
	}
}

bool FStateTreeDebuggerOwnerTrack::IsStale() const
{
	// Considered stale only if all sub tracks are stale
	if (SubTracks.IsEmpty())
	{
		return false;
	}

	for (const TSharedPtr<FStateTreeDebuggerInstanceTrack>& Track : SubTracks)
	{
		const FStateTreeDebuggerBaseTrack* InstanceTrack = Track.Get();
		if (InstanceTrack && InstanceTrack->IsStale() == false)
		{
			return false;
		}
	}

	return true;
}

#endif // WITH_STATETREE_TRACE_DEBUGGER
