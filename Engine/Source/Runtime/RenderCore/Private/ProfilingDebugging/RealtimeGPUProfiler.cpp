// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RHI.h"
#include "RenderCore.h"
#include "RenderingThread.h"
#include "GPUProfiler.h"
#include "Misc/ScopeRWLock.h"

#if RHI_NEW_GPU_PROFILER

// @todo - new gpu profiler

#elif HAS_GPU_STATS

typedef TArray<TCHAR, TInlineAllocator<4096u>> FDescriptionStringBuffer;

static TAutoConsoleVariable<int> CVarGPUStatsEnabled(
	TEXT("r.GPUStatsEnabled"),
	1,
	TEXT("Enables or disables GPU stat recording"));

static TAutoConsoleVariable<int> CVarGPUTracingStatsEnabled(
	TEXT("r.GPUTracingStatsEnabled"),
	1,
	TEXT("Enables or disables GPU stat recording to tracing profiler"));

static TAutoConsoleVariable<int> CVarGPUStatsChildTimesIncluded(
	TEXT("r.GPUStatsChildTimesIncluded"),
	0,
	TEXT("If this is enabled, the child stat timings will be included in their parents' times.\n")
	TEXT("This presents problems for non-hierarchical stats if we're expecting them to add up\n")
	TEXT("to the total GPU time, so we probably want this disabled.\n")
);

static const uint64 InvalidQueryResult = 0xFFFFFFFFFFFFFFFFull;

/*-----------------------------------------------------------------------------
FRealTimeGPUProfilerEvent class
-----------------------------------------------------------------------------*/
class FRealtimeGPUProfilerEvent
{
public:
	FRealtimeGPUProfilerEvent(FRHIRenderQueryPool& RenderQueryPool)
		: StartResultMicroseconds(InPlace, InvalidQueryResult)
		, EndResultMicroseconds(InPlace, InvalidQueryResult)
		, StartQuery(RenderQueryPool.AllocateQuery())
		, EndQuery(RenderQueryPool.AllocateQuery())
		, FrameNumber(-1)
		, DescriptionLength(0)
#if DO_CHECK || USING_CODE_ANALYSIS
		, bInsideQuery(false)
#endif
	{
		check(StartQuery.IsValid() && EndQuery.IsValid());
	}

	FRealtimeGPUProfilerQuery Begin(FRHIGPUMask InGPUMask, const FName& NewName, const TStatId& NewStat)
	{
		check(IsInParallelRenderingThread());
		check(!bInsideQuery && StartQuery.IsValid());
#if DO_CHECK || USING_CODE_ANALYSIS
		bInsideQuery = true;
#endif
		GPUMask = InGPUMask;

		Name = NewName;
		STAT(Stat = NewStat;)
		StartResultMicroseconds = TStaticArray<uint64, MAX_NUM_GPUS>(InPlace, InvalidQueryResult);
		EndResultMicroseconds = TStaticArray<uint64, MAX_NUM_GPUS>(InPlace, InvalidQueryResult);
		FrameNumber = GFrameNumberRenderThread;

		bStarted = false;
		bEnded = false;
		bDiscarded = false;

		return FRealtimeGPUProfilerQuery(GPUMask, StartQuery.GetQuery(), this);
	}

	FRealtimeGPUProfilerQuery End()
	{
		check(IsInParallelRenderingThread());
		check(bInsideQuery && EndQuery.IsValid());
#if DO_CHECK || USING_CODE_ANALYSIS
		bInsideQuery = false;
#endif

		return FRealtimeGPUProfilerQuery(GPUMask, EndQuery.GetQuery(), this);
	}

	bool GatherQueryResults(FRHICommandListImmediate& RHICmdList)
	{
		//QUICK_SCOPE_CYCLE_COUNTER(STAT_SceneUtils_GatherQueryResults);
		bool bUsed = bStarted && bEnded;
		if (bDiscarded || !bUsed)
		{
			for (uint32 GPUIndex : GPUMask)
			{
				StartResultMicroseconds[GPUIndex] = 0;
				EndResultMicroseconds[GPUIndex] = 0;
			}

			return bDiscarded;
		}

		// Get the query results which are still outstanding
		check(StartQuery.IsValid() && EndQuery.IsValid());

		for (uint32 GPUIndex : GPUMask)
		{
			if (StartResultMicroseconds[GPUIndex] == InvalidQueryResult)
			{
				if (!RHIGetRenderQueryResult(StartQuery.GetQuery(), StartResultMicroseconds[GPUIndex], false, GPUIndex))
				{
					StartResultMicroseconds[GPUIndex] = InvalidQueryResult;
				}
			}

			if (EndResultMicroseconds[GPUIndex] == InvalidQueryResult)
			{
				if (!RHIGetRenderQueryResult(EndQuery.GetQuery(), EndResultMicroseconds[GPUIndex], false, GPUIndex))
				{
					EndResultMicroseconds[GPUIndex] = InvalidQueryResult;
				}
			}
		}

		return HasValidResult();
	}

	uint64 GetResultUs(uint32 GPUIndex) const
	{
		check(HasValidResult(GPUIndex));

		if (StartResultMicroseconds[GPUIndex] > EndResultMicroseconds[GPUIndex])
		{
			return 0llu;
		}

		return EndResultMicroseconds[GPUIndex] - StartResultMicroseconds[GPUIndex];
	}

	bool HasValidResult(uint32 GPUIndex) const
	{
		return StartResultMicroseconds[GPUIndex] != InvalidQueryResult && EndResultMicroseconds[GPUIndex] != InvalidQueryResult;
	}

	bool HasValidResult() const
	{
		for (uint32 GPUIndex : GPUMask)
		{
			if (!HasValidResult(GPUIndex))
			{
				return false;
			}
		}
		return true;
	}

#if STATS
	FName GetStatName() const
	{
		return Stat.GetName();
	}
#endif

	const FName& GetName() const
	{
		return Name;
	}

	const TCHAR* GetDescription(const FDescriptionStringBuffer& DescriptionStringBuffer, uint32& OutDescriptionLength) const
	{
		OutDescriptionLength = DescriptionLength;
		return DescriptionLength ? &DescriptionStringBuffer[DescriptionOffset] : TEXT("");
	}

	void SetDescription(const TCHAR* Description, FDescriptionStringBuffer& DescriptionStringBuffer)
	{
		check(Description);
		uint32 TestDescriptionLength = FCString::Strlen(Description);
		if (TestDescriptionLength && (DescriptionStringBuffer.Num() + TestDescriptionLength <= UINT16_MAX))
		{
			DescriptionLength = (uint16)TestDescriptionLength;
			DescriptionOffset = (uint16)DescriptionStringBuffer.Num();
			DescriptionStringBuffer.AddUninitialized(DescriptionLength);
			FMemory::Memcpy(&DescriptionStringBuffer[DescriptionOffset], Description, DescriptionLength * sizeof(TCHAR));
		}
		else
		{
			ClearDescription();
		}
	}

	void ClearDescription()
	{
		DescriptionLength = 0;
		DescriptionOffset = 0;
	}

	FRHIGPUMask GetGPUMask() const
	{
		return GPUMask;
	}

	uint64 GetStartResultMicroseconds(uint32 GPUIndex) const
	{
		return StartResultMicroseconds[GPUIndex];
	}

	uint64 GetEndResultMicroseconds(uint32 GPUIndex) const
	{
		return EndResultMicroseconds[GPUIndex];
	}

	uint32 GetFrameNumber() const
	{
		return FrameNumber;
	}

	static constexpr uint32 GetNumRHIQueriesPerEvent()
	{
		return 2u;
	}

	bool IsDiscarded() const
	{
		return bDiscarded;
	}

	TStaticArray<uint64, MAX_NUM_GPUS> StartResultMicroseconds;
	TStaticArray<uint64, MAX_NUM_GPUS> EndResultMicroseconds;

private:
	FRHIPooledRenderQuery StartQuery;
	FRHIPooledRenderQuery EndQuery;

	// Flags to indicate if both halves of the query were actually submitted.
	std::atomic<bool> bStarted { false };
	std::atomic<bool> bEnded   { false };

	// True when this profiler event will never be submitted, and therefore will never have valid data.
	std::atomic<bool> bDiscarded{ false };

	FName Name;
	STAT(TStatId Stat;)

	FRHIGPUMask GPUMask;

	uint32 FrameNumber;

	uint16 DescriptionOffset;		// Offset in DescriptionStringBuffer
	uint16 DescriptionLength;

#if DO_CHECK || USING_CODE_ANALYSIS
	bool bInsideQuery;
#endif

	friend FRealtimeGPUProfilerQuery;
};

void FRealtimeGPUProfilerQuery::Submit(FRHICommandList& RHICmdList, bool bBegin) const
{
	if (Query)
	{
		SCOPED_GPU_MASK(RHICmdList, GPUMask);
		RHICmdList.EndRenderQuery(Query);

		if (bBegin)
		{
			Parent->bStarted = true;
		}
		else
		{
			Parent->bEnded = true;
		}
	}
}

void FRealtimeGPUProfilerQuery::Discard(bool bBegin)
{
	if (Query)
	{
		if (bBegin && !Parent->bStarted)
		{
			Parent->bDiscarded = true;
		}
		else if (!bBegin && !Parent->bEnded)
		{
			Parent->bDiscarded = true;
		}
	}
}

#if GPUPROFILERTRACE_ENABLED
void TraverseEventTree(
	const TIndirectArray<FRealtimeGPUProfilerEvent, TInlineAllocator<100u>>& GpuProfilerEvents,
	const TArray<TArray<int32>>& GpuProfilerEventChildrenIndices,
	const FDescriptionStringBuffer& DescriptionStringBuffer,
	int32 Root,
	uint32 GPUIndex)
{
	uint64 lastStartTime = 0;
	uint64 lastEndTime = 0;

	FName EventName;

	if (Root != 0)
	{
		uint32 DescriptionLength;
		const TCHAR* DescriptionData = GpuProfilerEvents[Root].GetDescription(DescriptionStringBuffer, DescriptionLength);
		if (DescriptionLength)
		{
			FString NameWithDescription;
			NameWithDescription = GpuProfilerEvents[Root].GetName().ToString();
			NameWithDescription.Append(TEXT(" - "));
			NameWithDescription.AppendChars(DescriptionData, DescriptionLength);

			EventName = FName(NameWithDescription);
		}
		else
		{
			EventName = GpuProfilerEvents[Root].GetName();
		}

		// Since the GpuProfiler uses the Comparison Index of FName, Gpu trace events named with the pattern <base>_N where N
		// is some non-negative integer, will all end up having the same name in Unreal Insights. Appending a space to the name
		// avoids this.
		if (EventName.GetNumber())
		{
			EventName = FName(EventName.ToString() + TEXT(" "));

			checkSlow(EventName.GetNumber() == 0);
		}

		check(GpuProfilerEvents[Root].GetGPUMask().Contains(GPUIndex));
		FGpuProfilerTrace::SpecifyEventByName(EventName);
		FGpuProfilerTrace::BeginEventByName(EventName, GpuProfilerEvents[Root].GetFrameNumber(), GpuProfilerEvents[Root].GetStartResultMicroseconds(GPUIndex));
	}

	for (int32 Subroot : GpuProfilerEventChildrenIndices[Root])
	{
		// Multi-GPU support : FGpuProfilerTrace is not yet MGPU-aware.
		if (GpuProfilerEvents[Subroot].GetGPUMask().Contains(GPUIndex))
		{
			check(GpuProfilerEvents[Subroot].GetStartResultMicroseconds(GPUIndex) >= lastEndTime);
			lastStartTime = GpuProfilerEvents[Subroot].GetStartResultMicroseconds(GPUIndex);
			lastEndTime = GpuProfilerEvents[Subroot].GetEndResultMicroseconds(GPUIndex);
			check(lastStartTime <= lastEndTime);
			if (Root != 0)
			{
				check(GpuProfilerEvents[Root].GetGPUMask().Contains(GPUIndex));
				check(lastStartTime >= GpuProfilerEvents[Root].GetStartResultMicroseconds(GPUIndex));
				check(lastEndTime <= GpuProfilerEvents[Root].GetEndResultMicroseconds(GPUIndex));
			}
			TraverseEventTree(GpuProfilerEvents, GpuProfilerEventChildrenIndices, DescriptionStringBuffer, Subroot, GPUIndex);
		}
	}

	if (Root != 0)
	{
		check(GpuProfilerEvents[Root].GetGPUMask().Contains(GPUIndex));
		FGpuProfilerTrace::SpecifyEventByName(EventName);
		FGpuProfilerTrace::EndEvent(GpuProfilerEvents[Root].GetEndResultMicroseconds(GPUIndex));
	}
}
#endif

/*-----------------------------------------------------------------------------
FRealtimeGPUProfilerFrame class
Container for a single frame's GPU stats
-----------------------------------------------------------------------------*/
class FRealtimeGPUProfilerFrame
{
public:
	FRealtimeGPUProfilerFrame(FRHIRenderQueryPool* InRenderQueryPool)
		: RenderQueryPool(InRenderQueryPool)
	{
		GpuProfilerEvents.Empty(GPredictedMaxNumEvents);
		for (uint32 Idx = 0; Idx < GPredictedMaxNumEvents; ++Idx)
		{
			GpuProfilerEvents.Add(new FRealtimeGPUProfilerEvent(*RenderQueryPool));
		}

		GpuProfilerEventParentIndices.Empty(GPredictedMaxNumEvents);
		GpuProfilerEventParentIndices.AddUninitialized();

		EventStack.Empty(GPredictedMaxStackDepth);
		EventStack.Add(0);

		EventAggregates.Empty(GPredictedMaxNumEvents);
		EventAggregates.AddUninitialized();

		DescriptionStringBuffer.Empty();

		CPUFrameStartTimestamp = FPlatformTime::Cycles64();
	}

	~FRealtimeGPUProfilerFrame() = default;

	FRealtimeGPUProfilerQuery PushEvent(FRHIGPUMask GPUMask, const FName& Name, const TStatId& Stat, const TCHAR* Description)
	{
		if (NextEventIdx >= GpuProfilerEvents.Num())
		{
			GpuProfilerEvents.Add(new FRealtimeGPUProfilerEvent(*RenderQueryPool));
		}

		const int32 EventIdx = NextEventIdx++;

		GpuProfilerEventParentIndices.Add(EventStack.Last());
		EventStack.Push(EventIdx);
		if (Description)
		{
			GpuProfilerEvents[EventIdx].SetDescription(Description, DescriptionStringBuffer);
		}
		else
		{
			GpuProfilerEvents[EventIdx].ClearDescription();
		}
		return GpuProfilerEvents[EventIdx].Begin(GPUMask, Name, Stat);
	}

	FRealtimeGPUProfilerQuery PopEvent()
	{
		const int32 EventIdx = EventStack.Pop(EAllowShrinking::No);

		return GpuProfilerEvents[EventIdx].End();
	}

	bool UpdateStats(FRHICommandListImmediate& RHICmdList
#if GPUPROFILERTRACE_ENABLED
		, FRealtimeGPUProfilerHistoryByDescription& HistoryByDescription
#endif
		)
	{
		// Gather any remaining results and check all the results are ready
		for (; NextResultPendingEventIdx < NextEventIdx; ++NextResultPendingEventIdx)
		{
			FRealtimeGPUProfilerEvent& Event = GpuProfilerEvents[NextResultPendingEventIdx];

			if (!Event.GatherQueryResults(RHICmdList))
			{
				// The frame isn't ready yet. Don't update stats - we'll try again next frame. 
				return false;
			}

			FGPUEventTimeAggregate Aggregate;
			// Multi-GPU support : Tracing profiler is MGPU-aware, but not CSV profiler or Unreal stats.
			Aggregate.InclusiveTimeUs = Event.GetGPUMask().Contains(0) ? (uint32)Event.GetResultUs(0) : 0;
			Aggregate.ExclusiveTimeUs = Aggregate.InclusiveTimeUs;
			EventAggregates.Add(Aggregate);
		}

		// Calculate inclusive and exclusive time for all events
		for (int32 EventIdx = 1; EventIdx < GpuProfilerEventParentIndices.Num(); ++EventIdx)
		{
			const int32 ParentIdx = GpuProfilerEventParentIndices[EventIdx];

			EventAggregates[ParentIdx].ExclusiveTimeUs -= EventAggregates[EventIdx].InclusiveTimeUs;
		}

		// Update the stats
#if CSV_PROFILER_STATS
		static IConsoleVariable* CVarGPUCsvStatsEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUCsvStatsEnabled"));
		const bool bCsvStatsEnabled = CVarGPUCsvStatsEnabled ? CVarGPUCsvStatsEnabled->GetBool() : false;
		FCsvProfiler* CsvProfiler = bCsvStatsEnabled ? FCsvProfiler::Get() : nullptr;
#else
		const bool bCsvStatsEnabled = false;
#endif
		const bool GPUStatsChildTimesIncluded = !!CVarGPUStatsChildTimesIncluded.GetValueOnRenderThread();
		uint64 TotalUs = 0llu;
		FNameSet StatSeenSet;

		for (int32 Idx = 1; Idx < NextEventIdx; ++Idx)
		{
			const FRealtimeGPUProfilerEvent& Event = GpuProfilerEvents[Idx];
			const FGPUEventTimeAggregate IncExcTime = EventAggregates[Idx];

			// Multi-GPU support : Tracing profiler is MGPU-aware, but not CSV profiler or Unreal stats.
			if (Event.GetGPUMask().Contains(0))
			{
				// Check if we've seen this stat yet 
				const bool bKnownStat = StatSeenSet.Add(Event.GetName());

				const int64 EventTimeUs = GPUStatsChildTimesIncluded ? IncExcTime.InclusiveTimeUs : IncExcTime.ExclusiveTimeUs;
				TotalUs += IncExcTime.ExclusiveTimeUs;

#if STATS
				const double EventTimeMs = EventTimeUs / 1000.;
				if (bKnownStat)
				{
					FThreadStats::AddMessage(Event.GetStatName(), EStatOperation::Add, EventTimeMs);
					TRACE_STAT_ADD(Event.GetStatName(), EventTimeMs);
				}
				else
				{
					FThreadStats::AddMessage(Event.GetStatName(), EStatOperation::Set, EventTimeMs);
					TRACE_STAT_SET(Event.GetStatName(), EventTimeMs);
				}
#endif

#if CSV_PROFILER_STATS
				if (CsvProfiler)
				{
					const ECsvCustomStatOp CsvStatOp = bKnownStat ? ECsvCustomStatOp::Accumulate : ECsvCustomStatOp::Set;
					CsvProfiler->RecordCustomStat(Event.GetName(), CSV_CATEGORY_INDEX(GPU), EventTimeUs / 1000.f, CsvStatOp);
				}
#endif
			}
		}

		extern RHI_API void RHISetGPUStatTotals(bool bCsvStatsEnabled, double TotalMs);
		RHISetGPUStatTotals(bCsvStatsEnabled, TotalUs / 1000.0);

#if GPUPROFILERTRACE_ENABLED
		TArray<TArray<int32>> GpuProfilerEventChildrenIndices;
		GpuProfilerEventChildrenIndices.AddDefaulted(NextEventIdx);

		for (int32 EventIdx = 1; EventIdx < GpuProfilerEventParentIndices.Num(); ++EventIdx)
		{
			const int32 ParentIdx = GpuProfilerEventParentIndices[EventIdx];

			GpuProfilerEventChildrenIndices[ParentIdx].Add(EventIdx);
		}

		FGPUTimingCalibrationTimestamp Timestamps[MAX_NUM_GPUS];
		FMemory::Memzero(Timestamps);

		for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; ++GPUIndex)
		{
			FGPUTimingCalibrationTimestamp& Timestamp = Timestamps[GPUIndex];

			if (TimestampCalibrationQuery.IsValid())
			{
				Timestamp.GPUMicroseconds = TimestampCalibrationQuery->GPUMicroseconds[GPUIndex];
				Timestamp.CPUMicroseconds = TimestampCalibrationQuery->CPUMicroseconds[GPUIndex];
			}

			if (Timestamp.GPUMicroseconds == 0 || Timestamp.CPUMicroseconds == 0) // Unimplemented platforms, or invalid on the first frame
			{
				bool bSuccess = false;
				for (int32 EventIdx = 1; EventIdx < NextEventIdx; ++EventIdx)
				{
					if (!GpuProfilerEvents[EventIdx].IsDiscarded())
					{
						// Align CPU and GPU frames
						Timestamp.GPUMicroseconds = GpuProfilerEvents[EventIdx].GetStartResultMicroseconds(GPUIndex);
						Timestamp.CPUMicroseconds = static_cast<uint64>(FPlatformTime::ToSeconds64(CPUFrameStartTimestamp) * 1000 * 1000);
						bSuccess = true;
						break;	
					}
				}

				if (!bSuccess)
				{
					// Fallback to legacy
					Timestamp = FGPUTiming::GetCalibrationTimestamp();
				}
			}
		}		

		// Sanitize event start/end times
		TArray<TStaticArray<uint64, MAX_NUM_GPUS>> LastEndTimes;
		LastEndTimes.AddZeroed(NextEventIdx);

		for (int32 EventIdx = 1; EventIdx < GpuProfilerEventParentIndices.Num(); ++EventIdx)
		{
			const int32 ParentIdx = GpuProfilerEventParentIndices[EventIdx];
			FRealtimeGPUProfilerEvent& Event = GpuProfilerEvents[EventIdx];
			
			for (uint32 GPUIndex : Event.GetGPUMask())
			{
				// Start time must be >= last end time
				Event.StartResultMicroseconds[GPUIndex] = FMath::Max(Event.StartResultMicroseconds[GPUIndex], LastEndTimes[ParentIdx][GPUIndex]);
				// End time must be >= start time
				Event.EndResultMicroseconds[GPUIndex] = FMath::Max(Event.StartResultMicroseconds[GPUIndex], Event.EndResultMicroseconds[GPUIndex]);

				if (ParentIdx != 0)
				{
					FRealtimeGPUProfilerEvent& EventParent = GpuProfilerEvents[ParentIdx];

					// Clamp start/end times to be inside parent start/end times
					Event.StartResultMicroseconds[GPUIndex] = FMath::Clamp(Event.StartResultMicroseconds[GPUIndex],
						EventParent.StartResultMicroseconds[GPUIndex],
						EventParent.EndResultMicroseconds[GPUIndex]);
					Event.EndResultMicroseconds[GPUIndex] = FMath::Clamp(Event.EndResultMicroseconds[GPUIndex],
						Event.StartResultMicroseconds[GPUIndex],
						EventParent.EndResultMicroseconds[GPUIndex]);
				}

				// Update last end time for this parent
				LastEndTimes[ParentIdx][GPUIndex] = Event.EndResultMicroseconds[GPUIndex];
			}
		}

		for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; ++GPUIndex)
		{
			FGpuProfilerTrace::BeginFrame(Timestamps[GPUIndex]);
			TraverseEventTree(GpuProfilerEvents, GpuProfilerEventChildrenIndices, DescriptionStringBuffer, 0, GPUIndex);
			FGpuProfilerTrace::EndFrame(GPUIndex);
		}

		// Logic to track performance by description for root level items.  For example, if rendering multiple view families via
		// UDisplayClusterViewportClient, each view family will have a description, and clients may want to turn render features
		// on or off per view family to tune performance, or select which GPU each view family renders on to balance performance.
		// The regular GPU render stats screen shows the sum of performance across all view families, and across all GPUs, which
		// isn't terribly useful for this purpose.  The alternative would be to use Unreal Insights, but it takes a lot of work
		// to get clean measurements there due to noise, which history averaging smooths out (plus it requires more knowledge to
		// know how to interpret Insights).
		{
			FRWScopeLock Lock(HistoryByDescription.Mutex, SLT_Write);

			// To clean up old descriptions, we first want to mark all existing descriptions as not updated this frame.
			for (auto Iterator = HistoryByDescription.History.CreateIterator(); Iterator; ++Iterator)
			{
				Iterator.Value().UpdatedThisFrame = false;
			}

			// Then scan for root items with descriptions and add history entries for them
			for (int32 Subroot : GpuProfilerEventChildrenIndices[0])
			{
				uint32 DescriptionLength;
				const TCHAR* DescriptionData = GpuProfilerEvents[Subroot].GetDescription(DescriptionStringBuffer, DescriptionLength);

				if (DescriptionLength)
				{
					FString Description;
					Description.AppendChars(DescriptionData, DescriptionLength);

					FRealtimeGPUProfilerHistoryItem& HistoryItem = HistoryByDescription.History.FindOrAdd(Description);

					// We could have more than one root entry for a given view -- advance history and subtract out previously accumulated time
					// the first time the given item is accessed on a frame, then accumulate from there.
					uint64* HistoryTime;
					if (!HistoryItem.UpdatedThisFrame)
					{
						HistoryItem.UpdatedThisFrame = true;
						HistoryItem.LastGPUMask = GpuProfilerEvents[Subroot].GetGPUMask();

						HistoryItem.NextWriteIndex++;
						HistoryTime = &HistoryItem.Times[(HistoryItem.NextWriteIndex - 1) % FRealtimeGPUProfilerHistoryItem::HistoryCount];

						HistoryItem.AccumulatedTime -= *HistoryTime;
						*HistoryTime = 0;
					}
					else
					{
						HistoryTime = &HistoryItem.Times[(HistoryItem.NextWriteIndex - 1) % FRealtimeGPUProfilerHistoryItem::HistoryCount];
					}

					// If multiple GPU masks, get the one with the largest time span
					uint64 MaxGpuTime = 0;

					for (uint32 GPUIndex : GpuProfilerEvents[Subroot].GetGPUMask())
					{
						MaxGpuTime = FMath::Max(MaxGpuTime, GpuProfilerEvents[Subroot].GetEndResultMicroseconds(GPUIndex) - GpuProfilerEvents[Subroot].GetStartResultMicroseconds(GPUIndex));
					}

					// Add that to the accumulated and history result
					HistoryItem.AccumulatedTime += MaxGpuTime;
					*HistoryTime += MaxGpuTime;
				}
			}

			// Finally, clean up any items that weren't updated this frame
			for (auto Iterator = HistoryByDescription.History.CreateIterator(); Iterator; ++Iterator)
			{
				if (!Iterator.Value().UpdatedThisFrame)
				{
					Iterator.RemoveCurrent();
				}
			}
		}
#endif

		return true;
	}

	uint64 CPUFrameStartTimestamp;
	FTimestampCalibrationQueryRHIRef TimestampCalibrationQuery;

private:
	struct FGPUEventTimeAggregate
	{
		int64 ExclusiveTimeUs;
		int64 InclusiveTimeUs;
	};

	static constexpr uint32 GPredictedMaxNumEvents = 100u;
	static constexpr uint32 GPredictedMaxNumEventsUpPow2 = 128u;
	static constexpr uint32 GPredictedMaxStackDepth = 32u;

	class FNameSet
	{
	public:
		FNameSet()
			: NumElements(0)
			, Capacity(GInitialCapacity)
			, SecondaryStore(nullptr)
		{
			FMemory::Memset(InlineStore, 0, GInitialCapacity * sizeof(FName));
		}

		~FNameSet()
		{
			if (SecondaryStore)
			{
				FMemory::Free(SecondaryStore);
				SecondaryStore = nullptr;
			}
		}

		// @return Whether Name is already in set
		bool Add(const FName& Name)
		{
			check(Name != NAME_None);

			if (NumElements * GResizeDivFactor > Capacity)
			{
				uint32 NewCapacity = Capacity;

				do
				{
					NewCapacity *= 2u;
				} while (NumElements * GResizeDivFactor > NewCapacity);

				Resize(NewCapacity);
			}

			FName* NameStore = GetNameStore();
			const uint32 NameHash = GetTypeHash(Name);
			const uint32 Mask = Capacity - 1;
			uint32 Idx = NameHash & Mask;
			uint32 Probe = 1;
			const FName NameNone = NAME_None;

			while (NameNone != NameStore[Idx] && Name != NameStore[Idx])
			{
				Idx = (Idx + Probe) & Mask;
				Probe++;
			}

			if (NameNone != NameStore[Idx])
			{
				return true;
			}
			else
			{
				NameStore[Idx] = Name;
				++NumElements;
				return false;
			}
		}

	private:
		void Resize(uint32 NewCapacity)
		{
			const bool bNeedFree = !!SecondaryStore;
			FName* OldStore = bNeedFree ? SecondaryStore : InlineStore;

			SecondaryStore = (FName*)FMemory::Malloc(NewCapacity * sizeof(FName));
			FMemory::Memset(SecondaryStore, 0, NewCapacity * sizeof(FName));

			const uint32 OldCapacity = Capacity;
			Capacity = NewCapacity;
			NumElements = 0;

			for (uint32 Idx = 0; Idx < OldCapacity; ++Idx)
			{
				const FName& Name = OldStore[Idx];
				if (Name != NAME_None)
				{
					Add(Name);
				}
			}

			if (bNeedFree)
			{
				FMemory::Free(OldStore);
			}
		}

		FName* GetNameStore()
		{
			return SecondaryStore ? SecondaryStore : InlineStore;
		}

		static constexpr uint32 GResizeDivFactor = 2u;
		static constexpr uint32 GInitialCapacity = GPredictedMaxNumEventsUpPow2 * GResizeDivFactor;

		uint32 NumElements;
		uint32 Capacity;
		FName InlineStore[GInitialCapacity];
		FName* SecondaryStore;
	};

	int32 NextEventIdx = 1;
	int32 NextResultPendingEventIdx = 1;

	FRenderQueryPoolRHIRef RenderQueryPool;

	TIndirectArray<FRealtimeGPUProfilerEvent, TInlineAllocator<GPredictedMaxNumEvents>> GpuProfilerEvents;
	TArray<int32, TInlineAllocator<GPredictedMaxNumEvents>> GpuProfilerEventParentIndices;
	TArray<int32, TInlineAllocator<GPredictedMaxStackDepth>> EventStack;
	TArray<FGPUEventTimeAggregate, TInlineAllocator<GPredictedMaxNumEvents>> EventAggregates;
	FDescriptionStringBuffer DescriptionStringBuffer;
};

/*-----------------------------------------------------------------------------
FRealtimeGPUProfiler
-----------------------------------------------------------------------------*/
FRealtimeGPUProfiler* FRealtimeGPUProfiler::Instance = nullptr;

FRealtimeGPUProfiler* FRealtimeGPUProfiler::Get()
{
	if (Instance == nullptr)
	{
		Instance = new FRealtimeGPUProfiler;
	}
	return Instance;
}


void FRealtimeGPUProfiler::SafeRelease()
{
	if (Instance)
		Instance->Cleanup();
	Instance = nullptr;
}


FRealtimeGPUProfiler::FRealtimeGPUProfiler()
{
	if (GSupportsTimestampRenderQueries)
	{
		RenderQueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime);
	}
}

void FRealtimeGPUProfiler::Cleanup()
{
	ActiveFrame.Reset();
	PendingFrames.Empty();

	RenderQueryPool.SafeRelease();
	
	TRACE_GPUPROFILER_DEINITIALIZE();
}

#if UE_TRACE_ENABLED
namespace GpuProfilerTrace
{
	UE_TRACE_CHANNEL_EXTERN(GpuChannel, RHI_API)
}
#endif

static std::atomic<bool> GAreGPUStatsEnabled{ false };

bool AreGPUStatsEnabled()
{
	return GAreGPUStatsEnabled;
}

void LatchAreGPUStatsEnabled()
{
	auto GetValue = []()
	{
		if (GSupportsTimestampRenderQueries == false || !CVarGPUStatsEnabled.GetValueOnRenderThread())
		{
			return false;
		}

#if GPUPROFILERTRACE_ENABLED
		// Force GPU profiler on if Unreal Insights is running
		if (UE_TRACE_CHANNELEXPR_IS_ENABLED(GpuProfilerTrace::GpuChannel))
		{
			return true;
		}
#endif

#if STATS 
		return true;
#elif !CSV_PROFILER_STATS
		return false;
#else

		// If we only have CSV stats, only capture if CSV GPU stats are enabled, and we're capturing
		static IConsoleVariable* CVarGPUCsvStatsEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUCsvStatsEnabled"));
		if (!CVarGPUCsvStatsEnabled || !CVarGPUCsvStatsEnabled->GetBool())
		{
			return false;
		}
		if (!FCsvProfiler::Get()->IsCapturing_Renderthread())
		{
			return false;
		}

		return true;
#endif
	};

	GAreGPUStatsEnabled = GetValue();
}

void FRealtimeGPUProfiler::BeginFrame(FRHICommandListImmediate& RHICmdList)
{
	LatchAreGPUStatsEnabled();

	if (!AreGPUStatsEnabled())
	{
		return;
	}

	check(!ActiveFrame.IsValid());
	ActiveFrame = MakeUnique<FRealtimeGPUProfilerFrame>(RenderQueryPool);

	ActiveFrame->TimestampCalibrationQuery = new FRHITimestampCalibrationQuery();
	RHICmdList.CalibrateTimers(ActiveFrame->TimestampCalibrationQuery);

	ActiveFrame->CPUFrameStartTimestamp = FPlatformTime::Cycles64();
}

void FRealtimeGPUProfiler::EndFrame(FRHICommandListImmediate& RHICmdList)
{
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	check(ActiveFrame.IsValid());
	PendingFrames.Enqueue(MoveTemp(ActiveFrame));

	if (TUniquePtr<FRealtimeGPUProfilerFrame>* FramePtr = PendingFrames.Peek())
	{
		if ((*FramePtr)->UpdateStats(RHICmdList
#if GPUPROFILERTRACE_ENABLED
			, HistoryByDescription
#endif
		))
		{
			PendingFrames.Pop();
		}
	}
}

void FRealtimeGPUProfiler::SuspendFrame()
{
	if (!AreGPUStatsEnabled())
	{
		return;
	}
}

FRealtimeGPUProfilerQuery FRealtimeGPUProfiler::PushEvent(FRHIGPUMask GPUMask, const FName& Name, const TStatId& Stat, const TCHAR* Description)
{
	check(IsInRenderingThread());
	if (ActiveFrame.IsValid())
	{
		return ActiveFrame->PushEvent(GPUMask, Name, Stat, Description);
	}
	else
	{
		return {};
	}
}

FRealtimeGPUProfilerQuery FRealtimeGPUProfiler::PopEvent()
{
	check(IsInRenderingThread());
	if (ActiveFrame.IsValid())
	{
		return ActiveFrame->PopEvent();
	}
	else
	{
		return {};
	}
}

void FRealtimeGPUProfiler::PushStat(FRHICommandListImmediate& RHICmdList, const FName& Name, const TStatId& Stat, const TCHAR* Description)
{
	PushEvent(RHICmdList.GetGPUMask(), Name, Stat, Description).Submit(RHICmdList, true);
}

void FRealtimeGPUProfiler::PopStat(FRHICommandListImmediate& RHICmdList)
{
	PopEvent().Submit(RHICmdList, false);
}

/*-----------------------------------------------------------------------------
FScopedGPUStatEvent
-----------------------------------------------------------------------------*/
FScopedGPUStatEvent::FScopedGPUStatEvent(FRHICommandListBase& InRHICmdList, const FName& Name, const TStatId& StatId, const TCHAR* Description)
{
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	if (InRHICmdList.IsImmediate() && EnumHasAnyFlags(InRHICmdList.GetPipeline(), ERHIPipeline::Graphics))
	{
		RHICmdList = &InRHICmdList;
		FRealtimeGPUProfiler::Get()->PushStat(InRHICmdList.GetAsImmediate(), Name, StatId, Description);
	}
}

FScopedGPUStatEvent::~FScopedGPUStatEvent()
{
	if (!RHICmdList)
	{
		return;
	}

	if (RHICmdList != nullptr)
	{
		// Command list is initialized only if it is immediate during Begin() and GetAsImmediate() also internally checks this.
		FRealtimeGPUProfiler::Get()->PopStat(RHICmdList->GetAsImmediate());
	}
}

FScopedDrawStatCategory::FScopedDrawStatCategory(FRHICommandListBase& InRHICmdList, FRHIDrawStatsCategory const& Category)
	: RHICmdList(Category.ShouldCountDraws() ? &InRHICmdList : nullptr)
{
	if (RHICmdList)
	{
		Previous = RHICmdList->SetDrawStatsCategory(&Category);
	}
}

FScopedDrawStatCategory::~FScopedDrawStatCategory()
{
	if (RHICmdList)
	{
		RHICmdList->SetDrawStatsCategory(Previous);
	}
}

#if GPUPROFILERTRACE_ENABLED
FRealtimeGPUProfilerHistoryItem::FRealtimeGPUProfilerHistoryItem()
{
	FMemory::Memset(*this, 0);
}

void FRealtimeGPUProfiler::FetchPerfByDescription(TArray<FRealtimeGPUProfilerDescriptionResult>& OutResults) const
{
	FRWScopeLock Lock(HistoryByDescription.Mutex, SLT_ReadOnly);

	OutResults.Empty(HistoryByDescription.History.Num());

	for (auto Iterator = HistoryByDescription.History.CreateConstIterator(); Iterator; ++Iterator)
	{
		FRealtimeGPUProfilerDescriptionResult Result;
		Result.Description = Iterator.Key();

		const FRealtimeGPUProfilerHistoryItem& HistoryValue = Iterator.Value();
		const uint64 ClampedTimeCount = FMath::Min(HistoryValue.NextWriteIndex, FRealtimeGPUProfilerHistoryItem::HistoryCount);

		Result.GPUMask = HistoryValue.LastGPUMask;
		Result.AverageTime = HistoryValue.AccumulatedTime / ClampedTimeCount;
		Result.MinTime = INT64_MAX;
		Result.MaxTime = 0;

		for (uint64 TimeIndex = 0; TimeIndex < ClampedTimeCount; TimeIndex++)
		{
			Result.MinTime = FMath::Min(Result.MinTime, HistoryValue.Times[TimeIndex]);
			Result.MaxTime = FMath::Max(Result.MaxTime, HistoryValue.Times[TimeIndex]);
		}

		OutResults.Add(Result);
	}
}
#endif  // GPUPROFILERTRACE_ENABLED

#endif // HAS_GPU_STATS
