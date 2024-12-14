// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/JiraIssue.h"
#include "Models/IntegrationOptions.h"
#include "Interfaces/IHttpRequest.h"
#include "Framework/SlateDelegates.h"
#include "Services/Interfaces/ISubmitToolService.h"

DECLARE_DELEGATE_OneParam(FOnJiraIssuesRetrieved, bool /*bValidResponse*/)

struct FJiraParameters;
class FJsonObject;
class FJsonValue;
class FSubmitToolServiceProvider;

class FJiraService final : public ISubmitToolService
{
public:
	FJiraService() = delete;
	FJiraService(const FJiraParameters& InJiraSettings, const int32 InMaxResults, TSharedRef<FSubmitToolServiceProvider> InServiceProvider);
	~FJiraService();

	bool FetchJiraTickets(bool InForce = false);
	void Reset();

	const TMap<FString, FJiraIssue>& GetIssues() { return this->JiraIssues; }

	FOnJiraIssuesRetrieved OnJiraIssuesRetrievedCallback;

	bool bOngoingRequest = false;
	bool IsBlockingRequestRunning()
	{
		return ServiceDeskRequest.IsValid() && ServiceDeskRequest->GetStatus() == EHttpRequestStatus::Processing;
	}

	void GetIssueAndCreateServiceDeskRequest(const FString& Key, const FString& Description, const FString& SwarmURL, const FString& InCurrentStream, const TMap<FString, TSharedPtr<FIntegrationOptionBase>>& InIntegrationOptions, const FOnBooleanValueChanged OnComplete);
	void CreateServiceDeskRequest(TSharedPtr<FJsonObject> InBaseJiraJsonObject, const FString& Description, const FString& SwarmURL, const FString& InCurrentStream, const TMap<FString, TSharedPtr<FIntegrationOptionBase>>& InIntegrationOptions, const FOnBooleanValueChanged OnComplete);
private:
	void QueryIssues();
	void QueryIssues_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

	bool ParseJsonObject(const TSharedPtr<FJsonObject>* InJsonObject, FJiraIssue& OutJiraIssue) const;

	const FString GetJiraIssuesFilepath() const;
	
	void GetuserInfo();
	void GetuserInfo_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

	void SaveJiraIssues() const;
	void LoadJiraIssues();
	
	void CreateServiceDeskRequest_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, const FOnBooleanValueChanged OnComplete);
	void GetIssueAndCreateServiceDeskRequest_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, const FString& Description, const FString& SwarmURL, const FString& InCurrentStream, const TMap<FString, TSharedPtr<FIntegrationOptionBase>>& InIntegrationOptions, const FOnBooleanValueChanged OnComplete);

private:
	const FJiraParameters Definition;
	FHttpRequestPtr JiraRequest = nullptr;
	FHttpRequestPtr ServiceDeskRequest = nullptr;
	const int32 MaxResults;
	int32 TotalIssues;
	TMap<FString, FJiraIssue> JiraIssues;

	TSharedRef<FSubmitToolServiceProvider> ServiceProvider;
};

Expose_TNameOf(FJiraService);