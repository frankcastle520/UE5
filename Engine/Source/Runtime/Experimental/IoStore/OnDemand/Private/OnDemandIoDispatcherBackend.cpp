// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandIoDispatcherBackend.h"

#include "AnalyticsEventAttribute.h"
#include "Containers/BitArray.h"
#include "Containers/StringView.h"
#include "DistributionEndpoints.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "HAL/Event.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/Platform.h"
#include "HAL/PlatformTime.h"
#include "HAL/PreprocessorHelpers.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IO/IoAllocators.h"
#include "IO/IoChunkEncoding.h"
#include "IO/IoDispatcher.h"
#include "IO/IoOffsetLength.h"
#include "IO/IoStatus.h"
#include "IO/IoStore.h"
#include "IO/IoStoreOnDemand.h"
#include "IasCache.h"
#include "Logging/StructuredLog.h"
#include "Math/NumericLimits.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/PathViews.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "OnDemandHttpClient.h"
#include "OnDemandIoStore.h"
#include "Statistics.h"
#include "Tasks/Task.h"

#include <atomic>

#if !UE_BUILD_SHIPPING
#include "Modules/ModuleManager.h"
#endif 

/** When enabled the IAS system can add additional debug console commands for development use */
#define UE_IAS_DEBUG_CONSOLE_CMDS (1 && !NO_CVARS && !UE_BUILD_SHIPPING)

namespace UE::IoStore
{

///////////////////////////////////////////////////////////////////////////////

extern FString GIasOnDemandTocExt;
void LatencyTest(FStringView, FStringView, uint32, TArrayView<int32>);

///////////////////////////////////////////////////////////////////////////////
/** Note that GIasHttpPrimaryEndpoint has no effect after initial start up */
int32 GIasHttpPrimaryEndpoint = 0;
static FAutoConsoleVariableRef CVar_IasHttpPrimaryEndpoint(
	TEXT("ias.HttpPrimaryEndpoint"),
	GIasHttpPrimaryEndpoint,
	TEXT("Primary endpoint to use returned from the distribution endpoint")
);

bool GIasHttpChangeEndpointAfterSuccessfulRetry = true;
static FAutoConsoleVariableRef CVar_IasHttpChangeEndpointAfterSuccessfulRetry(
	TEXT("ias.HttpChangeEndpointAfterSuccessfulRetry"),
	GIasHttpChangeEndpointAfterSuccessfulRetry,
	TEXT("Whether to change the current endpoint after a sucessful retry")
);

int32 GIasHttpPollTimeoutMs = 17;
static FAutoConsoleVariableRef CVar_GIasHttpPollTimeoutMs(
	TEXT("ias.HttpPollTimeoutMs"),
	GIasHttpPollTimeoutMs,
	TEXT("Http tick poll timeout in milliseconds")
);

int32 GIasHttpRateLimitKiBPerSecond = 0;
static FAutoConsoleVariableRef CVar_GIasHttpRateLimitKiBPerSecond(
	TEXT("ias.HttpRateLimitKiBPerSecond"),
	GIasHttpRateLimitKiBPerSecond,
	TEXT("Http throttle limit in KiBPerSecond")
);

static int32 GIasHttpRecvBufKiB = -1;
static FAutoConsoleVariableRef CVar_GIasHttpRecvBufKiB(
	TEXT("ias.HttpRecvBufKiB"),
	GIasHttpRecvBufKiB,
	TEXT("Recv buffer size")
);

static int32 GIasHttpConcurrentRequests = 8;
static FAutoConsoleVariableRef CVar_IasHttpConcurrentRequests(
	TEXT("ias.HttpConcurrentRequests"),
	GIasHttpConcurrentRequests,
	TEXT("Number of concurrent requests in the http client.")
);

static int32 GIasHttpConnectionCount = 4;
static FAutoConsoleVariableRef CVar_IasHttpConnectionCount(
	TEXT("ias.HttpConnectionCount"),
	GIasHttpConnectionCount,
	TEXT("Number of open HTTP connections to the on demand endpoint(s).")
);

/**
 *This is only applied when the connection was made to a single ServiceUrl rather than a DistributedUrl.
 * In the latter case we will make two attempts on the primary CDN followed by a single attempt for the
 * remaining CDN's to be tried in the order provided by the distributed endpoint.
 */
static int32 GIasHttpRetryCount = 2;
static FAutoConsoleVariableRef CVar_IasHttpRetryCount(
	TEXT("ias.HttpRetryCount"),
	GIasHttpRetryCount,
	TEXT("Number of HTTP request retries before failing the request (if connected to a service url rather than distributed endpoints).")
);

int32 GIasHttpTimeOutMs = 10 * 1000;
static FAutoConsoleVariableRef CVar_IasHttpTimeOutMs(
	TEXT("ias.HttpTimeOutMs"),
	GIasHttpTimeOutMs,
	TEXT("Time out value for HTTP requests in milliseconds")
);

int32 GIasHttpHealthCheckWaitTime = 3000;
static FAutoConsoleVariableRef CVar_IasHttpHealthCheckWaitTime(
	TEXT("ias.HttpHealthCheckWaitTime"),
	GIasHttpHealthCheckWaitTime,
	TEXT("Number of milliseconds to wait before reconnecting to avaiable endpoint(s)")
);

int32 GIasMaxEndpointTestCountAtStartup = 1;
static FAutoConsoleVariableRef CVar_IasMaxEndpointTestCountAtStartup(
	TEXT("ias.MaxEndpointTestCountAtStartup"),
	GIasMaxEndpointTestCountAtStartup,
	TEXT("Number of endpoint(s) to test at startup")
);

int32 GIasHttpErrorSampleCount = 8;
static FAutoConsoleVariableRef CVar_IasHttpErrorSampleCount(
	TEXT("ias.HttpErrorSampleCount"),
	GIasHttpErrorSampleCount,
	TEXT("Number of samples for computing the moving average of failed HTTP requests")
);

float GIasHttpErrorHighWater = 0.5f;
static FAutoConsoleVariableRef CVar_IasHttpErrorHighWater(
	TEXT("ias.HttpErrorHighWater"),
	GIasHttpErrorHighWater,
	TEXT("High water mark when HTTP streaming will be disabled")
);

bool GIasHttpEnabled = true;
static FAutoConsoleVariableRef CVar_IasHttpEnabled(
	TEXT("ias.HttpEnabled"),
	GIasHttpEnabled,
	TEXT("Enables individual asset streaming via HTTP")
);

bool GIasHttpOptionalBulkDataEnabled = true;
static FAutoConsoleVariableRef CVar_IasHttpOptionalBulkDataEnabled(
	TEXT("ias.HttpOptionalBulkDataEnabled"),
	GIasHttpOptionalBulkDataEnabled,
	TEXT("Enables optional bulk data via HTTP")
);

bool GIasReportAnalyticsEnabled = true;
static FAutoConsoleVariableRef CVar_IoReportAnalytics(
	TEXT("ias.ReportAnalytics"),
	GIasReportAnalyticsEnabled,
	TEXT("Enables reporting statics to the analytics system")
);

static int32 GIasHttpRangeRequestMinSizeKiB = 128;
static FAutoConsoleVariableRef CVar_IasHttpRangeRequestMinSizeKiB(
	TEXT("ias.HttpRangeRequestMinSizeKiB"),
	GIasHttpRangeRequestMinSizeKiB,
	TEXT("Minimum chunk size for partial chunk request(s)")
);

static int32 GDistributedEndpointRetryWaitTime = 15;
static FAutoConsoleVariableRef CVar_DistributedEndpointRetryWaitTime(
	TEXT("ias.DistributedEndpointRetryWaitTime"),
	GDistributedEndpointRetryWaitTime,
	TEXT("How long to wait (in seconds) after failing to resolve a distributed endpoint before retrying")
);

static int32 GDistributedEndpointAttemptCount = 5;
static FAutoConsoleVariableRef CVar_DistributedEndpointAttemptCount(
	TEXT("ias.DistributedEndpointAttemptCount"),
	GDistributedEndpointAttemptCount,
	TEXT("Number of times we should try to resolve a distributed endpoint befor eusing the fallback url (if there is one)")
);


// These priorities are indexed using the cvar below
static UE::Tasks::ETaskPriority GCompleteMaterializeTaskPriorities[] =
{
	UE::Tasks::ETaskPriority::High,
	UE::Tasks::ETaskPriority::Normal,
	UE::Tasks::ETaskPriority::BackgroundHigh,
	UE::Tasks::ETaskPriority::BackgroundNormal,
	UE::Tasks::ETaskPriority::BackgroundLow
};

static int32 GCompleteMaterializeTaskPriority = 3;
FAutoConsoleVariableRef CVarCompleteMaterializeTaskPriority(
	TEXT("ias.CompleteMaterializeTaskPriority"),
	GCompleteMaterializeTaskPriority,
	TEXT("Task priority for the CompleteMaterialize task (0 = foreground/high, 1 = foreground/normal, 2 = background/high, 3 = background/normal, 4 = background/low)."),
	ECVF_Default
);

// Thread priority cvar (settable at runtime)
// We declare these explicitly rather than just casting the cvar in case the enum changes in future
const int32 GOnDemandBackendThreadPriorities[] =
{
	EThreadPriority::TPri_Lowest,
	EThreadPriority::TPri_BelowNormal,
	EThreadPriority::TPri_SlightlyBelowNormal,
	EThreadPriority::TPri_Normal,
	EThreadPriority::TPri_AboveNormal
};

const TCHAR* GOnDemandBackendThreadPriorityNames[] =
{
	TEXT("TPri_Lowest"),
	TEXT("TPri_BelowNormal"),
	TEXT("TPri_SlightlyBelowNormal"),
	TEXT("TPri_Normal"),
	TEXT("TPri_AboveNormal")
};

static int32 GOnDemandBackendThreadPriorityIndex = 4; // EThreadPriority::TPri_AboveNormal
FAutoConsoleVariableRef CVarOnDemandBackendThreadPriority(
	TEXT("ias.onDemandBackendThreadPriority"),
	GOnDemandBackendThreadPriorityIndex,
	TEXT("Thread priority of the on demand backend thread: 0=Lowest, 1=BelowNormal, 2=SlightlyBelowNormal, 3=Normal, 4=AboveNormal\n")
	TEXT("Note that this is switchable at runtime"),
	ECVF_Default);

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand CVar_IasAbandonCache(
	TEXT("Ias.AbandonCache"),
	TEXT("Abandon the local file cache"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FIoStoreOnDemandModule& Module = FModuleManager::Get().GetModuleChecked<FIoStoreOnDemandModule>("IoStoreOnDemand");
		Module.AbandonCache();
	})
);
#endif //!UE_BUILD_SHIPPING
///////////////////////////////////////////////////////////////////////////////

static bool LatencyTest(FStringView Url, FStringView Path)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::LatencyTest);

	int32 Results[4] = {};
	LatencyTest(Url, Path, GIasHttpTimeOutMs, MakeArrayView(Results));

	if (Results[0] >= 0 || Results[1] >= 0 || Results[2] >= 0 || Results[3] >= 0)
	{
#if !UE_BUILD_SHIPPING
		UE_LOG(LogIas, Log, TEXT("Endpoint '%s' latency test (ms): %d %d %d %d"),
			Url.GetData(), Results[0], Results[1], Results[2], Results[3]);
#endif // !UE_BUILD_SHIPPING

		return true;
	}
	else
	{
		return false;
	}
}

///////////////////////////////////////////////////////////////////////////////
static int32 LatencyTest(TConstArrayView<FString> Urls, FStringView Path, std::atomic_bool& bCancel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::LatencyTest);

	for (int32 Idx = 0; Idx < Urls.Num() && !bCancel.load(std::memory_order_relaxed); ++Idx)
	{
		int32 LatencyMs = -1;
		LatencyTest(Urls[Idx], Path, GIasHttpTimeOutMs, MakeArrayView(&LatencyMs, 1));
		if (LatencyMs >= 0)
		{
			return Idx;
		}
	}

	return INDEX_NONE;
}
///////////////////////////////////////////////////////////////////////////////
struct FBitWindow
{
	void Reset(uint32 Count)
	{
		Count = FMath::RoundUpToPowerOfTwo(Count);
		Bits.SetNum(int32(Count), false);
		Counter = 0;
		Mask = Count - 1;
	}

	void Add(bool bValue)
	{
		const uint32 Idx = Counter++ & Mask;
		Bits[Idx] = bValue;
	}

	float AvgSetBits() const
	{
		return float(Bits.CountSetBits()) / float(Bits.Num());
	}

private:
	TBitArray<> Bits;
	uint32 Counter = 0;
	uint32 Mask = 0;
};
///////////////////////////////////////////////////////////////////////////////
FIoHash GetChunkKey(const FIoHash& ChunkHash, const FIoOffsetAndLength& Range)
{
	FIoHashBuilder HashBuilder;
	HashBuilder.Update(ChunkHash.GetBytes(), sizeof(FIoHash::ByteArray));
	HashBuilder.Update(&Range, sizeof(FIoOffsetAndLength));

	return HashBuilder.Finalize();
}

///////////////////////////////////////////////////////////////////////////////
template<typename T>
class TThreadSafeIntrusiveQueue
{
public:
	void Enqueue(T* Request)
	{
		check(Request->NextRequest == nullptr);
		FScopeLock _(&CriticalSection);

		if (Tail)
		{
			Tail->NextRequest = Request;
		}
		else
		{
			check(Head == nullptr);
			Head = Request;	
		}

		Tail = Request;
	}

	void EnqueueByPriority(T* Request)
	{
		FScopeLock _(&CriticalSection);
		EnqueueByPriorityInternal(Request);
	}

	T* Dequeue()
	{
		FScopeLock _(&CriticalSection);

		T* Requests = Head;
		Head = Tail = nullptr;

		return Requests;
	}

	void Reprioritize(T* Request)
	{
		// Switch to double linked list/array if this gets too expensive
		FScopeLock _(&CriticalSection);
		if (RemoveInternal(Request))
		{
			EnqueueByPriorityInternal(Request);
		}
	}

private:
	void EnqueueByPriorityInternal(T* Request)
	{
		check(Request->NextRequest == nullptr);

		if (Head == nullptr || Request->Priority > Head->Priority)
		{
			if (Head == nullptr)
			{
				check(Tail == nullptr);
				Tail = Request;
			}

			Request->NextRequest = Head;
			Head = Request;
		}
		else if (Request->Priority <= Tail->Priority)
		{
			check(Tail != nullptr);
			Tail->NextRequest = Request;
			Tail = Request;
		}
		else
		{
			// NOTE: This can get expensive if the queue gets too long, might be better to have x number of bucket(s)
			TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::EnqueueByPriority);
			T* It = Head;
			while (It->NextRequest != nullptr && Request->Priority <= It->NextRequest->Priority)
			{
				It = It->NextRequest;
			}

			Request->NextRequest = It->NextRequest;
			It->NextRequest = Request;
		}
	}

	bool RemoveInternal(T* Request)
	{
		check(Request != nullptr);
		if (Head == nullptr)
		{
			check(Tail == nullptr);
			return false;
		}

		if (Head == Request)
		{
			Head = Request->NextRequest; 
			if (Tail == Request)
			{
				check(Head == nullptr);
				Tail = nullptr;
			}

			Request->NextRequest = nullptr;
			return true;
		}
		else
		{
			T* It = Head;
			while (It->NextRequest && It->NextRequest != Request)
			{
				It = It->NextRequest;
			}

			if (It->NextRequest == Request)
			{
				It->NextRequest = It->NextRequest->NextRequest;
				Request->NextRequest = nullptr;
				if (Tail == Request)
				{
					Tail = It;
				}
				return true;
			}
		}

		return false;
	}

	FCriticalSection CriticalSection;
	T* Head = nullptr;
	T* Tail = nullptr;
};

///////////////////////////////////////////////////////////////////////////////
struct FChunkRequestParams
{
	static FChunkRequestParams Create(const FIoOffsetAndLength& OffsetLength, FOnDemandChunkInfo ChunkInfo)
	{
		FIoOffsetAndLength ChunkRange;
		if (ChunkInfo.EncodedSize() <= (uint64(GIasHttpRangeRequestMinSizeKiB) << 10))
		{
			ChunkRange = FIoOffsetAndLength(0, ChunkInfo.EncodedSize());
		}
		else
		{
			const uint64 RawSize = FMath::Min<uint64>(OffsetLength.GetLength(), ChunkInfo.RawSize() - OffsetLength.GetOffset());

			ChunkRange = FIoChunkEncoding::GetChunkRange(
				ChunkInfo.RawSize(),
				ChunkInfo.BlockSize(),
				ChunkInfo.Blocks(),
				OffsetLength.GetOffset(),
				RawSize).ConsumeValueOrDie();
		}

		return FChunkRequestParams{GetChunkKey(ChunkInfo.Hash(), ChunkRange), ChunkRange, ChunkInfo};
	}

	static FChunkRequestParams Create(FIoRequestImpl* Request, FOnDemandChunkInfo ChunkInfo)
	{
		check(Request);
		check(Request->NextRequest == nullptr);
		return Create(FIoOffsetAndLength(Request->Options.GetOffset(), Request->Options.GetSize()), ChunkInfo);
	}

	const FIoHash& GetUrlHash() const
	{
		return ChunkInfo.Hash();
	}

	void GetUrl(FAnsiStringBuilderBase& Url) const
	{
		const FString HashString = LexToString(ChunkInfo.Hash());
		Url << "/" << ChunkInfo.ChunksDirectory()
			<< "/" << HashString.Left(2)
			<< "/" << HashString << ANSITEXTVIEW(".iochunk");
	}

	FIoChunkDecodingParams GetDecodingParams() const
	{
		FIoChunkDecodingParams Params;
		Params.EncryptionKey = ChunkInfo.EncryptionKey(); 
		Params.CompressionFormat = ChunkInfo.CompressionFormat();
		Params.BlockSize = ChunkInfo.BlockSize();
		Params.TotalRawSize = ChunkInfo.RawSize();
		Params.EncodedBlockSize = ChunkInfo.Blocks(); 
		Params.BlockHash = ChunkInfo.BlockHashes(); 
		Params.EncodedOffset = ChunkRange.GetOffset();

		return Params;
	}

	FIoHash ChunkKey;
	FIoOffsetAndLength ChunkRange;
	FOnDemandChunkInfo ChunkInfo;
};

///////////////////////////////////////////////////////////////////////////////
struct FChunkRequest
{
	explicit FChunkRequest(FIoRequestImpl* Request, const FChunkRequestParams& RequestParams)
		: NextRequest()
		, Params(RequestParams)
		, RequestHead(Request)
		, RequestTail(Request)
		, StartTime(FPlatformTime::Cycles64())
		, Priority(Request->Priority)
		, RequestCount(1)
		, bCached(false)
	{
		check(Request && NextRequest == nullptr);
	}

	bool AddDispatcherRequest(FIoRequestImpl* Request)
	{
		check(RequestHead && RequestTail);
		check(Request && !Request->NextRequest);

		const bool bPriorityChanged = Request->Priority > RequestHead->Priority;
		if (bPriorityChanged)
		{
			Priority = Request->Priority;
			Request->NextRequest = RequestHead;
			RequestHead = Request;
		}
		else
		{
			FIoRequestImpl* It = RequestHead;
			while (It->NextRequest != nullptr && Request->Priority <= It->NextRequest->Priority)
			{
				It = It->NextRequest;
			}

			if (RequestTail == It)
			{
				check(It->NextRequest == nullptr);
				RequestTail = Request;
			}

			Request->NextRequest = It->NextRequest;
			It->NextRequest = Request;
		}

		RequestCount++;
		return bPriorityChanged;
	}

	int32 RemoveDispatcherRequest(FIoRequestImpl* Request)
	{
		check(Request != nullptr);
		check(RequestCount > 0);

		if (RequestHead == Request)
		{
			RequestHead = Request->NextRequest; 
			if (RequestTail == Request)
			{
				check(RequestHead == nullptr);
				RequestTail = nullptr;
			}
		}
		else
		{
			FIoRequestImpl* It = RequestHead;
			while (It->NextRequest != Request)
			{
				It = It->NextRequest;
				if (It == nullptr)
				{
					return INDEX_NONE; // Not found
				}
			}
			check(It->NextRequest == Request);
			It->NextRequest = It->NextRequest->NextRequest;
		}

		Request->NextRequest = nullptr;
		RequestCount--;

		return RequestCount;
	}

	FIoRequestImpl* DeqeueDispatcherRequests()
	{
		FIoRequestImpl* Head = RequestHead;
		RequestHead = RequestTail = nullptr;
		RequestCount = 0;

		return Head;
	}

	FChunkRequest* NextRequest;
	FChunkRequestParams Params;
	FIoRequestImpl* RequestHead;
	FIoRequestImpl* RequestTail;
	FIoBuffer Chunk;
	uint64 StartTime;
	int32 Priority;
	uint16 RequestCount;
	bool bCached;
	bool bCancelled = false;
	EIoErrorCode CacheGetStatus;
};

///////////////////////////////////////////////////////////////////////////////

static void LogIoResult(
	const FIoChunkId& ChunkId,
	const FIoHash& UrlHash,
	uint64 DurationMs,
	uint64 UncompressedSize,
	uint64 UncompressedOffset,
	const FIoOffsetAndLength& ChunkRange,
	uint64 ChunkSize,
	int32 Priority,
	bool bCached)
{
	const TCHAR* Prefix = [bCached, UncompressedSize]() -> const TCHAR*
	{
		if (UncompressedSize == 0)
		{
			return bCached ? TEXT("io-cache-error") : TEXT("io-http-error ");
		}
		return bCached ? TEXT("io-cache") : TEXT("io-http ");
	}();

	auto PrioToString = [](int32 Prio) -> const TCHAR*
	{
		if (Prio < IoDispatcherPriority_Low)
		{
			return TEXT("Min");
		}
		if (Prio < IoDispatcherPriority_Medium)
		{
			return TEXT("Low");
		}
		if (Prio < IoDispatcherPriority_High)
		{
			return TEXT("Medium");
		}
		if (Prio < IoDispatcherPriority_Max)
		{
			return TEXT("High");
		}

		return TEXT("Max");
	};

	UE_LOG(LogIas, VeryVerbose, TEXT("%s: %5" UINT64_FMT "ms %5" UINT64_FMT "KiB[%7" UINT64_FMT "] % s: % s | Range: %" UINT64_FMT "-%" UINT64_FMT "/%" UINT64_FMT " (%.2f%%) | Prio: %s"),
		Prefix,
		DurationMs,
		UncompressedSize >> 10,
		UncompressedOffset,
		*LexToString(ChunkId),
		*LexToString(UrlHash),
		ChunkRange.GetOffset(), (ChunkRange.GetOffset() + ChunkRange.GetLength() - 1), ChunkSize,
		100.0f * (float(ChunkRange.GetLength()) / float(ChunkSize)),
		PrioToString(Priority));
};

///////////////////////////////////////////////////////////////////////////////
struct FBackendStatus
{
	enum class EFlags : uint8
	{
		None						= 0,
		CacheEnabled				= (1 << 0),
		HttpEnabled					= (1 << 1),
		HttpError					= (1 << 2),
		HttpBulkOptionalDisabled	= (1 << 3),
		AbandonCache				= (1 << 4),

		// When adding new values here, remember to update operator<<(FStringBuilderBase& Sb, EFlags StatusFlags) below!
	};

	bool IsHttpEnabled() const
	{
		return IsHttpEnabled(Flags.load(std::memory_order_relaxed));
	}

	bool IsHttpEnabled(EIoChunkType ChunkType) const
	{
		const uint8 CurrentFlags = Flags.load(std::memory_order_relaxed);
		return IsHttpEnabled(CurrentFlags) &&
			(ChunkType != EIoChunkType::OptionalBulkData ||
				((CurrentFlags & uint8(EFlags::HttpBulkOptionalDisabled)) == 0 && GIasHttpOptionalBulkDataEnabled));
	}

	bool IsHttpError() const
	{
		return HasAnyFlags(EFlags::HttpError);
	}

	bool IsCacheEnabled() const
	{
		return HasAnyFlags(EFlags::CacheEnabled);
	}

	bool IsCacheWriteable() const
	{
		const uint8 CurrentFlags = Flags.load(std::memory_order_relaxed);
		return (CurrentFlags & uint8(EFlags::CacheEnabled)) && IsHttpEnabled(CurrentFlags); 
	}

	bool IsCacheReadOnly() const
	{
		const uint8 CurrentFlags = Flags.load(std::memory_order_relaxed);
		return (CurrentFlags & uint8(EFlags::CacheEnabled)) && !IsHttpEnabled(CurrentFlags);
	}

	bool ShouldAbandonCache() const
	{
		return HasAnyFlags(EFlags::AbandonCache);
	}

	void SetHttpEnabled(bool bEnabled)
	{
		AddOrRemoveFlags(EFlags::HttpEnabled, bEnabled, TEXT("HTTP streaming enabled"));
		FGenericCrashContext::SetEngineData(TEXT("IAS.Enabled"), bEnabled ? TEXT("true") : TEXT("false"));
	}

	void SetHttpOptionalBulkEnabled(bool bEnabled)
	{
		AddOrRemoveFlags(EFlags::HttpBulkOptionalDisabled, bEnabled == false, TEXT("HTTP streaming of optional bulk data disabled"));
	}

	void SetCacheEnabled(bool bEnabled)
	{
		AddOrRemoveFlags(EFlags::CacheEnabled, bEnabled, TEXT("Cache enabled"));
	}

	void SetHttpError(bool bError)
	{
		AddOrRemoveFlags(EFlags::HttpError, bError, TEXT("HTTP streaming error"));
	}

	void SetAbandonCache(bool bAbandon)
	{
		AddOrRemoveFlags(EFlags::AbandonCache, bAbandon, TEXT("Abandon cache"));
	}

private:
	static bool IsHttpEnabled(uint8 FlagsToTest)
	{
		constexpr uint8 HttpFlags = uint8(EFlags::HttpEnabled) | uint8(EFlags::HttpError);
		return ((FlagsToTest & HttpFlags) == uint8(EFlags::HttpEnabled)) && GIasHttpEnabled;
	}

	bool HasAnyFlags(uint8 Contains) const
	{
		return (Flags.load(std::memory_order_relaxed) & Contains) != 0;
	}
	
	bool HasAnyFlags(EFlags Contains) const
	{
		return HasAnyFlags(uint8(Contains));
	}

	uint8 AddFlags(EFlags FlagsToAdd)
	{
		return Flags.fetch_or(uint8(FlagsToAdd));
	}

	uint8 RemoveFlags(EFlags FlagsToRemove)
	{
		return Flags.fetch_and(~uint8(FlagsToRemove));
	}

	uint8 AddOrRemoveFlags(EFlags FlagsToAddOrRemove, bool bValue)
	{
		return bValue ? AddFlags(FlagsToAddOrRemove) : RemoveFlags(FlagsToAddOrRemove);
	}

	void AddOrRemoveFlags(EFlags FlagsToAddOrRemove, bool bValue, const TCHAR* DebugText)
	{
		const uint8 PrevFlags = AddOrRemoveFlags(FlagsToAddOrRemove, bValue);
		TStringBuilder<128> Sb;
		Sb.Append(DebugText)
			<< TEXT(" '");
		Sb.Append(bValue ? TEXT("true") : TEXT("false"))
			<< TEXT("', backend status '(")
			<< EFlags(PrevFlags)
			<< TEXT(") -> (")
			<< EFlags(Flags.load(std::memory_order_relaxed))
			<< TEXT(")'");
		UE_LOG(LogIas, Log, TEXT("%s"), Sb.ToString());
	}

	friend FStringBuilderBase& operator<<(FStringBuilderBase& Sb, EFlags StatusFlags)
	{
		if (StatusFlags == EFlags::None)
		{
			Sb.Append(TEXT("None"));
			return Sb;
		}

		bool bFirst = true;
		auto AppendIf = [StatusFlags, &Sb, &bFirst](EFlags Contains, const TCHAR* Str)
		{
			if (uint8(StatusFlags) & uint8(Contains))
			{
				if (!bFirst)
				{
					Sb.AppendChar(TEXT('|'));
				}
				Sb.Append(Str);
				bFirst = false;
			}
		};

		AppendIf(EFlags::CacheEnabled, TEXT("CacheEnabled"));
		AppendIf(EFlags::HttpEnabled, TEXT("HttpEnabled"));
		AppendIf(EFlags::HttpError, TEXT("HttpError"));
		AppendIf(EFlags::HttpBulkOptionalDisabled, TEXT("HttpBulkOptionalDisabled"));
		AppendIf(EFlags::AbandonCache, TEXT("AbandonCache"));

		return Sb;
	}

	std::atomic<uint8> Flags{0};
};
///////////////////////////////////////////////////////////////////////////////
class FOnDemandIoBackend final
	: public FRunnable
	, public IOnDemandIoDispatcherBackend
{
	using FIoRequestQueue = TThreadSafeIntrusiveQueue<FIoRequestImpl>;
	using FChunkRequestQueue = TThreadSafeIntrusiveQueue<FChunkRequest>;

	struct FAvailableEps
	{
		bool HasCurrent() const { return Current != INDEX_NONE; }
		const FString& GetCurrent() const { return Urls[Current]; }

		int32 Current = INDEX_NONE;
		TArray<FString> Urls;
	};

	struct FBackendData
	{
		static void Attach(FIoRequestImpl* Request, const FIoHash& ChunkKey)
		{
			check(Request->BackendData == nullptr);
			Request->BackendData = new FBackendData{ChunkKey};
		}

		static TUniquePtr<FBackendData> Detach(FIoRequestImpl* Request)
		{
			check(Request->BackendData != nullptr);
			void* BackendData = Request->BackendData;
			Request->BackendData = nullptr;
			return TUniquePtr<FBackendData>(static_cast<FBackendData*>(BackendData));
		}
		
		static FBackendData* Get(FIoRequestImpl* Request)
		{
			return static_cast<FBackendData*>(Request->BackendData);
		}

		FIoHash ChunkKey;
	};

	struct FChunkRequests
	{
		FChunkRequest* TryUpdatePriority(FIoRequestImpl* Request)
		{
			FScopeLock _(&Mutex);

			const FBackendData* BackendData = FBackendData::Get(Request);
			if (BackendData == nullptr)
			{
				return nullptr;
			}

			if (FChunkRequest** InflightRequest = Inflight.Find(BackendData->ChunkKey))
			{
				FChunkRequest* ChunkRequest = *InflightRequest;
				if (Request->Priority > ChunkRequest->Priority)
				{
					ChunkRequest->Priority = Request->Priority;
					return ChunkRequest;
				}
			}

			return nullptr;
		}

		FChunkRequest* Create(FIoRequestImpl* Request, const FChunkRequestParams& Params, bool& bOutPending, bool& bOutUpdatePriority)
		{
			FScopeLock _(&Mutex);
			
			FBackendData::Attach(Request, Params.ChunkKey);

			if (FChunkRequest** InflightRequest = Inflight.Find(Params.ChunkKey))
			{
				FChunkRequest* ChunkRequest = *InflightRequest;
				check(!ChunkRequest->bCancelled);
				bOutPending = true;
				bOutUpdatePriority = ChunkRequest->AddDispatcherRequest(Request);

				return ChunkRequest;
			}

			bOutPending = bOutUpdatePriority = false;
			FChunkRequest* ChunkRequest = Allocator.Construct(Request, Params);
			ChunkRequestCount++;
			Inflight.Add(Params.ChunkKey, ChunkRequest);

			return ChunkRequest;
		}

		bool Cancel(FIoRequestImpl* Request, IIasCache* TheCache)
		{
			FScopeLock _(&Mutex);

			const FBackendData* BackendData = FBackendData::Get(Request);
			if (BackendData == nullptr)
			{
				return false;
			}

			UE_LOG(LogIas, VeryVerbose, TEXT("%s"),
				*WriteToString<256>(TEXT("Cancelling I/O request ChunkId='"), Request->ChunkId, TEXT("' ChunkKey='"), BackendData->ChunkKey, TEXT("'")));

			if (FChunkRequest** InflightRequest = Inflight.Find(BackendData->ChunkKey))
			{
				FChunkRequest& ChunkRequest = **InflightRequest;
				const int32 RemainingCount = ChunkRequest.RemoveDispatcherRequest(Request);
				if (RemainingCount == INDEX_NONE)
				{
					// Not found
					// When a request A with ChunkKey X enters CompleteRequest its Inflight entry X->A is removed.
					// If a new request B with the same ChunkKey X is made, then Resolve will add a new Infligt entry X->B.
					// If we at this point cancel A, we will find the Inflight entry for B, which will not contain A, which is fine.
					return false;
				}

				check(Request->NextRequest == nullptr);

				if (RemainingCount == 0)
				{
					ChunkRequest.bCancelled = true;
					if (TheCache != nullptr)
					{
						TheCache->Cancel(ChunkRequest.Chunk);
					}
					Inflight.Remove(BackendData->ChunkKey);
				}

				return true;
			}

			return false;
		}

		FIoChunkId GetChunkId(FChunkRequest* Request)
		{
			FScopeLock _(&Mutex);
			return Request->RequestHead ? Request->RequestHead->ChunkId : FIoChunkId::InvalidChunkId;
		}

		void Remove(FChunkRequest* Request)
		{
			FScopeLock _(&Mutex);
			Inflight.Remove(Request->Params.ChunkKey);
		}

		void Release(FChunkRequest* Request)
		{
			FScopeLock _(&Mutex);
			Destroy(Request);
		}
		
		int32 Num()
		{
			FScopeLock _(&Mutex);
			return ChunkRequestCount; 
		}

	private:
		void Destroy(FChunkRequest* Request)
		{
			Allocator.Destroy(Request);
			ChunkRequestCount--;
			check(ChunkRequestCount >= 0);
		}

		TSingleThreadedSlabAllocator<FChunkRequest, 128> Allocator;
		TMap<FIoHash, FChunkRequest*> Inflight;
		FCriticalSection Mutex;
		int32 ChunkRequestCount = 0;
	};
public:

	FOnDemandIoBackend(const FOnDemandEndpointConfig& InConfig, FOnDemandIoStore& InIoStore, TUniquePtr<IIasCache>&& InCache);
	virtual ~FOnDemandIoBackend();

	// I/O dispatcher backend
	virtual void Initialize(TSharedRef<const FIoDispatcherBackendContext> Context) override;
	virtual void Shutdown() override;
	virtual void ResolveIoRequests(FIoRequestList Requests, FIoRequestList& OutUnresolved) override;
	virtual void CancelIoRequest(FIoRequestImpl* Request) override;
	virtual void UpdatePriorityForIoRequest(FIoRequestImpl* Request) override;
	virtual bool DoesChunkExist(const FIoChunkId& ChunkId) const override;
	virtual bool DoesChunkExist(const FIoChunkId& ChunkId, const FIoOffsetAndLength& ChunkRange) const override;
	virtual TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const override;
	virtual TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId, const FIoOffsetAndLength& ChunkRange, uint64& OutAvailable) const;
	virtual FIoRequestImpl* GetCompletedIoRequests() override;
	virtual TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) override;

	// I/O Http backend
	virtual void SetBulkOptionalEnabled(bool bEnabled) override;
	virtual void SetEnabled(bool bEnabled) override;
	virtual bool IsEnabled() const override;
	virtual void AbandonCache() override;
	virtual void ReportAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const override;

	// Runnable
	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	bool Resolve(FIoRequestImpl* Request);

	FString GetEndpointTestPath() const;
	void ConditionallyStartBackendThread();
	void CompleteRequest(FChunkRequest* ChunkRequest);
	void CompleteMaterialize(FChunkRequest* ChunkRequest);
	bool ResolveDistributedEndpoint(const FDistributedEndpointUrl& Url);
	void ProcessHttpRequests(FHttpClient& HttpClient, FBitWindow& HttpErrors, int32 MaxConcurrentRequests);
	int32 WaitForCompleteRequestTasks(float WaitTimeSeconds, float PollTimeSeconds);
	void DrainHttpRequests();
	void UpdateThreadPriorityIfNeeded();


	FOnDemandIoStore& IoStore;
	TUniquePtr<IIasCache> Cache;
	TSharedPtr<const FIoDispatcherBackendContext> BackendContext;
	TUniquePtr<FRunnableThread> BackendThread;
	FEventRef TickBackendEvent;
	FChunkRequests ChunkRequests;
	FIoRequestQueue CompletedRequests;
	FChunkRequestQueue HttpRequests;
	TArray<FChunkRequest*> PendingCacheGets;
	FOnDemandIoBackendStats Stats;
	FBackendStatus BackendStatus;
	FAvailableEps AvailableEps;
	FDistributedEndpointUrl DistributionUrl;
	FEventRef DistributedEndpointEvent;

	FString EndpointTestPath;
	EThreadPriority CurrentThreadPriority;

	mutable FRWLock Lock;
	std::atomic_uint32_t InflightCacheRequestCount{0};
	std::atomic_bool bStopRequested{false};

#if UE_IAS_DEBUG_CONSOLE_CMDS
	TArray<IConsoleCommand*> DynamicConsoleCommands;
#endif // UE_IAS_DEBUG_CONSOLE_CMDS
};

///////////////////////////////////////////////////////////////////////////////
FOnDemandIoBackend::FOnDemandIoBackend(const FOnDemandEndpointConfig& Config, FOnDemandIoStore& InIoStore, TUniquePtr<IIasCache>&& InCache)
	: IoStore(InIoStore)
	, Cache(MoveTemp(InCache))
	, Stats(Cache.IsValid() ? EStatsFlags::None : EStatsFlags::CachingDisabled)
	, CurrentThreadPriority(EThreadPriority::TPri_Num)
{
	EndpointTestPath = Config.TocPath;
	if (Config.DistributionUrl.IsEmpty() == false)
	{
		DistributionUrl = { Config.DistributionUrl, Config.FallbackUrl };
	}
	else
	{
		for (const FString& ServiceUrl : Config.ServiceUrls)
		{
			for (const FString& Url : Config.ServiceUrls)
			{
				AvailableEps.Urls.Add(Url.Replace(TEXT("https"), TEXT("http")).ToLower());
			}
		}
	}

	// Don't enable HTTP until the background thread has been started
	BackendStatus.SetHttpEnabled(false);
	BackendStatus.SetCacheEnabled(Cache.IsValid());

#if UE_IAS_DEBUG_CONSOLE_CMDS
	DynamicConsoleCommands.Emplace(
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ias.InvokeHttpFailure"),
		TEXT("Marks the current ias http connection as failed forcing the system to try to reconnect"),
		FConsoleCommandDelegate::CreateLambda([this]()
			{
				UE_LOG(LogIas, Display, TEXT("User invoked http error via 'ias.InvokeHttpFailure'"));
				BackendStatus.SetHttpError(true);

				TickBackendEvent->Trigger();
			}),
		ECVF_Cheat)
	);
#endif // UE_IAS_DEBUG_CONSOLE_CMDS
}

FOnDemandIoBackend::~FOnDemandIoBackend()
{
#if UE_IAS_DEBUG_CONSOLE_CMDS
	for (IConsoleCommand* Cmd : DynamicConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Cmd);
	}
#endif // UE_IAS_DEBUG_CONSOLE_CMDS

	Shutdown();
}

void FOnDemandIoBackend::Initialize(TSharedRef<const FIoDispatcherBackendContext> Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::Initialize);
	LLM_SCOPE_BYTAG(Ias);
	UE_LOG(LogIas, Log, TEXT("Initializing on demand I/O dispatcher backend"));
	BackendContext = Context;

	ConditionallyStartBackendThread();
}

void FOnDemandIoBackend::Shutdown()
{
	if (bStopRequested)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::Shutdown);

	UE_LOG(LogIas, Log, TEXT("Shutting down on demand I/O dispatcher backend"));

	// Stop and wait for our backend thread to finish.
	// Note, the IoDispatcher typically waits for all its pending io requests before shutting down its backends.
	BackendThread.Reset();

	// Drain any reamaining (cancelled) http requests that already been completed from the IoDispatcher point of view.
	DrainHttpRequests();

	// The CompleteRequest tasks may still be executing a while after the IoDispatcher has been notified about the completed io requests.
	const int32 NumPending = WaitForCompleteRequestTasks(5.0f, 0.1f);
	UE_CLOG(NumPending > 0, LogIas, Warning, TEXT("%d request(s) still pending after shutdown"), NumPending);

	BackendContext.Reset();
}

FString FOnDemandIoBackend::GetEndpointTestPath() const
{
	return EndpointTestPath;
}

void FOnDemandIoBackend::UpdateThreadPriorityIfNeeded()
{
	// Read the thread priority from the cvar
	int32 ThreadPriorityIndex = FMath::Clamp(GOnDemandBackendThreadPriorityIndex, 0, (int32)UE_ARRAY_COUNT(GOnDemandBackendThreadPriorities) - 1);
	EThreadPriority DesiredThreadPriority = (EThreadPriority)GOnDemandBackendThreadPriorities[ThreadPriorityIndex];
	if (DesiredThreadPriority != CurrentThreadPriority)
	{
		UE_LOG(LogIas, Log, TEXT("Setting backend http thread priority to %s"), GOnDemandBackendThreadPriorityNames[ThreadPriorityIndex]);
		FPlatformProcess::SetThreadPriority(DesiredThreadPriority);
		CurrentThreadPriority = DesiredThreadPriority;
	}
}


void FOnDemandIoBackend::ConditionallyStartBackendThread()
{
	FWriteScopeLock _(Lock);
	if (BackendThread.IsValid() == false)
	{
		// Read the desired thread priority from the cvar
		int32 ThreadPriorityIndex = FMath::Clamp(GOnDemandBackendThreadPriorityIndex, 0, (int32)UE_ARRAY_COUNT(GOnDemandBackendThreadPriorities) - 1);
		EThreadPriority DesiredThreadPriority = (EThreadPriority)GOnDemandBackendThreadPriorities[ThreadPriorityIndex];
		CurrentThreadPriority = DesiredThreadPriority;

		BackendThread.Reset(FRunnableThread::Create(this, TEXT("Ias.Http"), 0, DesiredThreadPriority));
	}
}

void FOnDemandIoBackend::CompleteRequest(FChunkRequest* ChunkRequest)
{
	LLM_SCOPE_BYTAG(Ias);
	TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::CompleteRequest);
	check(ChunkRequest != nullptr);

	if (ChunkRequest->bCancelled)
	{
		check(ChunkRequest->RequestHead == nullptr);
		check(ChunkRequest->RequestTail == nullptr);
		return ChunkRequests.Release(ChunkRequest);
	}

	ChunkRequests.Remove(ChunkRequest);
	
	FIoBuffer Chunk = MoveTemp(ChunkRequest->Chunk);
	FIoChunkDecodingParams DecodingParams = ChunkRequest->Params.GetDecodingParams();

	// Only cache chunks if HTTP streaming is enabled
	bool bCacheChunk = ChunkRequest->bCached == false && Chunk.GetSize() > 0;
	FIoRequestImpl* NextRequest = ChunkRequest->DeqeueDispatcherRequests();
	while (NextRequest)
	{
		FIoRequestImpl* Request = NextRequest;
		NextRequest = Request->NextRequest;
		Request->NextRequest = nullptr;

		bool bDecoded = false;
		if (Chunk.GetSize() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::DecodeBlocks);
			const uint64 RawSize = FMath::Min<uint64>(Request->Options.GetSize(), ChunkRequest->Params.ChunkInfo.RawSize());
			Request->CreateBuffer(RawSize);
			DecodingParams.RawOffset = Request->Options.GetOffset(); 
			bDecoded = FIoChunkEncoding::Decode(DecodingParams, Chunk.GetView(), Request->GetBuffer().GetMutableView());

			if (!bDecoded)
			{
				Stats.OnIoDecodeError();
			}
		}
		
		const uint64 DurationMs = Request->GetStartTime() > 0 ?
			(uint64)FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Request->GetStartTime()) : 0;

		if (bDecoded)
		{
			Stats.OnIoRequestComplete(Request->GetBuffer().GetSize(), DurationMs);
			LogIoResult(Request->ChunkId, ChunkRequest->Params.GetUrlHash(), DurationMs,
				Request->GetBuffer().DataSize(), Request->Options.GetOffset(),
				ChunkRequest->Params.ChunkRange, ChunkRequest->Params.ChunkInfo.EncodedSize(),
				ChunkRequest->Priority, ChunkRequest->bCached);
				
		}
		else
		{
			bCacheChunk = false;
			Request->SetFailed();

			Stats.OnIoRequestError();
			LogIoResult(Request->ChunkId, ChunkRequest->Params.GetUrlHash(), DurationMs,
				0, Request->Options.GetOffset(),
				ChunkRequest->Params.ChunkRange, ChunkRequest->Params.ChunkInfo.EncodedSize(),
				ChunkRequest->Priority, ChunkRequest->bCached);
		}

		CompletedRequests.Enqueue(Request);
		BackendContext->WakeUpDispatcherThreadDelegate.Execute();
	}

	if (bCacheChunk && BackendStatus.IsCacheWriteable())
	{
		Cache->Put(ChunkRequest->Params.ChunkKey, Chunk);
	}

	if (BackendStatus.ShouldAbandonCache() && InflightCacheRequestCount.load(std::memory_order_relaxed) == 0)
	{
		TickBackendEvent->Trigger();
	}

	ChunkRequests.Release(ChunkRequest);
}

void FOnDemandIoBackend::CompleteMaterialize(FChunkRequest* ChunkRequest)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::CompleteMaterialize);

	bool bWasCancelled = false;
	switch (ChunkRequest->CacheGetStatus)
	{
		case EIoErrorCode::Ok:
		check(ChunkRequest->Chunk.GetData() != nullptr);
		ChunkRequest->bCached = true;
		CompleteRequest(ChunkRequest);
		return;

	case EIoErrorCode::ReadError:
		FOnDemandIoBackendStats::Get()->OnCacheError();
		break;

	case EIoErrorCode::Cancelled:
		bWasCancelled = true;
		break;

	case EIoErrorCode::NotFound:
		break;
	}

	if (bWasCancelled || BackendStatus.IsHttpEnabled() == false)
	{
		UE_CLOG(BackendStatus.IsHttpEnabled() == false, LogIas, Log, TEXT("Chunk was not found in the cache and HTTP is disabled"));
		CompleteRequest(ChunkRequest);
		return;
	}

	Stats.OnHttpEnqueue();
	HttpRequests.EnqueueByPriority(ChunkRequest);
	TickBackendEvent->Trigger();
}

bool FOnDemandIoBackend::Resolve(FIoRequestImpl* Request)
{
	using namespace UE::Tasks;

	FOnDemandChunkInfo ChunkInfo = IoStore.GetStreamingChunkInfo(Request->ChunkId);
	if (!ChunkInfo.IsValid())
	{
		return false;
	}

	FChunkRequestParams RequestParams = FChunkRequestParams::Create(Request, ChunkInfo);

	if (BackendStatus.IsHttpEnabled(Request->ChunkId.GetChunkType()) == false)
	{ 
		// If the cache is not readonly the chunk may get evicted before the request is completed
		if (BackendStatus.IsCacheReadOnly() == false || Cache->ContainsChunk(RequestParams.ChunkKey) == false)
		{
			return false;
		}
	}

	Stats.OnIoRequestEnqueue();
	bool bPending = false;
	bool bUpdatePriority = false;
	FChunkRequest* ChunkRequest = ChunkRequests.Create(Request, RequestParams, bPending, bUpdatePriority);

	if (bPending)
	{
		if (bUpdatePriority)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::UpdatePriorityForIoRequest);
			HttpRequests.Reprioritize(ChunkRequest);
		}
		// The chunk for the request is already inflight 
		return true;
	}

	if (Cache.IsValid())
	{
		const FIoHash& Key = ChunkRequest->Params.ChunkKey;
		FIoBuffer& Buffer = ChunkRequest->Chunk;

		//TODO: Pass priority to cache
		EIoErrorCode GetStatus = Cache->Get(Key, Buffer);

		if (GetStatus == EIoErrorCode::Ok)
		{
			check(Buffer.GetData() != nullptr);
			ChunkRequest->bCached = true;

			UE::Tasks::ETaskPriority TaskPriority = GCompleteMaterializeTaskPriorities[FMath::Clamp(GCompleteMaterializeTaskPriority, 0, UE_ARRAY_COUNT(GCompleteMaterializeTaskPriorities) - 1)];
			Launch(UE_SOURCE_LOCATION, [this, ChunkRequest] {
				CompleteRequest(ChunkRequest);
			}, TaskPriority);
			return true;
		}

		if (GetStatus == EIoErrorCode::FileNotOpen)
		{
			InflightCacheRequestCount.fetch_add(1, std::memory_order_relaxed);

			FTaskEvent OnReadyEvent(TEXT("IasCacheMaterializeDone"));
			UE::Tasks::ETaskPriority TaskPriority = GCompleteMaterializeTaskPriorities[FMath::Clamp(GCompleteMaterializeTaskPriority, 0, UE_ARRAY_COUNT(GCompleteMaterializeTaskPriorities)-1)];

			Launch(UE_SOURCE_LOCATION, [this, ChunkRequest] {
				InflightCacheRequestCount.fetch_sub(1, std::memory_order_relaxed);
				CompleteMaterialize(ChunkRequest);
			}, OnReadyEvent, TaskPriority);

			EIoErrorCode& OutStatus = ChunkRequest->CacheGetStatus;
			Cache->Materialize(Key, Buffer, OutStatus, MoveTemp(OnReadyEvent));
			return true;
		}

		check(GetStatus == EIoErrorCode::NotFound);
	}

	Stats.OnHttpEnqueue();
	HttpRequests.EnqueueByPriority(ChunkRequest);
	TickBackendEvent->Trigger();
	return true;
}

void FOnDemandIoBackend::ResolveIoRequests(FIoRequestList Requests, FIoRequestList& OutUnresolved)
{
	while (FIoRequestImpl* Request = Requests.PopHead())
	{
		if (Resolve(Request) == false)
		{
			OutUnresolved.AddTail(Request);
		}
	}
}

void FOnDemandIoBackend::CancelIoRequest(FIoRequestImpl* Request)
{
	if (ChunkRequests.Cancel(Request, Cache.Get()))
	{
		CompletedRequests.Enqueue(Request);
		BackendContext->WakeUpDispatcherThreadDelegate.Execute();
	}
}

void FOnDemandIoBackend::UpdatePriorityForIoRequest(FIoRequestImpl* Request)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::UpdatePriorityForIoRequest);
	if (FChunkRequest* ChunkRequest = ChunkRequests.TryUpdatePriority(Request))
	{
		HttpRequests.Reprioritize(ChunkRequest);
	}
}

bool FOnDemandIoBackend::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	const TIoStatusOr<uint64> ChunkSize = GetSizeForChunk(ChunkId);
	return ChunkSize.IsOk();
}

bool FOnDemandIoBackend::DoesChunkExist(const FIoChunkId& ChunkId, const FIoOffsetAndLength& ChunkRange) const
{
	uint64 Unused = 0;
	const TIoStatusOr<uint64> ChunkSize = GetSizeForChunk(ChunkId, ChunkRange, Unused);
	return ChunkSize.IsOk();
}

TIoStatusOr<uint64> FOnDemandIoBackend::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	uint64 Unused = 0;
	const FIoOffsetAndLength ChunkRange(0, MAX_uint64);
	return GetSizeForChunk(ChunkId, ChunkRange, Unused);
}

TIoStatusOr<uint64> FOnDemandIoBackend::GetSizeForChunk(const FIoChunkId& ChunkId, const FIoOffsetAndLength& ChunkRange, uint64& OutAvailable) const
{
	OutAvailable = 0;

	const FOnDemandChunkInfo ChunkInfo = IoStore.GetStreamingChunkInfo(ChunkId);
	if (ChunkInfo.IsValid() == false)
	{
		return FIoStatus(EIoErrorCode::UnknownChunkID);
	}

	FIoOffsetAndLength RequestedRange(ChunkRange.GetOffset(), FMath::Min<uint64>(ChunkInfo.RawSize(), ChunkRange.GetLength()));
	OutAvailable = ChunkInfo.RawSize();

	if (BackendStatus.IsHttpEnabled(ChunkId.GetChunkType()) == false)
	{
		// If the cache is not readonly the chunk may get evicted before the request is resolved
		if (BackendStatus.IsCacheReadOnly() == false)
		{
			return FIoStatus(EIoErrorCode::UnknownChunkID);
		}

		check(Cache.IsValid());
		const FChunkRequestParams RequestParams = FChunkRequestParams::Create(RequestedRange, ChunkInfo);
		if (Cache->ContainsChunk(RequestParams.ChunkKey) == false)
		{
			return FIoStatus(EIoErrorCode::UnknownChunkID);
		}

		// Only the specified chunk range is available 
		OutAvailable = RequestedRange.GetLength();
	}

	return TIoStatusOr<uint64>(ChunkInfo.RawSize());
}

FIoRequestImpl* FOnDemandIoBackend::GetCompletedIoRequests()
{
	FIoRequestImpl* Requests = CompletedRequests.Dequeue();

	for (FIoRequestImpl* It = Requests; It != nullptr; It = It->NextRequest)
	{
		TUniquePtr<FBackendData> BackendData = FBackendData::Detach(It);
		check(It->BackendData == nullptr);
	}

	return Requests;
}

TIoStatusOr<FIoMappedRegion> FOnDemandIoBackend::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	return FIoStatus::Unknown;
}

bool FOnDemandIoBackend::ResolveDistributedEndpoint(const FDistributedEndpointUrl& DistributedEndpointUrl)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::ResolveDistributedEndpoint);

	check(DistributedEndpointUrl.IsValid());

	// We need to resolve the end point in this method which occurs after the config system has initialized
	// rather than in ::Mount which can occur before that.
	// Without the config system initialized the http module will not work properly and we will always fail
	// to resolve and the OnDemand system will not recover.
	check(GConfig->IsReadyForUse());

	int32 NumAttempts = 0;

	while (!bStopRequested)
	{
		TArray<FString> ServiceUrls;

		FDistributionEndpoints Resolver;
		FDistributionEndpoints::EResult Result = Resolver.ResolveEndpoints(DistributedEndpointUrl.EndpointUrl, ServiceUrls, *DistributedEndpointEvent.Get());
		if (Result == FDistributionEndpoints::EResult::Success)
		{
			FWriteScopeLock _(Lock);
			AvailableEps.Urls.Reserve(AvailableEps.Urls.Num() + ServiceUrls.Num());

			for (const FString& Url : ServiceUrls)
			{
				AvailableEps.Urls.Add(Url.Replace(TEXT("https"), TEXT("http")));
			}

			return true;
		}

		if (DistributedEndpointUrl.HasFallbackUrl() && ++NumAttempts == GDistributedEndpointAttemptCount)
		{
			FString FallbackUrl = DistributedEndpointUrl.FallbackUrl.Replace(TEXT("https"), TEXT("http"));
			UE_LOG(LogIas, Warning, TEXT("Failed to resolve the distributed endpoint %d times. Fallback CDN '%s' will be used instead"), GDistributedEndpointAttemptCount , *FallbackUrl);
			
			FWriteScopeLock _(Lock);
			AvailableEps.Urls.Emplace(MoveTemp(FallbackUrl));
		
			return true;
		}

		if (!bStopRequested)
		{
			const uint32 WaitTime = GDistributedEndpointRetryWaitTime >= 0 ? (static_cast<uint32>(GDistributedEndpointRetryWaitTime) * 1000) : MAX_uint32;
			DistributedEndpointEvent->Wait(WaitTime);
		}
	}

	return false;
}

void FOnDemandIoBackend::SetBulkOptionalEnabled(bool bEnabled)
{
	BackendStatus.SetHttpOptionalBulkEnabled(bEnabled);
}

void FOnDemandIoBackend::SetEnabled(bool bEnabled)
{
	BackendStatus.SetHttpEnabled(bEnabled);
}

bool FOnDemandIoBackend::IsEnabled() const
{
	return BackendStatus.IsHttpEnabled();
}

void FOnDemandIoBackend::AbandonCache()
{
	BackendStatus.SetCacheEnabled(false);
	BackendStatus.SetAbandonCache(true);
}

void FOnDemandIoBackend::ReportAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const
{
	// If we got this far we know that IAS is enabled for the current process as it has a valid backend.
	// However just because IAS is enabled does not mean we have managed to make a valid connection yet.

	if (!GIasReportAnalyticsEnabled)
	{
		return;
	}

	Stats.ReportGeneralAnalytics(OutAnalyticsArray);

	if (AvailableEps.HasCurrent())
	{
		FString CdnUrl = AvailableEps.GetCurrent();

		// Strip the prefix from the url as some analytics systems may have trouble dealing with it
		if (!CdnUrl.RemoveFromStart(TEXT("http://")))
		{
			CdnUrl.RemoveFromStart(TEXT("https://"));
		}

		AppendAnalyticsEventAttributeArray(OutAnalyticsArray, TEXT("IasCdnUrl"), MoveTemp(CdnUrl));

		Stats.ReportEndPointAnalytics(OutAnalyticsArray);
	}
}

void FOnDemandIoBackend::ProcessHttpRequests(FHttpClient& HttpClient, FBitWindow& HttpErrors, int32 MaxConcurrentRequests)
{
	int32 NumConcurrentRequests = 0;
	FChunkRequest* NextChunkRequest = HttpRequests.Dequeue();

	while (NextChunkRequest)
	{
		while (NextChunkRequest)
		{
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::IssueHttpGet);
				FChunkRequest* ChunkRequest = NextChunkRequest;
				NextChunkRequest = ChunkRequest->NextRequest;
				ChunkRequest->NextRequest = nullptr;

				Stats.OnHttpDequeue();

				if (ChunkRequest->bCancelled)
				{
					CompleteRequest(ChunkRequest);
					Stats.OnHttpCancel();
				}
				else if (BackendStatus.IsHttpEnabled() == false)
				{
					CompleteRequest(ChunkRequest);
					// Technically this request is being skipped because of a pre-existing error. It is not
					// an error itself and it is not being canceled by higher level code. However we do not
					// currently have a statistic for that and we have to call one of the existing types in
					// order to correctly reduce the pending count.
					Stats.OnHttpCancel();
				}
				else
				{
					check(HttpClient.GetEndpoint() != INDEX_NONE);
					TAnsiStringBuilder<256> Url;
					ChunkRequest->Params.GetUrl(Url);

					NumConcurrentRequests++;
					HttpClient.Get(Url.ToView(), ChunkRequest->Params.ChunkRange,
						[this, &NextChunkRequest, ChunkRequest, &NumConcurrentRequests, &HttpErrors]
						(TIoStatusOr<FIoBuffer> Status, uint64 DurationMs)
						{
							NumConcurrentRequests--;
							switch (Status.Status().GetErrorCode())
							{
							case EIoErrorCode::Ok:
							{
								HttpErrors.Add(false);
								ChunkRequest->Chunk = Status.ConsumeValueOrDie();
								Stats.OnHttpGet(ChunkRequest->Chunk.DataSize(), DurationMs);
								break;
							}
							case EIoErrorCode::ReadError:
							case EIoErrorCode::NotFound:
							{
								Stats.OnHttpError();
								HttpErrors.Add(true);

								const float Average = HttpErrors.AvgSetBits();
								const bool bAboveHighWaterMark = Average > GIasHttpErrorHighWater;
								UE_LOG(LogIas, Log, TEXT("%.2f%% the last %d HTTP requests failed"), Average * 100.0f, GIasHttpErrorSampleCount);

								if (bAboveHighWaterMark && BackendStatus.IsHttpEnabled())
								{
									BackendStatus.SetHttpError(true);
									UE_LOG(LogIas, Warning, TEXT("HTTP streaming disabled due to high water mark of %.2f of the last %d requests reached"),
										GIasHttpErrorHighWater * 100.0f, GIasHttpErrorSampleCount);
								}
								break;
							}
							case EIoErrorCode::Cancelled:
							{
								const FIoChunkId ChunkId = ChunkRequests.GetChunkId(ChunkRequest);
								UE_LOG(LogIas, Log, TEXT("HTTP request for chunk '%s' cancelled"), *LexToString(ChunkId));
								break;
							}
							default:
							{
								const FIoChunkId ChunkId = ChunkRequests.GetChunkId(ChunkRequest);
								UE_LOG(LogIas, Warning, TEXT("Unhandled HTTP response '%s' for chunk '%s'"),
									GetIoErrorText(Status.Status().GetErrorCode()), *LexToString(ChunkId));
								break;
							}
							}

							UE::Tasks::ETaskPriority TaskPriority = GCompleteMaterializeTaskPriorities[FMath::Clamp(GCompleteMaterializeTaskPriority, 0, UE_ARRAY_COUNT(GCompleteMaterializeTaskPriorities) - 1)];
							UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, ChunkRequest]()
							{
								CompleteRequest(ChunkRequest);
							}, TaskPriority);
						});
				}
			}

			if (NumConcurrentRequests >= MaxConcurrentRequests)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::TickHttpSaturated);
				while (NumConcurrentRequests >= MaxConcurrentRequests) //-V654
				{
					HttpClient.Tick(MAX_uint32, GIasHttpRateLimitKiBPerSecond);
				}
			}

			if (!NextChunkRequest)
			{
				NextChunkRequest = HttpRequests.Dequeue();
			}
		}

		{
			// Keep processing pending connections until all requests are completed or a new one is issued
			TRACE_CPUPROFILER_EVENT_SCOPE(IasBackend::TickHttp);
			while (HttpClient.Tick(GIasHttpPollTimeoutMs, GIasHttpRateLimitKiBPerSecond))
			{
				if (!NextChunkRequest)
				{
					NextChunkRequest = HttpRequests.Dequeue();
				}
				if (NextChunkRequest)
				{
					break;
				}
			}
		}
	} 
}

void FOnDemandIoBackend::DrainHttpRequests()
{
	FChunkRequest* Iterator = HttpRequests.Dequeue();
	while (Iterator != nullptr)
	{
		FChunkRequest* Request = Iterator;
		Iterator = Iterator->NextRequest;

		Stats.OnHttpDequeue();
		CompleteRequest(Request);
		Stats.OnHttpCancel();
	}
}

uint32 FOnDemandIoBackend::Run()
{
	LLM_SCOPE_BYTAG(Ias);

	if (DistributionUrl.IsValid())
	{
		if (ResolveDistributedEndpoint(DistributionUrl) == false)
		{
			// ResolveDistributedEndpoint should spin forever until either a valid url is found or
			// we give up and use a predetermined fallback url. If this returned false then we didn't
			// have a fallback url but the current process is shutting down so we might as well just
			// exist the thread early.
			UE_LOG(LogIas, Warning, TEXT("Failed to resolve CDN endpoints from distribution URL"));
			BackendStatus.SetHttpEnabled(false);

			return 0;
		}
	}

	if (AvailableEps.Urls.IsEmpty())
	{
		UE_LOG(LogIas, Error, TEXT("HTTP streaming disabled, no valid endpoint"));
		BackendStatus.SetHttpEnabled(false);
		return 0;
	}

	if (GIasHttpPrimaryEndpoint < 0)
	{
		UE_LOG(LogIas, Error, TEXT("ias.HttpPrimaryEndpoint should not be set as a negative number, defaulting to 0"));
		GIasHttpPrimaryEndpoint = 0;
	}

	// Rotate the list of urls so that the primary endpoint is the first element
	Algo::Rotate(AvailableEps.Urls, GIasHttpPrimaryEndpoint);
	AvailableEps.Current = 0;

	FBitWindow HttpErrors;
	HttpErrors.Reset(GIasHttpErrorSampleCount);

	TUniquePtr<FHttpClient> HttpClient = FHttpClient::Create(FHttpClientConfig
	{
		.Endpoints = AvailableEps.Urls,
		.PrimaryEndpoint = AvailableEps.Current,
		.MaxConnectionCount = GIasHttpConnectionCount,
		.MaxRetryCount = FMath::Max(AvailableEps.Urls.Num() + 1, GIasHttpRetryCount),
		.ReceiveBufferSize = GIasHttpRecvBufKiB >= 0 ? GIasHttpRecvBufKiB << 10 : -1,
		.bChangeEndpointAfterSuccessfulRetry = GIasHttpChangeEndpointAfterSuccessfulRetry,
	});

	check(HttpClient.IsValid());
	
	if (LatencyTest(AvailableEps.GetCurrent(), GetEndpointTestPath()))
	{
		HttpClient->SetEndpoint(AvailableEps.Current);

		BackendStatus.SetHttpEnabled(true);
		FOnDemandIoBackendStats::Get()->OnHttpConnected();
	}
	else
	{
		BackendStatus.SetHttpError(true);

		AvailableEps.Current = INDEX_NONE;
		HttpClient->SetEndpoint(INDEX_NONE);
		HttpErrors.Reset(GIasHttpErrorSampleCount);
	}

	while (!bStopRequested)
	{
		UpdateThreadPriorityIfNeeded();

		// Process HTTP request(s) even if the client is invalid to ensure enqueued request(s) gets completed.
		ProcessHttpRequests(*HttpClient, HttpErrors, FMath::Min(GIasHttpConcurrentRequests, 32));
		AvailableEps.Current = HttpClient->GetEndpoint();

		if (!bStopRequested)
		{
			uint32 WaitTime = MAX_uint32;
			if (BackendStatus.IsHttpError())
			{
				WaitTime = GIasHttpHealthCheckWaitTime;
				if (HttpClient->GetEndpoint() != INDEX_NONE)
				{
					FOnDemandIoBackendStats::Get()->OnHttpDisconnected();

					AvailableEps.Current = INDEX_NONE;
					HttpClient->SetEndpoint(INDEX_NONE);
					HttpErrors.Reset(GIasHttpErrorSampleCount);
				}

				UE_LOG(LogIas, Log, TEXT("Trying to reconnect to any available endpoint"));
				const FString TestPath = GetEndpointTestPath(); 
				if (int32 Idx = LatencyTest(AvailableEps.Urls, TestPath, bStopRequested); Idx != INDEX_NONE)
				{
					FOnDemandIoBackendStats::Get()->OnHttpConnected();

					AvailableEps.Current = Idx;
					HttpClient->SetEndpoint(Idx);
					BackendStatus.SetHttpError(false);
					UE_LOG(LogIas, Log, TEXT("Successfully reconnected to '%s'"), *AvailableEps.GetCurrent());
				}
			}
			else if (HttpClient->IsUsingPrimaryEndpoint() == false)
			{
				WaitTime = GIasHttpHealthCheckWaitTime;
				const FString TestPath = GetEndpointTestPath(); 
				TConstArrayView<FString> Urls = AvailableEps.Urls;

				if (int32 Idx = LatencyTest(Urls.Left(1), TestPath, bStopRequested); Idx != INDEX_NONE)
				{
					AvailableEps.Current = Idx;
					HttpClient->SetEndpoint(Idx);
					UE_LOG(LogIas, Log, TEXT("Reconnected to primary endpoint '%s'"), *AvailableEps.GetCurrent());
				}
			}
			if (BackendStatus.ShouldAbandonCache())
			{
				BackendStatus.SetAbandonCache(false);
				check(BackendStatus.IsCacheEnabled() == false);
				if (Cache.IsValid())
				{
					UE_LOG(LogIas, Log, TEXT("Abandoning cache, local file cache is no longer available"));
					Cache.Release()->Abandon(); // Will delete its self
				}
			}
			TickBackendEvent->Wait(WaitTime);
		}
	}

	return 0;
}

void FOnDemandIoBackend::Stop()
{
	bStopRequested = true;
	TickBackendEvent->Trigger();
	DistributedEndpointEvent->Trigger();
}

int32 FOnDemandIoBackend::WaitForCompleteRequestTasks(float WaitTimeSeconds, float PollTimeSeconds)
{
	const double StartTime = FPlatformTime::Seconds();
	while (ChunkRequests.Num() > 0 && float(FPlatformTime::Seconds() - StartTime) < WaitTimeSeconds)
	{
		FPlatformProcess::SleepNoStats(PollTimeSeconds);
	}

	return ChunkRequests.Num();
}

TSharedPtr<IOnDemandIoDispatcherBackend> MakeOnDemandIoDispatcherBackend(
	const FOnDemandEndpointConfig& Config,
	FOnDemandIoStore& IoStore,
	TUniquePtr<IIasCache>&& Cache)
{
	return MakeShareable<IOnDemandIoDispatcherBackend>(
		new FOnDemandIoBackend(Config, IoStore, MoveTemp(Cache)));
}

} // namespace UE::IoStore

#undef UE_IAS_DEBUG_CONSOLE_CMDS