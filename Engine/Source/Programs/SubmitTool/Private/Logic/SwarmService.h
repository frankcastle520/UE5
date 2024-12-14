// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Models/SwarmReview.h"
#include "Services/Interfaces/ISubmitToolService.h"
#include "Services/Interfaces/ISTSourceControlService.h"

#include "HttpModule.h"

class FSubmitToolServiceProvider;

DECLARE_DELEGATE_TwoParams(OnGetReviewComplete, const TUniquePtr<FSwarmReview>&, const FString&)
DECLARE_DELEGATE_TwoParams(OnCreateReviewComplete, bool, const FString&)

class FSwarmService : public ISubmitToolService
{
public:
	FSwarmService(TSharedRef<FSubmitToolServiceProvider> InServiceProvider);
	void FetchReview(const OnGetReviewComplete& OnComplete);

	void CreateReview(const OnCreateReviewComplete& OnComplete);

	void UpdateReviewDescription(const TDelegate<void(bool)>& OnComplete, const FString& InDescription);


	const TUniquePtr<FSwarmReview>& GetReview() const { return Review; }
	bool GetCurrentReviewUrl(FString& OutUrl) const
	{
		if(Review.IsValid())
		{
			OutUrl = BuildReviewURL(FString::FromInt(Review->Id));
		}

		return Review.IsValid();
	}

	FString BuildReviewURL(const FString& InReviewId) const
	{
		return FString::Printf(TEXT("%s/reviews/%s"), *SwarmURL, *InReviewId);
	}

	void CancelOperations()
	{
		if(CreateSwarmRequest.IsValid())
		{
			CreateSwarmRequest->CancelRequest();
		}
	}

	bool IsRequestRunning() const
	{
		return (CreateSwarmRequest.IsValid() && CreateSwarmRequest->GetStatus() == EHttpRequestStatus::Processing)
			|| (UpdateSwarmRequest.IsValid() && UpdateSwarmRequest->GetStatus() == EHttpRequestStatus::Processing);
	}

private:
	const FString& GetSwarmURL();
	FString ReviewsURL() const;

private:
	TSharedPtr<IHttpRequest> CreateSwarmRequest;
	TSharedPtr<IHttpRequest> UpdateSwarmRequest;

	TSharedRef<FSubmitToolServiceProvider> ServiceProvider;
	FString SwarmURL;
	TUniquePtr<FSwarmReview> Review = nullptr;

	bool bCanConnect;
	TArray<TSharedPtr<FUserData>>* Users;
	TArray<TSharedPtr<FString>>* Groups;
};

Expose_TNameOf(FSwarmService);