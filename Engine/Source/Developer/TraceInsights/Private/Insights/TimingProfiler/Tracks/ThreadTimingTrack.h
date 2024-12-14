// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

// TraceServices
#include "TraceServices/Model/TimingProfiler.h"

// TraceInsights
#include "Insights/ViewModels/TimingEventSearch.h" // for TTimingEventSearchCache
#include "Insights/ViewModels/TimingEventsTrack.h"

class FThreadTrackEvent;
class FTimingEventSearchParameters;

namespace UE::Insights { class FFilterConfigurator; }

namespace UE::Insights::TimingProfiler
{

class FThreadTimingSharedState;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FThreadTimingTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FThreadTimingTrack, FTimingEventsTrack)

public:
	typedef typename TraceServices::ITimeline<TraceServices::FTimingProfilerEvent>::FTimelineEventInfo TimelineEventInfo;

	struct FPendingEventInfo
	{
		double StartTime;
		double EndTime;
		uint32 Depth;
		uint32 TimerIndex;
	};

	explicit FThreadTimingTrack(FThreadTimingSharedState& InSharedState, const FString& InName, const TCHAR* InGroupName, uint32 InTimelineIndex, uint32 InThreadId)
		: FTimingEventsTrack(InName)
		, SharedState(InSharedState)
		, GroupName(InGroupName)
		, TimelineIndex(InTimelineIndex)
		, ThreadId(InThreadId)
	{
	}

	virtual ~FThreadTimingTrack() override {}

	const TCHAR* GetGroupName() const { return GroupName; };

	uint32 GetTimelineIndex() const { return TimelineIndex; }
	//void SetTimelineIndex(uint32 InTimelineIndex) { TimelineIndex = InTimelineIndex; }

	uint32 GetThreadId() const { return ThreadId; }
	//void SetThreadId(uint32 InThreadId) { ThreadId = InThreadId; }

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual void BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;

	virtual void PostDraw(const ITimingTrackDrawContext& Context) const override;

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	virtual const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;

	virtual void UpdateEventStats(ITimingEvent& InOutEvent) const override;
	virtual void OnEventSelected(const ITimingEvent& InSelectedEvent) const override;
	virtual void OnClipboardCopyEvent(const ITimingEvent& InSelectedEvent) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

	int32 GetDepthAt(double Time) const;

	virtual void SetFilterConfigurator(TSharedPtr<FFilterConfigurator> InFilterConfigurator) override;

	TSharedPtr<const ITimingEvent> FindMaxEventInstance(uint32 TimerId, double StartTime, double EndTime) const;
	TSharedPtr<const ITimingEvent> FindMinEventInstance(uint32 TimerId, double StartTime, double EndTime) const;

protected:
	virtual bool HasCustomFilter() const override;

private:
	bool FindTimingProfilerEvent(const FThreadTrackEvent& InTimingEvent, TFunctionRef<void(double, double, uint32, const TraceServices::FTimingProfilerEvent&)> InFoundPredicate) const;
	bool FindTimingProfilerEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const TraceServices::FTimingProfilerEvent&)> InFoundPredicate) const;

	void GetParentAndRoot(const FThreadTrackEvent& TimingEvent,
						  TSharedPtr<FThreadTrackEvent>& OutParentTimingEvent,
						  TSharedPtr<FThreadTrackEvent>& OutRootTimingEvent) const;

	static void CreateFThreadTrackEventFromInfo(const TimelineEventInfo& InEventInfo, const TSharedRef<const FBaseTimingTrack> InTrack, int32 InDepth, TSharedPtr<FThreadTrackEvent> &OutTimingEvent);
	static bool TimerIndexToTimerId(uint32 InTimerIndex, uint32 & OutTimerId);

private:
	FThreadTimingSharedState& SharedState;

	TSharedPtr<FFilterConfigurator> FilterConfigurator;

	const TCHAR* GroupName = nullptr;
	uint32 TimelineIndex = 0;
	uint32 ThreadId = 0;

	// Search cache
	mutable TTimingEventSearchCache<TraceServices::FTimingProfilerEvent> SearchCache;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FCpuTimingTrack : public FThreadTimingTrack
{
public:
	explicit FCpuTimingTrack(FThreadTimingSharedState& InSharedState, const FString& InName, const TCHAR* InGroupName, uint32 InTimelineIndex, uint32 InThreadId)
		: FThreadTimingTrack(InSharedState, InName, InGroupName, InTimelineIndex, InThreadId)
	{
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FGpuTimingTrack : public FThreadTimingTrack
{
public:
	static constexpr uint32 Gpu1ThreadId = uint32('GPU1');
	static constexpr uint32 Gpu2ThreadId = uint32('GPU2');

public:
	explicit FGpuTimingTrack(FThreadTimingSharedState& InSharedState, const FString& InName, const TCHAR* InGroupName, uint32 InTimelineIndex, uint32 InThreadId)
		: FThreadTimingTrack(InSharedState, InName, InGroupName, InTimelineIndex, InThreadId)
	{
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler
