// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpManager.h"
#include "HttpModule.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Http.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/Fork.h"
#include "HttpThread.h"
#include "GenericPlatform/HttpRequestCommon.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "Stats/Stats.h"
#include "Containers/BackgroundableTicker.h"

TAutoConsoleVariable<int32> CVarHttpEventLoopEnableChance(
	TEXT("http.CurlEventLoopEnableChance"),
	UE_HTTP_EVENT_LOOP_ENABLE_CHANCE_BY_DEFAULT,
	TEXT("Enable chance of event loop, from 0 to 100"),
	ECVF_SaveForNextBoot
);

TAutoConsoleVariable<FString> CVarHttpUrlPatternsToLogResponse(
	TEXT("http.UrlPatternsToLogResponse"),
	TEXT(""),
	TEXT("List of url patterns to log headers and json content: \"epicgames.com unrealengine.com ...\""),
	ECVF_SaveForNextBoot
);

TAutoConsoleVariable<FString> CVarHttpUrlPatternsToMockFailure(
	TEXT("http.UrlPatternsToMockFailure"),
	TEXT(""),
	TEXT("List of url patterns to mock failure with response code, 0 indicates ConnectionError: \"epicgames.com->0 unrealengine.com->503 ...\""),
	ECVF_SaveForNextBoot
);

// FHttpManager

const TCHAR* LexToString(const EHttpFlushReason& FlushReason)
{
	switch (FlushReason)
	{
	case EHttpFlushReason::Default:		return TEXT("Default");
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	case EHttpFlushReason::Background:	return TEXT("Background");
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	case EHttpFlushReason::Shutdown:	return TEXT("Shutdown");
	case EHttpFlushReason::FullFlush:	return TEXT("FullFlush");
	}

	checkNoEntry();
	return TEXT("Invalid");
}

namespace
{
	bool ShouldOutputHttpWarnings()
	{
		return !IsRunningCommandlet() && !FApp::IsUnattended();
	}
}

CSV_DEFINE_CATEGORY(HttpManager, true);
CSV_DEFINE_STAT(HttpManager, RequestsInQueue);
CSV_DEFINE_STAT(HttpManager, MaxRequestsInQueue);
CSV_DEFINE_STAT(HttpManager, RequestsInFlight);
CSV_DEFINE_STAT(HttpManager, MaxRequestsInFlight);
CSV_DEFINE_STAT(HttpManager, MaxTimeToWaitInQueue);
CSV_DEFINE_STAT(HttpManager, DownloadedMB);
CSV_DEFINE_STAT(HttpManager, BandwidthMbps);
CSV_DEFINE_STAT(HttpManager, DurationMsAvg);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FHttpManager::FHttpManager()
	: FTSTickerObjectBase(0.0f, FTSBackgroundableTicker::GetCoreTicker())
	, Thread(nullptr)
	, CorrelationIdMethod(FHttpManager::GetDefaultCorrelationIdMethod())
{
	bFlushing = false;
}

FHttpManager::~FHttpManager()
{
	if (Thread)
	{
		Thread->StopThread();
		delete Thread;
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FHttpManager::Initialize()
{
	if (!Thread)
	{
		bUseEventLoop = (FMath::RandRange(0, 99) < CVarHttpEventLoopEnableChance.GetValueOnGameThread());

		// Also support to change it through runtime args.
		// Can't set cvar CVarHttpEventLoopEnableChance through runtime args or .ini files because http module initialized too early
		FParse::Bool(FCommandLine::Get(), TEXT("useeventloop="), bUseEventLoop);

		Thread = CreateHttpThread();
		Thread->StartThread();
	}

	UpdateConfigs();

	UpdateUrlPatternsToLogResponse(CVarHttpUrlPatternsToLogResponse.AsVariable());
	CVarHttpUrlPatternsToLogResponse.AsVariable()->OnChangedDelegate().AddRaw(this, &FHttpManager::UpdateUrlPatternsToLogResponse);

	UpdateUrlPatternsToMockFailure(CVarHttpUrlPatternsToMockFailure.AsVariable());
	CVarHttpUrlPatternsToMockFailure.AsVariable()->OnChangedDelegate().AddRaw(this, &FHttpManager::UpdateUrlPatternsToMockFailure);
}

void FHttpManager::Shutdown()
{
	CVarHttpUrlPatternsToLogResponse.AsVariable()->OnChangedDelegate().Clear();
	CVarHttpUrlPatternsToMockFailure.AsVariable()->OnChangedDelegate().Clear();

	{
		FScopeLock ScopeLock(&RequestLock);

		// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
		UE_CLOG(ShouldOutputHttpWarnings() && Requests.Num(), LogHttp, Warning, TEXT("[FHttpManager::Shutdown] Unbinding delegates for %d outstanding Http Requests:"), Requests.Num());

		// Clear delegates since they may point to deleted instances
		for (TArray<FHttpRequestRef>::TIterator It(Requests); It; ++It)
		{
			TSharedPtr<IHttpRequest> Request = *It;
			StaticCastSharedPtr<FHttpRequestImpl>(Request)->Shutdown();

			// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
			UE_CLOG(ShouldOutputHttpWarnings(), LogHttp, Warning, TEXT("	verb=[%s] url=[%s] refs=[%d] status=%s"), *Request->GetVerb(), *Request->GetURL(), Request.GetSharedReferenceCount(), EHttpRequestStatus::ToString(Request->GetStatus()));
		}
	}

	// Clear general delegates since they may point to deleted instances
	RequestAddedDelegate.Unbind();
	RequestCompletedDelegate.Unbind();

	// Flush all requests
	Flush(EHttpFlushReason::Shutdown);
}

bool FHttpManager::HasAnyBoundDelegate() const
{
	FScopeLock ScopeLock(&RequestLock);

	for (TArray<FHttpRequestRef>::TConstIterator It(Requests); It; ++It)
	{
		const FHttpRequestRef& Request = *It;
		if (Request->OnProcessRequestComplete().IsBound())
		{
			return true;
		}
	}

	if (RequestAddedDelegate.IsBound())
	{
		return true;
	}

	if (RequestCompletedDelegate.IsBound())
	{
		return true;
	}

	return false;
}

void FHttpManager::ReloadFlushTimeLimits()
{
	FlushTimeLimitsMap.Reset();

	//Save int values of Default and FullFlush?
	for (EHttpFlushReason Reason : TEnumRange<EHttpFlushReason>())
	{
		double SoftLimitSeconds = 2.0;
		double HardLimitSeconds = 4.0;

		// We default the time limits to generous values, keeping the Hard limits always greater than the soft ones, and -1 for the unlimited
		switch (Reason)
		{
		case EHttpFlushReason::Default:
			GConfig->GetDouble(TEXT("HTTP"), TEXT("FlushSoftTimeLimitDefault"), SoftLimitSeconds, GEngineIni);
			GConfig->GetDouble(TEXT("HTTP"), TEXT("FlushHardTimeLimitDefault"), HardLimitSeconds, GEngineIni);
			break;
		case EHttpFlushReason::Shutdown:
			GConfig->GetDouble(TEXT("HTTP"), TEXT("FlushSoftTimeLimitShutdown"), SoftLimitSeconds, GEngineIni);
			GConfig->GetDouble(TEXT("HTTP"), TEXT("FlushHardTimeLimitShutdown"), HardLimitSeconds, GEngineIni);
			
			if ((HardLimitSeconds >= 0) && ((SoftLimitSeconds < 0) || (SoftLimitSeconds >= HardLimitSeconds)))
			{
				UE_CLOG(!IsRunningCommandlet(), LogHttp, Warning, TEXT("Soft limit[%.02f] is higher than the hard limit set[%.02f] in file [%s]. Please change the soft limit to a value lower than the hard limit for Flush to work correctly. - 1 is unlimited and therefore the highest possible value."), static_cast<float>(SoftLimitSeconds), static_cast<float>(HardLimitSeconds), *GEngineIni);
				// we need to be absolutely sure that SoftLimitSeconds is always strictly less than HardLimitSeconds so remaining requests (if any) can be canceled before exiting
				if (HardLimitSeconds > 0.0)
				{
					SoftLimitSeconds = HardLimitSeconds / 2.0;	// clamping SoftLimitSeconds to a reasonable value
				}
				else
				{
					// HardLimitSeconds should never be 0.0 while shutting down otherwise we can't cancel the remaining requests
					HardLimitSeconds = 0.05;	// using a non zero value 
					SoftLimitSeconds = 0.0;		// cancelling request immediately
				}
			}

			break;
		case EHttpFlushReason::FullFlush:
			SoftLimitSeconds = -1.0;
			HardLimitSeconds = -1.0;
			GConfig->GetDouble(TEXT("HTTP"), TEXT("FlushSoftTimeLimitFullFlush"), SoftLimitSeconds, GEngineIni);
			GConfig->GetDouble(TEXT("HTTP"), TEXT("FlushHardTimeLimitFullFlush"), HardLimitSeconds, GEngineIni);
			break;
		}

		FHttpFlushTimeLimit TimeLimit(SoftLimitSeconds, HardLimitSeconds);

		FlushTimeLimitsMap.Add(Reason, TimeLimit);
	}
}

void FHttpManager::SetCorrelationIdMethod(TFunction<FString()> InCorrelationIdMethod)
{
	check(InCorrelationIdMethod);
	CorrelationIdMethod = MoveTemp(InCorrelationIdMethod);
}

FString FHttpManager::CreateCorrelationId() const
{
	return CorrelationIdMethod();
}

bool FHttpManager::IsDomainAllowed(const FString& Url) const
{
	if (!URLRequestFilter.IsEmpty())
	{
		return URLRequestFilter.IsRequestAllowed(Url);
	}

#if !UE_BUILD_SHIPPING
#if !(UE_GAME || UE_SERVER)
	// Allowed domain filtering is opt-in in non-shipping non-game/server builds
	static const bool bForceUseAllowList = FParse::Param(FCommandLine::Get(), TEXT("EnableHttpDomainRestrictions"));
	if (!bForceUseAllowList)
	{
		return true;
	}
#else
	// The check is on by default but allow non-shipping game/server builds to disable the filtering
	static const bool bIgnoreAllowList = FParse::Param(FCommandLine::Get(), TEXT("DisableHttpDomainRestrictions"));
	if (bIgnoreAllowList)
	{
		return true;
	}
#endif
#endif // !UE_BUILD_SHIPPING

	return true;
}

/*static*/
TFunction<FString()> FHttpManager::GetDefaultCorrelationIdMethod()
{
	return []{ return FGuid::NewGuid().ToString(); };
}

void FHttpManager::OnBeforeFork()
{
	Flush(EHttpFlushReason::Default);
}

void FHttpManager::OnAfterFork()
{

}

void FHttpManager::OnEndFramePostFork()
{
	// nothing
}


void FHttpManager::UpdateConfigs()
{
	URLRequestFilter.UpdateConfig(TEXT("Online.HttpManager"), GEngineIni);

	ReloadFlushTimeLimits();

	if (Thread)
	{
		Thread->UpdateConfigs();
	}
}

void FHttpManager::AddGameThreadTask(TFunction<void()>&& Task, float Delay)
{
	if (Task)
	{
		GameThreadTicker.AddTicker(FTickerDelegate::CreateLambda([Task](float DeltaTime) {
			Task();
			return false;
		}), Delay);
	}
}

TSharedPtr<IHttpTaskTimerHandle> FHttpManager::AddHttpThreadTask(TFunction<void()>&& Task, float InDelay)
{
	check(Thread);
	return Thread->AddHttpThreadTask(MoveTemp(Task), InDelay);
}

void FHttpManager::RemoveHttpThreadTask(TSharedPtr<IHttpTaskTimerHandle> HttpTaskTimerHandle)
{
	check(Thread);
	HttpTaskTimerHandle->RemoveTaskFrom(Thread);
}

FHttpThreadBase* FHttpManager::CreateHttpThread()
{
	return new FLegacyHttpThread();
}

void FHttpManager::Flush(EHttpFlushReason FlushReason)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FHttpManager_Flush);

	checkf(FlushReason != EHttpFlushReason::Shutdown || !HasAnyBoundDelegate(), TEXT("Use Shutdown() instead of Flush(EHttpFlushReason::Shutdown) directly."));
	
	// This variable is set to indicate that flush is happening.
	// While flushing is in progress, the RequestLock is held and threads are blocked when trying to submit new requests.
	bFlushing = true;

	double FlushTimeSoftLimitSeconds = FlushTimeLimitsMap[FlushReason].SoftLimitSeconds;
	double FlushTimeHardLimitSeconds = FlushTimeLimitsMap[FlushReason].HardLimitSeconds;

	// this specifies how long to sleep between calls to tick.
	// The smaller the value, the more quickly we may find out that all requests have completed, but the more work may be done in the meantime.
	float SecondsToSleepForOutstandingThreadedRequests = 0.5f;
	GConfig->GetFloat(TEXT("HTTP"), TEXT("RequestCleanupDelaySec"), SecondsToSleepForOutstandingThreadedRequests, GEngineIni);

	UE_CLOG(!IsRunningCommandlet(), LogHttp, Verbose, TEXT("[FHttpManager::Flush] FlushReason [%s] FlushTimeSoftLimitSeconds [%.3fs] FlushTimeHardLimitSeconds [%.3fs] SecondsToSleepForOutstandingThreadedRequests [%.3fs]"), LexToString(FlushReason), FlushTimeSoftLimitSeconds, FlushTimeHardLimitSeconds, SecondsToSleepForOutstandingThreadedRequests);

	uint32 RequestsNum = 0;

	{
		FScopeLock ScopeLock(&RequestLock);
		RequestsNum = Requests.Num();
	}

	UE_CLOG(!IsRunningCommandlet() && RequestsNum, LogHttp, Verbose, TEXT("[FHttpManager::Flush] Cleanup starts for %d outstanding Http Requests."), RequestsNum);

	double BeginWaitTime = FPlatformTime::Seconds();
	double LastFlushTickTime = BeginWaitTime;
	double StallWarnTime = BeginWaitTime + 0.5;
	double AppTime = FPlatformTime::Seconds();


	// For a duration equal to FlushTimeHardLimitSeconds, we wait for ongoing http requests to complete
	while (RequestsNum > 0 && (FlushTimeHardLimitSeconds < 0 || (AppTime - BeginWaitTime < FlushTimeHardLimitSeconds)))
	{
		SCOPED_ENTER_BACKGROUND_EVENT(STAT_FHttpManager_Flush_Iteration);

		// If time equal to FlushTimeSoftLimitSeconds has passed and there's still ongoing http requests, we cancel them (setting FlushTimeSoftLimitSeconds to 0 does this immediately)
		if (FlushTimeSoftLimitSeconds >= 0 && (AppTime - BeginWaitTime >= FlushTimeSoftLimitSeconds))
		{
			// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
			UE_CLOG(ShouldOutputHttpWarnings(), LogHttp, Warning, TEXT("[FHttpManager::Flush] FlushTimeSoftLimitSeconds [%.3fs] exceeded. Cancelling %d outstanding HTTP requests:"), FlushTimeSoftLimitSeconds, RequestsNum);

			{
				TArray<FHttpRequestRef> RequestsToCancel;

				{
					FScopeLock ScopeLock(&RequestLock);
					RequestsToCancel = Requests;
				}

				for (TArray<FHttpRequestRef>::TIterator It(RequestsToCancel); It; ++It)
				{
					FHttpRequestRef& Request = *It;

					// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
					UE_CLOG(ShouldOutputHttpWarnings(), LogHttp, Warning, TEXT("	verb=[%s] url=[%s] refs=[%d] status=%s"), *Request->GetVerb(), *Request->GetURL(), Request.GetSharedReferenceCount(), EHttpRequestStatus::ToString(Request->GetStatus()));

					FScopedEnterBackgroundEvent(*Request->GetURL());

					Request->CancelRequest();
				}
			}
		}

		// Process ongoing Http Requests
		FlushTick(AppTime - LastFlushTickTime);
		LastFlushTickTime = AppTime;

		{
			FScopeLock ScopeLock(&RequestLock);
			RequestsNum = Requests.Num();
		}

		// Process threaded Http Requests
		if (RequestsNum > 0)
		{
			if (Thread)
			{
				if (Thread->NeedsSingleThreadTick())
				{
					if (AppTime >= StallWarnTime)
					{
						// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
						UE_CLOG(ShouldOutputHttpWarnings(), LogHttp, Warning, TEXT("	Ticking HTTPThread for %d outstanding Http requests."), RequestsNum);
						StallWarnTime = AppTime + 0.5;
					}
					Thread->Tick();
				}
				else
				{
					// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
					UE_CLOG(ShouldOutputHttpWarnings(), LogHttp, Warning, TEXT("	Sleeping %.3fs to wait for %d outstanding Http Requests."), SecondsToSleepForOutstandingThreadedRequests, RequestsNum);
					FPlatformProcess::Sleep(SecondsToSleepForOutstandingThreadedRequests);
				}
			}
		}

		AppTime = FPlatformTime::Seconds();
	}

	UE_CLOG(!IsRunningCommandlet(), LogHttp, Verbose, TEXT("[FHttpManager::Flush] Cleanup ended after %.3fs. %d outstanding Http Requests."), AppTime - BeginWaitTime, RequestsNum);

	// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
	if (RequestsNum > 0 && (FlushTimeHardLimitSeconds > 0 && (AppTime - BeginWaitTime > FlushTimeHardLimitSeconds)) && ShouldOutputHttpWarnings())
	{
		UE_LOG(LogHttp, Warning, TEXT("[FHttpManager::Flush] FlushTimeHardLimitSeconds [%.3fs] exceeded. The following requests are being abandoned without being flushed:"), FlushTimeHardLimitSeconds);

		FScopeLock ScopeLock(&RequestLock);

		for (TArray<FHttpRequestRef>::TIterator It(Requests); It; ++It)
		{
			FHttpRequestRef& Request = *It;
			//List the outstanding requests that are being abandoned without being canceled.
			UE_LOG(LogHttp, Warning, TEXT("	verb=[%s] url=[%s] refs=[%d] status=%s"), *Request->GetVerb(), *Request->GetURL(), Request.GetSharedReferenceCount(), EHttpRequestStatus::ToString(Request->GetStatus()));
		}
	}

	bFlushing = false;
}

bool FHttpManager::Tick(float DeltaSeconds)
{
    QUICK_SCOPE_CYCLE_COUNTER(STAT_FHttpManager_Tick);

	// Normally Tick() should only be called from game thread. But it's still possible Tick() be called 
	// from off-game thread when quit in purpose like GPU OOM, to flush remain HTTP analysis requests

	// Run GameThread tasks
	{
		FScopeLock ScopeLock(&GameThreadTickerLock);
		GameThreadTicker.Tick(DeltaSeconds);
	}

	if (Thread)
	{
		{
			// Tick each active request
			FScopeLock ScopeLock(&RequestLock);
			for (const FHttpRequestRef& Request : Requests)
			{
				Request->Tick(DeltaSeconds);
			}
		}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TArray<FHttpRequestCommon*> CompletedThreadedRequests;

		{
			// Thread->GetCompletedRequests doesn't support multi-thread access
			FScopeLock ScopeLock(&CompletedRequestLock);
			Thread->GetCompletedRequests(CompletedThreadedRequests);
		}

		// Finish and remove any completed requests
		for (FHttpRequestCommon* CompletedRequest : CompletedThreadedRequests)
		{
			FHttpRequestRef CompletedRequestRef = CompletedRequest->AsShared();

			{
				FScopeLock ScopeLock(&RequestLock);
				Requests.Remove(CompletedRequestRef);
			}

			if (CompletedRequest->GetDelegateThreadPolicy() == EHttpRequestDelegateThreadPolicy::CompleteOnGameThread)
			{
				CompletedRequest->FinishRequest();
				BroadcastHttpRequestCompleted(CompletedRequestRef);
			}
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	
	// Report csv stats.
	int32 TotalDownloadedMB = int32(HttpStats.TotalDownloadedBytes.load() >> 20);
	CSV_CUSTOM_STAT_DEFINED(RequestsInQueue, HttpStats.RequestsInQueue.load(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(MaxRequestsInQueue, int32(HttpStats.MaxRequestsInQueue.load()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(RequestsInFlight, HttpStats.RequestsInFlight.load(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(MaxRequestsInFlight, int32(HttpStats.MaxRequestsInFlight.load()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(MaxTimeToWaitInQueue, int32(HttpStats.MaxTimeToWaitInQueue.load()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(DownloadedMB, TotalDownloadedMB, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(BandwidthMbps, int32(HttpStats.BandwidthMbps.load()), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(DurationMsAvg, int32(HttpStats.HttpDurationMsAvg.load()), ECsvCustomStatOp::Set);

	// keep ticking
	return true;
}

void FHttpManager::FlushTick(float DeltaSeconds)
{
	Tick(DeltaSeconds);
}

void FHttpManager::AddRequest(const FHttpRequestRef& Request)
{
	{
		FScopeLock ScopeLock(&RequestLock);
		UE_CLOG(bFlushing, LogHttp, Warning, TEXT("Adding request %s to http manager while flushing"), *Request->GetURL());
		Requests.Add(Request);
	}
	RequestAddedDelegate.ExecuteIfBound(Request);
}

void FHttpManager::RemoveRequest(const FHttpRequestRef& Request)
{
	FScopeLock ScopeLock(&RequestLock);

	Requests.Remove(Request);
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FHttpManager::AddThreadedRequest(const TSharedRef<FHttpRequestCommon, ESPMode::ThreadSafe>& Request)
{
	check(Thread);
	{
		AddRequest(Request);
	}
	Thread->AddRequest(&Request.Get());
}

void FHttpManager::CancelThreadedRequest(const TSharedRef<FHttpRequestCommon, ESPMode::ThreadSafe>& Request)
{
	check(Thread);
	Thread->CancelRequest(&Request.Get());
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

bool FHttpManager::IsValidRequest(const IHttpRequest* RequestPtr) const
{
	FScopeLock ScopeLock(&RequestLock);

	bool bResult = false;
	for (const FHttpRequestRef& Request : Requests)
	{
		if (&Request.Get() == RequestPtr)
		{
			bResult = true;
			break;
		}
	}

	return bResult;
}

void FHttpManager::SetRequestAddedDelegate(const FHttpManagerRequestAddedDelegate& Delegate)
{
	RequestAddedDelegate = Delegate;
}

void FHttpManager::SetRequestCompletedDelegate(const FHttpManagerRequestCompletedDelegate& Delegate)
{
	RequestCompletedDelegate = Delegate;
}

void FHttpManager::DumpRequests(FOutputDevice& Ar) const
{
	FScopeLock ScopeLock(&RequestLock);

	Ar.Logf(TEXT("------- (%d) Http Requests"), Requests.Num());
	for (const FHttpRequestRef& Request : Requests)
	{
		Ar.Logf(TEXT("	verb=[%s] url=[%s] status=%s"),
			*Request->GetVerb(), *Request->GetURL(), EHttpRequestStatus::ToString(Request->GetStatus()));
	}
}

bool FHttpManager::SupportsDynamicProxy() const
{
	return false;
}

void FHttpManager::BroadcastHttpRequestCompleted(const FHttpRequestRef& Request)
{
	FHttpResponsePtr Response = Request->GetResponse();
	if (Response.IsValid())
	{
		const int64 OldDuration = HttpStatsHistory.DurationMs[HttpStatsHistory.HistoryIndex];
		const int64 NewDuration = FMath::RoundToInt64(Request->GetElapsedTime() * 1000.0f);

		HttpStatsHistory.TotalDuration -= OldDuration;
		HttpStatsHistory.TotalDuration += NewDuration;
		HttpStatsHistory.DurationMs[HttpStatsHistory.HistoryIndex] = NewDuration;

		const int64 SizeBytes = Response->GetContentLength();
		HttpStats.TotalDownloadedBytes += SizeBytes;

		HttpStatsHistory.TotalDownloadedBytes -= HttpStatsHistory.DownloadedBytes[HttpStatsHistory.HistoryIndex];
		HttpStatsHistory.TotalDownloadedBytes += SizeBytes;
		HttpStatsHistory.DownloadedBytes[HttpStatsHistory.HistoryIndex] = SizeBytes;

		HttpStats.BandwidthMbps = ((HttpStatsHistory.TotalDownloadedBytes * 8) / (HttpStatsHistory.TotalDuration + 1) / 1000);
		HttpStats.HttpDurationMsAvg = HttpStatsHistory.TotalDuration / FHttpStatsHistory::HttpHistoryCount;

		// Increment index
		HttpStatsHistory.HistoryIndex = (HttpStatsHistory.HistoryIndex + 1) % FHttpStatsHistory::HttpHistoryCount;
	}
		
	RequestCompletedDelegate.ExecuteIfBound(Request);
}

FHttpThreadBase* FHttpManager::GetThread()
{
	return Thread;
}

void FHttpManager::RecordStatTimeToConnect(float Duration)
{
	HttpStats.MaxTimeToConnect = FGenericPlatformMath::Max(Duration, HttpStats.MaxTimeToConnect.load());
}

void FHttpManager::RecordStatRequestsInFlight(uint32 RequestsInFlight)
{
	HttpStats.RequestsInFlight = RequestsInFlight;
	HttpStats.MaxRequestsInFlight = FGenericPlatformMath::Max(RequestsInFlight, HttpStats.MaxRequestsInFlight.load());
}

void FHttpManager::RecordStatRequestsInQueue(uint32 RequestsInQueue)
{
	HttpStats.RequestsInQueue = RequestsInQueue;
	HttpStats.MaxRequestsInQueue = FGenericPlatformMath::Max(RequestsInQueue, HttpStats.MaxRequestsInQueue.load());
}

void FHttpManager::RecordMaxTimeToWaitInQueue(float Duration)
{
	HttpStats.MaxTimeToWaitInQueue = FGenericPlatformMath::Max(Duration, HttpStats.MaxTimeToWaitInQueue.load());
}

void FHttpManager::RecordPlatformStats(const FHttpStatsPlatform& PlatformStats)
{
	HttpStats.PlatformStats = PlatformStats;
}

void FHttpManager::UpdateUrlPatternsToLogResponse(IConsoleVariable* CVar)
{
	const FScopeLock CacheLock(&UrlPatternsToLogResponseCriticalSection);
	const FString UrlPatternsToLogResponseStr = CVar->AsVariable()->GetString();
	UrlPatternsToLogResponseStr.ParseIntoArray(UrlPatternsToLogResponse, TEXT(" "));
}

bool FHttpManager::ShouldLogResponse(FStringView Url)
{
	const FScopeLock CacheLock(&UrlPatternsToLogResponseCriticalSection);
	for (const FString& UrlPatternToLogResponse : UrlPatternsToLogResponse)
	{
		if (Url.Contains(UrlPatternToLogResponse))
		{
			return true;
		}
	}

	return false;
}

void FHttpManager::UpdateUrlPatternsToMockFailure(IConsoleVariable* CVar)
{
	const FScopeLock CacheLock(&UrlPatternsToMockFailureCriticalSection);
	const FString UrlPatternsToMockFailureStr = CVar->AsVariable()->GetString();

	TArray<FString> UrlPatternsToMockFailureStrings;
	UrlPatternsToMockFailureStr.ParseIntoArray(UrlPatternsToMockFailureStrings, TEXT(" "));

	for (const FString& UrlPatternToMockFailureString : UrlPatternsToMockFailureStrings)
	{
		TArray<FString> UrlPattern;
		UrlPatternToMockFailureString.ParseIntoArray(UrlPattern, TEXT("->"));
		if (UrlPattern.Num() == 2)
		{
			int32 ResponseCode = FCString::Atoi(*UrlPattern[1]);
			UrlPatternsToMockFailure.Emplace(UrlPattern[0], ResponseCode);
		}
	}
}

TOptional<int32> FHttpManager::GetMockFailure(FStringView Url)
{
	TOptional<int32> Result;
	const FScopeLock CacheLock(&UrlPatternsToMockFailureCriticalSection);

	for (const TPair<FString, int32>& UrlPattern : UrlPatternsToMockFailure)
	{
		if (Url.Contains(UrlPattern.Key))
		{
			Result = UrlPattern.Value;
		}
	}

	return Result;
}

