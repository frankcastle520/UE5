// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemTagNode.h"

// TraceInsights
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"

#define LOCTEXT_NAMESPACE "UE::Insights::MemoryProfiler::FMemTagNode"

namespace UE::Insights::MemoryProfiler
{

INSIGHTS_IMPLEMENT_RTTI(FMemTagNode)

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FMemTagNode::GetTrackerText() const
{
	FMemorySharedState* SharedState = FMemoryProfilerManager::Get()->GetSharedState();
	if (SharedState)
	{
		FMemoryTrackerId TrackerId = GetMemTrackerId();
		const FMemoryTracker* Tracker = SharedState->GetTrackerById(TrackerId);
		if (Tracker)
		{
			return FText::FromString(Tracker->GetName());
		}
	}

	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemTagNode::ResetAggregatedStats()
{
	//AggregatedStats = TraceServices::FMemoryProfilerAggregatedStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/*
void FMemTagNode::SetAggregatedStats(const TraceServices::FMemoryProfilerAggregatedStats& InAggregatedStats)
{
	AggregatedStats = InAggregatedStats;
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::MemoryProfiler

#undef LOCTEXT_NAMESPACE
