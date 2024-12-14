// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IHttpResponse.h"

class FHttpRequestCommon;

/**
 * Contains implementation of some common functions that don't vary between implementations of different platforms
 */
class UE_DEPRECATED(5.5, "FHttpResponseCommon is deprecated and will be moved to internal") FHttpResponseCommon : public IHttpResponse
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	friend FHttpRequestCommon;

public:
	HTTP_API FHttpResponseCommon(const FHttpRequestCommon& HttpRequest);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// IHttpBase
	HTTP_API virtual FString GetURLParameter(const FString& ParameterName) const override;
	HTTP_API virtual FString GetURL() const override;
	HTTP_API virtual const FString& GetEffectiveURL() const override;
	HTTP_API virtual EHttpRequestStatus::Type GetStatus() const override;
	HTTP_API virtual EHttpFailureReason GetFailureReason() const override;
	HTTP_API virtual int32 GetResponseCode() const override;

protected:
	HTTP_API void SetRequestStatus(EHttpRequestStatus::Type InCompletionStatus);
	HTTP_API void SetRequestFailureReason(EHttpFailureReason InFailureReason);
	HTTP_API void SetEffectiveURL(const FString& InEffectiveURL);
	HTTP_API void SetResponseCode(int32 InResponseCode);

	FString URL;
	FString EffectiveURL;
	EHttpRequestStatus::Type CompletionStatus;
	EHttpFailureReason FailureReason;
	int32 ResponseCode = EHttpResponseCodes::Unknown;
};
