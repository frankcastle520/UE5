// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryTracker.h"

// TraceInsights
#include "Insights/InsightsManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTag.h"

namespace UE::Insights::MemoryProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMemoryTracker
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryTracker::FMemoryTracker(FMemoryTrackerId InTrackerId, const FString InTrackerName)
	: Id(InTrackerId)
	, Name(InTrackerName)
{
	//TODO: static_assert(FMemoryTracker::InvalidTrackerId == static_cast<FMemoryTracker>(FMemoryTrackerDesc::InvalidTrackerId), "Memory TrackerId type mismatch!");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryTracker::~FMemoryTracker()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::MemoryProfiler
