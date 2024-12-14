// Copyright Epic Games, Inc. All Rights Reserved.

#include "GpuProfilerTraceAnalysis.h"

#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "HAL/LowLevelMemTracker.h"

namespace TraceServices
{

FGpuProfilerAnalyzer::FGpuProfilerAnalyzer(FAnalysisSession& InSession, FTimingProfilerProvider& InTimingProfilerProvider)
	: Session(InSession)
	, TimingProfilerProvider(InTimingProfilerProvider)
{
}

void FGpuProfilerAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_EventSpec, "GpuProfiler", "EventSpec");
	Builder.RouteEvent(RouteId_Frame, "GpuProfiler", "Frame");
	Builder.RouteEvent(RouteId_Frame2, "GpuProfiler", "Frame2");
}

void FGpuProfilerAnalyzer::OnAnalysisEnd()
{
	if (NumFramesWithErrors > 0)
	{
		UE_LOG(LogTraceServices, Error, TEXT("[GpuProfiler] Frames with errors: %u"), NumFramesWithErrors);
	}

	if (NumFrames > 0 || EventTypeMap.Num() > 0)
	{
		UE_LOG(LogTraceServices, Log, TEXT("[GpuProfiler] Analysis completed (%u frames, %d timers)."), NumFrames, EventTypeMap.Num());
	}
}

bool FGpuProfilerAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Insights/FGpuProfilerAnalyzer"));

	FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;

	switch (RouteId)
	{
	case RouteId_EventSpec:
	{
		uint32 EventType = EventData.GetValue<uint32>("EventType");
		const auto& Name = EventData.GetArray<UTF16CHAR>("Name");

		auto NameTChar = StringCast<TCHAR>(Name.GetData(), Name.Num());
		uint32* TimerIndexPtr = EventTypeMap.Find(EventType);
		if (!TimerIndexPtr)
		{
			EventTypeMap.Add(EventType, TimingProfilerProvider.AddGpuTimer(FStringView(NameTChar.Get(), NameTChar.Length())));
		}
		else
		{
			TimingProfilerProvider.SetTimerName(*TimerIndexPtr, FStringView(NameTChar.Get(), NameTChar.Length()));
		}
		break;
	}
	case RouteId_Frame:
	case RouteId_Frame2:
	{
		TraceServices::FTimingProfilerProvider::TimelineInternal& ThisTimeline = (RouteId == RouteId_Frame) ?
			TimingProfilerProvider.EditGpuTimeline() :
			TimingProfilerProvider.EditGpu2Timeline();
		double& ThisMinTime = (RouteId == RouteId_Frame) ? MinTime : MinTime2;

		const auto& Data = EventData.GetArray<uint8>("Data");
		const uint8* BufferPtr = Data.GetData();
		const uint8* BufferEnd = BufferPtr + Data.Num();

		uint64 CalibrationBias = EventData.GetValue<uint64>("CalibrationBias");
		uint64 LastTimestamp = EventData.GetValue<uint64>("TimestampBase");
		uint32 RenderingFrameNumber = EventData.GetValue<uint32>("RenderingFrameNumber");

		++NumFrames;

		double LastTime = 0.0;
		uint32 CurrentDepth = 0;
		bool bHasErrors = false;

		while (BufferPtr < BufferEnd)
		{
			uint64 DecodedTimestamp = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint64 ActualTimestamp = (DecodedTimestamp >> 1) + LastTimestamp;
			LastTimestamp = ActualTimestamp;
			LastTime = double(ActualTimestamp + CalibrationBias) * 0.000001;
			LastTime += Context.EventTime.AsSeconds(0);

			if (LastTime < 0.0)
			{
				if (DecodedTimestamp & 1ull)
				{
					BufferPtr += sizeof(uint32);
				}
				bHasErrors = true;
				continue;
			}

			// The monolithic timeline assumes that timestamps are ever increasing, but
			// with gpu/cpu calibration and drift there can be a tiny bit of overlap between
			// frames. So we just clamp.
			if (ThisMinTime > LastTime)
			{
				LastTime = ThisMinTime;
			}
			ThisMinTime = LastTime;

			if (DecodedTimestamp & 1ull)
			{
				uint32 EventType = *reinterpret_cast<const uint32*>(BufferPtr);
				BufferPtr += sizeof(uint32);
				if (EventTypeMap.Contains(EventType))
				{
					FTimingProfilerEvent Event;
					Event.TimerIndex = EventTypeMap[EventType];
					ThisTimeline.AppendBeginEvent(LastTime, Event);
				}
				else
				{
					FTimingProfilerEvent Event;
					Event.TimerIndex = TimingProfilerProvider.AddGpuTimer(TEXTVIEW("<unknown>"));
					EventTypeMap.Add(EventType, Event.TimerIndex);
					ThisTimeline.AppendBeginEvent(LastTime, Event);
				}
				++CurrentDepth;
			}
			else
			{
				if (CurrentDepth > 0)
				{
					--CurrentDepth;
				}
				ThisTimeline.AppendEndEvent(LastTime);
			}
		}
		check(BufferPtr == BufferEnd);
		check(CurrentDepth == 0);
		if (bHasErrors && ++NumFramesWithErrors <= 100)
		{
			UE_LOG(LogTraceServices, Error, TEXT("[GpuProfiler] The rendering frame %u has invalid timestamps!"), RenderingFrameNumber);
		}
		Session.UpdateDurationSeconds(LastTime);
		break;
	}
	}

	return true;
}

} // namespace TraceServices
