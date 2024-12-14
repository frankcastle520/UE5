// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11Query.cpp: D3D query RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"
#include "RenderCore.h"

float GD3D11AbsoluteTimeQueryTimeoutValue = 30.0f;
static FAutoConsoleVariableRef CVarD3D11AbsoluteTimeQueryTimeoutValue(
	TEXT("r.D3D11.AbsoluteTimeQueryTimeoutValue"),
	GD3D11AbsoluteTimeQueryTimeoutValue,
	TEXT("Set the timeout value, in seconds, to wait for a D3D11 absolute time query."),
	ECVF_Default
);

float GD3D11QueryTimeoutValue = 5.0f;
static FAutoConsoleVariableRef CVarD3D11QueryTimeoutValue(
	TEXT("r.D3D11.QueryTimeoutValue"),
	GD3D11QueryTimeoutValue,
	TEXT("Set the timeout value, in seconds, to wait for a D3D11 query. This value does not apply to absolute time queries (which are controlled by r.D3D11.AbsoluteTimeQueryTimeoutValue)."),
	ECVF_Default
);

FD3D11RenderQuery::FD3D11RenderQuery(EType Type)
	: Type(Type)
{
	D3D11_QUERY_DESC Desc {};

	switch (Type)
	{
	case EType::Occlusion: Desc.Query = D3D11_QUERY_OCCLUSION; break;
	case EType::Timestamp: Desc.Query = D3D11_QUERY_TIMESTAMP; break;
	case EType::Profiler : Desc.Query = D3D11_QUERY_TIMESTAMP; break;

	default:
		checkNoEntry();
		return;
	}

	ID3D11Device* Device = FD3D11DynamicRHI::Get().GetDevice();
	VERIFYD3D11RESULT_EX(Device->CreateQuery(&Desc, Resource.GetInitReference()), Device);
}

FD3D11RenderQuery::~FD3D11RenderQuery()
{
	Unlink();
}

void FD3D11RenderQuery::Begin(ID3D11DeviceContext* Context)
{
	check(Type == EType::Occlusion);
	Context->Begin(Resource);
}

void FD3D11RenderQuery::End(ID3D11DeviceContext* Context, uint64* NewTarget)
{
	BOPCounter++;

	Context->End(Resource);
	Target = NewTarget;

	Link();
}

FRenderQueryRHIRef FD3D11DynamicRHI::RHICreateRenderQuery(ERenderQueryType QueryType)
{
	switch (QueryType)
	{
	case RQT_AbsoluteTime: return new FD3D11RenderQuery_RHI(FD3D11RenderQuery::EType::Timestamp);
	case RQT_Occlusion   : return new FD3D11RenderQuery_RHI(FD3D11RenderQuery::EType::Occlusion);

	default:
		checkNoEntry();
		return nullptr;
	}
}

void FD3D11DynamicRHI::RHIEndRenderQuery_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIRenderQuery* RenderQuery)
{
	FD3D11RenderQuery* Query = ResourceCast(RenderQuery);
	Query->TOPCounter++;

	FDynamicRHI::RHIEndRenderQuery_TopOfPipe(RHICmdList, RenderQuery);
}

void FD3D11DynamicRHI::RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery)
{
	ResourceCast(RenderQuery)->Begin(Direct3DDeviceIMContext);
}

void FD3D11DynamicRHI::RHIEndRenderQuery(FRHIRenderQuery* RenderQuery)
{
	FD3D11RenderQuery_RHI* Query = ResourceCast(RenderQuery);
	Query->End(Direct3DDeviceIMContext, &Query->Result);
}

bool FD3D11DynamicRHI::RHIGetRenderQueryResult(FRHIRenderQuery* QueryRHI, uint64& OutResult, bool bWait, uint32 GPUIndex)
{
	check(IsInRenderingThread());
	FD3D11RenderQuery_RHI* Query = ResourceCast(QueryRHI);

	bool bRHIThreadFlushed = false;

Retry:
	if (Query->TOPCounter == Query->LastCachedBOPCounter.load(std::memory_order_acquire))
	{
		// Early return for queries we already have the result for.
		check(!Query->IsLinked());
		OutResult = Query->Result;
		return true;
	}

	if (FRHICommandListExecutor::AreRHITasksActive())
	{
		if (!bWait)
		{
			//
			// The RHI thread is still processing work, the query has not yet completed, and we don't want to wait for the query result.
			// Return. The RHI thread will poll for results later.
			//
			OutResult = 0;
			return false;
		}
		else
		{
			//
			// The RHI thread is active, the query has not yet completed, and we want to wait for results.
			// 
			// Flushing the RHI thread will ensure a query poll operation has happened before the render thread resumes, which might successfully cache the results.
			// It will also make it safe for us to use the immediate device context in case the query still wasn't done when the RHI thread last polled for results.
			//
			FRHICommandListImmediate::Get().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			bRHIThreadFlushed = true;

			// Now the RHI thread is idle, retry grabbing the query results.
			goto Retry;
		}
	}

	//
	// From this point, the RHI thread is idle (although possibly not flushed). It is safe to use the immediate device context.
	// The query is unresolved. Either the GPU isn't done, or the commands to signal the query were never submitted (still recorded in the immediate command list).
	//

	if (Query->TOPCounter != Query->BOPCounter && !bRHIThreadFlushed)
	{
		// When TOPCounter != BOPCounter, there's an End() operation that was recorded at the TOP, but has not yet been submitted for translation by the RHI thread.
		// Flush the immediate command list to push this command into the RHI pipeline.
		FRHICommandListImmediate::Get().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		bRHIThreadFlushed = true;

		// Now the RHI thread is flushed, retry grabbing the query results.
		goto Retry;
	}

	checkf(Query->TOPCounter == Query->BOPCounter, TEXT("Attempting to get data from an RHI render query which was never issued."));
	if (!Query->CacheResult(*this, bWait))
	{
		OutResult = 0;
		return false;
	}

	check(!Query->IsLinked());
	OutResult = Query->Result;

	return true;
}

bool FD3D11RenderQuery::CacheResult(FD3D11DynamicRHI& RHI, bool bWait)
{
	if (BOPCounter == LastCachedBOPCounter.load(std::memory_order_relaxed))
	{
		// Value has been cached and no newer query operation has started.
		check(!IsLinked());
		return true;
	}

	check(Target);

	// Attempt to read the result from the GPU.
	uint64 Temp;
	if (!RHI.GetQueryData(Resource, &Temp, sizeof(Temp), Type == EType::Timestamp, /*bWait = */ bWait, /*bStallRHIThread = */ false))
	{
		return false;
	}

	// Data retrieved.
	// Adjust timer queries to engine-clock ticks.
	switch (Type)
	{
#if RHI_NEW_GPU_PROFILER
	case EType::Profiler:
		{
			// Convert from GPU timestamp to CPU timestamp (relative to FPlatformTime::Cycles64())
			uint64 GPUDelta = Temp - RHI.TimestampCalibration->GPUTimestamp;
			uint64 CPUDelta = (GPUDelta * RHI.TimestampCalibration->CPUFrequency) / RHI.TimestampCalibration->GPUFrequency;

			Temp = CPUDelta + RHI.TimestampCalibration->CPUTimestamp;
		}
		break;
#endif

	case EType::Timestamp:
		{
			// GetTimingFrequency is the number of ticks per second
			uint64 Div = FMath::Max(1llu, RHI.TimestampCalibration->GPUFrequency / (1000 * 1000));

			// convert from GPU specific timestamp to micro sec (1 / 1 000 000 s) which seems a reasonable resolution
			Temp = Temp / Div;
		}
		break;
	}

	*Target = Temp;
	Target = nullptr;

	Unlink();

	LastCachedBOPCounter.store(BOPCounter, std::memory_order_release);

#if RHI_NEW_GPU_PROFILER
	if (Type == EType::Profiler)
	{
		// Return the query to the pool
		RHI.Profiler.TimestampPool.Push(this);
	}
#endif

	return true;
}

void FD3D11RenderQuery::Link()
{
	// The renderer might re-use a query without reading its results back first.
	// Ensure this query is unlinked, so it can be re-linked at the end of the list.
	Unlink();

	auto& List = FD3D11DynamicRHI::Get().ActiveQueries;

	if (!List.First)
	{
		check(!List.Last);
		check(Next == nullptr);

		List.First = this;
		Prev = &List.First;
	}
	else
	{
		check(List.Last);
		check(List.Last->Next == nullptr);

		List.Last->Next = this;
		Prev = &List.Last->Next;
	}

	List.Last = this;
}

void FD3D11RenderQuery::Unlink()
{
	if (!IsLinked())
		return;

	auto& List = FD3D11DynamicRHI::Get().ActiveQueries;

	if (List.Last == this)
	{
		// This is the last node in the list, so the "List.Last" pointer needs fixing up.
		if (Prev == &List.First)
		{
			// This is also the first node in the list, meaning there's only 1 node total.
			// Just clear the "List.Last" pointer.
			List.Last = nullptr;
		}
		else
		{
			//
			// There's at least one real node before us.
			// 
			// "Prev" points to the "Next" member field of the previous node.
			// Subtract the "Next" field offset to get the actual previous node address.
			//
			List.Last = reinterpret_cast<FD3D11RenderQuery*>(reinterpret_cast<uintptr_t>(Prev) - UFIELD_OFFSET(FD3D11RenderQuery, Next));
		}
	}

	if (Next) { Next->Prev = Prev; }
	if (Prev) { *Prev = Next; }

	Next = nullptr;
	Prev = nullptr;
}

void FD3D11DynamicRHI::PollQueryResults()
{
	while (ActiveQueries.First)
	{
		if (!ActiveQueries.First->CacheResult(*this, /*bWait = */ false))
			break;
	}
}

bool FD3D11DynamicRHI::GetQueryData(ID3D11Query* Query, void* Data, SIZE_T DataSize, bool bTimestamp, bool bWait, bool bStallRHIThread)
{
	// Request the data from the query.
	HRESULT Result;

	auto SafeGetQueryData = [&]()
	{
		FScopedD3D11RHIThreadStaller StallRHIThread(bStallRHIThread);
		Result = Direct3DDeviceIMContext->GetData(Query, Data, DataSize, 0);
	};
	SafeGetQueryData();

	// Isn't the query finished yet, and can we wait for it?
	if (Result == S_FALSE && bWait)
	{
		SCOPE_CYCLE_COUNTER( STAT_RenderQueryResultTime );
		FRenderThreadIdleScope IdleScope(ERenderThreadIdleTypes::WaitingForGPUQuery);

		double StartTime = FPlatformTime::Seconds();
		double TimeoutWarningLimit = 5.0;
		// timer queries are used for Benchmarks which can stall a bit more
		double TimeoutValue = bTimestamp ? GD3D11AbsoluteTimeQueryTimeoutValue : GD3D11QueryTimeoutValue;

		do
		{
			SafeGetQueryData();

			if (Result == S_OK)
			{
				return true;
			}

			float DeltaTime = FPlatformTime::Seconds() - StartTime;
			if (DeltaTime > TimeoutWarningLimit)
			{
				HRESULT DeviceRemovedReason = Direct3DDevice->GetDeviceRemovedReason();
				TimeoutWarningLimit += 5.0;
				UE_LOG(LogD3D11RHI, Log, TEXT("GetQueryData is taking a very long time (%.1f s) (%08x)"), DeltaTime, (uint32)DeviceRemovedReason);
			}

			if (DeltaTime > TimeoutValue)
			{
				UE_LOG(LogD3D11RHI, Log, TEXT("Timed out while waiting for GPU query. (Timeout %.1f s) (ErrorCode %08x)"), TimeoutValue, (uint32)Result);
				VERIFYD3D11RESULT_EX(Result, Direct3DDevice);
				return false;
			}
		} while (Result == S_FALSE);
	}

	if (Result == S_OK)
	{
		return true;
	}
	else if (Result == S_FALSE && !bWait)
	{
		// Return failure if the query isn't complete, and waiting wasn't requested.
		return false;
	}
	else
	{
		VERIFYD3D11RESULT_EX(Result, Direct3DDevice);
		return false;
	}
}

void FD3D11EventQuery::IssueEvent()
{
	if (ShouldNotEnqueueRHICommand())
	{
		D3DRHI->GetDeviceContext()->End(Query);
	}
	else
	{
		RunOnRHIThread(
			[InQuery = Query]()
		{
			D3D11RHI_IMMEDIATE_CONTEXT->End(InQuery);
		});
	}
}

void FD3D11EventQuery::WaitForCompletion()
{
	BOOL bRenderingIsFinished = false;
	while(
		D3DRHI->GetQueryData(Query, &bRenderingIsFinished, sizeof(bRenderingIsFinished), false, true, true) &&
		!bRenderingIsFinished
		)
	{};
}

FD3D11EventQuery::FD3D11EventQuery(class FD3D11DynamicRHI* InD3DRHI):
	D3DRHI(InD3DRHI)
{
	D3D11_QUERY_DESC QueryDesc;
	QueryDesc.Query = D3D11_QUERY_EVENT;
	QueryDesc.MiscFlags = 0;
	VERIFYD3D11RESULT_EX(D3DRHI->GetDevice()->CreateQuery(&QueryDesc,Query.GetInitReference()), D3DRHI->GetDevice());
}


TOptional<FD3D11DynamicRHI::FTimestampCalibration> FD3D11DynamicRHI::CalibrateTimers()
{
	// Attempt to generate a timestamp on GPU and CPU as closely to each other as possible.
	// This works by first flushing any pending GPU work, then writing a GPU timestamp and waiting for GPU to finish.
	// CPU timestamp is continuously captured while we are waiting on GPU.

	HRESULT D3DResult = E_FAIL;

	TRefCountPtr<ID3D11Query> DisjointQuery;
	{
		D3D11_QUERY_DESC QueryDesc;
		QueryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		QueryDesc.MiscFlags = 0;
		D3DResult = Direct3DDevice->CreateQuery(&QueryDesc, DisjointQuery.GetInitReference());

		if (D3DResult != S_OK)
			return {};
	}

	TRefCountPtr<ID3D11Query> TimestampQuery;
	{
		D3D11_QUERY_DESC QueryDesc;
		QueryDesc.Query = D3D11_QUERY_TIMESTAMP;
		QueryDesc.MiscFlags = 0;
		D3DResult = Direct3DDevice->CreateQuery(&QueryDesc, TimestampQuery.GetInitReference());

		if (D3DResult != S_OK)
			return {};
	}

	TRefCountPtr<ID3D11Query> PendingWorkDoneQuery;
	TRefCountPtr<ID3D11Query> TimestampDoneQuery;
	{
		D3D11_QUERY_DESC QueryDesc;
		QueryDesc.Query = D3D11_QUERY_EVENT;
		QueryDesc.MiscFlags = 0;

		D3DResult = Direct3DDevice->CreateQuery(&QueryDesc, PendingWorkDoneQuery.GetInitReference());
		if (D3DResult != S_OK)
			return {};

		D3DResult = Direct3DDevice->CreateQuery(&QueryDesc, TimestampDoneQuery.GetInitReference());
		if (D3DResult != S_OK)
			return {};
	}

	// Flush any currently pending GPU work and wait for it to finish
	Direct3DDeviceIMContext->End(PendingWorkDoneQuery);
	Direct3DDeviceIMContext->Flush();

	for (;;)
	{
		BOOL EventComplete = false;
		Direct3DDeviceIMContext->GetData(PendingWorkDoneQuery, &EventComplete, sizeof(EventComplete), 0);

		if (EventComplete)
			break;

		FPlatformProcess::Sleep(0.001f);
	}

	const uint32 MaxCalibrationAttempts = 10;
	for (uint32 CalibrationAttempt = 0; CalibrationAttempt < MaxCalibrationAttempts; ++CalibrationAttempt)
	{
		Direct3DDeviceIMContext->Begin(DisjointQuery);
		Direct3DDeviceIMContext->End(TimestampQuery);
		Direct3DDeviceIMContext->End(DisjointQuery);
		Direct3DDeviceIMContext->End(TimestampDoneQuery);

		Direct3DDeviceIMContext->Flush();

		uint64 CPUTimestamp = 0;
		uint64 GPUTimestamp = 0;

		// Busy-wait for GPU to finish and capture CPU timestamp approximately when GPU work is done
		for (;;)
		{
			BOOL EventComplete = false;

			CPUTimestamp = FPlatformTime::Cycles64();
			Direct3DDeviceIMContext->GetData(TimestampDoneQuery, &EventComplete, sizeof(EventComplete), 0);

			if (EventComplete)
				break;
		}

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT DisjointQueryData = {};
		D3DResult = Direct3DDeviceIMContext->GetData(DisjointQuery, &DisjointQueryData, sizeof(DisjointQueryData), 0);

		// If timestamp was unreliable, try again
		if (D3DResult != S_OK || DisjointQueryData.Disjoint)
		{
			continue;
		}

		D3DResult = Direct3DDeviceIMContext->GetData(TimestampQuery, &GPUTimestamp, sizeof(GPUTimestamp), 0);

		// If we managed to get valid timestamps, save both of them (CPU & GPU) and return
		if (D3DResult == S_OK && GPUTimestamp)
		{
			return FD3D11DynamicRHI::FTimestampCalibration
			{
				.CPUTimestamp = CPUTimestamp,
				.CPUFrequency = uint64(1.0 / FPlatformTime::GetSecondsPerCycle64()),

				.GPUTimestamp = GPUTimestamp,
				.GPUFrequency = DisjointQueryData.Frequency
			};
		}
	}

	return {};
}



/*=============================================================================
 * class FD3D11BufferedGPUTiming
 *=============================================================================*/

#if (RHI_NEW_GPU_PROFILER == 0)

/**
 * Constructor.
 *
 * @param InD3DRHI			RHI interface
 * @param InBufferSize		Number of buffered measurements
 */
FD3D11BufferedGPUTiming::FD3D11BufferedGPUTiming( FD3D11DynamicRHI* InD3DRHI, int32 InBufferSize )
:	D3DRHI( InD3DRHI )
,	BufferSize( InBufferSize )
,	CurrentTimestamp( -1 )
,	NumIssuedTimestamps( 0 )
,	StartTimestamps( NULL )
,	EndTimestamps( NULL )
,	bIsTiming( false )
{
}

/**
 * Initializes the static variables, if necessary.
 */
void FD3D11BufferedGPUTiming::PlatformStaticInitialize(void* UserData)
{
	// Are the static variables initialized?
	check( !GAreGlobalsInitialized );

	// Get the GPU timestamp frequency.
	SetTimingFrequency(0);
	TRefCountPtr<ID3D11Query> FreqQuery;
	FD3D11DynamicRHI* D3DRHI = (FD3D11DynamicRHI*)UserData;
	ID3D11DeviceContext *D3D11DeviceContext = D3DRHI->GetDeviceContext();
	HRESULT D3DResult;

	D3D11_QUERY_DESC QueryDesc;
	QueryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
	QueryDesc.MiscFlags = 0;

	{
		// to track down some rare event where GTimingFrequency is 0 or <1000*1000
		uint32 DebugState = 0;
		uint32 DebugCounter = 0;

		D3DResult = D3DRHI->GetDevice()->CreateQuery(&QueryDesc, FreqQuery.GetInitReference());
		if (D3DResult == S_OK)
		{
			DebugState = 1;
			D3D11DeviceContext->Begin(FreqQuery);
			D3D11DeviceContext->End(FreqQuery);

			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT FreqQueryData;

			{
				FScopedD3D11RHIThreadStaller StallRHIThread;
				D3DResult = D3D11DeviceContext->GetData(FreqQuery, &FreqQueryData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0);
			}

			double StartTime = FPlatformTime::Seconds();
			while (D3DResult == S_FALSE && (FPlatformTime::Seconds() - StartTime) < 0.5f)
			{
				++DebugCounter;
				FPlatformProcess::Sleep(0.005f);

				FScopedD3D11RHIThreadStaller StallRHIThread;
				D3DResult = D3D11DeviceContext->GetData(FreqQuery, &FreqQueryData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0);
			}

			if (D3DResult == S_OK)
			{
				DebugState = 2;
				SetTimingFrequency(FreqQueryData.Frequency);
				checkSlow(!FreqQueryData.Disjoint);

				if (FreqQueryData.Disjoint)
				{
					DebugState = 3;
				}
			}
		}

		UE_LOG(LogD3D11RHI, Log, TEXT("GPU Timing Frequency: %f (Debug: %d %d)"), GetTimingFrequency() / (double)(1000 * 1000), DebugState, DebugCounter);
	}

	FreqQuery = NULL;

	CalibrateTimers(D3DRHI);
}

void FD3D11BufferedGPUTiming::CalibrateTimers(FD3D11DynamicRHI* InD3DRHI)
{
	TOptional<FD3D11DynamicRHI::FTimestampCalibration> Data = InD3DRHI->CalibrateTimers();
	if (Data.IsSet())
	{
		FGPUTimingCalibrationTimestamp CalibrationTimestamp;
		CalibrationTimestamp.CPUMicroseconds = uint64(FPlatformTime::ToSeconds64(Data->CPUTimestamp) * 1e6);
		CalibrationTimestamp.GPUMicroseconds = uint64(Data->GPUTimestamp * (1e6 / Data->GPUFrequency));
		SetCalibrationTimestamp(CalibrationTimestamp);
	}
}

/**
 * Initializes all D3D resources and if necessary, the static variables.
 */
void FD3D11BufferedGPUTiming::InitRHI(FRHICommandListBase& RHICmdList)
{
	StaticInitialize(D3DRHI, PlatformStaticInitialize);

	CurrentTimestamp = 0;
	NumIssuedTimestamps = 0;
	bIsTiming = false;

	// Now initialize the queries for this timing object.
	if ( GIsSupported )
	{
		StartTimestamps = new TRefCountPtr<ID3D11Query>[ BufferSize ];
		EndTimestamps = new TRefCountPtr<ID3D11Query>[ BufferSize ];
		for ( int32 TimestampIndex = 0; TimestampIndex < BufferSize; ++TimestampIndex )
		{
			HRESULT D3DResult;

			D3D11_QUERY_DESC QueryDesc;
			QueryDesc.Query = D3D11_QUERY_TIMESTAMP;
			QueryDesc.MiscFlags = 0;

			CA_SUPPRESS(6385);	// Doesn't like COM
			D3DResult = D3DRHI->GetDevice()->CreateQuery(&QueryDesc,StartTimestamps[TimestampIndex].GetInitReference());
			GIsSupported = GIsSupported && (D3DResult == S_OK);
			CA_SUPPRESS(6385);	// Doesn't like COM
			D3DResult = D3DRHI->GetDevice()->CreateQuery(&QueryDesc,EndTimestamps[TimestampIndex].GetInitReference());
			GIsSupported = GIsSupported && (D3DResult == S_OK);
		}
	}
}

/**
 * Releases all D3D resources.
 */
void FD3D11BufferedGPUTiming::ReleaseRHI()
{
	if ( StartTimestamps && EndTimestamps )
	{
		for ( int32 TimestampIndex = 0; TimestampIndex < BufferSize; ++TimestampIndex )
		{
			StartTimestamps[TimestampIndex] = NULL;
			EndTimestamps[TimestampIndex] = NULL;
		}
		delete [] StartTimestamps;
		delete [] EndTimestamps;
		StartTimestamps = NULL;
		EndTimestamps = NULL;
	}
}

/**
 * Start a GPU timing measurement.
 */
void FD3D11BufferedGPUTiming::StartTiming()
{
	// Issue a timestamp query for the 'start' time.
	if ( GIsSupported && !bIsTiming )
	{
		int32 NewTimestampIndex = (CurrentTimestamp + 1) % BufferSize;
		D3DRHI->GetDeviceContext()->End(StartTimestamps[NewTimestampIndex]);
		CurrentTimestamp = NewTimestampIndex;
		bIsTiming = true;
	}
}

/**
 * End a GPU timing measurement.
 * The timing for this particular measurement will be resolved at a later time by the GPU.
 */
void FD3D11BufferedGPUTiming::EndTiming()
{
	// Issue a timestamp query for the 'end' time.
	if ( GIsSupported && bIsTiming )
	{
		checkSlow( CurrentTimestamp >= 0 && CurrentTimestamp < BufferSize );
		D3DRHI->GetDeviceContext()->End(EndTimestamps[CurrentTimestamp]);
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
uint64 FD3D11BufferedGPUTiming::GetTiming(bool bGetCurrentResultsAndBlock)
{
	if ( GIsSupported )
	{
		checkSlow( CurrentTimestamp >= 0 && CurrentTimestamp < BufferSize );
		uint64 StartTime, EndTime;
		HRESULT D3DResult;

		int32 TimestampIndex = CurrentTimestamp;
		if (!bGetCurrentResultsAndBlock)
		{
			// Quickly check the most recent measurements to see if any of them has been resolved.  Do not flush these queries.
			for ( int32 IssueIndex = 1; IssueIndex < NumIssuedTimestamps; ++IssueIndex )
			{
				D3DResult = D3DRHI->GetDeviceContext()->GetData(EndTimestamps[TimestampIndex],&EndTime,sizeof(EndTime),D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if ( D3DResult == S_OK )
				{
					D3DResult = D3DRHI->GetDeviceContext()->GetData(StartTimestamps[TimestampIndex],&StartTime,sizeof(StartTime),D3D11_ASYNC_GETDATA_DONOTFLUSH);
					if ( D3DResult == S_OK && EndTime > StartTime)
					{
						return EndTime - StartTime;
					}
				}

				TimestampIndex = (TimestampIndex + BufferSize - 1) % BufferSize;
			}
		}
		
		if ( NumIssuedTimestamps > 0 || bGetCurrentResultsAndBlock )
		{
			// None of the (NumIssuedTimestamps - 1) measurements were ready yet,
			// so check the oldest measurement more thoroughly.
			// This really only happens if occlusion and frame sync event queries are disabled, otherwise those will block until the GPU catches up to 1 frame behind
			const bool bBlocking = ( NumIssuedTimestamps == BufferSize ) || bGetCurrentResultsAndBlock;
			const uint32 AsyncFlags = bBlocking ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH;
			{
				FRenderThreadIdleScope IdleScope(ERenderThreadIdleTypes::WaitingForGPUQuery);
				double StartTimeoutTime = FPlatformTime::Seconds();

				SCOPE_CYCLE_COUNTER( STAT_RenderQueryResultTime );
				// If we are blocking, retry until the GPU processes the time stamp command
				do 
				{
					D3DResult = D3DRHI->GetDeviceContext()->GetData( EndTimestamps[TimestampIndex], &EndTime, sizeof(EndTime), AsyncFlags );

					if ((FPlatformTime::Seconds() - StartTimeoutTime) > 0.5)
					{
						UE_LOG(LogD3D11RHI, Log, TEXT("Timed out while waiting for GPU to catch up. (500 ms)"));
						return 0;
					}
				} while ( D3DResult == S_FALSE && bBlocking );
			}

			if ( D3DResult == S_OK )
			{
				{
					FRenderThreadIdleScope IdleScope(ERenderThreadIdleTypes::WaitingForGPUQuery);
					double StartTimeoutTime = FPlatformTime::Seconds();
					do 
					{
						D3DResult = D3DRHI->GetDeviceContext()->GetData( StartTimestamps[TimestampIndex], &StartTime, sizeof(StartTime), AsyncFlags );

						if ((FPlatformTime::Seconds() - StartTimeoutTime) > 0.5)
						{
							UE_LOG(LogD3D11RHI, Log, TEXT("Timed out while waiting for GPU to catch up. (500 ms)"));
							return 0;
						}
					} while ( D3DResult == S_FALSE && bBlocking );
				}

				if ( D3DResult == S_OK && EndTime > StartTime )
				{
					return EndTime - StartTime;
				}
			}
		}
	}
	return 0;
}

FD3D11DisjointTimeStampQuery::FD3D11DisjointTimeStampQuery(class FD3D11DynamicRHI* InD3DRHI) :
	D3DRHI(InD3DRHI)
{

}

void FD3D11DisjointTimeStampQuery::StartTracking()
{
	ID3D11DeviceContext* D3D11DeviceContext = D3DRHI->GetDeviceContext();
	D3D11DeviceContext->Begin(DisjointQuery);
}

void FD3D11DisjointTimeStampQuery::EndTracking()
{
	ID3D11DeviceContext* D3D11DeviceContext = D3DRHI->GetDeviceContext();
	D3D11DeviceContext->End(DisjointQuery);
}

bool FD3D11DisjointTimeStampQuery::IsResultValid()
{
	return GetResult().Disjoint == 0;
}

D3D11_QUERY_DATA_TIMESTAMP_DISJOINT FD3D11DisjointTimeStampQuery::GetResult()
{
	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT DisjointQueryData;

	ID3D11DeviceContext* D3D11DeviceContext = D3DRHI->GetDeviceContext();
	HRESULT D3DResult = D3D11DeviceContext->GetData(DisjointQuery, &DisjointQueryData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0);

	const double StartTime = FPlatformTime::Seconds();
	while (D3DResult == S_FALSE && (FPlatformTime::Seconds() - StartTime) < 0.5)
	{
		FPlatformProcess::Sleep(0.005f);
		D3DResult = D3D11DeviceContext->GetData(DisjointQuery, &DisjointQueryData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0);
	}

	return DisjointQueryData;
}

void FD3D11DisjointTimeStampQuery::InitRHI(FRHICommandListBase& RHICmdList)
{
	D3D11_QUERY_DESC QueryDesc;
	QueryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
	QueryDesc.MiscFlags = 0;

	VERIFYD3D11RESULT_EX(D3DRHI->GetDevice()->CreateQuery(&QueryDesc, DisjointQuery.GetInitReference()), D3DRHI->GetDevice());
}

void FD3D11DisjointTimeStampQuery::ReleaseRHI()
{

}

#endif // (RHI_NEW_GPU_PROFILER == 0)
