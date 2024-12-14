// Copyright Epic Games, Inc. All Rights Reserved.

#ifndef UE_DNLD_SANDBOX

#include "IOS/IOSBackgroundURLSessionHandler.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"

#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"

#include "IOS/IOSAppDelegate.h"

#endif

#include <future>

// Force cancel all pending downloads.
// Useful when testing background downloads as they persist between application sessions.
static constexpr bool bCancelExistingDownloads = false;

// Always report via NSLog, useful for debugging.
static constexpr bool bReportToNSLog = false;

DEFINE_LOG_CATEGORY_STATIC(LogIOSBackgroundDownload, Log, All);

static void LogIOSBackgroundDownloadMessage(NSString* Message)
{
	if (bReportToNSLog)
	{
		NSLog(@"LogIOSBackgroundDownload %@", Message);
	}
	else if (UE_LOG_ACTIVE(LogIOSBackgroundDownload, Log))
	{
		const FString MessageWrapper(Message);
		UE_LOG(LogIOSBackgroundDownload, Log, TEXT("%s"), *MessageWrapper);
	}
}

#define UE_DNLD_LOG(...) LogIOSBackgroundDownloadMessage([NSString stringWithFormat:__VA_ARGS__])

// --------------------------------------------------------------------------------------------------------------------

// We need additional state in NSURLSessionDownloadTask to implement CDN failover.
// Convential way to implement this would be to set a delegate on NSURLSessionDownloadTask and add properties to the delegate.
//
// In case of background downloads API prohibits setting a delegate or setting NSObject values.
// Additionally we want to keep state in NSURLSessionDownloadTask itself to avoid pitfalls of maintaining separate state elsewhere.
//
// [NSURLSessionDownloadTask taskDescription] is one such property that can be used to track state and API maintains state between app sessions.
@interface FBackgroundNSURLSessionDownloadTaskData : NSObject

@property (nonatomic, retain) NSMutableArray<__kindof NSURL*>* URLs;
@property (nonatomic, retain) NSMutableArray<__kindof NSNumber*>* RetryCountPerURL;

// assumes all URL's have same content path but different domain
+ (FBackgroundNSURLSessionDownloadTaskData* _Nonnull)TaskDataWithURLs:(NSArray<__kindof NSURL*>*)URLs WithRetryCount:(NSInteger)RetryCount;
+ (FBackgroundNSURLSessionDownloadTaskData* _Nullable)TaskDataFromSerializedString:(NSString*)SerializedData;

- (NSString*)ToSerializedString;

- (NSURL* _Nonnull)GetFirstURL;
- (NSURL* _Nullable)GetNextURL;
- (void)ResetRetryCount:(NSInteger)RetryCount;
- (void)Cancel;

@end

// --------------------------------------------------------------------------------------------------------------------

@implementation FBackgroundNSURLSessionDownloadTaskData

NSString* const SerializationKeyProtocolVersion = @"v";
NSString* const SerializationKeyCDNs = @"c";
NSString* const SerializationKeyPath = @"p";
NSString* const SerializationKeyRetryCountPerURL = @"r";

+ (FBackgroundNSURLSessionDownloadTaskData* _Nonnull)TaskDataWithURLs:(NSArray<__kindof NSURL*>*)URLs WithRetryCount:(NSInteger)RetryCount
{
	FBackgroundNSURLSessionDownloadTaskData* Data = [[FBackgroundNSURLSessionDownloadTaskData alloc] init];

	Data.URLs = [NSMutableArray arrayWithArray:URLs];
	Data.RetryCountPerURL = [NSMutableArray arrayWithCapacity:URLs.count];
	for (NSURL* URL in URLs)
	{
		(void)URL;
		[Data.RetryCountPerURL addObject:[NSNumber numberWithInteger:RetryCount]];
	}
	NSAssert(Data.URLs.count > 0, @"URLs should be non empty");
	NSAssert(Data.URLs.count == Data.RetryCountPerURL.count, @"URLs and RetryCountPerURL arrays should have same size");

	return [Data autorelease];
}

+ (FBackgroundNSURLSessionDownloadTaskData* _Nullable)TaskDataFromSerializedString:(NSString*)SerializedData
{
	if (SerializedData == nil)
	{
		return nil;
	}

	NSData* SerializedDataEncoded = [SerializedData dataUsingEncoding:NSUTF8StringEncoding];
	if (SerializedDataEncoded == nil)
	{
		return nil;
	}

	FBackgroundNSURLSessionDownloadTaskData* Data = [[FBackgroundNSURLSessionDownloadTaskData alloc] init];

	NSError* Error = nil;
	NSDictionary* Dict = [NSJSONSerialization JSONObjectWithData:SerializedDataEncoded options:0 error:&Error];

	NSNumber* Version = [Dict valueForKey:SerializationKeyProtocolVersion];
	NSMutableArray<__kindof NSString*>* CDNs = [Dict valueForKey:SerializationKeyCDNs];
	NSString* Path = [Dict valueForKey:SerializationKeyPath];
	NSArray<__kindof NSNumber*>* RetryCountPerURL = [Dict valueForKey:SerializationKeyRetryCountPerURL];

	if (Error != nil || Version == nil || Version.intValue != 1 || CDNs == nil || Path == nil || RetryCountPerURL == nil)
	{
		UE_DNLD_LOG(@"Failed to deserialize task state '%@' due to '%@', %u, %u, %u, %u, %u",
			SerializedData,
			Error != nil ? Error.localizedDescription : @"",
			Version == nil ? 1 : 0,
			Version.intValue,
			CDNs == nil ? 1 : 0,
			Path == nil ? 1 : 0,
			RetryCountPerURL == nil ? 1 : 0
		);
		[Data release];
		return nil;
	}

	Data.URLs = [NSMutableArray arrayWithCapacity:CDNs.count];
	Data.RetryCountPerURL = [NSMutableArray arrayWithArray:RetryCountPerURL];
	for (NSString* CDN in CDNs)
	{
		[Data.URLs addObject:[NSURL URLWithString:[NSString stringWithFormat:@"%@%@", CDN, Path]]];
	}
	NSAssert(Data.URLs.count > 0, @"URLs should be non empty");
	NSAssert(Data.URLs.count == Data.RetryCountPerURL.count, @"URLs and RetryCountPerURL arrays should have same size");

	return [Data autorelease];
}

- (void)dealloc
{
	[_URLs release];
	[_RetryCountPerURL release];
	[super dealloc];
}

- (NSString *)ToSerializedString
{
	NSMutableDictionary* Dict = [NSMutableDictionary dictionaryWithCapacity:4];

	NSString* Path = [[self.URLs firstObject] path];

	NSMutableArray<__kindof NSString*>* CDNs = [NSMutableArray arrayWithCapacity:self.URLs.count];
	for (NSURL* URL in self.URLs)
	{
		NSString* URLString = URL.absoluteString;
		if (![URLString hasSuffix:Path])
		{
			UE_DNLD_LOG(@"Expected all URLs have same path but got '%@' when expected path '%@'", URLString, Path);
			continue;
		}
		NSString* Domain = [[URLString componentsSeparatedByString:URL.path] firstObject];
		[CDNs addObject:Domain];
	}

	[Dict setValue:[NSNumber numberWithInt:1] forKey:SerializationKeyProtocolVersion];
	[Dict setValue:CDNs forKey:SerializationKeyCDNs];
	[Dict setValue:Path forKey:SerializationKeyPath];
	[Dict setValue:self.RetryCountPerURL forKey:SerializationKeyRetryCountPerURL];

	NSError* Error = nil;
	NSData* Data = [NSJSONSerialization dataWithJSONObject:Dict options:NSJSONWritingSortedKeys error:&Error];

	NSString* String = [[NSString alloc] initWithData:Data encoding:NSUTF8StringEncoding];
	return [String autorelease];
}

- (NSURL* _Nonnull)GetFirstURL
{
	return self.URLs.firstObject;
}

- (NSURL* _Nullable)GetNextURL
{
	for (NSUInteger i = 0; i < self.RetryCountPerURL.count; ++i)
	{
		NSInteger RetryValue = [self.RetryCountPerURL objectAtIndex:i].integerValue;
		if (RetryValue == 0)
		{
			continue;
		}
		else if (RetryValue <= -1) // special case for infinitely retrying same URL
		{
			return [self.URLs objectAtIndex:i];
		}

		RetryValue--;
		[self.RetryCountPerURL replaceObjectAtIndex:i withObject:[NSNumber numberWithInteger:RetryValue]];
		return [self.URLs objectAtIndex:i];
	}

	return nil;
}

- (void)ResetRetryCount:(NSInteger)RetryCount
{
	for (NSUInteger i = 0; i < self.RetryCountPerURL.count; ++i)
	{
		[self.RetryCountPerURL replaceObjectAtIndex:i withObject:[NSNumber numberWithInteger:RetryCount]];
	}
}

- (void)Cancel
{
	[self ResetRetryCount:0];
}

@end

// --------------------------------------------------------------------------------------------------------------------

enum class EBackgroundNSURLCDNInfoResponse : uint32 // Beware these constants define sorting order in SortingKeyWith
{
	// CDN responded with a valid HTTP response with a code smaller than HTTPStatusCodeErrorBadRequest (400)
	Ok = 1,

	// CDN request timed outed or was cancelled
	Timeout = 2,

	// CDN or networking responded with error, e.g. DNS resolution error, etc
	Error = 3
};

// NSURLSession CDN info
@interface FBackgroundNSURLCDNInfo : NSObject

@property (nonatomic, retain) NSString* CDNHost;
@property (nonatomic, retain) NSString* CDNAbsoluteURL;
@property (nonatomic) EBackgroundNSURLCDNInfoResponse Response;
@property (nonatomic) NSTimeInterval ResponseTime;
@property (nonatomic) NSUInteger ProvidedOrder;

- (void)SetFromURL:(NSURL*)URL;
- (double)SortingKeyWith:(BOOL)bSortByResponseTime;

@end

@implementation FBackgroundNSURLCDNInfo

- (void)SetFromURL:(NSURL*)URL
{
	[self setCDNHost:URL.host];

	NSURLComponents* Components = [NSURLComponents componentsWithURL:URL resolvingAgainstBaseURL:NO];
	Components.fragment = nil;
	Components.path = nil;
	Components.query = nil;

	NSString* AbsoluteURL = Components.string;
	if (![AbsoluteURL hasSuffix:@"/"])
	{
		// Trailing / is not part of RFC 3986, but is used in CDN configs,
		// append it to keep CDN string formatting.
		AbsoluteURL = [AbsoluteURL stringByAppendingString:@"/"];
	}

	[self setCDNAbsoluteURL:AbsoluteURL];
}

- (double)SortingKeyWith:(BOOL)bSortByResponseTime
{
	double Key = (double)self.Response * 100000.0;

	if (bSortByResponseTime && self.Response == EBackgroundNSURLCDNInfoResponse::Ok)
	{
		// sort by response time only if response was valid
		Key += self.ResponseTime;
	}
	else
	{
		// all other cases should be sorted by provided order
		Key += self.ProvidedOrder;
	}

	return Key;
}

@end

// --------------------------------------------------------------------------------------------------------------------

// NSURLSession wrapper focused on background downloading and CDN failover
@interface FBackgroundNSURLSession : NSObject<NSURLSessionDelegate, NSURLSessionTaskDelegate, NSURLSessionDownloadDelegate>

@property (atomic) BOOL AllowCellular;

+ (FBackgroundNSURLSession*)Shared;
+ (NSUInteger)GetInvalidDownloadId;
+ (NSString*)GetNSURLSessionIdentifier;

- (void)Initialize;
- (void)SetFileHashHelper:(BackgroundHttpFileHashHelperRef)HelperRef;
- (BackgroundHttpFileHashHelperRef)GetFileHashHelper;
- (void)SaveFileHashHelperState;
- (NSString*)GetTempPathForURL:(NSURL* _Nonnull)URL;
- (NSMutableArray<__kindof NSURL*>*)ReorderCDNsByReachability:(NSMutableArray<__kindof NSURL*>*)URLs;
- (NSArray<__kindof FBackgroundNSURLCDNInfo*>*)GetCDNInfo;
- (NSURLSessionDownloadTask*)CreateDownloadForURL:(NSURL* _Nonnull)URL WithPriority:(float)Priority WithTaskData:(FBackgroundNSURLSessionDownloadTaskData* _Nonnull)TaskData;
- (NSURLSessionDownloadTask*)CreateDownloadForResumeData:(NSData* _Nonnull)ResumeData WithPriority:(float)Priority WithTaskData:(FBackgroundNSURLSessionDownloadTaskData* _Nonnull)TaskData;

- (NSUInteger)CreateOrFindDownloadForURLs:(NSArray<__kindof NSString*>*)URLStrings WithPriority:(float)Priority;
- (void)PauseDownload:(NSUInteger)DownloadId;
- (void)ResumeDownload:(NSUInteger)DownloadId;
- (void)CancelDownload:(NSUInteger)DownloadId;
- (void)SetPriority:(float)Priority ForDownload:(NSUInteger)DownloadId;
- (void)SetCurrentDownloadedBytes:(uint64)DownloadedBytes ForTask:(NSURLSessionDownloadTask*)Task;
- (uint64)GetCurrentDownloadedBytes:(NSUInteger)DownloadId;
- (void)RecreateDownload:(NSUInteger)DownloadId ShouldResetRetryCount:(bool)ResetRetryCount;
- (void)RecreateDownloads;
#if !UE_BUILD_SHIPPING
- (NSString*)GetDownloadDebugText:(NSUInteger)DownloadId;
#endif

- (void)StartCheckingForStaleDownloads;
- (void)StopCheckingForStaleDownloads;
- (void)CheckForStaleDownloads:(NSTimer*)Timer;

- (void)HandleDidEnterBackground;
- (void)HandleWillEnterForeground;

- (NSURLSessionDownloadTask*)FindDownloadTaskFor:(NSUInteger)DownloadId;
- (NSUInteger)FindDownloadIdForTask:(NSURLSessionDownloadTask*)Task;
- (NSUInteger)EnsureTaskIsTracked:(NSURLSessionDownloadTask*)Task;
- (void)ReplaceTrackedTaskWith:(NSURLSessionDownloadTask*)NewTask ForDownloadId:(NSUInteger)DownloadId;
- (void)EnsureTaskIsNotTracked:(NSURLSessionDownloadTask*)Task;

- (void)SetDownloadResult:(NSInteger)HTTPCode WithTempFile:(NSString*)TempFile ForDownload:(NSURLSessionDownloadTask*)Task;
- (NSString* _Nullable)GetDownloadResult:(NSUInteger)DownloadId OutStatus:(BOOL* _Nonnull)OutStatus OutStatusCode:(NSInteger*)OutStatusCode;

// From NSURLSessionDelegate
- (void)URLSessionDidFinishEventsForBackgroundURLSession:(NSURLSession*)Session;

// From NSURLSessionTaskDelegate
- (void)URLSession:(NSURLSession*)Session task:(NSURLSessionTask*)Task didCompleteWithError:(NSError*)Error;
//- (void)URLSession:(NSURLSession*)Session taskIsWaitingForConnectivity:(NSURLSessionTask *)Task;
//- (void)URLSession:(NSURLSession*)Session task:(NSURLSessionTask *)Task willBeginDelayedRequest:(NSURLRequest *)Request completionHandler:(void (^)(NSURLSessionDelayedRequestDisposition Disposition, NSURLRequest* NewRequest))CompletionHandler;

// From NSURLSessionDownloadDelegate
- (void)URLSession:(NSURLSession*)Session downloadTask:(NSURLSessionDownloadTask*)Task didFinishDownloadingToURL:(NSURL*)Location;
- (void)URLSession:(NSURLSession*)Session downloadTask:(NSURLSessionDownloadTask*)Task didWriteData:(int64_t)BytesWritten totalBytesWritten:(int64_t)TotalBytesWritten totalBytesExpectedToWrite:(int64_t)TotalBytesExpectedToWrite;
//- (void)URLSession:(NSURLSession*)Session downloadTask:(NSURLSessionDownloadTask*)Task didResumeAtOffset:(int64_t)FileOffset expectedTotalBytes:(int64_t)ExpectedTotalBytes;

@end

// --------------------------------------------------------------------------------------------------------------------

@implementation FBackgroundNSURLSession
{
	NSURLSession* _Session;
	NSMutableDictionary<__kindof NSNumber*, __kindof NSURLSessionDownloadTask*>* _AllDownloads;
	NSUInteger _NextDownloadId;
	std::promise<void> _AllDownloadsPromise;
	std::future<void> _AllDownloadsFuture;
	NSMutableArray<__kindof FBackgroundNSURLCDNInfo*>* _CDNInfo;
	NSTimer* _ForegroundStaleDownloadCheckTimer;
	BackgroundHttpFileHashHelperPtr _HelperPtr;
	int32 _MaximumConnectionsPerHost;
	int32 _RetryResumeDataLimit;
	int32 _CDNReorderingTimeout;
	bool _bCDNReorderByPingTime;
	double _CheckForForegroundStaleDownloadsWithInterval;
	double _ForegroundStaleDownloadTimeout;
	std::atomic<bool> _bAnyTaskDidCompleteWithError;
}

static constexpr NSUInteger InvalidDownloadId = 0;

NSString* const NSURLSessionIdentifier = @"com.epicgames.backgrounddownloads";

NSProgressUserInfoKey const NSProgressDownloadLastUpdateTime = @"com.epicgames.nsprogress.lastupdatetime";
NSProgressUserInfoKey const NSProgressDownloadCompletedBytes = @"com.epicgames.nsprogress.completedbytes";
NSProgressUserInfoKey const NSProgressDownloadResultStatusCode = @"com.epicgames.nsprogress.resultstatuscode";
NSProgressUserInfoKey const NSProgressDownloadResultTempFilePath = @"com.epicgames.nsprogress.tempfilepath";

static constexpr NSInteger HTTPStatusCodeSuccessCreated = 201;
static constexpr NSInteger HTTPStatusCodeErrorBadRequest = 400;
static constexpr NSInteger HTTPStatusCodeErrorServer = 500;

+ (FBackgroundNSURLSession*)Shared
{
	static FBackgroundNSURLSession* Shared = nil;
	static dispatch_once_t Once = {};
	dispatch_once(&Once, ^{
		Shared = [[self alloc] init];
	});
	return Shared;
}

+ (NSUInteger)GetInvalidDownloadId;
{
	return InvalidDownloadId;
}

+ (NSString*)GetNSURLSessionIdentifier
{
	return NSURLSessionIdentifier;
}

- (id)init
{
	if (self = [super init])
	{
		[self Initialize];
	}
	return self;
}

- (void)Initialize
{
	bool bUseForegroundSession = false;
	bool bDiscretionary = false;
	bool bShouldSendLaunchEvents = true;
	_MaximumConnectionsPerHost = 6;
	double TimeoutIntervalForRequest = 120.0; // Note, ignored in background sessions (if bUseForegroundSession is false).
	double TimeoutIntervalForResource = 60.0 * 60.0;
	_RetryResumeDataLimit = 3;
	_CDNReorderingTimeout = 400;
	_bCDNReorderByPingTime = false;
	_CheckForForegroundStaleDownloadsWithInterval = 1.0; // how often to check for stale downloads, <=0.0 to disable
	_ForegroundStaleDownloadTimeout = 30.0; // If download hasn't received any bytes for this duration, cancel and retry if possible

#ifndef UE_DNLD_SANDBOX
	GConfig->GetBool(TEXT("BackgroundHttp.iOSSettings"), TEXT("bUseForegroundSession"), bUseForegroundSession, GEngineIni);
	GConfig->GetBool(TEXT("BackgroundHttp.iOSSettings"), TEXT("bDiscretionary"), bDiscretionary, GEngineIni);
	GConfig->GetBool(TEXT("BackgroundHttp.iOSSettings"), TEXT("bShouldSendLaunchEvents"), bShouldSendLaunchEvents, GEngineIni);
	GConfig->GetInt(TEXT("BackgroundHttp.iOSSettings"), TEXT("MaximumConnectionsPerHost"), _MaximumConnectionsPerHost, GEngineIni);
	GConfig->GetDouble(TEXT("BackgroundHttp.iOSSettings"), TEXT("BackgroundReceiveTimeout"), TimeoutIntervalForRequest, GEngineIni);
	GConfig->GetDouble(TEXT("BackgroundHttp.iOSSettings"), TEXT("BackgroundHttpResourceTimeout"), TimeoutIntervalForResource, GEngineIni);
	GConfig->GetInt(TEXT("BackgroundHttp.iOSSettings"), TEXT("RetryResumeDataLimit"), _RetryResumeDataLimit, GEngineIni);
	GConfig->GetInt(TEXT("BackgroundHttp.iOSSettings"), TEXT("CDNReorderingTimeout"), _CDNReorderingTimeout, GEngineIni);
	GConfig->GetBool(TEXT("BackgroundHttp.iOSSettings"), TEXT("bCDNReorderByPingTime"), _bCDNReorderByPingTime, GEngineIni);
	GConfig->GetDouble(TEXT("BackgroundHttp.iOSSettings"), TEXT("CheckForForegroundStaleDownloadsWithInterval"), _CheckForForegroundStaleDownloadsWithInterval, GEngineIni);
	GConfig->GetDouble(TEXT("BackgroundHttp.iOSSettings"), TEXT("ForegroundStaleDownloadTimeout"), _ForegroundStaleDownloadTimeout, GEngineIni);
#endif

	UE_DNLD_LOG(@"bUseForegroundSession=%u", bUseForegroundSession ? 1 : 0);
	UE_DNLD_LOG(@"bDiscretionary=%u", bDiscretionary ? 1 : 0);
	UE_DNLD_LOG(@"bShouldSendLaunchEvents=%u", bShouldSendLaunchEvents ? 1 : 0);
	UE_DNLD_LOG(@"MaximumConnectionsPerHost=%i", _MaximumConnectionsPerHost);
	UE_DNLD_LOG(@"TimeoutIntervalForRequest=%f", TimeoutIntervalForRequest);
	UE_DNLD_LOG(@"TimeoutIntervalForResource=%f", TimeoutIntervalForResource);
	UE_DNLD_LOG(@"RetryResumeDataLimit=%i", _RetryResumeDataLimit);
	UE_DNLD_LOG(@"CDNReorderingTimeout=%i", _CDNReorderingTimeout);
	UE_DNLD_LOG(@"CDNReorderByPingTime=%u", _bCDNReorderByPingTime ? 1 : 0);
	UE_DNLD_LOG(@"CheckForForegroundStaleDownloadsWithInterval=%f", _CheckForForegroundStaleDownloadsWithInterval);
	UE_DNLD_LOG(@"ForegroundStaleDownloadTimeout=%f", _ForegroundStaleDownloadTimeout);

	_AllDownloads = [NSMutableDictionary new];
	_AllDownloadsFuture = _AllDownloadsPromise.get_future();
	_NextDownloadId = InvalidDownloadId + 1;
	_CDNInfo = [NSMutableArray new];
	_ForegroundStaleDownloadCheckTimer = nil;
	_bAnyTaskDidCompleteWithError = false;

	// Never allow cellular unless we get explicit opt-in from the user.
	self.AllowCellular = NO;

	NSURLSessionConfiguration* Configuration = bUseForegroundSession ?
		[NSURLSessionConfiguration defaultSessionConfiguration] :
		[NSURLSessionConfiguration backgroundSessionConfigurationWithIdentifier: NSURLSessionIdentifier];

	// iOS will schedule downloads on it's own if true,
	// otherwise all downloads will be scheduled ASAP if false
	Configuration.discretionary = bDiscretionary;

	// In case if our app gets killed in background, iOS will launch it and report finished downloads via handleEventsForBackgroundURLSession.
	// This will help us to retry/fail-over downloads in background without waiting for user to open the game again.
	// Note that this behavior can be disabled via Background App Refresh set to No in iOS settings.
	Configuration.sessionSendsLaunchEvents = bShouldSendLaunchEvents;

	// Set session to allow cellular and instead control this on NSMutableURLRequest level because this value cannot be changed after session is created.
	Configuration.allowsCellularAccess = YES;

	Configuration.networkServiceType = bUseForegroundSession ? NSURLNetworkServiceTypeDefault : NSURLNetworkServiceTypeBackground;

	// TODO Is this any use for us? Needs entitlement.
	//Configuration.multipathServiceType = NSURLSessionMultipathServiceTypeAggregate;

	Configuration.HTTPMaximumConnectionsPerHost = _MaximumConnectionsPerHost;

	Configuration.timeoutIntervalForRequest = TimeoutIntervalForRequest;

	Configuration.timeoutIntervalForResource = TimeoutIntervalForResource;

	_Session = [[NSURLSession sessionWithConfiguration:Configuration delegate:self delegateQueue:nil] retain];
	UE_DNLD_LOG(@"sessionWithConfiguration '%@'", Configuration.identifier);

	[_Session getTasksWithCompletionHandler:^(NSArray<__kindof NSURLSessionDataTask*>*, NSArray<__kindof NSURLSessionUploadTask*>*, NSArray<__kindof NSURLSessionDownloadTask*>* Downloads)
	{
		UE_DNLD_LOG(@"getTasksWithCompletionHandler block with %lu tasks", (unsigned long)[Downloads count]);
		
		if (Downloads != nil)
		{
			for (NSURLSessionDownloadTask* ExistingTask in Downloads)
			{
				const bool bCanRestartTask =
					(ExistingTask.state == NSURLSessionTaskStateRunning) ||
					(ExistingTask.state == NSURLSessionTaskStateSuspended);

				if (!bCanRestartTask)
				{
					UE_DNLD_LOG(@"Skipping tracking for existing download task with taskIdentifier %lu because it's not in resumable state", ExistingTask.taskIdentifier);
					continue;
				}

				const NSUInteger DownloadId = [self EnsureTaskIsTracked:ExistingTask];

				if (bCancelExistingDownloads)
				{
					UE_DNLD_LOG(@"Canceling existing download task with taskIdentifier %lu", ExistingTask.taskIdentifier);
					[self CancelDownload:DownloadId];
				}

				UE_DNLD_LOG(@"Existing task '%@'", ExistingTask.taskDescription);
			}
		}

		_AllDownloadsPromise.set_value();
	}];

	const FString& DirectoryPath = FBackgroundHttpFileHashHelper::GetTemporaryRootPath();
	if (ensureAlwaysMsgf(!DirectoryPath.IsEmpty(), TEXT("Invalid FBackgroundHttpFileHashHelper::GetTemporaryRootPath()")))
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		if (bCancelExistingDownloads)
		{
			PlatformFile.DeleteDirectory(*DirectoryPath);
		}

		PlatformFile.CreateDirectory(*DirectoryPath);

		if (!PlatformFile.DirectoryExists(*DirectoryPath))
		{
			ensureAlwaysMsgf(false, TEXT("Failed to create temporary directory for background downloads"));
		}
	}
}

- (void)dealloc
{
	[_Session release];
	_Session = nil;

	[_AllDownloads release];
	_AllDownloads = nil;

	if (_CDNInfo != nil)
	{
		[_CDNInfo release];
		_CDNInfo = nil;
	}

	if (_ForegroundStaleDownloadCheckTimer != nil)
	{
		[_ForegroundStaleDownloadCheckTimer release];
		_ForegroundStaleDownloadCheckTimer = nil;
	}

	[super dealloc];
}

- (void)SetFileHashHelper:(BackgroundHttpFileHashHelperRef)HelperRef
{
	_HelperPtr = HelperRef.ToSharedPtr();
}

- (BackgroundHttpFileHashHelperRef)GetFileHashHelper
{
	// initialize a new instance in case if we get here from handleEventsForBackgroundURLSession
	if (!_HelperPtr.IsValid())
	{
		_HelperPtr = MakeShared<FBackgroundHttpFileHashHelper, ESPMode::ThreadSafe>();
		_HelperPtr->LoadData();
	}

	return _HelperPtr.ToSharedRef();
}

- (void)SaveFileHashHelperState
{
	@synchronized (self)
	{
		[self GetFileHashHelper]->SaveData();
	}
}

- (NSString*)GetTempPathForURL:(NSURL* _Nonnull)URL
{
	@synchronized (self)
	{
		BackgroundHttpFileHashHelperRef HelperRef = [self GetFileHashHelper];

		const FString TaskURL(URL.absoluteString);
		const FString& TempFileName = HelperRef->FindOrAddTempFilenameMappingForURL(TaskURL);
		const FString& DestinationPath = HelperRef->GetFullPathOfTempFilename(TempFileName);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const FString ConvertedPath = PlatformFile.ConvertToAbsolutePathForExternalAppForWrite(*DestinationPath);

		return ConvertedPath.GetNSString();
	}
}

- (NSMutableArray<__kindof NSURL*>*)ReorderCDNsByReachability:(NSMutableArray<__kindof NSURL*>*)URLs
{
	if (_CDNReorderingTimeout == 0 || [URLs count] == 0)
	{
		return URLs;
	}

	@synchronized (_CDNInfo)
	{
		if ([_CDNInfo count] == 0)
		{
			UE_DNLD_LOG(@"Starting to check for CDN reachability");

			const double CDNReorderingTimeoutInSeconds = (double)_CDNReorderingTimeout / 1000;

			// Creating a temporary foreground NSURLSession for pinging CDN's
			NSURLSessionConfiguration* Configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
			Configuration.discretionary = NO;
			// Don't go over cellular here, during first download attempt we would try to over non-cellular connections first.
			// Hence it makes more sense to prioritize CDN's that are reachable via non-cellular connections.
			Configuration.allowsCellularAccess = NO;
			Configuration.networkServiceType = NSURLNetworkServiceTypeResponsiveData;
			Configuration.timeoutIntervalForRequest = CDNReorderingTimeoutInSeconds;
			Configuration.timeoutIntervalForResource = CDNReorderingTimeoutInSeconds;
			Configuration.HTTPMaximumConnectionsPerHost = _MaximumConnectionsPerHost;

			NSURLSession* Session = [NSURLSession sessionWithConfiguration:Configuration];

			std::shared_ptr<std::atomic<int32>> PendingTasks = std::make_shared<std::atomic<int32>>();
			std::shared_ptr<std::promise<void>> PendingTasksFinished = std::make_shared<std::promise<void>>();
			std::shared_ptr<std::atomic<bool>> WaitingForTasksCompletionHandlers = std::make_shared<std::atomic<bool>>(true);

			PendingTasks->fetch_add((int32)URLs.count);

			NSDate* StartTime = [NSDate date];

			for (NSURL* URL in URLs)
			{
				NSMutableURLRequest* Request = [NSMutableURLRequest requestWithURL:URL];
				// Use HEAD request because we want the smallest response possible from the CDN to see if the connection works at all.
				[Request setHTTPMethod:@"HEAD"];

				UE_DNLD_LOG(@"Create data task for '%@'", Request.URL.absoluteString);

				// Note, completion handler might be invoked after end of this method.
				NSURLSessionDataTask* Task = [Session dataTaskWithRequest:Request completionHandler:^(NSData* _Nullable Data, NSURLResponse* _Nullable Response, NSError* _Nullable Error)
				{
					// The delegate can be invoked from other thread way past invalidateAndCancel,
					// we cannot modify _CDNInfo without synchronization.
					if (!WaitingForTasksCompletionHandlers->load())
					{
						return;
					}

					const NSTimeInterval ResponseTime = -[StartTime timeIntervalSinceNow];

					bool bIsOk = false;
					const bool bIsTimeout = (Error != nil) ? (Error.code == NSURLErrorTimedOut || Error.code == NSURLErrorCancelled) : false;

					if (Response != nil && [Response isKindOfClass:[NSHTTPURLResponse class]])
					{
						NSHTTPURLResponse* HTTPResponse = (NSHTTPURLResponse*)Response;
						UE_DNLD_LOG(@"Finished data task for '%@' (host '%@') with status code %li and response time %f", Request.URL.absoluteString, Request.URL.host, HTTPResponse.statusCode, ResponseTime);

						if (HTTPResponse.statusCode < HTTPStatusCodeErrorBadRequest)
						{
							bIsOk = true;
						}
					}
					else
					{
						UE_DNLD_LOG(@"Finished data task for '%@' with error '%@' (%i, %i) and response time %f", Request.URL.absoluteString, (Error != nil ? Error.localizedDescription : @"nil"), (Error != nil ? (int32)Error.code : 0), bIsTimeout ? 1 : 0, ResponseTime);
					}

					FBackgroundNSURLCDNInfo* Info = [[FBackgroundNSURLCDNInfo alloc] init];
					[Info SetFromURL:Request.URL];
					if (bIsOk)
					{
						[Info setResponse:EBackgroundNSURLCDNInfoResponse::Ok];
					}
					else if (bIsTimeout)
					{
						[Info setResponse:EBackgroundNSURLCDNInfoResponse::Timeout];
					}
					else
					{
						[Info setResponse:EBackgroundNSURLCDNInfoResponse::Error];
					}
					[Info setResponseTime:ResponseTime];
					[_CDNInfo addObject:Info];
					[Info release];

					if (PendingTasks->fetch_add(-1) <= 1)
					{
						UE_DNLD_LOG(@"Finished all data tasks for CDN reachability");
						PendingTasksFinished->set_value();
					}
				}];
				[Task setPriority:NSURLSessionTaskPriorityHigh];
				[Task resume];
			}

			PendingTasksFinished->get_future().wait_for(std::chrono::milliseconds(_CDNReorderingTimeout));
			UE_DNLD_LOG(@"Finished waiting for CDN reachability");

			WaitingForTasksCompletionHandlers->store(false);
			[Session invalidateAndCancel];

			for (NSUInteger URLIndex = 0; URLIndex < [URLs count]; URLIndex++)
			{
				NSURL* URL = [URLs objectAtIndex:URLIndex];
				bool bFoundCDNInfo = false;

				for (FBackgroundNSURLCDNInfo* Info in _CDNInfo)
				{
					if ([Info.CDNHost isEqualToString:URL.host])
					{
						[Info setProvidedOrder:URLIndex];
						bFoundCDNInfo = true;
						break;
					}
				}

				if (!bFoundCDNInfo)
				{
					FBackgroundNSURLCDNInfo* Info = [[FBackgroundNSURLCDNInfo alloc] init];
					[Info SetFromURL:URL];
					// If cdn/networking hasn't provided us with any info, consider request timed out
					[Info setResponse:EBackgroundNSURLCDNInfoResponse::Timeout];
					[Info setProvidedOrder:URLIndex];
					[_CDNInfo addObject:Info];
					[Info release];
				}
			}
			
			[_CDNInfo sortUsingComparator:^NSComparisonResult(FBackgroundNSURLCDNInfo* _Nonnull A, FBackgroundNSURLCDNInfo* _Nonnull B)
			{
				const double KeyA = [A SortingKeyWith:_bCDNReorderByPingTime];
				const double KeyB = [B SortingKeyWith:_bCDNReorderByPingTime];

				if (KeyA < KeyB)
				{
					return NSOrderedAscending;
				}
				else if (KeyA > KeyB)
				{
					return NSOrderedDescending;
				}
				else
				{
					return NSOrderedSame;
				}
			}];

			for (NSUInteger i = 0; i < [_CDNInfo count]; i++)
			{
				FBackgroundNSURLCDNInfo* Info = [_CDNInfo objectAtIndex:i];
				UE_DNLD_LOG(@"%lu CDN '%@' AbsoluteURL '%@' Response:%u ResponseTime:%f ProvidedOrder:%lu SortingKey:%f", i, Info.CDNHost, Info.CDNAbsoluteURL, Info.Response, Info.ResponseTime, (unsigned long)Info.ProvidedOrder, [Info SortingKeyWith:_bCDNReorderByPingTime]);
			}
		}

		// Array sizes are assumed small enough that hashmap is not needed
		NSMutableArray<__kindof NSURL*>* Result = [NSMutableArray arrayWithCapacity:[URLs count]];

		for (FBackgroundNSURLCDNInfo* Info in _CDNInfo)
		{
			for (NSURL* URL in URLs)
			{
				if ([Info.CDNHost isEqualToString:URL.host])
				{
					[Result addObject:URL];
					break;
				}
			}
		}

		// Add CDN's that weren't present at first lookup
		for (NSURL* URL in URLs)
		{
			if (![Result containsObject:URL])
			{
				[Result addObject:URL];
			}
		}

		return Result;
	}
}

- (NSArray<__kindof FBackgroundNSURLCDNInfo*>*)GetCDNInfo
{
	@synchronized (_CDNInfo)
	{
		return _CDNInfo;
	}
}

- (NSURLSessionDownloadTask*)CreateDownloadForURL:(NSURL* _Nonnull)URL WithPriority:(float)Priority WithTaskData:(FBackgroundNSURLSessionDownloadTaskData* _Nonnull)TaskData;
{
	NSMutableURLRequest* URLRequest = [NSMutableURLRequest requestWithURL:URL];
	URLRequest.allowsCellularAccess = self.AllowCellular;

	NSURLSessionDownloadTask* Task = [_Session downloadTaskWithRequest:URLRequest];
	[Task setPriority:Priority];
	[Task setTaskDescription:[TaskData ToSerializedString]];

	UE_DNLD_LOG(@"CreateDownloadForURL '%@' with taskIdentifier %lu", Task.taskDescription, Task.taskIdentifier);
	return Task;
}

- (NSURLSessionDownloadTask*)CreateDownloadForResumeData:(NSData* _Nonnull)ResumeData WithPriority:(float)Priority WithTaskData:(FBackgroundNSURLSessionDownloadTaskData* _Nonnull)TaskData
{
	NSURLSessionDownloadTask* Task = [_Session downloadTaskWithResumeData:ResumeData];
	[Task setPriority:Priority];
	[Task setTaskDescription:[TaskData ToSerializedString]];

	UE_DNLD_LOG(@"CreateDownloadForResumeData '%@' with taskIdentifier %lu", Task.taskDescription, Task.taskIdentifier);
	return Task;
}

- (NSUInteger)CreateOrFindDownloadForURLs:(NSArray<__kindof NSString*>*)URLStrings WithPriority:(float)Priority
{
	if (_AllDownloadsFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
	{
		UE_DNLD_LOG(@"Starting wait for existing downloads status");
		
		_AllDownloadsFuture.wait();

		UE_DNLD_LOG(@"Done waiting for existing downloads status");
	}

	// To be able to store less state we assume all strings have same asset path suffix
	NSString* AssetPath = nil;
	NSMutableArray<__kindof NSURL*>* URLs = [NSMutableArray arrayWithCapacity:URLStrings.count];
	for (NSString* URLString in URLStrings)
	{
		NSURL* URLValue = [NSURL URLWithString:URLString];
		if (AssetPath == nil)
		{
			AssetPath = URLValue.path;
		}
		if (![URLValue.absoluteString hasSuffix:AssetPath])
		{
			UE_DNLD_LOG(@"Expected all URLs have same path but got '%@' when expected path '%@'", URLString, AssetPath);
		}
		[URLs addObject:URLValue];
	}

	URLs = [self ReorderCDNsByReachability:URLs];

	// Serialize current settings
	FBackgroundNSURLSessionDownloadTaskData* TaskData = [FBackgroundNSURLSessionDownloadTaskData TaskDataWithURLs:URLs WithRetryCount:_RetryResumeDataLimit];

	// Check for existing download task, could be from previous app session.
	UE_DNLD_LOG(@"Trying to find existing download for asset path '%@'", AssetPath);
	if (AssetPath != nil)
	{
		@synchronized(_AllDownloads)
		{
			__block NSUInteger ExistingDownloadId = InvalidDownloadId;
			__block NSURLSessionDownloadTask* ExistingTask = nil;
			[_AllDownloads enumerateKeysAndObjectsUsingBlock:^(NSNumber* IterKey, NSURLSessionDownloadTask* IterTask, BOOL* IterStop)
			 {
				if ((IterTask.originalRequest != nil && [IterTask.originalRequest.URL.path isEqualToString:AssetPath])
					|| (IterTask.currentRequest != nil && [IterTask.currentRequest.URL.path isEqualToString:AssetPath]))
				{
					ExistingDownloadId = IterKey.unsignedIntegerValue;
					ExistingTask = [IterTask retain]; // Retain in case if task gets killed in another thread.
					*IterStop = YES;
				}
			}];

			if (ExistingDownloadId != InvalidDownloadId && ExistingTask != nil)
			{
				UE_DNLD_LOG(@"Found existing download task for path '%@' with DownloadId %lu", AssetPath, ExistingDownloadId);
				
				// Update existing task state to new one, to reset retry counters, cdn links, etc.
				[ExistingTask setTaskDescription:[TaskData ToSerializedString]];
				
				[ExistingTask resume]; // Resume task just in case if it was not running before.
				[ExistingTask release];
				
				return ExistingDownloadId;
			}
		}
	}

	NSURL* URL = [TaskData GetFirstURL];
	NSURLSessionDownloadTask* Task = [self CreateDownloadForURL:URL WithPriority:Priority WithTaskData:TaskData];
	const NSUInteger DownloadId = [self EnsureTaskIsTracked:Task];
	[Task resume];

	return DownloadId;
}

- (void)PauseDownload:(NSUInteger)DownloadId
{
	UE_DNLD_LOG(@"PauseDownload for DownloadId %lu", DownloadId);

	NSURLSessionDownloadTask* Task = [self FindDownloadTaskFor:DownloadId];
	if (Task != nil)
	{
		[Task suspend];
	}
}

- (void)ResumeDownload:(NSUInteger)DownloadId
{
	UE_DNLD_LOG(@"ResumeDownload for DownloadId %lu", DownloadId);

	NSURLSessionDownloadTask* Task = [self FindDownloadTaskFor:DownloadId];
	if (Task != nil)
	{
		[Task resume];
	}
}

- (void)CancelDownload:(NSUInteger)DownloadId
{
	UE_DNLD_LOG(@"CancelDownload for DownloadId %lu", DownloadId);

	NSURLSessionDownloadTask* Task = [self FindDownloadTaskFor:DownloadId];
	if (Task == nil)
	{
		return;
	}

	FBackgroundNSURLSessionDownloadTaskData* TaskData = [FBackgroundNSURLSessionDownloadTaskData TaskDataFromSerializedString:Task.taskDescription];
	if (TaskData != nil)
	{
		// Remove task data from this download task, otherwise didCompleteWithError might retry the request.
		[TaskData Cancel];
		Task.taskDescription = [TaskData ToSerializedString];
	}

	// Must retain otherwise EnsureTaskIsNotTracked will free the object.
	[Task retain];

	// We're done with this task.
	[self EnsureTaskIsNotTracked:Task];

	// Will invoke didCompleteWithError if task is incomplete.
	[Task cancel];
	
	// Finally release the object.
	[Task release];
}

- (void)SetPriority:(float)Priority ForDownload:(NSUInteger)DownloadId
{
	UE_DNLD_LOG(@"SetPriority %f for DownloadId %lu", Priority, DownloadId);

	NSURLSessionDownloadTask* Task = [self FindDownloadTaskFor:DownloadId];
	if (Task != nil)
	{
		[Task setPriority:Priority];
	}
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)metrics {
#if !UE_BUILD_SHIPPING
	NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    [formatter setDateFormat:@"yyyy-MM-dd'T'HH:mm:ss.SSSZZZZZ"];
    [formatter setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];
    
    // Calculate task interval
    NSURLSessionTaskTransactionMetrics *firstMetric = metrics.transactionMetrics.firstObject;
    NSURLSessionTaskTransactionMetrics *lastMetric = metrics.transactionMetrics.lastObject;
	UE_DNLD_LOG(@"Task %lu got metrics", task.taskIdentifier);
    if (firstMetric && lastMetric) {
        NSTimeInterval taskInterval = [lastMetric.requestEndDate timeIntervalSinceDate:firstMetric.requestStartDate];
        UE_DNLD_LOG(@"Task Interval: %f", taskInterval);
    }

    // Get redirect count
    NSInteger redirectCount = metrics.transactionMetrics.count - 1; // The first metric is the initial request, redirects are additional metrics
    UE_DNLD_LOG(@"Redirect Count: %ld", (long)redirectCount);
#endif

	for (NSURLSessionTaskTransactionMetrics *metric in metrics.transactionMetrics) {
#if !UE_BUILD_SHIPPING
		UE_DNLD_LOG(@"Network Protocol Name: %@", metric.networkProtocolName);
		UE_DNLD_LOG(@"Reused Connection: %@", metric.reusedConnection ? @"Yes" : @"No");
		UE_DNLD_LOG(@"Proxy Connection: %@", metric.proxyConnection ? @"Yes": @"No");

        UE_DNLD_LOG(@"Fetch Start Date: %@", [formatter stringFromDate:metric.fetchStartDate]);
        UE_DNLD_LOG(@"Request Start Date: %@", [formatter stringFromDate:metric.requestStartDate]);
        UE_DNLD_LOG(@"Response Start Date: %@", [formatter stringFromDate:metric.responseStartDate]);
        UE_DNLD_LOG(@"Request End Date: %@", [formatter stringFromDate:metric.requestEndDate]);
        UE_DNLD_LOG(@"Response End Date: %@", [formatter stringFromDate:metric.responseEndDate]);
#endif

		// Calculate and log response duration
        if (metric.responseStartDate && metric.responseEndDate) {
            NSTimeInterval responseDuration = [metric.responseEndDate timeIntervalSinceDate:metric.responseStartDate];
#if !UE_BUILD_SHIPPING
            UE_DNLD_LOG(@"Response Duration: %f seconds", responseDuration);
#endif

			// Calculate and log download speed
            if (responseDuration > 0 && metric.countOfResponseBodyBytesReceived > 0) {
				
				FBackgroundURLSessionHandler::OnDownloadMetrics.Broadcast((uint64)task.taskIdentifier, (int32)metric.countOfResponseBodyBytesReceived, (float)responseDuration);
				
#if !UE_BUILD_SHIPPING
				// bytes per second
				double downloadSpeed = (double)metric.countOfResponseBodyBytesReceived / responseDuration;
				NSString *formattedSpeed = [self formattedSpeed:downloadSpeed];
				UE_DNLD_LOG(@"Download Speed: %@", formattedSpeed);
            } else {
                UE_DNLD_LOG(@"Download Speed: Not Available");
#endif
            }
        }
#if !UE_BUILD_SHIPPING
		else {
            UE_DNLD_LOG(@"Response Duration: Not Available");
        }
#endif
    }
#if !UE_BUILD_SHIPPING
	UE_DNLD_LOG(@"-------------------------");
#endif
}

#if !UE_BUILD_SHIPPING
- (NSString *)formattedSpeed:(double)speedInBytesPerSecond {
    NSArray *units = @[@"bytes/second", @"KB/s", @"MB/s", @"GB/s"];
    double speed = speedInBytesPerSecond;
    NSInteger unitIndex = 0;

    while (speed >= 1024 && unitIndex < units.count - 1) {
        speed /= 1024;
        unitIndex++;
    }
    
    return [NSString stringWithFormat:@"%.2f %@", speed, units[unitIndex]];
}
#endif

- (void)SetCurrentDownloadedBytes:(uint64)DownloadedBytes ForTask:(NSURLSessionDownloadTask*)Task
{
	if (Task != nil)
	{
		[Task.progress setUserInfoObject:[NSNumber numberWithDouble:[[NSDate date] timeIntervalSince1970]] forKey:NSProgressDownloadLastUpdateTime];
		[Task.progress setUserInfoObject:[NSNumber numberWithLongLong:DownloadedBytes] forKey:NSProgressDownloadCompletedBytes];
	}
}

- (uint64)GetCurrentDownloadedBytes:(NSUInteger)DownloadId
{
	NSURLSessionDownloadTask* Task = [self FindDownloadTaskFor:DownloadId];
	if (Task != nil)
	{
		NSNumber* CompletedBytes = [Task.progress.userInfo objectForKey:NSProgressDownloadCompletedBytes];
		if (CompletedBytes != nil)
		{
			return CompletedBytes.unsignedLongLongValue;
		}
	}

	return 0;
}

- (void)RecreateDownload:(NSUInteger)DownloadId ShouldResetRetryCount:(bool)ResetRetryCount
{
	UE_DNLD_LOG(@"RecreateDownload for DownloadId %lu", DownloadId);

	NSURLSessionDownloadTask* OldTask = [self FindDownloadTaskFor:DownloadId];
	const float OldTaskPriority = OldTask.priority;
	const NSURLSessionTaskState OldTaskState = OldTask.state;

	FBackgroundNSURLSessionDownloadTaskData* NewTaskData = [FBackgroundNSURLSessionDownloadTaskData TaskDataFromSerializedString:OldTask.taskDescription];
	if (NewTaskData == nil)
	{
		return;
	}

	// Cancel old task
	OldTask = nil;
	[self CancelDownload:DownloadId];

	if (ResetRetryCount)
	{
		[NewTaskData ResetRetryCount:_RetryResumeDataLimit];
	}

	// Start a new task
	NSURLSessionDownloadTask* NewTask = [self CreateDownloadForURL:[NewTaskData GetFirstURL] WithPriority:OldTaskPriority WithTaskData:NewTaskData];
	[self ReplaceTrackedTaskWith:NewTask ForDownloadId:DownloadId];
	if (OldTaskState != NSURLSessionTaskStateSuspended)
	{
		[NewTask resume];
	}
}

- (void)RecreateDownloads
{
	UE_DNLD_LOG(@"RecreateDownloads started");

	// Copy keys to avoid deadlocking in case if cancel/resume/etc will call delegates in-place
	NSArray<__kindof NSNumber*>* AllKeys = nil;
	@synchronized(_AllDownloads)
	{
		AllKeys = [_AllDownloads allKeys];
	}

	for (NSNumber* IterKey in AllKeys)
	{
		const NSUInteger DownloadId = IterKey.unsignedIntegerValue;
		[self RecreateDownload:DownloadId ShouldResetRetryCount:true];
	}

	UE_DNLD_LOG(@"RecreateDownloads finished");
}

#if !UE_BUILD_SHIPPING
- (NSString*)GetDownloadDebugText:(NSUInteger)DownloadId
{
	NSURLSessionDownloadTask* Task = [self FindDownloadTaskFor:DownloadId];
	if (Task != nil)
	{
		NSString* Description = [NSString stringWithFormat:@"iOSBG %llu %@", (uint64)DownloadId, Task.currentRequest.URL.absoluteString];

		NSNumber* ResultStatusCode = [Task.progress.userInfo objectForKey:NSProgressDownloadResultStatusCode];
		if (ResultStatusCode != nil)
		{
			return [NSString stringWithFormat:@"%@ finished with status %i", Description, (int32)ResultStatusCode.integerValue];
		}

		NSNumber* CompletedBytes = [Task.progress.userInfo objectForKey:NSProgressDownloadCompletedBytes];
		if (CompletedBytes != nil)
		{
			return [NSString stringWithFormat:@"%@ downloaded %.2f MBytes", Description, (double)CompletedBytes.unsignedLongLongValue / (1024.0 * 1024.0)];
		}

		return [NSString stringWithFormat:@"%@ pending", Description];
	}
	else
	{
		return [NSString stringWithFormat:@"%llu is not tracked", (uint64)DownloadId];
	}
}
#endif

- (void)StartCheckingForStaleDownloads
{
	if (_ForegroundStaleDownloadCheckTimer == nil && _CheckForForegroundStaleDownloadsWithInterval > 0.0)
	{
		dispatch_async(dispatch_get_main_queue(), ^
		{
			if (_ForegroundStaleDownloadCheckTimer == nil && _CheckForForegroundStaleDownloadsWithInterval > 0.0)
			{
				_ForegroundStaleDownloadCheckTimer = [[NSTimer
													   scheduledTimerWithTimeInterval:_CheckForForegroundStaleDownloadsWithInterval
													   target:self
													   selector:@selector(CheckForStaleDownloads:)
													   userInfo:nil
													   repeats:YES]
													  retain];
				_ForegroundStaleDownloadCheckTimer.tolerance = _CheckForForegroundStaleDownloadsWithInterval * 0.5;
				[_ForegroundStaleDownloadCheckTimer fire];
				
				UE_DNLD_LOG(@"Start checking for stale downloads");
			}
		});
	}
}

- (void)StopCheckingForStaleDownloads
{
	if (_ForegroundStaleDownloadCheckTimer != nil)
	{
		dispatch_async(dispatch_get_main_queue(), ^
		{
			if (_ForegroundStaleDownloadCheckTimer != nil)
			{
				[_ForegroundStaleDownloadCheckTimer invalidate];
				[_ForegroundStaleDownloadCheckTimer release];
				_ForegroundStaleDownloadCheckTimer = nil;
				
				UE_DNLD_LOG(@"Stop checking for stale downloads");
			}
		});
	}
}

- (void)CheckForStaleDownloads:(NSTimer*)Timer
{
	// Only check for stale downloads in foreground
	if ([UIApplication sharedApplication].applicationState != UIApplicationStateActive)
	{
		return;
	}

	// Copy keys to avoid deadlocking in case if cancel/resume/etc will call delegates in-place
	NSArray<__kindof NSNumber*>* AllKeys = nil;
	@synchronized(_AllDownloads)
	{
		AllKeys = [_AllDownloads allKeys];
	}

	const NSTimeInterval CurrentTime = [[NSDate date] timeIntervalSince1970];

	for (NSNumber* IterKey in AllKeys)
	{
		const NSUInteger DownloadId = IterKey.unsignedIntegerValue;
		NSURLSessionDownloadTask* Task = nil;
		@synchronized(_AllDownloads)
		{
			Task = [_AllDownloads objectForKey:IterKey];
		}
		if (Task == nil)
		{
			continue;
		}

		NSNumber* ResultStatusCode = [Task.progress.userInfo objectForKey:NSProgressDownloadResultStatusCode];
		if (ResultStatusCode != nil)
		{
			// Task is finished downloading, skip.
			continue;
		}

		NSNumber* LastUpdateTimeNumber = [Task.progress.userInfo objectForKey:NSProgressDownloadLastUpdateTime];
		if (LastUpdateTimeNumber == nil)
		{
			// There is no known last update time, skip.
			// As we don't get any notification from background tasks on when they started,
			// we can't distinguish between task pending processing vs task stuck on getting first byte.
			continue;
		}

		const NSTimeInterval TimeSinceLastUpdate = CurrentTime - LastUpdateTimeNumber.doubleValue;
		if (TimeSinceLastUpdate >= _ForegroundStaleDownloadTimeout)
		{
			UE_DNLD_LOG(@"Task '%@' with taskIdentifier %lu is considering stale, retrying", Task.taskDescription, Task.taskIdentifier);

			// Clear last update property to avoid canceling task twice in next tick.
			[Task.progress setUserInfoObject:nil forKey:NSProgressDownloadLastUpdateTime];
			Task = nil;

			[self RecreateDownload:DownloadId ShouldResetRetryCount:false];
		}
	}
}

- (void)HandleDidEnterBackground
{
}

- (void)HandleWillEnterForeground
{
	@synchronized(_AllDownloads)
	{
		[_AllDownloads enumerateKeysAndObjectsUsingBlock:^(NSNumber* Key, NSURLSessionDownloadTask* Task, BOOL* IterStop)
		{
			NSNumber* ResultStatusCode = [Task.progress.userInfo objectForKey:NSProgressDownloadResultStatusCode];

			// If task was not complete
			if (ResultStatusCode == nil)
			{
				NSNumber* LastUpdateTimeNumber = [Task.progress.userInfo objectForKey:NSProgressDownloadLastUpdateTime];

				// But has last update time
				if (LastUpdateTimeNumber != nil)
				{
					// Refresh task update time so stale timer doesn't retry task for first N seconds after app goes to foreground
					[Task.progress setUserInfoObject:[NSNumber numberWithDouble:[[NSDate date] timeIntervalSince1970]] forKey:NSProgressDownloadLastUpdateTime];

					UE_DNLD_LOG(@"Refreshing last update time for task '%@' taskIdentifier %lu", Task.taskDescription, Task.taskIdentifier);
				}
			}
		}];
	}
}

- (NSURLSessionDownloadTask*)FindDownloadTaskFor:(NSUInteger)DownloadId
{
	@synchronized(_AllDownloads)
	{
		return [_AllDownloads objectForKey:[NSNumber numberWithUnsignedInteger:DownloadId]];
	}
}

- (NSUInteger)FindDownloadIdForTask:(NSURLSessionDownloadTask*)Task
{
	// TODO this is slow, optimize if needed.
	__block NSUInteger ExistingDownloadId = InvalidDownloadId;
	@synchronized(_AllDownloads)
	{
		[_AllDownloads enumerateKeysAndObjectsUsingBlock:^(NSNumber* IterKey, NSURLSessionDownloadTask* IterTask, BOOL* IterStop)
		{
			if (IterTask == Task)
			{
				ExistingDownloadId = IterKey.unsignedIntegerValue;
				*IterStop = YES;
			}
		}];
	}

	return ExistingDownloadId;
}

- (NSUInteger)EnsureTaskIsTracked:(NSURLSessionDownloadTask*)Task
{
	const NSUInteger ExistingDownloadId = [self FindDownloadIdForTask:Task];
	if (ExistingDownloadId != InvalidDownloadId)
	{
		return ExistingDownloadId;
	}

	@synchronized(_AllDownloads)
	{
		const NSUInteger DownloadId = _NextDownloadId++;
		[_AllDownloads setObject:Task forKey:[NSNumber numberWithUnsignedInteger:DownloadId]];

		// start timer as long as we have active tasks
		[self StartCheckingForStaleDownloads];

		return DownloadId;
	}
}

- (void)ReplaceTrackedTaskWith:(NSURLSessionDownloadTask*)NewTask ForDownloadId:(NSUInteger)DownloadId
{
	@synchronized(_AllDownloads)
	{
		[_AllDownloads setObject:NewTask forKey:[NSNumber numberWithUnsignedInteger:DownloadId]];
	}
}

- (void)EnsureTaskIsNotTracked:(NSURLSessionDownloadTask*)Task
{
	const NSUInteger ExistingDownloadId = [self FindDownloadIdForTask:Task];
	if (ExistingDownloadId != InvalidDownloadId)
	{
		@synchronized(_AllDownloads)
		{
			[_AllDownloads removeObjectForKey:[NSNumber numberWithUnsignedInteger:ExistingDownloadId]];

			if (_AllDownloads.count == 0)
			{
				// stop timer when we have no ongoing tasks
				[self StopCheckingForStaleDownloads];
			}
		}
	}
}

- (void)SetDownloadResult:(NSInteger)HTTPCode WithTempFile:(NSString*)TempFile ForDownload:(NSURLSessionDownloadTask*)Task
{
	const NSUInteger DownloadId = [self FindDownloadIdForTask:Task];
	if (DownloadId == InvalidDownloadId)
	{
		UE_DNLD_LOG(@"Can't find DownloadId for task '%@'", Task.taskDescription);
		return;
	}

	// We don't necessarily care if these values survive between application restarts.
	// Otherwise we need to put them inside FBackgroundNSURLSessionDownloadTaskData.
	[Task.progress setUserInfoObject:[NSNumber numberWithInteger:HTTPCode] forKey:NSProgressDownloadResultStatusCode];
	[Task.progress setUserInfoObject:TempFile forKey:NSProgressDownloadResultTempFilePath];

	const bool bDownloadSuccess = TempFile != nil;
	FBackgroundURLSessionHandler::OnDownloadCompleted.Broadcast(DownloadId, bDownloadSuccess);
}

- (NSString* _Nullable)GetDownloadResult:(NSUInteger)DownloadId OutStatus:(BOOL* _Nonnull)OutStatus OutStatusCode:(NSInteger*)OutStatusCode
{
	NSURLSessionDownloadTask* Task = [self FindDownloadTaskFor:DownloadId];
	if (Task == nil)
	{
		*OutStatus = NO;
		return nil;
	}

	NSNumber* ResultStatusCode = [Task.progress.userInfo objectForKey:NSProgressDownloadResultStatusCode];
	if (ResultStatusCode == nil)
	{
		*OutStatus = NO;
		return nil;
	}

	*OutStatus = YES;
	*OutStatusCode = ResultStatusCode.integerValue;

	return [Task.progress.userInfo objectForKey:NSProgressDownloadResultTempFilePath];
}

//- (void)URLSession:(NSURLSession*)Session didBecomeInvalidWithError:(NSError*)Error
//{
//	UE_DNLD_LOG(@"didBecomeInvalidWithError");
//}

- (void)URLSessionDidFinishEventsForBackgroundURLSession:(NSURLSession*)Session
{
	UE_DNLD_LOG(@"URLSessionDidFinishEventsForBackgroundURLSession");

	IOSAppDelegate* AppDelegate = [IOSAppDelegate GetDelegate];
	if (AppDelegate == nullptr || AppDelegate.BackgroundSessionEventCompleteDelegate == nil)
	{
		return;
	}
	
	void(^CompletionHandler)() = [AppDelegate.BackgroundSessionEventCompleteDelegate retain];
	AppDelegate.BackgroundSessionEventCompleteDelegate = nil;

	// Completion handler has to be invoked on the main thread.
	[[NSOperationQueue mainQueue] addOperationWithBlock:^
	{
		UE_DNLD_LOG(@"URLSessionDidFinishEventsForBackgroundURLSession calling completion handler.");

		FBackgroundURLSessionHandler::OnDownloadsCompletedWhileAppWasNotRunning.Broadcast(!_bAnyTaskDidCompleteWithError);
		_bAnyTaskDidCompleteWithError = false;

		[self SaveFileHashHelperState];

		CompletionHandler();
		[CompletionHandler release];
	}];
}

- (void)URLSession:(NSURLSession*)Session task:(NSURLSessionTask*)GenericTask didCompleteWithError:(NSError*)Error
{
	if (Error == nil)
	{
		UE_DNLD_LOG(@"didCompleteWithError, task '%@' with taskIdentifier %lu is completed", GenericTask.taskDescription, GenericTask.taskIdentifier);
		return;
	}

	if (![GenericTask isKindOfClass:[NSURLSessionDownloadTask class]])
	{
		UE_DNLD_LOG(@"didCompleteWithError, ignoring task '%@' with taskIdentifier %lu", GenericTask.taskDescription, GenericTask.taskIdentifier);
		return;
	}

	// Set it even if we will retry the download, as the only use of this variable is to report it URLSessionDidFinishEventsForBackgroundURLSession,
	// in that context retrying any download means that all downloads hasn't been completed yet.
	_bAnyTaskDidCompleteWithError = true;

	NSURLSessionDownloadTask* Task = (NSURLSessionDownloadTask*)GenericTask;
	NSString* LocalizedDescription = Error.localizedDescription;

	const NSUInteger DownloadId = [self FindDownloadIdForTask:Task];
	const bool bIsTrackedTask = DownloadId != InvalidDownloadId;

	FBackgroundNSURLSessionDownloadTaskData* TaskData = [FBackgroundNSURLSessionDownloadTaskData TaskDataFromSerializedString:Task.taskDescription];
	if (bIsTrackedTask && TaskData != nil)
	{
		NSData* ResumeData = [Error.userInfo objectForKey:NSURLSessionDownloadTaskResumeData];
		const bool bHasResumeData = ResumeData != nil && ResumeData.length > 0;
		
		NSURL* NextURL = [TaskData GetNextURL];
		
		// Continue trying if next URL is available.
		if (NextURL != nil)
		{
			// Create resume request if our URL is the same and we have resume data.
			if (bHasResumeData && NextURL != nil && [NextURL.absoluteString isEqualToString:Task.originalRequest.URL.absoluteString])
			{
				UE_DNLD_LOG(@"didCompleteWithError, task '%@' with taskIdentifier %lu failed due to '%@' and has resume data and next url is the same, retrying", Task.taskDescription, Task.taskIdentifier, LocalizedDescription);

				NSURLSessionDownloadTask* NewTask = [self CreateDownloadForResumeData:ResumeData WithPriority:Task.priority WithTaskData:TaskData];

				Task = nil;
				[self ReplaceTrackedTaskWith:NewTask ForDownloadId:DownloadId];

				[NewTask resume];
				return;
			}
			else
			{
				if (bHasResumeData)
				{
					// It should be possible to patch resume data to point to a new URL. But there is no public API to do that yet.
					UE_DNLD_LOG(@"didCompleteWithError, task '%@' with taskIdentifier %lu failed due to '%@' and has resume data but next url is different, retrying", Task.taskDescription, Task.taskIdentifier, LocalizedDescription);
				}
				else
				{
					UE_DNLD_LOG(@"didCompleteWithError, task '%@' with taskIdentifier %lu failed due to '%@' and has no resume data or next url is different, retrying", Task.taskDescription, Task.taskIdentifier, LocalizedDescription);
				}

				NSURLSessionDownloadTask* NewTask = [self CreateDownloadForURL:NextURL WithPriority:Task.priority WithTaskData:TaskData];

				Task = nil;
				[self ReplaceTrackedTaskWith:NewTask ForDownloadId:DownloadId];

				[NewTask resume];
				return;
			}
		}
	}

	// Can't retry anymore, fail the request
	{
		UE_DNLD_LOG(@"didCompleteWithError, task '%@' with taskIdentifier %lu failed due to '%@', has no retry data or no next url, failing request", Task.taskDescription, Task.taskIdentifier, LocalizedDescription);

		NSURLResponse* GenericResponse = Task.response;
		NSInteger StatusCode = HTTPStatusCodeErrorServer;
		if ([GenericResponse isKindOfClass:[NSHTTPURLResponse class]])
		{
			NSHTTPURLResponse* Response = (NSHTTPURLResponse*)GenericResponse;
			if (Response.statusCode >= HTTPStatusCodeErrorBadRequest)
			{
				StatusCode = Response.statusCode;
			}
		}

		[self SetDownloadResult:StatusCode WithTempFile:nil ForDownload:Task];
	}
}

//- (void)URLSession:(NSURLSession*)Session taskIsWaitingForConnectivity:(NSURLSessionTask*)Task
//{
//	UE_DNLD_LOG(@"taskIsWaitingForConnectivity '%@'", Task.taskDescription);
//}

//- (void)URLSession:(NSURLSession*)Session task:(NSURLSessionTask*)Task willBeginDelayedRequest:(NSURLRequest*)Request completionHandler:(void (^)(NSURLSessionDelayedRequestDisposition Disposition, NSURLRequest* NewRequest))CompletionHandler
//{
//	UE_DNLD_LOG(@"willBeginDelayedRequest '%@' for '%@' with taskIdentifier %lu", Task.taskDescription, Request.debugDescription, Task.taskIdentifier);
//	CompletionHandler(NSURLSessionDelayedRequestContinueLoading, Request);
//}

- (void)URLSession:(NSURLSession*)Session downloadTask:(NSURLSessionDownloadTask*)Task didFinishDownloadingToURL:(NSURL*)Location
{
	// Should not be needed, but ensure this just in case
	[self EnsureTaskIsTracked:Task];

	NSString* DestinationPath = [self GetTempPathForURL:Task.originalRequest.URL];

	[self SaveFileHashHelperState];

	// Try to remove existing file in case if we have a stale file.
	if ([[NSFileManager defaultManager] fileExistsAtPath:DestinationPath])
	{
		[[NSFileManager defaultManager] removeItemAtPath:DestinationPath error:nil];
	}

	NSError* Error = nil;
	[[NSFileManager defaultManager] moveItemAtURL:Location toURL:[NSURL fileURLWithPath:DestinationPath] error:&Error];

	// Update task progress just in case didWriteData was not invoked
	const uint64 TotalBytesWritten = [[[NSFileManager defaultManager] attributesOfItemAtPath:DestinationPath error:nil] fileSize];
	[self SetCurrentDownloadedBytes:TotalBytesWritten ForTask:Task];

	if (Error != nil)
	{
		[self SetDownloadResult:HTTPStatusCodeErrorServer WithTempFile:nil ForDownload:Task];

		UE_DNLD_LOG(@"didFinishDownloadingToURL task '%@' with taskIdentifier %lu failed to move file to '%@' due to '%@'", Task.taskDescription, Task.taskIdentifier, DestinationPath, Error.localizedDescription);
	}
	else
	{
		[self SetDownloadResult:HTTPStatusCodeSuccessCreated WithTempFile:DestinationPath ForDownload:Task];

		UE_DNLD_LOG(@"didFinishDownloadingToURL task '%@' with taskIdentifier %lu move file to '%@', download finished", Task.taskDescription, Task.taskIdentifier, DestinationPath);
	}
}

- (void)URLSession:(NSURLSession*)Session downloadTask:(NSURLSessionDownloadTask*)Task didWriteData:(int64_t)BytesWritten totalBytesWritten:(int64_t)TotalBytesWritten totalBytesExpectedToWrite:(int64_t)TotalBytesExpectedToWrite
{
	//UE_DNLD_LOG(@"didWriteData taskIdentifier %lu wrote %lli", Task.taskIdentifier, TotalBytesWritten);
	[self SetCurrentDownloadedBytes:TotalBytesWritten ForTask:Task];
}

//- (void)URLSession:(NSURLSession*)Session downloadTask:(NSURLSessionDownloadTask*)Task didResumeAtOffset:(int64_t)FileOffset expectedTotalBytes:(int64_t)ExpectedTotalBytes
//{
//	UE_DNLD_LOG(@"didResumeAtOffset taskIdentifier %lu offset %lli total %lli", Task.taskIdentifier, FileOffset, ExpectedTotalBytes);
//}

@end

const uint64 FBackgroundURLSessionHandler::InvalidDownloadId = [FBackgroundNSURLSession GetInvalidDownloadId];

FBackgroundURLSessionHandler::FOnDownloadCompleted FBackgroundURLSessionHandler::OnDownloadCompleted;
FBackgroundURLSessionHandler::FOnDownloadMetrics FBackgroundURLSessionHandler::OnDownloadMetrics;

FBackgroundURLSessionHandler::FOnDownloadsCompletedWhileAppWasNotRunning FBackgroundURLSessionHandler::OnDownloadsCompletedWhileAppWasNotRunning;

void FBackgroundURLSessionHandler::AllowCellular(bool bAllow)
{
	@autoreleasepool
	{
		const BOOL bCurrentValue = [FBackgroundNSURLSession Shared].AllowCellular;
		const BOOL bNewValue = bAllow ? YES : NO;
		if (bCurrentValue == bNewValue)
		{
			return;
		}

		[[FBackgroundNSURLSession Shared] setAllowCellular:bNewValue];
		[[FBackgroundNSURLSession Shared] RecreateDownloads];
	}
}

uint64 FBackgroundURLSessionHandler::CreateOrFindDownload(const TArray<FString>& URLs, const float Priority, BackgroundHttpFileHashHelperRef HelperRef)
{
	@autoreleasepool
	{
		NSMutableArray* URLArray = [NSMutableArray arrayWithCapacity:URLs.Num()];
		for (const FString& URL: URLs)
		{
			[URLArray addObject:URL.GetNSString()];
		}

		[[FBackgroundNSURLSession Shared] SetFileHashHelper:HelperRef];
		return [[FBackgroundNSURLSession Shared] CreateOrFindDownloadForURLs:URLArray WithPriority:Priority];
	}
}

void FBackgroundURLSessionHandler::PauseDownload(const uint64 DownloadId)
{
	@autoreleasepool
	{
		[[FBackgroundNSURLSession Shared] PauseDownload:DownloadId];
	}
}

void FBackgroundURLSessionHandler::ResumeDownload(const uint64 DownloadId)
{
	@autoreleasepool
	{
		[[FBackgroundNSURLSession Shared] ResumeDownload:DownloadId];
	}
}

void FBackgroundURLSessionHandler::CancelDownload(const uint64 DownloadId)
{
	@autoreleasepool
	{
		[[FBackgroundNSURLSession Shared] CancelDownload:DownloadId];
	}
}

void FBackgroundURLSessionHandler::SetPriority(const uint64 DownloadId, const float Priority)
{
	@autoreleasepool
	{
		[[FBackgroundNSURLSession Shared] SetPriority:Priority ForDownload:DownloadId];
	}
}

uint64 FBackgroundURLSessionHandler::GetCurrentDownloadedBytes(const uint64 DownloadId)
{
	@autoreleasepool
	{
		return [[FBackgroundNSURLSession Shared] GetCurrentDownloadedBytes:DownloadId];
	}
}

bool FBackgroundURLSessionHandler::IsDownloadFinished(const uint64 DownloadId, int32& OutResultHTTPCode, FString& OutTemporaryFilePath)
{
	@autoreleasepool
	{
		BOOL Status = NO;
		NSInteger StatusCode = 0;
		NSString* TempFile = [[FBackgroundNSURLSession Shared] GetDownloadResult:DownloadId OutStatus:&Status OutStatusCode:&StatusCode];
		if (!Status)
		{
			return false;
		}
		
		OutResultHTTPCode = (int32)StatusCode;
		if (TempFile != nil)
		{
			OutTemporaryFilePath = FString(TempFile);
			UE_DNLD_LOG(@"DownloadId %lli finished with status code %li and path '%@'", DownloadId, (long)StatusCode, TempFile);
		}
		else
		{
			UE_DNLD_LOG(@"DownloadId %lli finished with status code %li and no path", DownloadId, (long)StatusCode);
		}
		
		return true;
	}
}

void FBackgroundURLSessionHandler::HandleEventsForBackgroundURLSession(const FString& SessionIdentifier)
{
	@autoreleasepool
	{
		NSString* Identifier = SessionIdentifier.GetNSString();
		if (![[FBackgroundNSURLSession GetNSURLSessionIdentifier] isEqualToString:Identifier])
		{
			UE_DNLD_LOG(@"HandleEventsForBackgroundURLSession ignoring session identifier '%@'", Identifier);
			return;
		}
		
		UE_DNLD_LOG(@"HandleEventsForBackgroundURLSession will initializes session with identifier '%@'", Identifier);
		[FBackgroundNSURLSession Shared];
		// will invoke URLSessionDidFinishEventsForBackgroundURLSession internally.
	}
}

void FBackgroundURLSessionHandler::HandleDidEnterBackground()
{
	@autoreleasepool
	{
		[[FBackgroundNSURLSession Shared] HandleDidEnterBackground];
	}
}

void FBackgroundURLSessionHandler::HandleWillEnterForeground()
{
	@autoreleasepool
	{
		[[FBackgroundNSURLSession Shared] HandleWillEnterForeground];
	}
}

void FBackgroundURLSessionHandler::SaveBackgroundHttpFileHashHelperState()
{
	@autoreleasepool
	{
		[[FBackgroundNSURLSession Shared] SaveFileHashHelperState];
	}
}

TArray<FString> FBackgroundURLSessionHandler::GetCDNOrderArray()
{
	TArray<FString> Result;

	@autoreleasepool
	{
		NSArray<__kindof FBackgroundNSURLCDNInfo*>* CDNInfo = [[FBackgroundNSURLSession Shared] GetCDNInfo];

		for (FBackgroundNSURLCDNInfo* Info in CDNInfo)
		{
			Result.Add(FString(Info.CDNAbsoluteURL));
		}
	}

	return Result;
}

#if !UE_BUILD_SHIPPING
void FBackgroundURLSessionHandler::GetDownloadDebugText(const uint64 DownloadId, TArray<FString>& Output)
{
	@autoreleasepool
	{
		const FString DebugText([[FBackgroundNSURLSession Shared] GetDownloadDebugText:DownloadId]);
		Output.Add(DebugText);
	}
}
#endif
