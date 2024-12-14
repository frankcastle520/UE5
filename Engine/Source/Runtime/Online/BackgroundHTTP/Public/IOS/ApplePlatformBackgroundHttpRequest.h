// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IBackgroundHttpRequest.h"
#include "BackgroundHttpRequestImpl.h"

/**
 * Contains implementation of Apple specific background http requests
 */
class BACKGROUNDHTTP_API FApplePlatformBackgroundHttpRequest 
	: public FBackgroundHttpRequestImpl
{
public:
	FApplePlatformBackgroundHttpRequest();
	virtual ~FApplePlatformBackgroundHttpRequest() = default;

	virtual void PauseRequest() override;
	virtual void ResumeRequest() override;

	//Used to provide some extra debug information over normal GetRequestID()
	//Returns string in format of X.Y where X is the underlying Task Identifier if set and Y is what was set in the SetRequestID() call if this reqeust has associated with a task.
	//Returns the same as GetRequestID() if no task has been associated yet.
	UE_DEPRECATED(5.5, "Method deprecated") const FString& GetRequestDebugID() const;

#if !UE_BUILD_SHIPPING
	virtual void GetDebugText(TArray<FString>& Output) override;
#endif

protected:
	void SetInternalDownloadId(uint64 Id) {DownloadId = Id;}
	uint64 GetInternalDownloadId() const {return DownloadId;}

	void UpdateProgress();

	static float BackgroundRequestPriorityToNSURLSessionPriority(const EBackgroundHTTPPriority Priority);
	float GetNSURLSessionPriority() const
	{
		return BackgroundRequestPriorityToNSURLSessionPriority(GetRequestPriority());
	}

	friend class FApplePlatformBackgroundHttpManager;
private:
	uint64 DownloadId;

	uint64 LastReportedDownloadedBytes = 0;
};

typedef TSharedPtr<class FApplePlatformBackgroundHttpRequest, ESPMode::ThreadSafe> FAppleBackgroundHttpRequestPtr;
