// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLQuery.cpp: OpenGL query RHI implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "OpenGLDrv.h"
#include "OpenGLDrvPrivate.h"
#include "RenderCore.h"

FOpenGLRenderQuery::FActiveQueries FOpenGLRenderQuery::ActiveQueries;
FOpenGLRenderQuery::FQueryPool FOpenGLRenderQuery::PooledQueries;

FOpenGLRenderQuery::~FOpenGLRenderQuery()
{
	VERIFY_GL_SCOPE();
	ReleaseGlQuery();
}

void FOpenGLRenderQuery::Link()
{
	// The renderer might re-use a query without reading its results back first.
	// Ensure this query is unlinked, so it can be re-linked at the end of the list.
	Unlink();

	if (!ActiveQueries.First)
	{
		check(!ActiveQueries.Last);
		check(Next == nullptr);

		ActiveQueries.First = this;
		Prev = &ActiveQueries.First;
	}
	else
	{
		check(ActiveQueries.Last);
		check(ActiveQueries.Last->Next == nullptr);

		ActiveQueries.Last->Next = this;
		Prev = &ActiveQueries.Last->Next;
	}

	ActiveQueries.Last = this;
}

void FOpenGLRenderQuery::Unlink()
{
	if (!IsLinked())
		return;

	if (ActiveQueries.Last == this)
	{
		// This is the last node in the list, so the "ActiveQueries.Last" pointer needs fixing up.
		if (Prev == &ActiveQueries.First)
		{
			// This is also the first node in the list, meaning there's only 1 node total.
			// Just clear the "ActiveQueries.Last" pointer.
			ActiveQueries.Last = nullptr;
		}
		else
		{
			//
			// There's at least one real node before us.
			// 
			// "Prev" points to the "Next" member field of the previous node.
			// Subtract the "Next" field offset to get the actual previous node address.
			//
			ActiveQueries.Last = reinterpret_cast<FOpenGLRenderQuery*>(reinterpret_cast<uintptr_t>(Prev) - offsetof(FOpenGLRenderQuery, Next));
		}
	}

	if (Next) { Next->Prev = Prev; }
	if (Prev) { *Prev = Next; }

	Next = nullptr;
	Prev = nullptr;
}

void FOpenGLRenderQuery::CheckContext()
{
	check(Resource);
	if (bSharedContext)
	{
		check(FOpenGLDynamicRHI::GetCurrentContext() == CONTEXT_Shared);
	}
	else
	{
		check(FOpenGLDynamicRHI::GetCurrentContext() == CONTEXT_Rendering);
	}
}

void FOpenGLRenderQuery::AcquireGlQuery()
{
	if (Resource != 0)
	{
		// Already acquired
		return;
	}

	EOpenGLCurrentContext Context = FOpenGLDynamicRHI::GetCurrentContext();
	if (Context == CONTEXT_Shared)
	{
		// Don't do any pooling on the shared context.
		bSharedContext = true;
		FOpenGL::GenQueries(1, &Resource);
	}
	else
	{
		check(Context == CONTEXT_Rendering);

		while (ActiveQueries.First && ActiveQueries.Count >= GRHIMaximumInFlightQueries)
		{
			// We can't start another query until more become available, due to the query count limit.
			// Block for results on the oldest in-flight queries.
			ActiveQueries.First->CacheResult(true);
		}

		ActiveQueries.Count++;

		if (PooledQueries[Type].Num())
		{
			Resource = PooledQueries[Type].Pop();
		}
		else
		{
			FOpenGL::GenQueries(1, &Resource);
		}
	}
}

void FOpenGLRenderQuery::ReleaseGlQuery()
{
	if (Resource == 0)
	{
		// Already released
		check(!IsLinked());
		return;
	}

	CheckContext();

	if (bSharedContext)
	{
		// Don't do any pooling on the shared context.
		FOpenGL::DeleteQueries(1, &Resource);
		bSharedContext = false;
	}
	else
	{
		check(ActiveQueries.Count > 0);
		ActiveQueries.Count--;

		PooledQueries[Type].Add(Resource);
	}
	
	Resource = 0;

	Unlink();
}

void FOpenGLRenderQuery::Begin()
{
	VERIFY_GL_SCOPE();

	check(Resource == 0);
	AcquireGlQuery();

	CheckContext();

	switch(Type)
	{
	default:
	case EType::Timestamp:
		checkNoEntry();
		break;

	case EType::Occlusion:
		FOpenGL::BeginQuery(
			FOpenGL::SupportsExactOcclusionQueries()
				? UGL_SAMPLES_PASSED
				: UGL_ANY_SAMPLES_PASSED
			, Resource
		);
		break;

	case EType::Disjoint:
		FOpenGL::BeginQuery(UGL_TIME_ELAPSED, Resource);
		break;
	};
}

void FOpenGLRenderQuery::End()
{
	VERIFY_GL_SCOPE();
	AcquireGlQuery();
	
	CheckContext();

	switch (Type)
	{
	case EType::Occlusion:
		check(Resource);
		FOpenGL::EndQuery(FOpenGL::SupportsExactOcclusionQueries()
			? UGL_SAMPLES_PASSED
			: UGL_ANY_SAMPLES_PASSED
		);
		break;

	case EType::Timestamp:
		FOpenGL::QueryTimestampCounter(Resource);
		break;

	case EType::Disjoint:
		FOpenGL::EndQuery(UGL_TIME_ELAPSED);
		break;
	}

	BOPCounter++;

	Link();
}

bool FOpenGLRenderQuery::CacheResult(bool bWait)
{
	if (BOPCounter == LastCachedBOPCounter.load(std::memory_order_relaxed))
	{
		// Value has been cached and no newer query operation has started.
		check(!IsLinked());
		return true;
	}

	CheckContext();

	if (!bWait)
	{
		// If we don't want to wait, we need to check if the result is available first.
		GLuint IsAvailable = GL_FALSE;
		FOpenGL::GetQueryObject(Resource, FOpenGL::QM_ResultAvailable, &IsAvailable);

		if (IsAvailable == GL_FALSE)
		{
			// Not ready yet.
			return false;
		}
	}

	// Read the result back (and block if its not ready)
	switch (Type)
	{
	default:
		checkNoEntry();
		break;
	
	case EType::Occlusion:
		{
			GLuint Result32 = 0;
			FOpenGL::GetQueryObject(Resource, FOpenGL::QM_Result, &Result32);
			SetResult(Result32 * (FOpenGL::SupportsExactOcclusionQueries() ? 1 : 500000)); // half a mega pixel display
		}
		break;

	case EType::Timestamp:
		{
			GLuint64 Value = 0;
			FOpenGL::GetQueryObject(Resource, FOpenGL::QM_Result, &Value);

			// Convert to microseconds (GL queries are in nanoseconds)
			SetResult(Value / 1000);
		}
		break;

	case EType::Disjoint:
		{
			// TimerQueryDisjoint is a one-shot state in the driver, it is not pipelined.
			// If it returns true, all timers we've submitted after this timer but haven't
			// yet resolved should be discarded for having invalid data.
			if (FOpenGL::TimerQueryDisjoint())
			{
				for (FOpenGLRenderQuery* Other = this; Other; Other = Other->Next)
				{
					if (Other->Type == EType::Disjoint)
					{
						Other->SetResult(InvalidDisjointMask);
					}
				}
			}
			else
			{
				GLuint64 Value;
				FOpenGL::GetQueryObject(Resource, FOpenGL::QM_Result, &Value);

				// Convert to microseconds (GL queries are in nanoseconds)
				SetResult(Value / 1000);
			}
		}
		break;
	}

	return true;
}

void FOpenGLRenderQuery::SetResult(uint64 Value)
{
	Result = Value;
	ReleaseGlQuery();

	LastCachedBOPCounter.store(BOPCounter, std::memory_order_release);
}

bool FOpenGLRenderQuery_RHI::GetResult(bool bWait, uint64& OutResult)
{
	if (TOPCounter == LastCachedBOPCounter.load(std::memory_order_acquire))
	{
		// Early return for queries we already have the result for.
		check(!IsLinked());
		OutResult = FOpenGLRenderQuery::GetResult();
		return true;
	}

	if (!bWait)
	{
		//
		// The query has not yet completed, and we don't want to wait for the query result.
		// Return. The RHI thread will poll for results later.
		//
		OutResult = 0;
		return false;
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();

	//
	// The query has not yet completed, and we want to wait for results.
	// Append an RHI thread command that will force a readback of the GL query, then flush the RHI thread.
	//	
	RHICmdList.EnqueueLambda([Counter = TOPCounter, this](FRHICommandListImmediate&)
	{
		if (BOPCounter != Counter)
		{
			// Query result is not ready yet. Fetch it...
			CacheResult(true);
		}
	});

	// Wait for the above lambda to execute
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);

	checkf(TOPCounter == BOPCounter, TEXT("Attempting to get data from an RHI render query which was never issued."));
	check(!IsLinked());

	OutResult = FOpenGLRenderQuery::GetResult();
	return true;
}

void OpenGL_PollAllFences();

void FOpenGLRenderQuery::PollQueryResults()
{
	if (ActiveQueries.First)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PollQueryResults);

		do
		{
			if (!ActiveQueries.First->CacheResult(/*bWait = */ false))
				break;
		}
		while (ActiveQueries.First);
	}
	EOpenGLCurrentContext Context = FOpenGLDynamicRHI::GetCurrentContext();
	if (Context == CONTEXT_Rendering)
	{
 		OpenGL_PollAllFences();
	}
}

void FOpenGLRenderQuery::Cleanup()
{
	VERIFY_GL_SCOPE();
	check(ActiveQueries.Count == 0);

	for (auto& Array : PooledQueries)
	{
		for (GLuint Resource : Array)
		{
			FOpenGL::DeleteQueries(1, &Resource);
		}

		Array.Reset();
	}
}

FRenderQueryRHIRef FOpenGLDynamicRHI::RHICreateRenderQuery(ERenderQueryType QueryType)
{
	check(QueryType == RQT_Occlusion || QueryType == RQT_AbsoluteTime);
	if (QueryType == RQT_AbsoluteTime && FOpenGL::SupportsTimestampQueries() == false)
	{
		return nullptr;
	}

	return new FOpenGLRenderQuery_RHI(QueryType);
}

void FOpenGLDynamicRHI::RHIBeginRenderQuery_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIRenderQuery* RenderQuery)
{
	if (!RenderQuery)
		return;

	FDynamicRHI::RHIBeginRenderQuery_TopOfPipe(RHICmdList, RenderQuery);
}

void FOpenGLDynamicRHI::RHIEndRenderQuery_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIRenderQuery* RenderQuery)
{
	if (!RenderQuery)
		return;

	ResourceCast(RenderQuery)->End_TopOfPipe();
	FDynamicRHI::RHIEndRenderQuery_TopOfPipe(RHICmdList, RenderQuery);
}

void FOpenGLDynamicRHI::RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery)
{
	ResourceCast(RenderQuery)->Begin();
}

void FOpenGLDynamicRHI::RHIEndRenderQuery(FRHIRenderQuery* RenderQuery)
{
	FOpenGLRenderQuery_RHI* Query = ResourceCast(RenderQuery);
	Query->End();
}

bool FOpenGLDynamicRHI::RHIGetRenderQueryResult(FRHIRenderQuery* QueryRHI, uint64& OutResult, bool bWait, uint32 GPUIndex)
{
	if (!QueryRHI)
	{
		OutResult = 0;
		return true;
	}

	FOpenGLRenderQuery_RHI* Query = ResourceCast(QueryRHI);
	return Query->GetResult(bWait, OutResult);
}

void FOpenGLEventQuery::IssueEvent()
{
	VERIFY_GL_SCOPE();
	if(Sync)
	{
		FOpenGL::DeleteSync(Sync);
		Sync = UGLsync();
	}
	Sync = FOpenGL::FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	FOpenGL::Flush();

	checkSlow(FOpenGL::IsSync(Sync));
}

void FOpenGLEventQuery::WaitForCompletion()
{
	VERIFY_GL_SCOPE();

	QUICK_SCOPE_CYCLE_COUNTER(STAT_FOpenGLEventQuery_WaitForCompletion);

	checkSlow(FOpenGL::IsSync(Sync));

	// Wait up to 1/2 second for sync execution
	FOpenGL::EFenceResult Status = FOpenGL::ClientWaitSync( Sync, 0, 500*1000*1000);

	switch (Status)
	{
	case FOpenGL::FR_AlreadySignaled:
	case FOpenGL::FR_ConditionSatisfied:
		break;

	case FOpenGL::FR_TimeoutExpired:
		UE_LOG(LogRHI, Log, TEXT("Timed out while waiting for GPU to catch up. (500 ms)"));
		break;

	case FOpenGL::FR_WaitFailed:
		UE_LOG(LogRHI, Log, TEXT("Wait on GPU failed in driver"));
		break;

	default:
	    UE_LOG(LogRHI, Log, TEXT("Unknown error while waiting on GPU"));
	    check(0);
		break;
	}	
}

FOpenGLEventQuery::FOpenGLEventQuery()
{
	VERIFY_GL_SCOPE();

	// Initialize the query by issuing an initial event.
	IssueEvent();

	check(FOpenGL::IsSync(Sync));
}

FOpenGLEventQuery::~FOpenGLEventQuery()
{
	VERIFY_GL_SCOPE();
	FOpenGL::DeleteSync(Sync);
}

/*=============================================================================
 * class FOpenGLBufferedGPUTiming
 *=============================================================================*/

#if (RHI_NEW_GPU_PROFILER == 0)

/**
 * Constructor.
 *
 * @param InOpenGLRHI			RHI interface
 * @param InBufferSize		Number of buffered measurements
 */
FOpenGLBufferedGPUTiming::FOpenGLBufferedGPUTiming(int32 InBufferSize)
	: BufferSize(InBufferSize)
{
}

/**
 * Initializes the static variables, if necessary.
 */
void FOpenGLBufferedGPUTiming::PlatformStaticInitialize(void* UserData)
{
	// Are the static variables initialized?
	if ( !GAreGlobalsInitialized )
	{
		GIsSupported = FOpenGL::SupportsTimestampQueries();
		SetTimingFrequency(1000 * 1000 * 1000);
		GAreGlobalsInitialized = true;
	}
}

/**
 * Initializes all OpenGL resources and if necessary, the static variables.
 */

static TArray<FOpenGLRenderQuery*> TimerQueryPool;

static FOpenGLRenderQuery* GetTimeQuery()
{
	if (TimerQueryPool.Num())
	{
		return TimerQueryPool.Pop();
	}
	return new FOpenGLRenderQuery(FOpenGLRenderQuery::EType::Timestamp);
}

void FOpenGLBufferedGPUTiming::InitResources()
{
	StaticInitialize(nullptr, PlatformStaticInitialize);

	CurrentTimestamp = 0;
	NumIssuedTimestamps = 0;
	bIsTiming = false;
	GIsSupported = FOpenGL::SupportsTimestampQueries();

	if ( GIsSupported )
	{
		StartTimestamps.Reserve(BufferSize);
		EndTimestamps.Reserve(BufferSize);

		for(int32 BufferIndex = 0; BufferIndex < BufferSize; ++BufferIndex)
		{
			StartTimestamps.Add(GetTimeQuery());
			EndTimestamps.Add(GetTimeQuery());
		}
	}
}

/**
 * Releases all OpenGL resources.
 */
void FOpenGLBufferedGPUTiming::ReleaseResources()
{
	VERIFY_GL_SCOPE();

	for (FOpenGLRenderQuery* Query : StartTimestamps)
	{
		TimerQueryPool.Add(Query);
	}

	for (FOpenGLRenderQuery* Query : EndTimestamps)
	{
		TimerQueryPool.Add(Query);
	}

	StartTimestamps.Reset();
	EndTimestamps.Reset();

}

/**
 * Start a GPU timing measurement.
 */
void FOpenGLBufferedGPUTiming::StartTiming()
{
	VERIFY_GL_SCOPE();
	// Issue a timestamp query for the 'start' time.
	if ( GIsSupported && !bIsTiming )
	{
		int32 NewTimestampIndex = (CurrentTimestamp + 1) % BufferSize;
		StartTimestamps[NewTimestampIndex]->End();

		CurrentTimestamp = NewTimestampIndex;
		bIsTiming = true;
	}
}

/**
 * End a GPU timing measurement.
 * The timing for this particular measurement will be resolved at a later time by the GPU.
 */
void FOpenGLBufferedGPUTiming::EndTiming()
{
	VERIFY_GL_SCOPE();
	// Issue a timestamp query for the 'end' time.
	if ( GIsSupported && bIsTiming )
	{
		checkSlow( CurrentTimestamp >= 0 && CurrentTimestamp < BufferSize );
		EndTimestamps[CurrentTimestamp]->End();

		NumIssuedTimestamps = FMath::Min<int32>(NumIssuedTimestamps + 1, BufferSize);
		bIsTiming = false;
	}
}

/**
 * Retrieves the most recently resolved timing measurement.
 * The unit is the same as for FPlatformTime::Cycles(). Returns 0 if there are no resolved measurements.
 *
 * @return	Value of the most recently resolved timing, or 0 if no measurements have been resolved by the GPU yet.
 */
uint64 FOpenGLBufferedGPUTiming::GetTiming(bool bGetCurrentResultsAndBlock)
{
	VERIFY_GL_SCOPE();

	if (GIsSupported)
	{
		checkSlow(CurrentTimestamp >= 0 && CurrentTimestamp < BufferSize);
		int32 TimestampIndex = CurrentTimestamp;

		if (!bGetCurrentResultsAndBlock)
		{
			// Quickly check the most recent measurements to see if any of them has been resolved.  Do not flush these queries.
			for (int32 IssueIndex = 1; IssueIndex < NumIssuedTimestamps; ++IssueIndex)
			{
				FOpenGLRenderQuery* StartQuery = StartTimestamps[TimestampIndex];
				FOpenGLRenderQuery* EndQuery = EndTimestamps[TimestampIndex];

				if (StartQuery->CacheResult(false) && EndQuery->CacheResult(false))
				{
					uint64 StartTime = StartQuery->GetResult();
					uint64 EndTime = EndQuery->GetResult();

					if (EndTime > StartTime)
					{
						return EndTime - StartTime;
					}
				}

				TimestampIndex = (TimestampIndex + BufferSize - 1) % BufferSize;
			}
		}

		if (NumIssuedTimestamps > 0 || bGetCurrentResultsAndBlock)
		{
			// None of the (NumIssuedTimestamps - 1) measurements were ready yet,
			// so check the oldest measurement more thoroughly.
			// This really only happens if occlusion and frame sync event queries are disabled, otherwise those will block until the GPU catches up to 1 frame behind
			const bool bBlocking = ( NumIssuedTimestamps == BufferSize ) || bGetCurrentResultsAndBlock;

			FOpenGLRenderQuery* StartQuery = StartTimestamps[TimestampIndex];
			FOpenGLRenderQuery* EndQuery = EndTimestamps[TimestampIndex];

			bool bHasStart = false, bHasEnd = false;

			{
				FRenderThreadIdleScope IdleScope(ERenderThreadIdleTypes::WaitingForGPUQuery);
				SCOPE_CYCLE_COUNTER(STAT_RenderQueryResultTime);

				double StartTimeoutTime = FPlatformTime::Seconds();

				// If we are blocking, retry until the GPU processes the time stamp command
				while (true)
				{
					bHasStart = StartQuery->CacheResult(false);
					bHasEnd = EndQuery->CacheResult(false);
					
					if (bBlocking && !(bHasStart && bHasEnd))
					{
						if ((FPlatformTime::Seconds() - StartTimeoutTime) > 0.5)
						{
							UE_LOG(LogRHI, Log, TEXT("Timed out while waiting for GPU to catch up. (500 ms)"));
							return 0;
						}
					}
					else
					{
						break;
					}
				}
			}

			if (bHasStart && bHasEnd)
			{
				uint64 StartTime = StartQuery->GetResult();
				uint64 EndTime = EndQuery->GetResult();

				if (EndTime > StartTime)
				{
					return EndTime - StartTime;
				}
			}
		}
	}
	return 0;
}

void FOpenGLDisjointTimeStampQuery::StartTracking()
{
	VERIFY_GL_SCOPE();
	if (IsSupported())
	{
		DisjointQuery->Begin();
	}
}

void FOpenGLDisjointTimeStampQuery::EndTracking()
{
	VERIFY_GL_SCOPE();

	if (IsSupported())
	{
		DisjointQuery->End();
	}
}

bool FOpenGLDisjointTimeStampQuery::IsResultValid()
{
	checkSlow(IsSupported());
	return bIsResultValid;
}

bool FOpenGLDisjointTimeStampQuery::GetResult(uint64* OutResult)
{
	VERIFY_GL_SCOPE();

	if (IsSupported())
	{
		DisjointQuery->CacheResult(true);

		uint64 Result = DisjointQuery->GetResult();
		bIsResultValid = (Result & FOpenGLRenderQuery::InvalidDisjointMask) == 0;

		*OutResult = Result & (~FOpenGLRenderQuery::InvalidDisjointMask);
	}

	return bIsResultValid;
}

#endif // (RHI_NEW_GPU_PROFILER == 0)

// Fence implementation
struct FOpenGLGPUFenceProxy
{
	FOpenGLGPUFenceProxy()
		: Fence(0)
		, bIsSignaled(false)
	{
	}

	~FOpenGLGPUFenceProxy()
	{
		AllOpenGLGPUFences.RemoveSwap(this);

		if (Fence != 0)
		{
			FOpenGL::DeleteSync(Fence);
		}
	}

	void Write()
	{
		if (Fence != 0)
		{
			FOpenGL::DeleteSync(Fence);
		}
		else
		{
			AllOpenGLGPUFences.Add(this);
		}

		Fence = FOpenGL::FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		bIsSignaled = false;
	}
	
	void Poll()
	{
		if (Fence != 0 && !bIsSignaled)
		{
			FOpenGLBase::EFenceResult Result = (FOpenGL::ClientWaitSync(Fence, 0, 0));
			bIsSignaled = (Result == FOpenGLBase::FR_AlreadySignaled || Result == FOpenGLBase::FR_ConditionSatisfied);
		}
	}

	UGLsync Fence;
	bool bIsSignaled;

	//
	static TArray<FOpenGLGPUFenceProxy*> AllOpenGLGPUFences;
	
	static void PollAllFences()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FOpenGLGPUFence_PollAllFences);

		for (int32 FenceIdx = 0; FenceIdx < AllOpenGLGPUFences.Num(); ++FenceIdx)
		{
			AllOpenGLGPUFences[FenceIdx]->Poll();
		}
	}
};

TArray<FOpenGLGPUFenceProxy*> FOpenGLGPUFenceProxy::AllOpenGLGPUFences;

void OpenGL_PollAllFences()
{
	VERIFY_GL_SCOPE();
	FOpenGLGPUFenceProxy::PollAllFences();
}

FGPUFenceRHIRef FOpenGLDynamicRHI::RHICreateGPUFence(const FName &Name)
{
	return new FOpenGLGPUFence(Name);
}

FOpenGLGPUFence::FOpenGLGPUFence(FName InName)
	: FRHIGPUFence(InName)
{
	Proxy = new FOpenGLGPUFenceProxy();
}

FOpenGLGPUFence::~FOpenGLGPUFence()
{
	VERIFY_GL_SCOPE();
	delete Proxy;
}

void FOpenGLGPUFence::Clear()
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
	RHICmdList.EnqueueLambda([Proxy = Proxy](FRHICommandListImmediate&)
	{
		VERIFY_GL_SCOPE();
		delete Proxy;
	});

	Proxy = new FOpenGLGPUFenceProxy();
}

bool FOpenGLGPUFence::Poll() const
{
	if (Proxy->bIsSignaled)
	{
		return true;
	}

	if (!(IsInRenderingThread() || IsInRHIThread()))
	{
		ENQUEUE_RENDER_COMMAND(FOpenGLGPUFence_Poll)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Poll();
		});
	}
	else
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
		RHICmdList.EnqueueLambda([Proxy = Proxy](FRHICommandListImmediate&)
		{
			VERIFY_GL_SCOPE();
			check(Proxy != nullptr);
			Proxy->Poll();
		});
	}

	return Proxy->bIsSignaled;
}

void FOpenGLGPUFence::WriteInternal()
{
	VERIFY_GL_SCOPE();
	Proxy->Write();
}
