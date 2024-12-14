// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOS/ApplePlatformBackgroundHttpRequest.h"
#include "IOS/ApplePlatformBackgroundHttpManager.h"
#include "IOS/ApplePlatformBackgroundHttp.h"
#include "IOS/IOSBackgroundURLSessionHandler.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"

FApplePlatformBackgroundHttpRequest::FApplePlatformBackgroundHttpRequest()
	: DownloadId(FBackgroundURLSessionHandler::InvalidDownloadId)
{
}

void FApplePlatformBackgroundHttpRequest::PauseRequest()
{
	FBackgroundURLSessionHandler::PauseDownload(DownloadId);
}

void FApplePlatformBackgroundHttpRequest::ResumeRequest()
{
	FBackgroundURLSessionHandler::ResumeDownload(DownloadId);
}

#if !UE_BUILD_SHIPPING
void FApplePlatformBackgroundHttpRequest::GetDebugText(TArray<FString>& Output)
{
	FBackgroundURLSessionHandler::GetDownloadDebugText(DownloadId, Output);
}
#endif

const FString& FApplePlatformBackgroundHttpRequest::GetRequestDebugID() const
{
	static const FString EmptyString = TEXT("");
	return EmptyString;
}

void FApplePlatformBackgroundHttpRequest::UpdateProgress()
{
	if (DownloadId == FBackgroundURLSessionHandler::InvalidDownloadId)
	{
		return;
	}

	const uint64 CurrentDownloadedBytes = FBackgroundURLSessionHandler::GetCurrentDownloadedBytes(DownloadId);
	const uint64 BytesSinceLastReport = CurrentDownloadedBytes > LastReportedDownloadedBytes ? CurrentDownloadedBytes - LastReportedDownloadedBytes : 0;
	LastReportedDownloadedBytes = CurrentDownloadedBytes;

	if (BytesSinceLastReport > 0)
	{
		OnProgressUpdated().ExecuteIfBound(SharedThis(this), CurrentDownloadedBytes, BytesSinceLastReport);
	}

	int32 ResultHTTPCode;
	FString TemporaryFilePath;
	if (FBackgroundURLSessionHandler::IsDownloadFinished(DownloadId, ResultHTTPCode, TemporaryFilePath))
	{
		const bool bFileExists = !TemporaryFilePath.IsEmpty() && FPlatformFileManager::Get().GetPlatformFile().FileExists(*TemporaryFilePath);

		// Fail request if we can't access the file
		if (!bFileExists)
		{
			ResultHTTPCode = EHttpResponseCodes::ServerError;
		}

		FBackgroundHttpResponsePtr Response = FPlatformBackgroundHttp::ConstructBackgroundResponse(ResultHTTPCode, TemporaryFilePath);
		CompleteWithExistingResponseData(Response); // will internally call RemoveRequest which will cancel download
	}
}

float FApplePlatformBackgroundHttpRequest::BackgroundRequestPriorityToNSURLSessionPriority(const EBackgroundHTTPPriority Priority)
{
	switch (Priority)
	{
	case EBackgroundHTTPPriority::High:
		return NSURLSessionTaskPriorityHigh;
	case EBackgroundHTTPPriority::Low:
		return NSURLSessionTaskPriorityLow;
	case EBackgroundHTTPPriority::Normal:
	default:
		return NSURLSessionTaskPriorityDefault;
	}
}
