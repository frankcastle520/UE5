// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HttpManager.h"
#include "HttpRetrySystem.h"
#include "Http.h"
#include "HttpPath.h"
#include "HttpRouteHandle.h"
#include "IHttpRouter.h"
#include "HttpServerModule.h"
#include "Misc/CommandLine.h"
#include "TestHarness.h"
#include "Serialization/JsonSerializerMacros.h"
#include "catch2/generators/catch_generators.hpp"

/**
 *  HTTP Tests
 *  -----------------------------------------------------------------------------------------------
 *
 *  PURPOSE:
 *
 *	Integration Tests to make sure all kinds of HTTP client features in C++ work well on different platforms,
 *  including but not limited to error handing, retrying, threading, streaming, SSL and profiling.
 *
 *  Refer to WebTests/README.md for more info about how to run these tests
 * 
 *  -----------------------------------------------------------------------------------------------
 */

#define HTTP_TAG "[HTTP]"
#define HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST 0.5f
#define HTTP_TEST_TIMEOUT_CHUNK_SIZE 16*1024 // Use a big chunk size so it triggers data received callback in time on all platforms

extern TAutoConsoleVariable<bool> CVarHttpInsecureProtocolEnabled;
extern TAutoConsoleVariable<bool> CVarHttpRetrySystemNonGameThreadSupportEnabled;
extern TAutoConsoleVariable<int32> CVarHttpMaxConcurrentRequests;
extern TAutoConsoleVariable<FString> CVarHttpUrlPatternsToMockFailure;

class FMockHttpModule : public FHttpModule
{
public:
	using FHttpModule::HttpConnectionTimeout;
	using FHttpModule::HttpTotalTimeout;
	using FHttpModule::HttpActivityTimeout;
};

class FHttpTestLogLevelInitializer
{
public:
	FHttpTestLogLevelInitializer()
		: OldVerbosity(LogHttp.GetVerbosity())
	{
		FParse::Bool(FCommandLine::Get(), TEXT("very_verbose="), bVeryVerbose);
		if (bVeryVerbose)
		{
			LogHttp.SetVerbosity(ELogVerbosity::VeryVerbose);
		}
	}

	~FHttpTestLogLevelInitializer()
	{
		ResumeLogVerbosity();
	}

	void DisableWarningsInThisTest()
	{
		if (!bVeryVerbose)
		{
			LogHttp.SetVerbosity(ELogVerbosity::Error);
		}
	}

	void ResumeLogVerbosity()
	{
		if (OldVerbosity != LogHttp.GetVerbosity())
		{
			LogHttp.SetVerbosity(OldVerbosity);
		}
	}

	bool bVeryVerbose = false;
	ELogVerbosity::Type OldVerbosity;
};

class FMockRetryManager : public FHttpRetrySystem::FManager
{
public:
	using FHttpRetrySystem::FManager::FManager;
	using FHttpRetrySystem::FManager::RequestList;
	using FHttpRetrySystem::FManager::FHttpRetryRequestEntry;
	using FHttpRetrySystem::FManager::RetryTimeoutRelativeSecondsDefault;
	using FHttpRetrySystem::FManager::RetryLimitCountDefault;
	using FHttpRetrySystem::FManager::RetryLimitCountForConnectionErrorDefault;

	bool IsEmpty()
	{
		FScopeLock ScopeLock(&RequestListLock);
		return RequestList.IsEmpty();
	}
};

class FHttpModuleTestFixture
{
public:
	FHttpModuleTestFixture()
		: WebServerIp(TEXT("127.0.0.1"))
		, WebServerHttpPort(8000)
		, bRunHeavyTests(false)
		, bRetryEnabled(true)
	{
		ParseSettingsFromCommandLine();

		bRetryEnabled &= CVarHttpRetrySystemNonGameThreadSupportEnabled.GetValueOnAnyThread();

		InitModule();

		CVarHttpInsecureProtocolEnabled->Set(true);
	}

	void InitModule()
	{
		HttpModule = new FMockHttpModule();
		IModuleInterface* Module = HttpModule;
		Module->StartupModule();
		if (bRetryEnabled)
		{
			HttpRetryManager = MakeShared<FMockRetryManager>(FHttpRetrySystem::FRetryLimitCountSetting(0), FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting(/*RetryTimeoutRelativeSeconds*/));
		}
	}

	void ShutdownModule()
	{
		HttpRetryManager = nullptr;

		IModuleInterface* Module = HttpModule;
		if (Module)
		{
			Module->ShutdownModule();
			delete Module;
			HttpModule = nullptr;
		}
	}

	virtual ~FHttpModuleTestFixture()
	{
		ShutdownModule();
	}

	void ParseSettingsFromCommandLine()
	{
		FParse::Value(FCommandLine::Get(), TEXT("web_server_ip="), WebServerIp);
		FParse::Bool(FCommandLine::Get(), TEXT("run_heavy_tests="), bRunHeavyTests);
		FParse::Bool(FCommandLine::Get(), TEXT("retry_enabled="), bRetryEnabled);
		FParse::Value(FCommandLine::Get(), TEXT("web_server_unix_socket="), WebServerUnixSocket);
	}

	void DisableWarningsInThisTest()
	{
		HttpTestLogLevelInitializer.DisableWarningsInThisTest();
	}

	void ResumeLogVerbosity()
	{
		HttpTestLogLevelInitializer.ResumeLogVerbosity();
	}

	TSharedRef<IHttpRequest> CreateRequest()
	{
		return bRetryEnabled ? HttpRetryManager->CreateRequest() : HttpModule->CreateRequest();
	}

	const FString UrlWithInvalidPortToTestConnectTimeout() const { return TEXT("http://10.255.255.1:8765"); } // non-routable IP address with a random port
	const FString UrlBase() const { return FString::Format(TEXT("http://{0}:{1}"), { *WebServerIp, WebServerHttpPort }); }
	const FString UrlHttpTests() const { return FString::Format(TEXT("{0}/webtests/httptests"), { *UrlBase() }); }
	const FString UrlToTestMethods() const { return FString::Format(TEXT("{0}/methods"), { *UrlHttpTests() }); }
	const FString UrlStreamDownload(uint32 Chunks, uint32 ChunkSize, uint32 ChunkLatency=0) { return FString::Format(TEXT("{0}/streaming_download/{1}/{2}/{3}/"), { *UrlHttpTests(), Chunks, ChunkSize, ChunkLatency }); }
	const FString UrlStreamUpload() { return FString::Format(TEXT("{0}/streaming_upload_put"), { *UrlHttpTests() }); }
	const FString UrlMockLatency(uint32 Latency) const { return FString::Format(TEXT("{0}/mock_latency/{1}/"), { *UrlHttpTests(), Latency }); }
	const FString UrlMockStatus(uint32 StatusCode) const { return FString::Format(TEXT("{0}/mock_status/{1}/"), { *UrlHttpTests(), StatusCode }); }
	const FString UrlUnixSocketHttpTests() const { return "http://localhost/webtests/unixsockettests"; }

	FString WebServerIp;
	FString WebServerUnixSocket;
	uint32 WebServerHttpPort;
	FMockHttpModule* HttpModule = nullptr;
	bool bRunHeavyTests;
	bool bRetryEnabled;
	FHttpTestLogLevelInitializer HttpTestLogLevelInitializer;
	TSharedPtr<FMockRetryManager> HttpRetryManager;
};

TEST_CASE_METHOD(FHttpModuleTestFixture, "Shutdown http module without issue when there are ongoing upload http requests.", HTTP_TAG)
{
	DisableWarningsInThisTest();

	uint32 ChunkSize = 1024 * 1024;
	TArray<uint8> DataChunk;
	DataChunk.SetNum(ChunkSize);
	FMemory::Memset(DataChunk.GetData(), 'd', ChunkSize);

	for (int32 i = 0; i < 10; ++i)
	{
		IHttpRequest* LeakingHttpRequest = FPlatformHttp::ConstructRequest(); // Leaking in purpose to make sure it's ok

		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlToTestMethods());
		HttpRequest->SetVerb(TEXT("PUT"));
		// TODO: Use some shared data, like cookie, openssl session etc.
		HttpRequest->SetContent(DataChunk);
		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
		});
		HttpRequest->ProcessRequest();
	}

	HttpModule->GetHttpManager().Tick(0.0f);
}

TEST_CASE_METHOD(FHttpModuleTestFixture, "Shutdown http module without issue when there are ongoing streaming http requests with timeout.", HTTP_TAG)
{
	if (!bRunHeavyTests)
	{
		return;
	}

	// When use generator, it doesn't do the ctor and dtor of FHttpModuleTestFixture each time, so manually 
	// shutdown and init here to shutdown module a lot of times
	ShutdownModule();
	InitModule();

	int32 NumRequests = GENERATE(
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
		21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
		31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
		41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
		51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
		61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
		71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
		81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
		91, 92, 93, 94, 95, 96, 97, 98, 99, 100
	);

	//Output NumRequests when error occurs.
	UNSCOPED_INFO(NumRequests);
	HttpModule->HttpTotalTimeout = 2.0f;
	HttpModule->HttpActivityTimeout = 1.0f;

	DYNAMIC_SECTION(" making " << NumRequests << " requests")
	{
		DisableWarningsInThisTest();

		uint32 ChunkSize = 1024 * 1024;
		TArray<uint8> DataChunk;
		DataChunk.SetNum(ChunkSize);
		FMemory::Memset(DataChunk.GetData(), 'd', ChunkSize);

		for (int32 i = 0; i < NumRequests; ++i)
		{
			{
				TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
				HttpRequest->SetURL(UrlToTestMethods());
				HttpRequest->SetVerb(TEXT("PUT"));
				HttpRequest->SetContent(DataChunk);
				HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
					CHECK(bSucceeded);
					});
				HttpRequest->ProcessRequest();
			}

			{
				TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
				HttpRequest->SetURL(UrlStreamDownload(2, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2));
				HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
					CHECK(bSucceeded);
				});
				HttpRequest->ProcessRequest();
			}
		}

		HttpModule->GetHttpManager().Tick(0.0f);
	}

	ShutdownModule();
}

class FWaitUntilCompleteHttpFixture : public FHttpModuleTestFixture
{
public:
	FWaitUntilCompleteHttpFixture()
	{
		HttpModule->GetHttpManager().SetRequestAddedDelegate(FHttpManagerRequestAddedDelegate::CreateRaw(this, &FWaitUntilCompleteHttpFixture::OnRequestAdded));
		HttpModule->GetHttpManager().SetRequestCompletedDelegate(FHttpManagerRequestCompletedDelegate::CreateRaw(this, &FWaitUntilCompleteHttpFixture::OnRequestCompleted));
	}

	~FWaitUntilCompleteHttpFixture()
	{
		WaitUntilAllHttpRequestsComplete();

		CHECK(ExpectingExtraCallbacks == 0);

		HttpModule->GetHttpManager().SetRequestAddedDelegate(FHttpManagerRequestAddedDelegate());
		HttpModule->GetHttpManager().SetRequestCompletedDelegate(FHttpManagerRequestCompletedDelegate());
	}

	void OnRequestAdded(const FHttpRequestRef& Request)
	{
		++OngoingRequests;
	}

	void OnRequestCompleted(const FHttpRequestRef& Request)
	{
		ensure(--OngoingRequests >= 0);
	}

	void TickHttpManager()
	{
		double Now = FPlatformTime::Seconds();
		static double LastTick = Now;
		double Duration = Now - LastTick;
		LastTick = Now;
		HttpModule->GetHttpManager().Tick(Duration);
		FPlatformProcess::Sleep(TickFrequency);
	}

	void WaitUntilAllHttpRequestsComplete()
	{
		while (HasOngoingRequest())
		{
			TickHttpManager();
		}

		// In case in http thread the http request complete and set OngoingRequests to 0, http manager never 
		// had chance to Tick and remove the request
		TickHttpManager();
	}

	bool HasOngoingRequest() const
	{
		return OngoingRequests != 0 || (bRetryEnabled && !HttpRetryManager->IsEmpty());
	}

	std::atomic<int32> OngoingRequests = 0;
	float TickFrequency = 1.0f / 60; /*60 FPS*/;

	uint32 RetryLimitCount = 0;
	uint32 ExpectingExtraCallbacks = 0;
};

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http Methods", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	CHECK(HttpRequest->GetVerb() == TEXT("GET"));

	HttpRequest->SetURL(UrlToTestMethods());

	SECTION("Default GET")
	{
	}
	SECTION("GET")
	{
		HttpRequest->SetVerb(TEXT("GET"));
	}
	SECTION("POST")
	{
		HttpRequest->SetVerb(TEXT("POST"));
	}
	SECTION("PUT")
	{
		HttpRequest->SetVerb(TEXT("PUT"));
	}
	SECTION("DELETE")
	{
		HttpRequest->SetVerb(TEXT("DELETE"));
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can process https request", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(TEXT("https://www.unrealengine.com/"));
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can mock connection error through CVar", HTTP_TAG)
{
	CVarHttpUrlPatternsToMockFailure->Set(TEXT("epicgames.com->0 unrealengine.com->503"));

	float ExpectedTimeoutDuration = 2.0f;
	HttpModule->HttpConnectionTimeout = ExpectedTimeoutDuration;
	const double StartTime = FPlatformTime::Seconds();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(TEXT("https://www.epicgames.com/"));
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ExpectedTimeoutDuration](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(!HttpResponse);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, ExpectedTimeoutDuration, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can mock response failure through CVar", HTTP_TAG)
{
	CVarHttpUrlPatternsToMockFailure->Set(TEXT("epicgames.com->0 unrealengine.com->503"));

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(TEXT("https://www.unrealengine.com/"));
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 503);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can complete successfully for different response codes", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));

	int32 ExpectedStatusCode = 0;
	SECTION("For status 200")
	{
		ExpectedStatusCode = 200;
	}
	SECTION("For status 206")
	{
		ExpectedStatusCode = 206;
	}
	SECTION("For status 400")
	{
		ExpectedStatusCode = 400;
	}

	HttpRequest->SetURL(UrlMockStatus(ExpectedStatusCode));

	HttpRequest->OnProcessRequestComplete().BindLambda([ExpectedStatusCode](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == ExpectedStatusCode);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can do blocking call", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->ProcessRequestUntilComplete();
	CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Succeeded);
	FHttpResponsePtr HttpResponse = HttpRequest->GetResponse();
	REQUIRE(HttpResponse != nullptr);
	CHECK(HttpResponse->GetResponseCode() == 200);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Get large response content without chunks", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	uint32 DataLength = 0;
	uint32 RepeatAt = 0;
	SECTION("case A")
	{
		DataLength = 1024 * 1024;
		RepeatAt = 10;
	}
	SECTION("cast B")
	{
		DataLength = 1025 * 1023;
		RepeatAt = 9;
	}
	HttpRequest->SetURL(FString::Format(TEXT("{0}/get_data_without_chunks/{1}/{2}/"), { *UrlHttpTests(), DataLength, RepeatAt}));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([DataLength, RepeatAt](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);

		const TArray<uint8>& Content = HttpResponse->GetContent();
		CHECK(Content.Num() == DataLength);

		bool bAllMatch = true;

		// Make sure the data got read is in good state
		for (int32 i = 0; i < Content.Num(); ++i)
		{
			bAllMatch &= (Content[i] == '0' + (i % RepeatAt));
		}

		CHECK(bAllMatch);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request connect timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	HttpModule->HttpActivityTimeout = 3.0f; // Make sure this won't be triggered before establishing connection
	float ExpectedTimeoutDuration = 15.0f;
	HttpModule->HttpConnectionTimeout = ExpectedTimeoutDuration;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();

	HttpRequest->SetURL(UrlWithInvalidPortToTestConnectTimeout());
	HttpRequest->SetVerb(TEXT("GET"));

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ExpectedTimeoutDuration](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpResponse == nullptr);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, ExpectedTimeoutDuration, UE_HTTP_CONNECTION_TIMEOUT_MAX_DEVIATION));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http download", HTTP_TAG)
{
	uint32 Chunks = 3;
	uint32 ChunkSize = 1024*1024;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
	HttpRequest->SetVerb(TEXT("GET"));

	TSharedRef<int64> TotalBytesReceived = MakeShared<int64>(0);

	SECTION("Success without stream provided")
	{
		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
		});
	}
	SECTION("Success with customized stream")
	{
		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			FTestHttpReceiveStream(TSharedRef<int64> InTotalBytesReceived)
				: TotalBytesReceived(InTotalBytesReceived)
			{
			}

			virtual void Serialize(void* V, int64 Length) override
			{
				*TotalBytesReceived += Length;
			}

			TSharedRef<int64> TotalBytesReceived;
		};

		TSharedRef<FTestHttpReceiveStream> Stream = MakeShared<FTestHttpReceiveStream>(TotalBytesReceived);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(Stream));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(*TotalBytesReceived == Chunks * ChunkSize);
		});
	}
	SECTION("Success with customized stream delegate")
	{
		FHttpRequestStreamDelegateV2 Delegate;
		Delegate.BindLambda([TotalBytesReceived](void* Ptr, int64& Length) {
			*TotalBytesReceived += Length;
		});
		CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(*TotalBytesReceived == Chunks * ChunkSize);
		});
	}
	SECTION("Use customized stream to receive response body but failed when serialize")
	{
		DisableWarningsInThisTest();

		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			FTestHttpReceiveStream(TSharedRef<int64> InTotalBytesReceived)
				: TotalBytesReceived(InTotalBytesReceived)
			{
			}

			virtual void Serialize(void* V, int64 Length) override
			{
				*TotalBytesReceived += Length;
				SetError();
			}

			TSharedRef<int64> TotalBytesReceived;
		};

		TSharedRef<FTestHttpReceiveStream> Stream = MakeShared<FTestHttpReceiveStream>(TotalBytesReceived);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(Stream));

		HttpRequest->OnProcessRequestComplete().BindLambda([ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!bSucceeded);
			CHECK(HttpResponse != nullptr);
			CHECK(*TotalBytesReceived <= ChunkSize);
		});
	}
	SECTION("Use customized stream delegate to receive response body but failed when call")
	{
		DisableWarningsInThisTest();

		FHttpRequestStreamDelegateV2 Delegate;
		Delegate.BindLambda([TotalBytesReceived](void* Ptr, int64& Length) {
			*TotalBytesReceived += Length;
			Length = 0; // Mark as no data was serialized successfully
		});
		CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));

		HttpRequest->OnProcessRequestComplete().BindLambda([ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!bSucceeded);
			CHECK(HttpResponse != nullptr);
			CHECK(*TotalBytesReceived <= ChunkSize);
		});
	}
	SECTION("Success with file stream to receive response body")
	{
		FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamDownload.dat");
		FArchive* RawFile = IFileManager::Get().CreateFileWriter(*Filename);
		CHECK(RawFile != nullptr);
		TSharedRef<FArchive> FileToWrite = MakeShareable(RawFile);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(FileToWrite));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, Filename, FileToWrite](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());

			FileToWrite->FlushCache();
			FileToWrite->Close();

			TSharedRef<FArchive> FileToRead = MakeShareable(IFileManager::Get().CreateFileReader(*Filename));
			CHECK(FileToRead->TotalSize() == Chunks * ChunkSize);
			FileToRead->Close();

			IFileManager::Get().Delete(*Filename);
		});
	}

	HttpRequest->ProcessRequest();
}

// This user streaming class is supposed to be used to receive streaming data through function OnReceivedData 
// and it's not supposed to be called once destroyed
class FUserStreamingClass
{
public:
	FUserStreamingClass()
		: TotalBytesReceived(new int64(0))
	{
	}

	~FUserStreamingClass()
	{
		delete TotalBytesReceived;
		TotalBytesReceived = nullptr;
	}

	void OnReceivedData(void* Ptr, int64& Length)
	{
		*TotalBytesReceived += Length;
	}

	int64* TotalBytesReceived;
};

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "In streaming downloading http request won't trigger response body receive delegate after canceling", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(60, 1024*1024));

	TSharedPtr<FUserStreamingClass> UserInstance = MakeShared<FUserStreamingClass>();

	FHttpRequestStreamDelegateV2 Delegate;
	Delegate.BindThreadSafeSP(UserInstance.ToSharedRef(), &FUserStreamingClass::OnReceivedData);
	CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpResponse != nullptr);
	});
	HttpRequest->ProcessRequest();

	while (*UserInstance->TotalBytesReceived == 0) // Make sure it started receiving data
	{
		FPlatformProcess::Sleep(0.001f);
	}
	CHECK(*UserInstance->TotalBytesReceived < 60 * 1024 * 1024);
	CHECK(UserInstance.GetSharedReferenceCount() == 1);
	HttpRequest->CancelRequest();
	UserInstance.Reset();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "In streaming downloading http request won't crash if shared ptr bound to delegate got destroyed", HTTP_TAG)
{
	DisableWarningsInThisTest(); // Failed writing received data to disk/application

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(60, 1024*1024));

	TSharedPtr<FUserStreamingClass> UserInstance = MakeShared<FUserStreamingClass>();

	FHttpRequestStreamDelegateV2 Delegate;
	Delegate.BindThreadSafeSP(UserInstance.ToSharedRef(), &FUserStreamingClass::OnReceivedData);
	CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));
	HttpRequest->ProcessRequest();

	while (*UserInstance->TotalBytesReceived == 0) // Make sure it started receiving data
	{
		FPlatformProcess::Sleep(0.001f);
	}
	CHECK(*UserInstance->TotalBytesReceived < 60 * 1024 * 1024);
	CHECK(UserInstance.GetSharedReferenceCount() == 1);
	UserInstance.Reset();
}

class FInvalidateDelegateShutdownFixture : public FHttpModuleTestFixture
{
public:
	FInvalidateDelegateShutdownFixture()
	{
		UserStreamingInstance = MakeShared<FUserStreamingClass>();
	}

	TSharedPtr<FUserStreamingClass> UserStreamingInstance;
};

TEST_CASE_METHOD(FInvalidateDelegateShutdownFixture, "Shutdown http module without issue when there are ongoing download http requests", HTTP_TAG)
{
	DisableWarningsInThisTest();

	for (int32 i = 0; i < 10; ++i)
	{
		TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(10, 1024*1024));
		FHttpRequestStreamDelegateV2 Delegate;
		Delegate.BindThreadSafeSP(UserStreamingInstance.ToSharedRef(), &FUserStreamingClass::OnReceivedData);
		CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate));

		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
		});
		HttpRequest->ProcessRequest();
	}

	while (*UserStreamingInstance->TotalBytesReceived == 0) // Make sure it started receiving data
	{
		FPlatformProcess::Sleep(0.001f);
	}

	HttpModule->GetHttpManager().Tick(0.1f);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can run parallel stream download requests", HTTP_TAG)
{
	uint32 Chunks = 5;
	uint32 ChunkSize = 1024*1024;

	for (int i = 0; i < 3; ++i)
	{
		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(bSucceeded);
			CHECK(HttpResponse->GetResponseCode() == 200);
		});
		HttpRequest->ProcessRequest();
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can download big file exceeds 32 bits", HTTP_TAG)
{
	if (!bRunHeavyTests)
	{
		return;
	}

	// 5 * 1024 * 1024 * 1024 BYTES = 5368709120 BYTES = 5 GB
	uint64 Chunks = 5 * 1024;
	uint64 ChunkSize = 1024 * 1024;

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
	HttpRequest->SetVerb(TEXT("GET"));

	TSharedRef<int64> TotalBytesReceived = MakeShared<int64>(0);
	FHttpRequestStreamDelegateV2 Delegate;
	Delegate.BindLambda([TotalBytesReceived](void* Ptr, int64& Length) {
		*TotalBytesReceived += Length;
	});
	HttpRequest->SetResponseBodyReceiveStreamDelegateV2(Delegate);

	HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
		CHECK(HttpResponse->GetContent().IsEmpty());
		CHECK(*TotalBytesReceived == Chunks * ChunkSize);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http upload from memory", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(FString::Format(TEXT("{0}/streaming_upload_post"), { *UrlHttpTests() }));
	HttpRequest->SetVerb(TEXT("POST"));

	const char* BoundaryLabel = "test_http_boundary";
	HttpRequest->SetHeader(TEXT("Content-Type"), FString::Format(TEXT("multipart/form-data; boundary={0}"), { BoundaryLabel }));

	// Data will be sent by chunks in http request
	const uint32 FileSize = 10*1024*1024;
	char* FileData = (char*)FMemory::Malloc(FileSize + 1);
	FMemory::Memset(FileData, 'd', FileSize);
	FileData[FileSize + 1] = '\0';

	TArray<uint8> ContentData;
	const int32 ContentMaxSize = FileSize + 256/*max length of format string*/;
	ContentData.Reserve(ContentMaxSize);
	const int32 ContentLength = FCStringAnsi::Snprintf(
		(char*)ContentData.GetData(),
		ContentMaxSize,
		"--%s\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"bigfile.zip\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n"
		"%s\r\n"
		"--%s--",
		BoundaryLabel, FileData, BoundaryLabel);

	FMemory::Free(FileData);

	CHECK(ContentLength > 0);
	CHECK(ContentLength < ContentMaxSize);
	ContentData.SetNumUninitialized(ContentLength);
	HttpRequest->SetContent(MoveTemp(ContentData));

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

class FTestHttpUploadStream final : public FArchive
{
public:
	FTestHttpUploadStream(uint64 InTotalSize)
		: FakeTotalSize(InTotalSize)
	{
	}

	virtual void Serialize(void* V, int64 Length) override
	{
		for (int64 i = 0; i < Length; ++i)
		{
			((char*)V)[i] = 'd';
		}

		CurrentPos += Length;
	}

	virtual int64 TotalSize() override
	{
		return FakeTotalSize;
	}

	virtual void Seek(int64 InPos) 
	{ 
		CurrentPos = InPos;
	}

	virtual int64 Tell() override
	{
		return CurrentPos;
	}

	uint64 FakeTotalSize;
	uint64 CurrentPos = 0;
};

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can upload big file exceeds 32 bits", HTTP_TAG)
{
	if (!bRunHeavyTests)
	{
		return;
	}

	// TODO: Back to check later. xCurl 2206.4.0.0 doesn't work with file bigger than 32 bits
	// 5 * 1024 * 1024 * 1024 BYTES = 5368709120 BYTES = 5 GB
	//const uint64 TotalSize = 5368709120;
	//const uint64 TotalSize = 4294967296;
	//const uint64 TotalSize = 4294967295;
	//const uint64 TotalSize = 2147483648;
	const uint64 TotalSize = 2147483647;
	TSharedRef<FTestHttpUploadStream> Stream = MakeShared<FTestHttpUploadStream>(TotalSize);

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamUpload());
	HttpRequest->SetVerb(TEXT("PUT"));
	HttpRequest->SetContentFromStream(Stream);
	HttpRequest->SetHeader(TEXT("Content-Disposition"), TEXT("attachment;filename=TestStreamUpload.dat"));
	HttpRequest->OnProcessRequestComplete().BindLambda([Stream, TotalSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
		CHECK(Stream->CurrentPos == TotalSize);
	});
	HttpRequest->ProcessRequest();
}

namespace UE
{
namespace TestHttp
{

void WriteTestFile(const FString& TestFileName, uint64 TestFileSize)
{
	FArchive* RawFile = IFileManager::Get().CreateFileWriter(*TestFileName);
	CHECK(RawFile != nullptr);
	TSharedRef<FArchive> FileToWrite = MakeShareable(RawFile);
	char* FileData = (char*)FMemory::Malloc(TestFileSize);
	FMemory::Memset(FileData, 'd', TestFileSize);
	FileToWrite->Serialize(FileData, TestFileSize);
	FileToWrite->FlushCache();
	FileToWrite->Close();
	FMemory::Free(FileData);
}

}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request activity timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	float ActivityTimeoutSetting = 3.0f;
	HttpModule->HttpActivityTimeout = ActivityTimeoutSetting;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();

	SECTION("By default activity timeout from http module")
	{
	}
	SECTION("By customized activity timeout per http request which will override default settings from http module")
	{
		ActivityTimeoutSetting = 4.0f;
		HttpRequest->SetActivityTimeout(ActivityTimeoutSetting);
	}

	HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 5/*ChunkLatency*/));
	HttpRequest->SetVerb(TEXT("GET"));

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ActivityTimeoutSetting](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);

		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
#if UE_HTTP_ACTIVITY_TIMER_START_AFTER_RECEIVED_DATA
		// Unlike libCurl, currently there is an issue in xCurl that it triggers CURLINFO_HEADER_OUT even if can't
		// connect. Had to disable that code, make sure not to treat that event as connected
		// In a similar way on MacOS/iOS we don't get any notification until some data is received
		// So it takes 5s to receive the first chunk to be considered as connected, then start response timer and
		// take 3s to response timeout
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, ActivityTimeoutSetting + 5, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
#else
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, ActivityTimeoutSetting, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
#endif

	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request won't trigger activity timeout after cancelling", HTTP_TAG)
{
	HttpModule->HttpActivityTimeout = 2.0f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 5/*ChunkLatency*/));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

	const double TimeToWaitBeforeCancel = 1.0f;
	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, TimeToWaitBeforeCancel](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, TimeToWaitBeforeCancel, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Cancelled);
	});
	HttpRequest->ProcessRequest();
	FPlatformProcess::Sleep(TimeToWaitBeforeCancel);
	HttpRequest->CancelRequest();
	FPlatformProcess::Sleep(3.0f); // Just make sure there is no warning or assert triggered by the activity timeout callback
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request won't trigger activity timeout after total timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	HttpModule->HttpActivityTimeout = 2.0f;
	HttpModule->HttpTotalTimeout = 3.5f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(5/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 1/*ChunkLatency*/));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);
		ResumeLogVerbosity();
	});
	HttpRequest->ProcessRequest();
	FPlatformProcess::Sleep(6.0f); // Just make sure there is no warning or assert triggered by the activity timeout callback
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request receive won't timeout for streaming request", HTTP_TAG)
{
	HttpModule->HttpActivityTimeout = 3.0f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2/*ChunkLatency*/)); // Needs 6s to complete
	HttpRequest->SetVerb(TEXT("GET"));

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([this, StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
		const double DurationInSeconds = FPlatformTime::Seconds() - StartTime;
		CHECK(DurationInSeconds > HttpModule->HttpActivityTimeout);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request total timeout with get", HTTP_TAG)
{
	DisableWarningsInThisTest();

	float TotalTimeoutSetting = 3.0f;
	HttpModule->HttpConnectionTimeout = 5.0f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlMockLatency(10));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetTimeout(TotalTimeoutSetting);

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, TotalTimeoutSetting](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, TotalTimeoutSetting, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request total timeout with streaming download", HTTP_TAG)
{
	DisableWarningsInThisTest();

	float TimeoutSetting = 3.0f;
	HttpModule->HttpActivityTimeout = 2.5f; // Make sure it won't fail because of receive timeout
	HttpModule->HttpTotalTimeout = TimeoutSetting;

	if (bRetryEnabled)
	{
		TimeoutSetting = 4.0f; // This will override http module default timeout
		HttpRetryManager->RetryTimeoutRelativeSecondsDefault = TimeoutSetting;
	}

	TSharedPtr<IHttpRequest> HttpRequest;
	SECTION("Use default timeout from http module or retry manager depends on bRetryEnabled")
	{
		HttpRequest = CreateRequest();
	}
	SECTION("Override from http request")
	{
		TimeoutSetting = 5.0f; // This will override default timeout in http module and retry manager

		if (bRetryEnabled)
		{
			HttpRequest = HttpRetryManager->CreateRequest(FHttpRetrySystem::FRetryLimitCountSetting(), TimeoutSetting);
		}
		else
		{
			HttpRequest = HttpModule->CreateRequest();
			HttpRequest->SetTimeout(TimeoutSetting);
		}
	}

	HttpRequest->SetURL(UrlStreamDownload(4/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2/*ChunkLatency*/)); // Needs 8s to complete
	HttpRequest->SetVerb(TEXT("GET"));

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, TimeoutSetting](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, TimeoutSetting, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http upload from file by PUT can work well", HTTP_TAG)
{
	FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamUpload.dat");
	UE::TestHttp::WriteTestFile(Filename, 5*1024*1024/*5MB*/);

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamUpload());
	HttpRequest->SetVerb(TEXT("PUT"));
	HttpRequest->SetHeader(TEXT("Content-Disposition"), TEXT("attachment;filename=TestStreamUpload.dat"));
	HttpRequest->SetContentAsStreamedFile(Filename);
	HttpRequest->OnProcessRequestComplete().BindLambda([Filename](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);
		IFileManager::Get().Delete(*Filename);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming uploading http request will re-open file when retry", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}
	DisableWarningsInThisTest();

	FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamUploadRetry.dat");
	UE::TestHttp::WriteTestFile(Filename, 1*1024*1024/*1MB*/);

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	HttpRequest->SetHeader(TEXT("Retry-After"), TEXT("1")); // Will be forwarded back in response
	HttpRequest->SetVerb(TEXT("PUT"));
	HttpRequest->SetHeader(TEXT("Content-Disposition"), TEXT("attachment;filename=TestStreamUploadRetry.dat"));
	HttpRequest->SetContentAsStreamedFile(Filename);

	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		Request->SetURL(UrlStreamUpload());
	});

	HttpRequest->OnProcessRequestComplete().BindLambda([Filename](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);
		IFileManager::Get().Delete(*Filename);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Redirect enabled by default and can work well", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	FString OriginalURL = FString::Format(TEXT("{0}/redirect_from"), { *UrlHttpTests() });
	FString ExpectedURL = FString::Format(TEXT("{0}/redirect_to"), { *UrlHttpTests() });
	HttpRequest->SetURL(OriginalURL);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([OriginalURL, ExpectedURL](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);
		CHECK(HttpResponse->GetURL() == OriginalURL);
		CHECK(HttpResponse->GetEffectiveURL() == ExpectedURL);
	});
	HttpRequest->ProcessRequest();
}


class FWaitUntilQuitFromTestFixture : public FWaitUntilCompleteHttpFixture
{
public:
	FWaitUntilQuitFromTestFixture()
	{
	}

	~FWaitUntilQuitFromTestFixture()
	{
		WaitUntilQuitFromTest();
	}

	void WaitUntilQuitFromTest()
	{
		while (!bQuitRequested)
		{
			TickHttpManager();
		}
	}

	bool bQuitRequested = false;
};

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Http request can be reused", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(TEXT("POST"));

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);

		// Using a different URL
		uint32 Chunks = 3;
		uint32 ChunkSize = 1024;
		HttpRequest->SetURL(UrlStreamDownload(Chunks, ChunkSize));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->OnProcessRequestComplete().BindLambda([this, Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);

			// Simulate retry with same URL info
			HttpRequest->OnProcessRequestComplete().BindLambda([this, Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(bSucceeded);
				REQUIRE(HttpResponse != nullptr);
				CHECK(HttpResponse->GetResponseCode() == 200);
				CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
				bQuitRequested = true;
			});
			HttpRequest->ProcessRequest();
		});
		HttpRequest->ProcessRequest();
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Http request can be reused when there is total timeout setting", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlMockLatency(3));
	HttpRequest->SetTimeout(2);

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);

		HttpRequest->SetURL(UrlMockLatency(1));
		HttpRequest->ResetTimeoutStatus(); // Must do this in order to restart timeout

		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			bQuitRequested = true;
		});
		HttpRequest->ProcessRequest();
	});
	HttpRequest->ProcessRequest();
}

#if UE_HTTP_CONNECTION_TIMEOUT_SUPPORT_RETRY
TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Make sure connection time out can work well for 2nd same http request", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();

	float ConnectionTimeoutDuration = 2.0f;
	HttpModule->HttpConnectionTimeout = ConnectionTimeoutDuration;

	HttpRequest->SetURL(UrlWithInvalidPortToTestConnectTimeout());

	const double StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([this, StartTime, ConnectionTimeoutDuration](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		TSharedRef<IHttpRequest> HttpRequest2 = CreateRequest();
		HttpRequest2->SetURL(UrlWithInvalidPortToTestConnectTimeout());
		HttpRequest2->OnProcessRequestComplete().BindLambda([this, StartTime, ConnectionTimeoutDuration](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			bQuitRequested = true;
			const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
			CHECK(FMath::IsNearlyEqual(DurationInSeconds, ConnectionTimeoutDuration * 2, UE_HTTP_CONNECTION_TIMEOUT_MAX_DEVIATION * 2));
		});
		HttpRequest2->ProcessRequest();
	});
	HttpRequest->ProcessRequest();
}
#endif // UE_HTTP_CONNECTION_TIMEOUT_SUPPORT_RETRY

// Response shared ptr should be able to be kept by user code and valid to access without http request
class FValidateResponseDependencyFixture : public FWaitUntilCompleteHttpFixture
{
public:
	DECLARE_DELEGATE(FValidateResponseDependencyDelegate);

	~FValidateResponseDependencyFixture()
	{
		WaitUntilAllHttpRequestsComplete();

		ValidateResponseDependencyDelegate.ExecuteIfBound();
	}

	FValidateResponseDependencyDelegate ValidateResponseDependencyDelegate;
};

TEST_CASE_METHOD(FValidateResponseDependencyFixture, "Http query with parameters", HTTP_TAG)
{
	struct FQueryWithParamsResponse : public FJsonSerializable
	{
		int32 VarInt;
		FString VarStr;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("var_int", VarInt);
			JSON_SERIALIZE("var_str", VarStr);
		END_JSON_SERIALIZER
	};

	TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
	FString UrlQueryWithParams = FString::Format(TEXT("{0}/query_with_params/?var_int=3&var_str=abc"), { *UrlHttpTests() });
	HttpRequest->SetURL(UrlQueryWithParams);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([this, UrlQueryWithParams](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);

		CHECK(HttpRequest->GetURL() == UrlQueryWithParams);

		FQueryWithParamsResponse QueryWithParamsResponse;
		REQUIRE(QueryWithParamsResponse.FromJson(HttpResponse->GetContentAsString()));

		CHECK(FString::FromInt(QueryWithParamsResponse.VarInt) == HttpRequest->GetURLParameter(TEXT("var_int")));
		CHECK(QueryWithParamsResponse.VarStr == HttpRequest->GetURLParameter(TEXT("var_str")));

		CHECK(FString::FromInt(QueryWithParamsResponse.VarInt) == HttpResponse->GetURLParameter(TEXT("var_int")));
		CHECK(QueryWithParamsResponse.VarStr == HttpResponse->GetURLParameter(TEXT("var_str")));

		ValidateResponseDependencyDelegate.BindLambda([HttpResponse, UrlQueryWithParams, QueryWithParamsResponse](){
			// Validate all interfaces of http response can be called without accessing the destroyed http request
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetContent().IsEmpty());
			CHECK(!HttpResponse->GetContentAsString().IsEmpty());
			CHECK(HttpResponse->GetContentType() == TEXT("application/json"));
			CHECK(HttpResponse->GetHeader("Content-Type") == TEXT("application/json"));
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			CHECK(HttpResponse->GetURL() == UrlQueryWithParams);
			CHECK(HttpResponse->GetURLParameter(TEXT("var_int")) == FString::FromInt(QueryWithParamsResponse.VarInt));
			CHECK(HttpResponse->GetURLParameter(TEXT("var_str")) == QueryWithParamsResponse.VarStr);
		});
	});
	HttpRequest->ProcessRequest();
}

class FThreadedHttpRunnable : public FRunnable
{
public:
	DECLARE_DELEGATE(FRunActualTestCodeDelegate);

	FRunActualTestCodeDelegate& OnRunFromThread()
	{
		return ThreadCallback;
	}

	// FRunnable interface
	virtual uint32 Run() override
	{
		ThreadCallback.ExecuteIfBound();
		return 0;
	}

	void StartTestHttpThread(bool bBlockGameThread)
	{
		bBlockingGameThreadTick = bBlockGameThread;

		RunnableThread = TSharedPtr<FRunnableThread>(FRunnableThread::Create(this, TEXT("Test Http Thread")));

		while (bBlockingGameThreadTick)
		{
			float TickFrequency = 1.0f / 60; /*60 FPS*/;
			FPlatformProcess::Sleep(TickFrequency);
		}
	}

	void UnblockGameThread()
	{
		bBlockingGameThreadTick = false;
	}

private:
	FRunActualTestCodeDelegate ThreadCallback;
	TSharedPtr<FRunnableThread> RunnableThread;
	std::atomic<bool> bBlockingGameThreadTick = true;
};

class FWaitThreadedHttpFixture : public FWaitUntilCompleteHttpFixture
{
public:
	~FWaitThreadedHttpFixture()
	{
		WaitUntilAllHttpRequestsComplete();
	}

	FThreadedHttpRunnable ThreadedHttpRunnable;
};

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Http streaming download request can work in non game thread", HTTP_TAG)
{
	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this]() {
		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, 1024/*ChunkSize*/));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			virtual void Serialize(void* V, int64 Length) override
			{
				// No matter what's the thread policy, Serialize always get called in http thread.
				CHECK(!IsInGameThread());
			}
		};
		CHECK(HttpRequest->SetResponseBodyReceiveStream(MakeShared<FTestHttpReceiveStream>()));

		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			// EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread was used, so not in game thread here
			CHECK(!IsInGameThread());
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(!HttpResponse->GetAllHeaders().IsEmpty());
			ThreadedHttpRunnable.UnblockGameThread();
		});

		HttpRequest->ProcessRequest();
	});

	ThreadedHttpRunnable.StartTestHttpThread(true/*bBlockGameThread*/);
}

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Http download request progress callback can be received in http thread", HTTP_TAG)
{
	std::atomic<bool> bRequestProgressTriggered = false;
	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this, &bRequestProgressTriggered]() {
		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetURL(UrlStreamDownload(10/*Chunks*/, 1024*1024/*ChunkSize*/));
		HttpRequest->SetVerb(TEXT("GET"));

		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnRequestProgress64().BindLambda([this, &bRequestProgressTriggered](FHttpRequestPtr Request, uint64 /*BytesSent*/, uint64 BytesReceived) {
			if (!bRequestProgressTriggered)
			{
				// Only do these checks once, because when http request complete, this callback also get triggered
				CHECK(BytesReceived > 0);
				CHECK(BytesReceived < 10/*Chunks*/ * 1024*1024/*ChunkSize*/);
				CHECK(!IsInGameThread());
				CHECK(Request->GetStatus() == EHttpRequestStatus::Processing);
				bRequestProgressTriggered = true;
			}
		});
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr /*HttpRequest*/, FHttpResponsePtr /*HttpResponse */, bool bSucceeded) {
			CHECK(bSucceeded);
			ThreadedHttpRunnable.UnblockGameThread();
		});

		HttpRequest->ProcessRequest();
	});

	ThreadedHttpRunnable.StartTestHttpThread(true/*bBlockGameThread*/);

	CHECK(bRequestProgressTriggered);
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request pre check will fail", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();

	SECTION("when verb was set to empty")
	{
		HttpRequest->SetURL(UrlToTestMethods());
		HttpRequest->SetVerb(TEXT(""));
	}
	SECTION("when url protocol is not valid")
	{
		HttpRequest->SetURL("http_abc://www.epicgames.com");
		HttpRequest->SetVerb(TEXT("GET"));
	}
	SECTION("when url was not set")
	{
		HttpRequest->SetVerb(TEXT("GET"));
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
	});

	HttpRequest->ProcessRequest();
}

class FValidateHeaderReceiveOrderFixture : public FWaitUntilCompleteHttpFixture
{
public:
	~FValidateHeaderReceiveOrderFixture()
	{
		WaitUntilAllHttpRequestsComplete();
	}

	std::atomic<bool> bHeaderReceived = false;
	std::atomic<bool> bCompleteCallbackTriggered = false;
	std::atomic<bool> bAnyDataReceived = false;
};

TEST_CASE_METHOD(FValidateHeaderReceiveOrderFixture, "Http request header received callback will be called by thread policy", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(2/*Chunks*/, 1024/*ChunkSize*/));
	HttpRequest->SetVerb(TEXT("GET"));

	FHttpRequestStreamDelegateV2 StreamDelegate;
	StreamDelegate.BindLambda([this](void *InDataPtr, int64& InLength) {
		bAnyDataReceived = true;
		CHECK(!bCompleteCallbackTriggered);
	});
	HttpRequest->SetResponseBodyReceiveStreamDelegateV2(StreamDelegate);

	SECTION("in http thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnHeaderReceived().BindLambda([this](FHttpRequestPtr Request, const FString& HeaderName, const FString& HeaderValue) {
			CHECK(!bAnyDataReceived);
			CHECK(!bCompleteCallbackTriggered);
			CHECK(!IsInGameThread());
			bHeaderReceived = true;
		});
	}
	SECTION("in game thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
		HttpRequest->OnHeaderReceived().BindLambda([this](FHttpRequestPtr Request, const FString& HeaderName, const FString& HeaderValue) {
			// Data received delegate always triggered from http thread, so it could have been received, while header will be received
			// from game thread in this test section
			// CHECK(!bAnyDataReceived);
			CHECK(!bCompleteCallbackTriggered);
			CHECK(IsInGameThread());
			bHeaderReceived = true;
		});
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr /*HttpRequest*/, FHttpResponsePtr /*HttpResponse */, bool bSucceeded) {
		CHECK(bAnyDataReceived);
		CHECK(bHeaderReceived);
		bCompleteCallbackTriggered = true;
		CHECK(bSucceeded);
	});

	HttpRequest->ProcessRequest();
}

class FValidateStatusCodeReceiveOrderFixture : public FWaitUntilCompleteHttpFixture
{
public:
	~FValidateStatusCodeReceiveOrderFixture()
	{
		WaitUntilAllHttpRequestsComplete();
	}

	std::atomic<bool> bStatusCodeReceived = false;
	std::atomic<bool> bCompleteCallbackTriggered = false;
};

TEST_CASE_METHOD(FValidateStatusCodeReceiveOrderFixture, "Http request status code received callback will be called by thread policy", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(20/*Chunks*/, 1024*1024/*ChunkSize*/));
	HttpRequest->SetVerb(TEXT("GET"));

	SECTION("in http thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnStatusCodeReceived().BindLambda([this](FHttpRequestPtr Request, int32 StatusCode) {
			CHECK(StatusCode == 200);
			CHECK(!bCompleteCallbackTriggered);
			CHECK(!IsInGameThread());
			bStatusCodeReceived = true;
		});
	}
	SECTION("in game thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
		HttpRequest->OnStatusCodeReceived().BindLambda([this](FHttpRequestPtr Request, int32 StatusCode) {
			CHECK(StatusCode == 200);
			CHECK(!bCompleteCallbackTriggered);
			CHECK(IsInGameThread());
			bStatusCodeReceived = true;
		});
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr /*HttpRequest*/, FHttpResponsePtr /*HttpResponse */, bool bSucceeded) {
		CHECK(bStatusCodeReceived);
		bCompleteCallbackTriggered = true;
		CHECK(bSucceeded);
	});

	HttpRequest->ProcessRequest();
}

namespace UE
{
namespace TestHttp
{

void SetupURLRequestFilter(FHttpModule* HttpModule)
{
	// Pre check will fail when domain is not allowed
	UE::Core::FURLRequestFilter::FRequestMap SchemeMap;
	SchemeMap.Add(TEXT("http"), TArray<FString>{TEXT("epicgames.com")});
	UE::Core::FURLRequestFilter Filter{SchemeMap};
	HttpModule->GetHttpManager().SetURLRequestFilter(Filter);
}

}
}

// Pre-check failed requests won't be added into http manager, so it can't rely on the requested added/completed callback in FWaitUntilCompleteHttpFixture
TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Http request pre check will fail by thread policy", HTTP_TAG)
{
	DisableWarningsInThisTest();

	// Pre check will fail when domain is not allowed
	UE::TestHttp::SetupURLRequestFilter(HttpModule);

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(UrlToTestMethods());

	SECTION("on game thread")
	{
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(IsInGameThread());
			CHECK(!bSucceeded);
			bQuitRequested = true;
		});
	}
	SECTION("on http thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!IsInGameThread());
			CHECK(!bSucceeded);
			bQuitRequested = true;
		});
	}

	HttpRequest->ProcessRequest();
}

class FWaitUntilQuitFromTestThreadedFixture : public FWaitUntilQuitFromTestFixture
{
public:
	~FWaitUntilQuitFromTestThreadedFixture()
	{
		WaitUntilQuitFromTest();
	}

	FThreadedHttpRunnable ThreadedHttpRunnable;
};

// Pre-check failed requests won't be added into http manager, so it can't rely on the requested added/completed callback in FWaitUntilCompleteHttpFixture
TEST_CASE_METHOD(FWaitUntilQuitFromTestThreadedFixture, "Threaded http request pre check will fail by thread policy", HTTP_TAG)
{
	DisableWarningsInThisTest();

	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this]() {
		// Pre check will fail when domain is not allowed
		UE::TestHttp::SetupURLRequestFilter(HttpModule);

		TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->SetURL(UrlToTestMethods());

		SECTION("on game thread")
		{
			HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(IsInGameThread());
				CHECK(!bSucceeded);
				bQuitRequested = true;
			});
		}
		SECTION("on http thread")
		{
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
			HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(!IsInGameThread());
				CHECK(!bSucceeded);
				bQuitRequested = true;
			});
		}

		HttpRequest->ProcessRequest();
	});

	ThreadedHttpRunnable.StartTestHttpThread(false/*bBlockGameThread*/);
}

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Cancel http request without ProcessRequest called", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());
	++ExpectingExtraCallbacks;
	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		--ExpectingExtraCallbacks;
		CHECK(!bSucceeded);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Cancelled);
		bQuitRequested = true;
	});
	HttpRequest->CancelRequest();
}

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Cancel http request with ProcessRequest called but before started from queue", HTTP_TAG)
{
	CVarHttpMaxConcurrentRequests->Set(1);

	std::atomic<bool> bFirstRequestCompleted = false;

	FHttpManager& HttpManager = HttpModule->GetHttpManager();
	const FHttpStats HttpStats = HttpManager.GetHttpStats();
	CHECK(HttpStats.RequestsInQueue == 0);
	CHECK(HttpStats.MaxRequestsInQueue == 0);

	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this, &bFirstRequestCompleted]() {
		TSharedRef<IHttpRequest> HttpRequestRunning = CreateRequest();
		HttpRequestRunning->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 1/*ChunkLatency*/));
		HttpRequestRunning->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequestRunning->OnProcessRequestComplete().BindLambda([this, &bFirstRequestCompleted](FHttpRequestPtr HttpRequestQueuing, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			bFirstRequestCompleted = true;
			ThreadedHttpRunnable.UnblockGameThread();
		});
		HttpRequestRunning->ProcessRequest();


		TSharedRef<IHttpRequest> HttpRequestQueuing = CreateRequest();
		HttpRequestQueuing->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 1/*ChunkLatency*/));
		HttpRequestQueuing->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequestQueuing->OnHeaderReceived().BindLambda([this](FHttpRequestPtr Request, const FString& HeaderName, const FString& HeaderValue) {
			// Should never be started
			CHECK(false);
		});
		HttpRequestQueuing->OnRequestProgress64().BindLambda([this](FHttpRequestPtr Request, uint64 /*BytesSent*/, uint64 BytesReceived) {
			// Should never be started
			CHECK(false);
		});

		++ExpectingExtraCallbacks;
		HttpRequestQueuing->OnProcessRequestComplete().BindLambda([this, &bFirstRequestCompleted](FHttpRequestPtr HttpRequestQueuing, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			--ExpectingExtraCallbacks;
			CHECK(!bSucceeded);
			CHECK(HttpRequestQueuing->GetFailureReason() == EHttpFailureReason::Cancelled);
			CHECK(!bFirstRequestCompleted);
		});
		HttpRequestQueuing->ProcessRequest();
		FPlatformProcess::Sleep(1); // Make sure the first request started

		FHttpManager& HttpManager = HttpModule->GetHttpManager();
		const FHttpStats HttpStats = HttpManager.GetHttpStats();
		CHECK(HttpStats.RequestsInQueue == 1);
		CHECK(HttpStats.MaxRequestsInQueue == 1);

		HttpRequestQueuing->CancelRequest();
	});

	ThreadedHttpRunnable.StartTestHttpThread(true/*bBlockGameThread*/);
}

#if UE_HTTP_CONNECTION_TIMEOUT_SUPPORT_RETRY
TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Cancel http request connect before timeout", HTTP_TAG)
{
	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlWithInvalidPortToTestConnectTimeout());
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetTimeout(7);
	const double StartTime = FPlatformTime::Seconds();
	++ExpectingExtraCallbacks;
	HttpRequest->OnProcessRequestComplete().BindLambda([this, StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		--ExpectingExtraCallbacks;
		CHECK(!bSucceeded);
		const double DurationInSeconds = FPlatformTime::Seconds() - StartTime;
		CHECK(DurationInSeconds < 2.0);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::Cancelled);
	});
	SECTION("ProcessRequest called")
	{
		HttpRequest->ProcessRequest();
		FPlatformProcess::Sleep(0.5);
	}
	SECTION("ProcessRequest not called")
	{
	}
	HttpRequest->CancelRequest();
	HttpRequest->CancelRequest(); // Duplicated calls to CancelRequest should be fine
}
#endif

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Retry respect Retry-After header in response", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail}/*InRetryResponseCodes*/
	);

	SECTION("TooManyRequests")
	{
		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	}

	uint32 RetryAfter = 4;

	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { RetryAfter })); // Will be forwarded back in response

	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this, RetryAfter](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		CHECK(FMath::IsNearlyEqual(LockoutPeriod, (float)(RetryAfter)));
	});

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([RetryAfter, StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, (float)(RetryAfter), HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Request can time out during lock out", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	EHttpRequestDelegateThreadPolicy ThreadPolicyExpected = EHttpRequestDelegateThreadPolicy::CompleteOnGameThread;
	SECTION("From game thread")
	{
	}
	SECTION("From http thread")
	{
		ThreadPolicyExpected = EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread;
	}

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	HttpRequest->SetTimeout(1.0f);
	HttpRequest->SetDelegateThreadPolicy(ThreadPolicyExpected);

	uint32 RetryAfter = 4;

	// Will be forwarded back in response
	HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { RetryAfter }));

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ThreadPolicyExpected](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		// When timeout during lock out period, it fails with result of last request before lock out
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetFailureReason() == EHttpFailureReason::None);
		CHECK(HttpResponse->GetResponseCode() == EHttpResponseCodes::TooManyRequests);
		CHECK(HttpResponse->GetContentLength() > 0);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, 1.0, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
		CHECK((ThreadPolicyExpected == EHttpRequestDelegateThreadPolicy::CompleteOnGameThread && IsInGameThread() || ThreadPolicyExpected == EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread && !IsInGameThread()));
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Request can time out during retry request", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	HttpRequest->SetTimeout(3.0f);

	uint32 RetryAfter = 2;
	// Will be forwarded back in response
	HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { RetryAfter }));

	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		// Now retry with a latency during request
		Request->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2/*ChunkLatency*/));
	});

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);

		// When timeout during retrying request, it fails with result of last request before retrying, to 
		// keep it the same behavior when timeout during lockout
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetFailureReason() == EHttpFailureReason::None);
		CHECK(HttpResponse->GetResponseCode() == EHttpResponseCodes::TooManyRequests);
		CHECK(HttpResponse->GetContentLength() > 0);
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, 3.0, HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST));
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Request will not retry", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(1/*InRetryLimitCountOverride*/);
	SECTION("When response code is not listed for retry")
	{
		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
		// Will be forwarded back in response
		HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { 2 }));
	}
	SECTION("When there is any response and timed out during streaming download")
	{
		HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 2/*ChunkLatency*/));
		HttpRequest->SetTimeout(3.0f);

		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
			CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::TimedOut);
		});
	}

	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr Response, float LockoutPeriod) {
		CHECK(false);
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Retry immediately without lock out if connect failed and there are alt domains", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	HttpModule->HttpConnectionTimeout = 1.0f;

	TArray<FString> AltDomains;
	AltDomains.Add(UrlToTestMethods());
	FHttpRetrySystem::FRetryDomainsPtr RetryDomains = MakeShared<FHttpRetrySystem::FRetryDomains>(MoveTemp(AltDomains));
	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		1/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{ EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail }/*InRetryResponseCodes*/,
		FHttpRetrySystem::FRetryVerbs(),
		RetryDomains
	);

	HttpRequest->SetURL(UrlWithInvalidPortToTestConnectTimeout());
	HttpRequest->SetVerb(TEXT("GET"));
	++ExpectingExtraCallbacks;
	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		CHECK(LockoutPeriod == 0);
	});
	HttpRequest->ProcessRequest();
}

#if UE_HTTP_CONNECTION_TIMEOUT_SUPPORT_RETRY
TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Optionally retry limit can be set differently for connection error", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	HttpModule->HttpConnectionTimeout = 1.0f;

	FHttpRetrySystem::FExponentialBackoffCurve RetryBackoffCurve;
	RetryBackoffCurve.MinCoefficient = 1.0f; // no jitter

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		3/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{ EHttpResponseCodes::TooManyRequests, EHttpResponseCodes::ServiceUnavail }/*InRetryResponseCodes*/,
		FHttpRetrySystem::FRetryVerbs(), /*unused*/
		FHttpRetrySystem::FRetryDomainsPtr(), /*unused*/
		1, /*InRetryLimitCountForConnectionErrorOverride*/
		RetryBackoffCurve/*InExponentialBackoffCurve*/
	);

	float ExpectedTimeoutDuration = 0.0f;
	float TimeDiffTolerance = 0.0f;
	SECTION("RetryLimitCountForConnectionErrorDefault:1 will be used so retries for connection error take less time")
	{
		HttpRequest->SetURL(UrlWithInvalidPortToTestConnectTimeout());

		ExpectedTimeoutDuration = 6.0f; // each request will take 1s, 1st retry back off takes 4s
		TimeDiffTolerance = 2 * UE_HTTP_CONNECTION_TIMEOUT_MAX_DEVIATION;
	}
	SECTION("RetryLimitCountDefault:3 will be used so retries in general take long")
	{
		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
		HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { 3 }));

		ExpectedTimeoutDuration = 9.0f; // each request will take 0s, 3 retry back offs, each back off takes 3s;
		TimeDiffTolerance = 3 * HTTP_TIME_DIFF_TOLERANCE_OF_REQUEST;
	}

	const double StartTime = FPlatformTime::Seconds();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, ExpectedTimeoutDuration, TimeDiffTolerance](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		const double DurationInSeconds  = FPlatformTime::Seconds() - StartTime;
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, ExpectedTimeoutDuration, TimeDiffTolerance));
	});
	HttpRequest->ProcessRequest();
}
#endif // UE_HTTP_CONNECTION_TIMEOUT_SUPPORT_RETRY

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Retry fallback with exponential lock out if there is no Retry-After header", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
		2/*InRetryLimitCountOverride*/,
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
		{EHttpResponseCodes::TooManyRequests}/*InRetryResponseCodes*/
	);

	HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
	HttpRequest->SetVerb(TEXT("GET"));

	ExpectingExtraCallbacks = 2;

	HttpRequest->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
		--ExpectingExtraCallbacks;
		// Default value in FExponentialBackoffCurve Compute(1) is 4 with default value in FBackoffJitterCoefficient applied
		CHECK(LockoutPeriod >= (4 * 0.5f));
		CHECK(LockoutPeriod <= (4 * 1.0f));
		Request->OnRequestWillRetry().BindLambda([this](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, float LockoutPeriod) {
			--ExpectingExtraCallbacks;
			// Default value in FExponentialBackoffCurve Compute(2) is 8 with default value in FBackoffJitterCoefficient applied
			CHECK(LockoutPeriod >= (8 * 0.5f));
			CHECK(LockoutPeriod <= (8 * 1.0f));
		});
	});

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Dead lock test by retrying requests while completing requests", HTTP_TAG)
{
	if (!bRetryEnabled)
	{
		return;
	}

	DisableWarningsInThisTest();

	for (uint32 i = 0; i < 50; ++i)
	{
		TSharedRef<IHttpRequest> HttpRequest = HttpRetryManager->CreateRequest(
			5/*InRetryLimitCountOverride*/,
			FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting()/*InRetryTimeoutRelativeSecondsOverride unused*/,
			{ EHttpResponseCodes::TooManyRequests }/*InRetryResponseCodes*/
		);

		HttpRequest->SetURL(UrlMockStatus(EHttpResponseCodes::TooManyRequests));
		HttpRequest->SetHeader(TEXT("Retry-After"), FString::Format(TEXT("{0}"), { 0.1 }));
		HttpRequest->ProcessRequest();
	}
}

class FThreadedBatchRequestsFixture : public FWaitThreadedHttpFixture
{
public:
	void LaunchBatchRequests(uint32 BatchSize)
	{
		for (uint32 i = 0; i < BatchSize; ++i)
		{
			TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
			HttpRequest->SetURL(UrlStreamDownload(3, 1024*1024));
			HttpRequest->SetVerb(TEXT("GET"));
			HttpRequest->ProcessRequest();
		}
	}

	void BlockUntilFlushed()
	{
		if (bRetryEnabled)
		{
			HttpRetryManager->BlockUntilFlushed(5.0);
		}
		else
		{
			HttpModule->GetHttpManager().Flush(EHttpFlushReason::Default);
		}
	}
};

TEST_CASE_METHOD(FThreadedBatchRequestsFixture, "Retry manager and http manager is thread safe for flushing", HTTP_TAG)
{
	DisableWarningsInThisTest();

	ThreadedHttpRunnable.OnRunFromThread().BindLambda([this]() {
		LaunchBatchRequests(10);
		BlockUntilFlushed();
	});
	ThreadedHttpRunnable.StartTestHttpThread(false/*bBlockGameThread*/);

	LaunchBatchRequests(10);
	BlockUntilFlushed();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Flush while activity timeout shouldn't dead lock", HTTP_TAG)
{
	DisableWarningsInThisTest();

	HttpModule->HttpActivityTimeout = 2.0f;

	TSharedPtr<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(UrlStreamDownload(3/*Chunks*/, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 5/*ChunkLatency*/));
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed);
		CHECK(HttpRequest->GetFailureReason() == EHttpFailureReason::ConnectionError);
	});
	HttpRequest->ProcessRequest();

	HttpModule->GetHttpManager().Flush(EHttpFlushReason::FullFlush);
}

#if UE_HTTP_SUPPORT_LOCAL_SERVER

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Scheme besides http and https can work if allowed by settings", HTTP_TAG)
{
	bool bShouldSucceed = false;
	SECTION("when allowed")
	{
		bShouldSucceed = true;
	}
	SECTION("when not allowed")
	{
		DisableWarningsInThisTest();
		// Pre check will fail when scheme is not listed
		UE::TestHttp::SetupURLRequestFilter(HttpModule);
	}

	FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestProtocolAllowed.dat");
	UE::TestHttp::WriteTestFile(Filename, 10/*Bytes*/);

	TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(FString(TEXT("file://")) + Filename.Replace(TEXT(" "), TEXT("%20")));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([Filename, bShouldSucceed](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded == bShouldSucceed);
		IFileManager::Get().Delete(*Filename);
	});
	HttpRequest->ProcessRequest();
}

class FLocalHttpServerFixture : public FWaitUntilCompleteHttpFixture
{
public:
	FLocalHttpServerFixture()
	{
		HttpServerModule = new FHttpServerModule();
		IModuleInterface* Module = HttpServerModule;
		Module->StartupModule();

		HttpRouter = HttpServerModule->GetHttpRouter(LocalHttpServerPort);
		CHECK(HttpRouter.IsValid());
	}

	void StartServerWithHandler(const FHttpPath& HttpPath, EHttpServerRequestVerbs Verb, FHttpRequestHandler RequestHandler)
	{
		CHECK(HttpRouteHandle == nullptr);
		HttpRouteHandle = HttpRouter->BindRoute(HttpPath, Verb, RequestHandler);
		HttpServerModule->StartAllListeners();
	}

	~FLocalHttpServerFixture()
	{
		while (HasOngoingRequest())
		{
			HttpServerModule->Tick(TickFrequency);
			HttpModule->GetHttpManager().Tick(TickFrequency);
			FPlatformProcess::Sleep(TickFrequency);
		}

		HttpRouter->UnbindRoute(HttpRouteHandle);
		HttpRouter.Reset();

		IModuleInterface* Module = HttpServerModule;
		Module->ShutdownModule();
		delete Module;
	}

	TSharedPtr<IHttpRouter> HttpRouter;
	FHttpRouteHandle HttpRouteHandle;
	FHttpServerModule* HttpServerModule = nullptr;
	uint32 LocalHttpServerPort = 9000;
};

TEST_CASE_METHOD(FLocalHttpServerFixture, "Local http server can serve large file", HTTP_TAG)
{
	const uint32 FileSize = 100 * 1024 * 1024; // 100 MB seems good enough to repro SE_EWOULDBLOCK or SE_TRY_AGAIN on Mac
	StartServerWithHandler(FHttpPath(TEXT("/large_file")), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateLambda([FileSize](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
		TArray<uint8> ResultData;
		ResultData.SetNum(FileSize);
		FMemory::Memset(ResultData.GetData(), 'd', FileSize);

		OnComplete(FHttpServerResponse::Create(MoveTemp(ResultData), TEXT("text/text")));
		return true;
	}));

	// Start client request
	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	HttpRequest->SetURL(TEXT("http://localhost:9000/large_file"));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([FileSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetContentLength() == FileSize);
	});
	HttpRequest->ProcessRequest();
}

#endif // UE_HTTP_SUPPORT_LOCAL_SERVER

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Accessing request initial information without issue while request is running", HTTP_TAG)
{
	for (int32 i = 0; i < 30; ++i) // Use 2 "for" loops so it doesn't trigger the warning the request waited for too long in the queue
	{
		TArray<TSharedRef<IHttpRequest>> Requests;
		for (int32 j = 0; j < 30; ++j)
		{
			TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
			HttpRequest->SetHeader(TEXT("Custom-HeaderA"), TEXT("a"));
			HttpRequest->SetHeader(TEXT("Custom-HeaderB"), TEXT("b"));
			HttpRequest->SetHeader(TEXT("Custom-HeaderC"), TEXT("c"));
			HttpRequest->SetURL(UrlStreamDownload(3, HTTP_TEST_TIMEOUT_CHUNK_SIZE, 0));
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
			HttpRequest->ProcessRequest();
			Requests.Add(HttpRequest);
		}

		bool bRequestsStillRunning = true;
		while (bRequestsStillRunning)
		{
			bRequestsStillRunning = false;
			for (TSharedRef<IHttpRequest> Request : Requests)
			{
				if (!EHttpRequestStatus::IsFinished(Request->GetStatus()))
				{
					bRequestsStillRunning = true;

					CHECK(!Request->GetAllHeaders().IsEmpty());
					CHECK(!Request->GetURL().IsEmpty());
				}
			}
		}
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Test platform request requests limits", HTTP_TAG "[LIMIT]")
{
	bool bCheckCancel = GENERATE(false, true);
	int32 NumRequests = GENERATE(1, 10, 20, 50, 100, 200, 500, 1000);
	//Output NumRequests when error occurs.
	UNSCOPED_INFO(NumRequests);
	UNSCOPED_INFO(bCheckCancel);

	DYNAMIC_SECTION(" making " << NumRequests << " requests with bCheckCancel=" << bCheckCancel)
	{
		if (NumRequests > 50 && !bRunHeavyTests)
		{
			return;
		}

		TArray<TSharedRef<IHttpRequest>> Requests;

		for (int32 i = 0; i < NumRequests; ++i)
		{
			TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
			// Requests server to serve 1024b chunks to allow time for cancel to happen
			HttpRequest->SetURL(UrlStreamDownload(3, HTTP_TEST_TIMEOUT_CHUNK_SIZE, /*ChunkLatency=*/bCheckCancel ? 1 : 0));
			HttpRequest->SetVerb(TEXT("GET"));

			// Since catch2 uses std::srand, use std::rand here should make it deterministic when use same seed through --rng-seed
			if (std::rand() % 2)
			{
				HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
			}

			HttpRequest->OnProcessRequestComplete().BindLambda([bCheckCancel](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
			{
				//Only assert if response is successful on non-canceled requests
				if (!bCheckCancel)
				{
					CHECK(bSucceeded);
					CHECK(HttpResponse != nullptr);
				}
			});
			HttpRequest->ProcessRequest();

			Requests.Add(HttpRequest);
		}

		CHECK(Requests.Num() == NumRequests);

		if(bCheckCancel)
		{
			// Make sure requests are started in http thread
			FPlatformProcess::Sleep(0.1);

			for (auto Request : Requests)
			{
				Request->CancelRequest();
			}
		}
	}
}

#if UE_HTTP_SUPPORT_UNIX_SOCKET

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http Methods over Unix Domain Socket", HTTP_TAG)
{
	if (WebServerUnixSocket.Len() == 0)
	{
		return;
	}

	TSharedRef<IHttpRequest> HttpRequest = CreateRequest();
	CHECK(HttpRequest->GetVerb() == TEXT("GET"));

	const int Number = FPlatformTime::Cycles();

	HttpRequest->SetURL(FString::Format(TEXT("{0}/{1}"), { *UrlUnixSocketHttpTests(), Number }));
	HttpRequest->SetOption(HttpRequestOptions::UnixSocketPath, WebServerUnixSocket);

	SECTION("Default GET")
	{
	}
	SECTION("GET")
	{
		HttpRequest->SetVerb(TEXT("GET"));
	}
	SECTION("POST")
	{
		HttpRequest->SetVerb(TEXT("POST"));
	}
	SECTION("PUT")
	{
		HttpRequest->SetVerb(TEXT("PUT"));
	}
	SECTION("DELETE")
	{
		HttpRequest->SetVerb(TEXT("DELETE"));
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([Number](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);

		FString ResponseContent = HttpResponse->GetContentAsString();

		int NumberReturned = FCString::Atoi(*ResponseContent);
		CHECK(Number == NumberReturned);

		});
	HttpRequest->ProcessRequest();
}

#endif //UE_HTTP_SUPPORT_UNIX_SOCKET