// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BackgroundHttpManagerImpl.h"
#include "Interfaces/IBackgroundHttpRequest.h"
#include "IOS/ApplePlatformBackgroundHttpRequest.h"

typedef TWeakPtr<FApplePlatformBackgroundHttpRequest,ESPMode::ThreadSafe> FBackgroundHttpURLMappedRequestPtr;

DECLARE_DELEGATE(FIOSBackgroundHttpPostSessionWorkCallback);

/**
 * Manages Background Http request that are currently being processed if we are on an Apple Platform
 */
class BACKGROUNDHTTP_API FApplePlatformBackgroundHttpManager
	: public FBackgroundHttpManagerImpl
{
public:
	FApplePlatformBackgroundHttpManager();
	virtual ~FApplePlatformBackgroundHttpManager();

	virtual void AddRequest(const FBackgroundHttpRequestPtr Request) override;
	virtual void RemoveRequest(const FBackgroundHttpRequestPtr Request) override;
	virtual void SetCellularPreference(int32 Value) override;
	virtual bool IsGenericImplementation() const override { return false; }
	virtual bool Tick(float DeltaTime) override;

	UE_DEPRECATED(5.5, "Variable deprecated") static FString BackgroundSessionIdentifier;
	UE_DEPRECATED(5.5, "Variable deprecated") static float ActiveTimeOutSetting;
	UE_DEPRECATED(5.5, "Variable deprecated") static int RetryResumeDataLimitSetting;
private:
	TArray<FBackgroundHttpRequestPtr> PendingRemoveRequests;
	FRWLock PendingRemoveRequestLock;
	FDelegateHandle OnDownloadCompletedHandle;
	FDelegateHandle OnDownloadMetricsHandle;
	void OnDownloadCompleted(const uint64 DownloadId, const bool bSuccess);
	void OnDownloadMetrics(const uint64 DownloadId, const int32 TotalBytesDownloaded, const float DownloadDuration);
};
