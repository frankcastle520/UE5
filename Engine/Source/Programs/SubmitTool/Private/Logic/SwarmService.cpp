// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwarmService.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "Interfaces/IHttpResponse.h"
#include "Logic/Services/SubmitToolServiceProvider.h"
#include "Logic/Services/Interfaces/ISTSourceControlService.h"
#include "Logic/TagService.h"
#include "Logic/ChangelistService.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

#include "Logging/SubmitToolLog.h"

FSwarmService::FSwarmService(TSharedRef<FSubmitToolServiceProvider> InServiceProvider) :
	ServiceProvider(InServiceProvider)
{
	GetSwarmURL();
	ServiceProvider->GetService<ISTSourceControlService>()->GetUsersAndGroups(FOnUsersAndGroupsGet::FDelegate::CreateLambda([this](TArray<TSharedPtr<FUserData>>& InUsers, TArray<TSharedPtr<FString>>& InGroups) {
		Users = &InUsers;
		Groups = &InGroups;
	}));
}

void FSwarmService::FetchReview(const OnGetReviewComplete& OnComplete)
{
	if (!bCanConnect)
	{
		return;
	}

	FString Changelist = ServiceProvider->GetService<FChangelistService>()->GetCLID();

	if (Changelist.IsEmpty())
	{
		return;
	}

	const TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	HttpRequest->SetHeader(TEXT("Authorization"), *ServiceProvider->GetService<ISTSourceControlService>()->GetAuthTicket().ToString());

	HttpRequest->SetURL(FString::Format(TEXT("{0}?change={1}&max={2}"),
		{
			ReviewsURL(),
			Changelist, // get the review for a specific CL
			1 // we only want a single review.
		}));
	HttpRequest->SetVerb(TEXT("GET"));

	HttpRequest->OnProcessRequestComplete().BindLambda([OnComplete, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			if (!bConnectedSuccessfully)
			{
				OnComplete.ExecuteIfBound(Review, TEXT("Connection Failed"));
				return;
			}

			UE_LOG(LogSubmitToolDebug, Log, TEXT("Fetch review Response: %s"), *Response->GetContentAsString());

			if(EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				FSwarmReviewCollection ReviewCollection;
				if(FJsonObjectConverter::JsonObjectStringToUStruct<FSwarmReviewCollection>(Response->GetContentAsString(), &ReviewCollection, 0, 0))
				{
					if(ReviewCollection.Reviews.Num() > 0)
					{
						Review = MakeUnique<FSwarmReview>(ReviewCollection.Reviews[0]);
						OnComplete.ExecuteIfBound(Review, {});
					}
					else
					{
						OnComplete.ExecuteIfBound(Review, TEXT("No available reviews."));
					}
				}
				else
				{
					OnComplete.ExecuteIfBound(Review, TEXT("Could not parse the response json."));
				}
			}
			else
			{
				UE_LOG(LogSubmitTool, Error, TEXT("Could not communicate with swarm due to error %d.\n%s"), Response->GetResponseCode(), *Response->GetContentAsString());
				OnComplete.ExecuteIfBound(Review, FString::Printf(TEXT("Error code %d."), Response->GetResponseCode()));
			}
		});

	HttpRequest->ProcessRequest();
}

void FSwarmService::CreateReview(const OnCreateReviewComplete& OnComplete)
{
	if(!bCanConnect)
	{
		OnComplete.ExecuteIfBound(false, FString());
		return;
	}

	FString Changelist = ServiceProvider->GetService<FChangelistService>()->GetCLID();

	if(Changelist.IsEmpty())
	{
		return;
	}

	if(CreateSwarmRequest.IsValid())
	{
		CreateSwarmRequest->CancelRequest();
	}

	CreateSwarmRequest = FHttpModule::Get().CreateRequest();
	CreateSwarmRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	CreateSwarmRequest->SetHeader(TEXT("Authorization"), *ServiceProvider->GetService<ISTSourceControlService>()->GetAuthTicket().ToString());

	CreateSwarmRequest->SetURL(ReviewsURL());
	CreateSwarmRequest->SetVerb(TEXT("POST"));

	TSharedRef<FJsonObject> RequestJson = MakeShared<FJsonObject>();
	RequestJson->SetNumberField(TEXT("change"), FCString::Atoi(*Changelist));

	TArray<TSharedPtr<FJsonValue>> ReviewersObject;
	TArray<TSharedPtr<FJsonValue>> GroupsObject;

	for (const FTag* Tag : ServiceProvider->GetService<FTagService>()->GetTagsArray())
	{
		if (Tag->Definition.InputSubType.Equals(TEXT("Swarm"), ESearchCase::IgnoreCase))
		{
			for (const FString& value : Tag->GetValues())
			{
				if (Users != nullptr)
				{
					TSharedPtr<FUserData>* FoundUser = Users->FindByPredicate([value = value.TrimChar(TCHAR('@'))](TSharedPtr<FUserData>& InUserData) { return InUserData->Username == value; });
					if (FoundUser != nullptr)
					{
						ReviewersObject.Add(MakeShared<FJsonValueString>((*FoundUser)->Username));
					}
					continue;
				}

				if (Groups != nullptr)
				{
					TSharedPtr<FString>* FoundGroup = Groups->FindByPredicate([&value](TSharedPtr<FString>& InGroupData) { return *InGroupData == value; });
					if (FoundGroup != nullptr)
					{
						TSharedPtr<FJsonObject> GroupObject = MakeShared<FJsonObject>();
						TSharedPtr<FJsonObject> GroupDetails = MakeShared<FJsonObject>();
						GroupDetails->SetStringField(TEXT("required"), TEXT("false"));
						GroupObject->SetObjectField(*(*FoundGroup), GroupDetails);
						GroupsObject.Add(MakeShared<FJsonValueObject>(GroupObject));
					}
				}
			}
		}
	}

	if (!ReviewersObject.IsEmpty())
	{
		RequestJson->SetArrayField(TEXT("reviewers"), ReviewersObject);
	}

	if(!GroupsObject.IsEmpty())
	{
		RequestJson->SetArrayField(TEXT("requiredGroups"), GroupsObject);
	}

	RequestJson->SetStringField(TEXT("description"), ServiceProvider->GetService<FChangelistService>()->GetCLDescription());

	FString BodyString;
	TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(RequestJson, JsonWriter);
	UE_LOG(LogSubmitToolDebug, Log, TEXT("Create Swarm request body:\n%s"), *BodyString);
	CreateSwarmRequest->SetContentAsString(BodyString);

	CreateSwarmRequest->OnProcessRequestComplete().BindLambda([OnComplete, this](FHttpRequestPtr Request, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			if(!bConnectedSuccessfully)
			{
				if(HttpResponse.IsValid())
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Unable to create swarm review. Connection error %d - %s."), HttpResponse->GetResponseCode(), *EHttpResponseCodes::GetDescription(static_cast<EHttpResponseCodes::Type>(HttpResponse->GetResponseCode())).ToString());
					UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unable to create swarm review. Connection error\nResponse: %s"), *HttpResponse->GetContentAsString());
				}
				else
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Unable to create swarm review. Connection error, no response."));
				}
				OnComplete.ExecuteIfBound(false, FString());
				return;
			}
			if(HttpResponse.IsValid())
			{
				UE_LOG(LogSubmitToolDebug, Log, TEXT("Create review Response: %s"), *HttpResponse->GetContentAsString());
				if(EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
				{
					TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(HttpResponse->GetContentAsString());
					TSharedPtr<FJsonObject> JsonObj;
					if(!FJsonSerializer::Deserialize(Reader, JsonObj))
					{
						UE_LOG(LogSubmitTool, Error, TEXT("Unable to deserialize swarm create response"));
						return;
					}

					FSwarmReview ReviewObj;
					if (FJsonObjectConverter::JsonObjectToUStruct<FSwarmReview>(JsonObj->GetObjectField(TEXT("review")).ToSharedRef(), &ReviewObj))
					{
						Review = MakeUnique<FSwarmReview>(ReviewObj);
					}
					FString ReviewId = JsonObj->GetObjectField(TEXT("review"))->GetStringField(TEXT("id"));
					FString ReviewURL = BuildReviewURL(ReviewId);
					OnComplete.ExecuteIfBound(true, ReviewURL);
				}
				else
				{
					UE_LOG(LogSubmitTool, Error, TEXT("Could not create a swarm review due to error %d - %s."), HttpResponse->GetResponseCode(), *EHttpResponseCodes::GetDescription(static_cast<EHttpResponseCodes::Type>(HttpResponse->GetResponseCode())).ToString());
					OnComplete.ExecuteIfBound(false, FString());
				}
			}
			else
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("Unable to create swarm review. Failed with unknown connection error"));
				OnComplete.ExecuteIfBound(false, FString());
			}
		});

	UE_LOG(LogSubmitToolP4, Log, TEXT("Creating swarm review"));
	CreateSwarmRequest->ProcessRequest();
}


void FSwarmService::UpdateReviewDescription(const TDelegate<void(bool)>& OnComplete, const FString& InDescription)
{
	if(!bCanConnect || !Review.IsValid())
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Tried to update swarm review but Swarm API is not available or there is no review for this CL"));
		OnComplete.ExecuteIfBound(false);
		return;
	}

	if(UpdateSwarmRequest.IsValid())
	{
		UpdateSwarmRequest->CancelRequest();
	}

	UpdateSwarmRequest = FHttpModule::Get().CreateRequest();
	UpdateSwarmRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	UpdateSwarmRequest->SetHeader(TEXT("Authorization"), *ServiceProvider->GetService<ISTSourceControlService>()->GetAuthTicket().ToString());

	FString URL = ReviewsURL() / FString::FromInt(Review->Id);
	UpdateSwarmRequest->SetURL(URL);
	UpdateSwarmRequest->SetVerb(TEXT("PATCH"));

	TSharedRef<FJsonObject> RequestJson = MakeShared<FJsonObject>();
	RequestJson->SetStringField(TEXT("description"), InDescription);

	FString BodyString;
	TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(RequestJson, JsonWriter);
	UE_LOG(LogSubmitToolDebug, Log, TEXT("Update Swarm request body:\n%s"), *BodyString);
	UpdateSwarmRequest->SetContentAsString(BodyString);

	UpdateSwarmRequest->OnProcessRequestComplete().BindLambda([OnComplete, this](FHttpRequestPtr Request, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			if(!bConnectedSuccessfully || !HttpResponse.IsValid())
			{
				if(HttpResponse.IsValid())
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Unable to update swarm review. Connection error %d - %s."), HttpResponse->GetResponseCode(), *EHttpResponseCodes::GetDescription(static_cast<EHttpResponseCodes::Type>(HttpResponse->GetResponseCode())).ToString());
					UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unable to update swarm review. Connection error %d\nResponse: %s"), HttpResponse->GetResponseCode(), *HttpResponse->GetContentAsString());
				}
				else
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Unable to update swarm review. Connection error, no response."));
				}

				OnComplete.ExecuteIfBound(false);
				return;
			}
			else
			{
				UE_LOG(LogSubmitToolDebug, Log, TEXT("Update review response: %s"), *HttpResponse->GetContentAsString());
				if(EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
				{
					UE_LOG(LogSubmitTool, Log, TEXT("Swarm description updated successfully"));
					OnComplete.ExecuteIfBound(true);
				}
				else
				{
					UE_LOG(LogSubmitTool, Error, TEXT("Could not update swarm description due to error %d - %s."), HttpResponse->GetResponseCode(), *EHttpResponseCodes::GetDescription(static_cast<EHttpResponseCodes::Type>(HttpResponse->GetResponseCode())).ToString());
					OnComplete.ExecuteIfBound(false);
				}
			}
		});

	UE_LOG(LogSubmitToolP4, Log, TEXT("Updating swarm review description"));
	UpdateSwarmRequest->ProcessRequest();

}

const FString& FSwarmService::GetSwarmURL()
{
	if(SwarmURL.IsEmpty())
	{
		ServiceProvider->GetService<ISTSourceControlService>()->RunCommand(TEXT("property"), { "-l", "-n", "P4.Swarm.URL" }, FOnSCCCommandComplete::CreateLambda(
			[this](bool bSuccess, const FSCCRecordSet& InResultValues, const FSourceControlResultInfo& InResultsInfo)
			{
				if(bSuccess && InResultValues.Num() > 0 && InResultValues[0].Contains(TEXT("value")))
				{
					SwarmURL = InResultValues[0][TEXT("value")];
				}

			})).Wait();
	}
	return SwarmURL;
}

FString FSwarmService::ReviewsURL() const
{
	return SwarmURL.IsEmpty() ? FString{} : SwarmURL / TEXT("api/v9/reviews");
}