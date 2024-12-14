// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CoreMinimal.h"

#include "BackgroundHttpFileHashHelper.h"

//Call backs called by the bellow FBackgroundURLSessionHandler so higher-level systems can respond to task updates.
class UE_DEPRECATED(5.5, "Use new download methods in FBackgroundURLSessionHandler.") APPLICATIONCORE_API FIOSBackgroundDownloadCoreDelegates
{
public:
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FIOSBackgroundDownload_DidFinishDownloadingToURL, NSURLSessionDownloadTask*, NSError*, const FString&);
	DECLARE_MULTICAST_DELEGATE_FourParams(FIOSBackgroundDownload_DidWriteData, NSURLSessionDownloadTask*, int64_t /*Bytes Written Since Last Call */, int64_t /*Total Bytes Written */, int64_t /*Total Bytes Expedted To Write */);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FIOSBackgroundDownload_DidCompleteWithError, NSURLSessionTask*, NSError*);
	DECLARE_DELEGATE(FIOSBackgroundDownload_DelayedBackgroundURLSessionCompleteHandler);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FIOSBackgroundDownload_SessionDidFinishAllEvents, NSURLSession*, FIOSBackgroundDownload_DelayedBackgroundURLSessionCompleteHandler);

	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static FIOSBackgroundDownload_DidFinishDownloadingToURL OnIOSBackgroundDownload_DidFinishDownloadingToURL;
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static FIOSBackgroundDownload_DidWriteData OnIOSBackgroundDownload_DidWriteData;
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static FIOSBackgroundDownload_DidCompleteWithError OnIOSBackgroundDownload_DidCompleteWithError;
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static FIOSBackgroundDownload_SessionDidFinishAllEvents OnIOSBackgroundDownload_SessionDidFinishAllEvents;
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static FIOSBackgroundDownload_DelayedBackgroundURLSessionCompleteHandler OnDelayedBackgroundURLSessionCompleteHandler;
};

//Interface for wrapping a NSURLSession configured to support background downloading of NSURLSessionDownloadTasks.
//This exists here as we can have to re-associate with our background session after app launch and need to re-associate with downloads
//right away before the HttpModule is loaded.
class APPLICATIONCORE_API FBackgroundURLSessionHandler
{
public:
	// Initializes a BackgroundSession with the given identifier. If the current background session already exists, returns true if the identifier matches. False if identifier doesn't match or if the session fails to create.
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static bool InitBackgroundSession(const FString& SessionIdentifier) {return false;}

	//bShouldInvalidateExistingTasks determines if the session cancells all outstanding tasks immediately and cancels the session immediately or waits for them to finish and then invalidates the session
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static void ShutdownBackgroundSession(bool bShouldFinishTasksFirst = true) {}

	//Gets a pointer to the current background session
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static NSURLSession* GetBackgroundSession() {return nullptr;}

	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static void CreateBackgroundSessionWorkingDirectory() {}
	
	//Function to mark if you would like for the NSURLSession to wait to call the completion handler when
	//OnIOSBackgroundDownload_SessionDidFinishAllEvents is called for you to call the passed completion handler
	//NOTE: Call DURING OnIOSBackgroundDownload_SessionDidFinishAllEvents
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static void AddDelayedBackgroundURLSessionComplete() {}
	
	//Function to handle calls to OnDelayedBackgroundURLSessionCompleteHandler
	//The intention is to call this for every call to AddDelayedBackgroundURLSessionComplete.
	//NOTE: Once calling this your task should be completely finished with work and ready to be backgrounded!
	UE_DEPRECATED(5.5, "Use new API in FBackgroundURLSessionHandler.") static void OnDelayedBackgroundURLSessionCompleteHandlerCalled();

	// New API for background downloading

	// Value of invalid download id which could be used to compare return value of CreateOrFindDownload. 
	static const uint64 InvalidDownloadId;

	// Will be invoked from didFinishDownloadingToURL or didCompleteWithError.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDownloadCompleted, const uint64 /*DownloadId*/, const bool /*bSuccess*/);
	static FOnDownloadCompleted OnDownloadCompleted;
	
	// Will be invoked from didFinishCollectingMetrics
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnDownloadMetrics, const uint64 /*DownloadId*/, const int32 /*TotalBytesDownloaded*/, const float /*DownloadDuration*/)
	static FOnDownloadMetrics OnDownloadMetrics;

	// Will be invoked from handleEventsForBackgroundURLSession application delegate. Needs to be registered very early, e.g. from static constructor.
	// handleEventsForBackgroundURLSession is only invoked if app was killed by OS while in background and then relaunched to notify that downloads were completed.
	// Is not invoked in any other scenario.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDownloadsCompletedWhileAppWasNotRunning, const bool /*bSuccess*/);
	static FOnDownloadsCompletedWhileAppWasNotRunning OnDownloadsCompletedWhileAppWasNotRunning;

	// Sets if cellular is allowed to be used for new downloads.
	// Existing downloads will be recreated to reflect new setting value.
	static void AllowCellular(bool bAllow);

	// Creates a new download or finds existing download matching URL path.
	// All URL's should have same path and only differ in domain.
	// Priority is a value between 0.0 to 1.0, see NSURLSessionTaskPriorityDefault.
	// HelperRef is optional shared reference to BackgroundHttpFileHashHelperRef.
	// In case of HandleEventsForBackgroundURLSession this subsystem will create it's own reference.
	static uint64 CreateOrFindDownload(const TArray<FString>& URLs, const float Priority, BackgroundHttpFileHashHelperRef HelperRef);

	static void PauseDownload(const uint64 DownloadId);

	static void ResumeDownload(const uint64 DownloadId);

	// Cancels and invalidates DownloadId.
	static void CancelDownload(const uint64 DownloadId);

	// Priority is a value between 0.0 to 1.0, see NSURLSessionTaskPriorityDefault.
	static void SetPriority(const uint64 DownloadId, const float Priority);

	static uint64 GetCurrentDownloadedBytes(const uint64 DownloadId);

	static bool IsDownloadFinished(const uint64 DownloadId, int32& OutResultHTTPCode, FString& OutTemporaryFilePath);

	// To be used by app delegate, call it from handleEventsForBackgroundURLSession.
	static void HandleEventsForBackgroundURLSession(const FString& SessionIdentifier);

	// To be used by app delegate, call it from applicationDidEnterBackground
	static void HandleDidEnterBackground();

	// To be used by app delegate, call it from applicationWillEnterForeground
	static void HandleWillEnterForeground();

	// To be used by ApplePlatformBackgroundHttpManager
	static void SaveBackgroundHttpFileHashHelperState();

	// Returns an ordered list of CDNs used to issue actual downloads.
	// A list of URLS provided to CreateOrFindDownload might change order if CDNReorderingTimeout > 0 to ensure better success rate.
	// List is empty before first CreateOrFindDownload call.
	static TArray<FString> GetCDNOrderArray();

#if !UE_BUILD_SHIPPING
	static void GetDownloadDebugText(const uint64 DownloadId, TArray<FString>& Output);
#endif
};
