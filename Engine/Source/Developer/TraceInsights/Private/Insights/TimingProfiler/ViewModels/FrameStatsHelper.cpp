// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameStatsHelper.h"

// TraceServices
#include "TraceServices/Model/TimingProfiler.h"

// TraceInsights
#include "Insights/InsightsManager.h"

namespace UE::Insights::TimingProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameStatsHelper::ComputeFrameStatsForTimer(TArray<FFrameStatsCachedEvent>& FrameStatsEvents, uint32 TimerId, const TSet<uint32>& Timelines)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		for (uint32 TimelineIndex : Timelines)
		{
			ProcessTimeline(FrameStatsEvents, TimerId, TimelineIndex);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameStatsHelper::ComputeFrameStatsForTimer(TArray<FFrameStatsCachedEvent>& FrameStatsEvents, uint32 TimerId)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Session->ReadAccessCheck();

		const TraceServices::ITimingProfilerProvider& TimingProfilerProvider = *TraceServices::ReadTimingProfilerProvider(*Session.Get());

		for (uint32 TimelineIndex = 0; TimelineIndex < TimingProfilerProvider.GetTimelineCount(); ++TimelineIndex)
		{
			ProcessTimeline(FrameStatsEvents, TimerId, TimelineIndex);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameStatsHelper::ProcessTimeline(TArray<FFrameStatsCachedEvent>& FrameStatsEvents, uint32 TimerId, uint32 TimelineIndex)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Session->ReadAccessCheck();

		const double SessionDuration = Session->GetDurationSeconds();

		const TraceServices::ITimingProfilerProvider& TimingProfilerProvider = *TraceServices::ReadTimingProfilerProvider(*Session.Get());

		const TraceServices::ITimingProfilerTimerReader* TimerReader;
		TimingProfilerProvider.ReadTimers([&TimerReader](const TraceServices::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		TimingProfilerProvider.ReadTimeline(TimelineIndex,
			[SessionDuration, &FrameStatsEvents, TimerReader, TimerId](const TraceServices::ITimingProfilerProvider::Timeline& Timeline)
			{
				struct TaskData
				{
					double StartTime;
					int32 NestedDepth = 0;
				};

				TArray<TaskData> DataArray;

				TraceServices::ITimeline<TraceServices::FTimingProfilerEvent>::EnumerateAsyncParams Params;
				Params.IntervalStart = 0;
				Params.IntervalEnd = SessionDuration;
				Params.Resolution = 0.0;
				Params.SetupCallback = [&DataArray](uint32 NumTasks) { DataArray.AddDefaulted(NumTasks); };
				Params.EventCallback = [TimerReader, &FrameStatsEvents, TimerId, &DataArray](bool bIsEnter, double Time, const TraceServices::FTimingProfilerEvent& Event, uint32 TaskIndex)
				{
					const TraceServices::FTimingProfilerTimer* Timer = TimerReader->GetTimer(Event.TimerIndex);
					if (ensure(Timer != nullptr))
					{
						if (Timer->Id == TimerId)
						{
							TaskData& CurrentTaskData = DataArray[TaskIndex];
							if (bIsEnter)
							{
								if (CurrentTaskData.NestedDepth == 0)
								{
									CurrentTaskData.StartTime = Time;
								}

								++CurrentTaskData.NestedDepth;
							}
							else
							{
								check(CurrentTaskData.NestedDepth > 0);
								if (--CurrentTaskData.NestedDepth > 0)
								{
									return TraceServices::EEventEnumerate::Continue;
								}

								int32 Index = Algo::UpperBoundBy(FrameStatsEvents, CurrentTaskData.StartTime, &FFrameStatsCachedEvent::FrameStartTime);
								if (Index > 0)
								{
									--Index;
								}

								// This can can happen when the event is between frames.
								if (CurrentTaskData.StartTime > FrameStatsEvents[Index].FrameEndTime)
								{
									Index++;
									if (Index >= FrameStatsEvents.Num())
									{
										return TraceServices::EEventEnumerate::Continue;
									}
								}

								double EndTime = Time;
								do
								{
									FFrameStatsCachedEvent& Entry = FrameStatsEvents[Index];

									if (EndTime < Entry.FrameStartTime)
									{
										return TraceServices::EEventEnumerate::Continue;
									}

									if (CurrentTaskData.StartTime < Entry.FrameStartTime)
									{
										CurrentTaskData.StartTime = Entry.FrameStartTime;
									}

									const double Duration = FMath::Min(EndTime, Entry.FrameEndTime) - CurrentTaskData.StartTime;
									ensure(Duration >= 0.0f);
									for (double Value = Entry.Duration.load(); !Entry.Duration.compare_exchange_strong(Value, Value + Duration););

									Index++;
								} while (Index < FrameStatsEvents.Num());
							}
						}
					}
					return TraceServices::EEventEnumerate::Continue;
				};

				Timeline.EnumerateEventsDownSampledAsync(Params);
			});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler
