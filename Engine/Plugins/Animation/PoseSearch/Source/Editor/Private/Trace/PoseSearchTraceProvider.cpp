// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTraceProvider.h"

namespace UE::PoseSearch
{

const FName FTraceProvider::ProviderName("PoseSearchTraceProvider");

FTraceProvider::FTraceProvider(TraceServices::IAnalysisSession& InSession) : Session(InSession)
{
}

bool FTraceProvider::ReadMotionMatchingStateTimeline(uint64 InAnimInstanceId, int32 InSearchId, TFunctionRef<void(const FMotionMatchingStateTimeline&)> Callback) const
{
	Session.ReadAccessCheck();
	return MotionMatchingStateTimelineStorage.ReadTimeline(InAnimInstanceId, InSearchId, Callback);
}

bool FTraceProvider::EnumerateMotionMatchingStateTimelines(uint64 InAnimInstanceId, TFunctionRef<void(const FMotionMatchingStateTimeline&)> Callback) const
{
	Session.ReadAccessCheck();
	return MotionMatchingStateTimelineStorage.EnumerateSearchTimelines(InAnimInstanceId, Callback);
}

void FTraceProvider::AppendMotionMatchingState(const FTraceMotionMatchingStateMessage& InMessage, double InTime)
{
	Session.WriteAccessCheck();

	TSharedRef<TraceServices::TPointTimeline<FTraceMotionMatchingStateMessage>> Timeline = MotionMatchingStateTimelineStorage.GetTimeline(Session, InMessage.AnimInstanceId, InMessage.GetSearchId());
	Timeline->AppendEvent(InTime, InMessage);
	
	Session.UpdateDurationSeconds(InTime);
}

} // namespace UE::PoseSearch
