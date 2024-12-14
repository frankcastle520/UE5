// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOS/ApplePlatformBackgroundHttpManager.h"
#include "IOS/ApplePlatformBackgroundHttp.h"
#include "IOS/ApplePlatformBackgroundHttpRequest.h"
#include "IOS/ApplePlatformBackgroundHttpResponse.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformAtomics.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeRWLock.h"

#include "Stats/Stats.h"

#include "IOS/IOSBackgroundURLSessionHandler.h"

#include "PlatformBackgroundHttp.h"

FApplePlatformBackgroundHttpManager::FApplePlatformBackgroundHttpManager()
	: FBackgroundHttpManagerImpl()
	, PendingRemoveRequests()
	, PendingRemoveRequestLock()
{
	OnDownloadCompletedHandle = FBackgroundURLSessionHandler::OnDownloadCompleted.AddRaw(this, &FApplePlatformBackgroundHttpManager::OnDownloadCompleted);
	OnDownloadMetricsHandle = FBackgroundURLSessionHandler::OnDownloadMetrics.AddRaw(this, &FApplePlatformBackgroundHttpManager::OnDownloadMetrics);
}

FApplePlatformBackgroundHttpManager::~FApplePlatformBackgroundHttpManager()
{
	FBackgroundURLSessionHandler::OnDownloadCompleted.Remove(OnDownloadCompletedHandle);
	FBackgroundURLSessionHandler::OnDownloadMetrics.Remove(OnDownloadMetricsHandle);
}

void FApplePlatformBackgroundHttpManager::AddRequest(const FBackgroundHttpRequestPtr GenericRequest)
{
	FAppleBackgroundHttpRequestPtr Request = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(GenericRequest);
	if (!ensureAlwaysMsgf(Request.IsValid(), TEXT("Adding a non-Apple background request to our Apple Background Http Manager2! This is not supported or expected!")))
	{
		return;
	}

	if (AssociateWithAnyExistingRequest(Request))
	{
		return;
	}

	const uint64 DownloadId = FBackgroundURLSessionHandler::CreateOrFindDownload(Request->GetURLList(), Request->GetNSURLSessionPriority(), GetFileHashHelper());
	Request->SetInternalDownloadId(DownloadId);

	// Should never happen in practice
	if (DownloadId == FBackgroundURLSessionHandler::InvalidDownloadId)
	{
		FBackgroundHttpResponsePtr Response = FPlatformBackgroundHttp::ConstructBackgroundResponse(EHttpResponseCodes::ServerError, FString());
		Request->CompleteWithExistingResponseData(Response);
		return;
	}

	{
		FRWScopeLock ActiveScopeLock(ActiveRequestLock, SLT_Write);
		ActiveRequests.Add(Request);
	}

	++NumCurrentlyActiveRequests;
}

void FApplePlatformBackgroundHttpManager::RemoveRequest(const FBackgroundHttpRequestPtr GenericRequest)
{
	// UpdateProgress might call RemoveRequest, so we could deadlock on ActiveRequestLock if we try to edit ActiveRequests
	FRWScopeLock PendingScopeRemoveRequestLock(PendingRemoveRequestLock, SLT_Write);
	PendingRemoveRequests.Add(GenericRequest);
}

void FApplePlatformBackgroundHttpManager::SetCellularPreference(int32 Value)
{
	FBackgroundURLSessionHandler::AllowCellular(Value > 0);
}

bool FApplePlatformBackgroundHttpManager::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FApplePlatformBackgroundHttpManager_Tick);

	ensureAlwaysMsgf(IsInGameThread(), TEXT("Called from un-expected thread! Potential error in an implementation of background downloads!"));

	{
		FRWScopeLock ScopeLock(ActiveRequestLock, SLT_ReadOnly);
		for (FBackgroundHttpRequestPtr& GenericRequest : ActiveRequests)
		{
			FAppleBackgroundHttpRequestPtr Request = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(GenericRequest);
			if (ensureAlwaysMsgf(Request.IsValid(), TEXT("Invalid Request Pointer in ActiveRequests list!")))
			{
				Request->UpdateProgress();
			}
		}
	}

	{
		FRWScopeLock PendingScopeRemoveRequestLock(PendingRemoveRequestLock, SLT_Write);
		for (FBackgroundHttpRequestPtr& GenericRequest : PendingRemoveRequests)
		{
			FAppleBackgroundHttpRequestPtr Request = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(GenericRequest);
			if (ensureAlwaysMsgf(Request.IsValid(), TEXT("Adding a non-Apple background request to our Apple Background Http Manager2! This is not supported or expected!")))
			{
				const uint64 DownloadId = Request->GetInternalDownloadId();
				FBackgroundURLSessionHandler::CancelDownload(DownloadId);
				Request->SetInternalDownloadId(FBackgroundURLSessionHandler::InvalidDownloadId);

				FBackgroundHttpManagerImpl::RemoveRequest(Request);
			}
		}

		PendingRemoveRequests.Empty();
	}

	FBackgroundURLSessionHandler::SaveBackgroundHttpFileHashHelperState();

	return true;
}

void FApplePlatformBackgroundHttpManager::OnDownloadCompleted(const uint64 DownloadId, const bool bSuccess)
{
	// Assuming this function will be called rarily (once a few seconds) and overall amount of downloads is limited (<500),
	// it's then faster to just iterate over array to find the corresponding request.

	FRWScopeLock ScopeLock(ActiveRequestLock, SLT_ReadOnly);
	for (FBackgroundHttpRequestPtr& GenericRequest : ActiveRequests)
	{
		FAppleBackgroundHttpRequestPtr Request = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(GenericRequest);
		if (ensureAlwaysMsgf(Request.IsValid(), TEXT("Invalid Request Pointer in ActiveRequests list!")))
		{
			if (Request->GetInternalDownloadId() == DownloadId)
			{
				Request->NotifyNotificationObjectOfComplete(bSuccess);
				return;
			}
		}
	}
}

void FApplePlatformBackgroundHttpManager::OnDownloadMetrics(const uint64 DownloadId, const int32 TotalBytesDownloaded, const float DownloadDuration)
{
	FRWScopeLock ScopeLock(ActiveRequestLock, SLT_ReadOnly);
	for (FBackgroundHttpRequestPtr& GenericRequest : ActiveRequests)
	{
		FAppleBackgroundHttpRequestPtr Request = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(GenericRequest);
		if (ensureAlwaysMsgf(Request.IsValid(), TEXT("Invalid Request Pointer in ActiveRequests list!")))
		{
			if (Request->GetInternalDownloadId() == DownloadId)
			{
				Request->NotifyRequestMetricsAvailable(TotalBytesDownloaded, DownloadDuration);
				return;
			}
		}
	}
}
