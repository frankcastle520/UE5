// Copyright Epic Games, Inc. All Rights Reserved.

#include "JiraService.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Logic/DialogFactory.h"
#include "Logging/SubmitToolLog.h"
#include "SubmitToolUtils.h"
#include "Logic/TagService.h"
#include "Logic/PreflightService.h"
#include "Logic/Services/SubmitToolServiceProvider.h"
#include "Logic/Services/Interfaces/ISTSourceControlService.h"
#include "Logic/CredentialsService.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Json.h"
#include "Modules/ModuleManager.h"
#include "Parameters/SubmitToolParameters.h"
#include "JsonObjectConverter.h"
#include "Configuration/Configuration.h"


FJiraService::FJiraService(const FJiraParameters& InJiraSettings, const int32 InMaxResults, TSharedRef<FSubmitToolServiceProvider> InServiceProvider) :
	Definition(InJiraSettings),
	MaxResults(InMaxResults),
	ServiceProvider(InServiceProvider)
{
	if(!Definition.ServerAddress.IsEmpty())
	{
		FetchJiraTickets(false);
		LoadJiraIssues();
	}
}

FJiraService::~FJiraService()
{
	if(JiraRequest.IsValid())
	{
		JiraRequest->CancelRequest();
	}
	OnJiraIssuesRetrievedCallback.Unbind();
}

bool FJiraService::FetchJiraTickets(bool InForce)
{
	TSharedPtr<FCredentialsService> CredentialsService = ServiceProvider->GetService<FCredentialsService>();
	if(CredentialsService->HasCredentials() && !Definition.ServerAddress.IsEmpty())
	{
		if (InForce || (JiraIssues.Num() == 0 && CredentialsService->AreCredentialsValid()))
		{
			QueryIssues();
			return true;
		}
	}

	return false;
}

void FJiraService::Reset()
{
	JiraIssues.Reset();
}

void FJiraService::QueryIssues()
{
	if(JiraRequest.IsValid())
	{
		return;
	}

	FHttpModule& HttpModule = FModuleManager::LoadModuleChecked<FHttpModule>("HTTP");
	JiraRequest = HttpModule.Get().CreateRequest();
	
	JiraRequest->OnProcessRequestComplete().BindRaw(this, &FJiraService::QueryIssues_HttpRequestComplete);

	TSharedPtr<FCredentialsService> CredentialsService = ServiceProvider->GetService<FCredentialsService>();
	FString Url = FString::Format(TEXT("https://{0}/rest/api/2/search?maxResults={1}&jql=assignee={2}"), { Definition.ServerAddress, this->MaxResults, CredentialsService->GetUsername()});

	JiraRequest->SetURL(Url);
	JiraRequest->SetHeader(TEXT("Authorization"), TEXT("Basic ") + CredentialsService->GetEncodedLoginString());
	JiraRequest->SetVerb(TEXT("GET"));
	bOngoingRequest = true;
	UE_LOG(LogSubmitToolDebug, Log, TEXT("Sending Jira request for tickets assigned to %s"), *CredentialsService->GetUsername())
	JiraRequest->ProcessRequest();
}

void FJiraService::QueryIssues_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	JiraRequest = nullptr;
	bOngoingRequest = false;
	TSharedPtr<FCredentialsService> CredentialsService = ServiceProvider->GetService<FCredentialsService>();

	if (!bSucceeded)
	{
		UE_LOG(LogSubmitToolDebug, Error, TEXT("Unable to retrieve JIRA issues at the moment."))
		return;
	}

	if (HttpResponse.IsValid())
	{
		if (EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
		{
			CredentialsService->SetCredentialsValid(true);
			UE_LOG(LogSubmitToolDebug, Log, TEXT("Successfully connected to Jira"));

			FString ResponseStr = HttpResponse->GetContentAsString();

			TSharedPtr<FJsonObject> RootJsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
			FJsonSerializer::Deserialize(Reader, RootJsonObject);

			if (RootJsonObject.IsValid())
			{
				int32 Total;
				if (RootJsonObject->TryGetNumberField(TEXT("total"), Total))
				{
					this->TotalIssues = Total;
				}

				const TArray<TSharedPtr<FJsonValue>>* Issues;
				if (RootJsonObject->TryGetArrayField(TEXT("issues"), Issues))
				{
					UE_LOG(LogSubmitToolDebug, Log, TEXT("Retrieved %d issues for username %s"), Issues->Num(), *CredentialsService->GetUsername());

					JiraIssues.Reset();

					for (const TSharedPtr<FJsonValue>& ArrVal : *Issues)
					{
						if (!ArrVal.IsValid())
						{
							continue;
						}

						const TSharedPtr<FJsonObject>* IssueObject;
						if (ArrVal->TryGetObject(IssueObject))
						{
							FJiraIssue Issue;
							if(ParseJsonObject(IssueObject, Issue))
							{
								JiraIssues.Add(Issue.Key, Issue);
							}
						}
					}

					this->SaveJiraIssues();
				}
			}
		}
		else
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Jira Request failed with error code %d, please make sure you're logging with the right credentials. if your Okta password expired recently, make sure you log into JIRA via browser at least once."), HttpResponse->GetResponseCode());
			CredentialsService->SetCredentialsValid(false);
		}
	}

	OnJiraIssuesRetrievedCallback.ExecuteIfBound(EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()));
}

bool FJiraService::ParseJsonObject(const TSharedPtr<FJsonObject>* InJsonObject, FJiraIssue& OutJiraIssue) const
{
	if(InJsonObject->IsValid())
	{
		FString Key{ "" };
		FString Summary{ "" };
		FString Description{ "" };
		FString PriorityName{ "" };
		FString StatusName{ "" };
		FString IssueTypeName{ "" };

		InJsonObject->Get()->TryGetStringField(TEXT("key"), Key);

		const TSharedPtr<FJsonObject>* FieldsObject;
		if(InJsonObject->Get()->TryGetObjectField(TEXT("fields"), FieldsObject))
		{
			if(FieldsObject->IsValid())
			{
				FieldsObject->Get()->TryGetStringField(TEXT("description"), Description);
				FieldsObject->Get()->TryGetStringField(TEXT("summary"), Summary);

				const TSharedPtr<FJsonObject>* PriorityObject;
				if(FieldsObject->Get()->TryGetObjectField(TEXT("priority"), PriorityObject))
				{
					if(PriorityObject->IsValid())
					{
						PriorityObject->Get()->TryGetStringField(TEXT("name"), PriorityName);
					}
				}

				const TSharedPtr<FJsonObject>* StatusObject;
				if(FieldsObject->Get()->TryGetObjectField(TEXT("status"), StatusObject))
				{
					if(StatusObject->IsValid())
					{
						StatusObject->Get()->TryGetStringField(TEXT("name"), StatusName);
					}
				}

				const TSharedPtr<FJsonObject>* IssueTypeObject;
				if(FieldsObject->Get()->TryGetObjectField(TEXT("issuetype"), IssueTypeObject))
				{
					if(IssueTypeObject->IsValid())
					{
						IssueTypeObject->Get()->TryGetStringField(TEXT("name"), IssueTypeName);
					}
				}
			}
		}

		if(!Key.IsEmpty() && !this->JiraIssues.Contains(Key))
		{
			FString Link = Definition.ServerAddress + TEXT("/browse/") + Key;
			OutJiraIssue = FJiraIssue(Key, Summary, Link, Description, PriorityName, StatusName, IssueTypeName);
			return true;
		}
	}
	return false;
}

constexpr int JiraIssuesDatVersion = 1;
void FJiraService::SaveJiraIssues() const
{
	FArchive* File = IFileManager::Get().CreateFileWriter(*GetJiraIssuesFilepath(), EFileWrite::FILEWRITE_EvenIfReadOnly);

	if (File != nullptr)
	{
		int32 Version = JiraIssuesDatVersion;
		*File << Version;

		int32 Size = this->JiraIssues.Num();
		*File << Size;

		for (TPair<FString, FJiraIssue> Issue : this->JiraIssues)
		{
			FJiraIssue::StaticStruct()->SerializeBin(*File, &Issue.Value);
		}

		File->Close();
		delete File;
		File = nullptr;
	}
	else
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Could not create file '%s'."), *GetJiraIssuesFilepath());
	}
}

void FJiraService::LoadJiraIssues()
{
	// do not load the issues if there is no credentials
	if (!ServiceProvider->GetService<FCredentialsService>()->HasCredentials())
	{
		return;
	}

	if (IFileManager::Get().FileExists(*GetJiraIssuesFilepath()))
	{
		FArchive* File = IFileManager::Get().CreateFileReader(*GetJiraIssuesFilepath());

		if (File != nullptr)
		{
			this->JiraIssues.Reset();

			int32 Version;
			*File << Version;

			// Check Versions here
			if (Version != JiraIssuesDatVersion)
			{
				UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unexpected Jira Issues Version, aborting issues loading."));
				File->Close();
				delete File;
				File = nullptr;
				return;
			}

			int32 Size = 0;
			*File << Size;

			for (int32 Idx = 0; Idx < Size; Idx++)
			{
				FJiraIssue Issue;
				FJiraIssue::StaticStruct()->SerializeBin(*File, &Issue);

				if (!this->JiraIssues.Contains(Issue.Key))
				{
					this->JiraIssues.Add(Issue.Key, Issue);
				}
			}

			File->Close();
			delete File;
			File = nullptr;
		}
		else
		{
			UE_LOG(LogSubmitTool, Warning, TEXT("Could not read file '%s'."), *GetJiraIssuesFilepath());
		}
	}
	else
	{
		UE_LOG(LogSubmitToolDebug, Log, TEXT("File %s does not exists, no issues loaded"), *GetJiraIssuesFilepath())
	}
}

const FString FJiraService::GetJiraIssuesFilepath() const
{
	return FPaths::Combine(FSubmitToolUtils::GetLocalAppDataPath(), TEXT("SubmitTool"), TEXT("jira.issues.dat"));
}

void FJiraService::GetuserInfo()
{
	FHttpModule& HttpModule = FModuleManager::LoadModuleChecked<FHttpModule>("HTTP");
	FHttpRequestRef HttpRequest = HttpModule.Get().CreateRequest();

	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FJiraService::GetuserInfo_HttpRequestComplete);

	FString Url = FString::Format(TEXT("https://{0}/rest/api/2/myself}"), { Definition.ServerAddress });

	HttpRequest->SetURL(Url);

	HttpRequest->SetHeader(TEXT("Authorization"), TEXT("Basic ") + ServiceProvider->GetService<FCredentialsService>()->GetEncodedLoginString());
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->ProcessRequest();
}

void FJiraService::GetuserInfo_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!bSucceeded)
	{
		UE_LOG(LogSubmitToolDebug, Error, TEXT("Unable to retrieve JIRA issues at the moment."))
		return;
	}

	if (HttpResponse.IsValid())
	{
		if (EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
		{
			ServiceProvider->GetService<FCredentialsService>()->SetCredentialsValid(true);
			FString ResponseStr = HttpResponse->GetContentAsString();

			TSharedPtr<FJsonObject> RootJsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
			FJsonSerializer::Deserialize(Reader, RootJsonObject);

			if (RootJsonObject.IsValid())
			{
			}
		}
	}
}

void FJiraService::GetIssueAndCreateServiceDeskRequest(const FString& Key, const FString& Description, const FString& SwarmURL, const FString& InCurrentStream, const TMap<FString, TSharedPtr<FIntegrationOptionBase>>& InIntegrationOptions, const FOnBooleanValueChanged OnComplete)
{
	if(Key.IsEmpty() || Key.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		CreateServiceDeskRequest(TSharedPtr<FJsonObject>(), Description, SwarmURL, InCurrentStream, InIntegrationOptions, OnComplete);
		return;
	}

	UE_LOG(LogSubmitTool, Log, TEXT("Requesting Information for linked Jira issue %s"), *Key);
	// I set this up so that it gets information from the linked jira, but I think they changed their minds on what information is taken from the jira
	FHttpModule& HttpModule = FModuleManager::LoadModuleChecked<FHttpModule>("HTTP");
	FHttpRequestRef HttpRequest = HttpModule.Get().CreateRequest();

	HttpRequest->OnProcessRequestComplete().BindLambda([=, this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {FJiraService::GetIssueAndCreateServiceDeskRequest_HttpRequestComplete(HttpRequest, HttpResponse, bSucceeded, Description, SwarmURL, InCurrentStream, InIntegrationOptions, OnComplete); });
	
	FString Url = FString::Format(TEXT("https://{0}/rest/api/2/issue/{1}"), { Definition.ServerAddress, Key });
	HttpRequest->SetURL(Url);
	HttpRequest->SetHeader(TEXT("Authorization"), TEXT("Basic ") + ServiceProvider->GetService<FCredentialsService>()->GetEncodedLoginString());
	HttpRequest->SetVerb(TEXT("GET"));

	HttpRequest->ProcessRequest();
}

void FJiraService::GetIssueAndCreateServiceDeskRequest_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, const FString& Description, const FString& SwarmURL, const FString& InCurrentStream, const TMap<FString, TSharedPtr<FIntegrationOptionBase>>& InIntegrationOptions, const FOnBooleanValueChanged OnComplete)
{
	TSharedPtr<FJsonObject> RootJsonObject;
	if (!bSucceeded)
	{
		if(HttpResponse.IsValid())
		{
			UE_LOG(LogSubmitTool, Log, TEXT("Unable to retrieve Base JIRA issue information. Summary will be created with the current CL description instead. Failed with code %d"), HttpResponse->GetResponseCode());
			UE_LOG(LogSubmitToolDebug, Log, TEXT("Unable to retrieve JIRA issue information. Summary will be created with the current CL description instead. Failed with code %d\nResponse: %s"), HttpResponse->GetResponseCode(), *HttpResponse->GetContentAsString());
		}
		else
		{
			UE_LOG(LogSubmitTool, Warning, TEXT("Unable to retrieve Base JIRA issue information. Unknown failure"));
		}
	}
	else
	{
		if(HttpResponse.IsValid())
		{
			if(EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
			{
				FString ResponseStr = HttpResponse->GetContentAsString();
				UE_LOG(LogSubmitToolDebug, Log, TEXT("Obtained information from Jira Issue %s"), *ResponseStr);

				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
				FJsonSerializer::Deserialize(Reader, RootJsonObject);
			}
			else
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("Unable to retrieve Base JIRA issue information."));
				UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unable to retrieve Base JIRA issue information. Failed with code %d\nResponse: %s"), HttpResponse->GetResponseCode(), *HttpResponse->GetContentAsString());
			}
		}
	}

	// Call the function to actually create the service desk request with the required information
	CreateServiceDeskRequest(RootJsonObject, Description, SwarmURL, InCurrentStream, InIntegrationOptions, OnComplete);
}

void FJiraService::CreateServiceDeskRequest(TSharedPtr<FJsonObject> InBaseJiraJsonObject, const FString& Description, const FString& SwarmURL, const FString& InCurrentStream, const TMap<FString, TSharedPtr<FIntegrationOptionBase>>& InIntegrationOptions, const FOnBooleanValueChanged OnComplete)
{
	UE_LOG(LogSubmitTool, Log, TEXT("Requesting creation of Jira ServiceDesk ticket..."));
	FHttpModule& HttpModule = FModuleManager::LoadModuleChecked<FHttpModule>("HTTP");
	ServiceDeskRequest = HttpModule.Get().CreateRequest();

	ServiceDeskRequest->OnProcessRequestComplete().BindLambda([this, OnComplete](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) { CreateServiceDeskRequest_HttpRequestComplete(HttpRequest, HttpResponse, bSucceeded, OnComplete); });
	FString Url = FString::Format(TEXT("https://{0}/rest/servicedeskapi/request"), { Definition.ServerAddress });

	ServiceDeskRequest->SetURL(Url);
	ServiceDeskRequest->SetHeader(TEXT("Authorization"), FString::Format(TEXT("Basic {0}"), { Definition.ServiceDeskToken }));
	ServiceDeskRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ServiceDeskRequest->SetVerb(TEXT("POST"));
	
	TSharedPtr<FJsonObject> RequestJson = MakeShared<FJsonObject>();

	// These values should probably be put inside the .ini file
	RequestJson->SetNumberField(TEXT("serviceDeskId"), Definition.ServiceDeskID);
	RequestJson->SetNumberField(TEXT("requestTypeId"), Definition.RequestFormID);


	TSharedPtr<FJsonObject> RequestFieldValuesJson = MakeShared<FJsonObject>();

	if(InBaseJiraJsonObject.IsValid())
	{
		TSharedPtr<FJsonObject> BaseJiraFields = InBaseJiraJsonObject->GetObjectField(TEXT("fields"));
		RequestFieldValuesJson->SetStringField(TEXT("summary"), BaseJiraFields->GetStringField(TEXT("summary")));
	}
	else
	{
		FString Summary = Description.Left(50).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(""));
		RequestFieldValuesJson->SetStringField(TEXT("summary"), Summary);

	}

	RequestFieldValuesJson->SetStringField(TEXT("description"), Description);

	if(!SwarmURL.IsEmpty() && !Definition.SwarmUrlField.IsEmpty())
	{
		RequestFieldValuesJson->SetStringField(Definition.SwarmUrlField, SwarmURL);
	}

	if(!InCurrentStream.IsEmpty() && !Definition.StreamField.IsEmpty())
	{
		RequestFieldValuesJson->SetStringField(Definition.StreamField, InCurrentStream);
	}

	if(!Definition.PreflightField.IsEmpty())
	{		
		const FTag* PreflightTag = ServiceProvider->GetService<FTagService>()->GetTag(TEXT("#preflight"));
		if (PreflightTag != nullptr && PreflightTag->GetValues().Num() > 0)
		{
			FString PreflightTagValue = PreflightTag->GetValues()[0];

			if(!PreflightTagValue.IsEmpty())
			{
				if(!PreflightTagValue.Contains(TEXT("/job/")))
				{
					RequestFieldValuesJson->SetStringField(Definition.PreflightField, FString::Format(TEXT("{0}job/{1}"), { ServiceProvider->GetService<FPreflightService>()->GetHordeServerAddress(), PreflightTagValue }));
				}
				else
				{
					RequestFieldValuesJson->SetStringField(Definition.PreflightField, PreflightTagValue);
				}
			}
		}
	}

	FString Requestor = FConfiguration::Substitute(TEXT("$(USER)"));
	TSharedPtr<FUserData> LocalUserData = ServiceProvider->GetService<ISTSourceControlService>()->GetUserDataFromCache(Requestor);
	if (LocalUserData.IsValid())
	{
		Requestor = LocalUserData.Get()->Email;
	}

	RequestFieldValuesJson->SetStringField(Definition.RequestorField, Requestor);

	for(const TPair<FString, TSharedPtr<FIntegrationOptionBase>>& Pair : InIntegrationOptions)
	{
		FString Value;
		if(!Pair.Value->GetJiraValue(Value))
		{
			continue;
		}

		TSharedPtr<FJsonObject> JiraObject = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> JiraArrayObject;
		JiraObject->SetStringField(TEXT("value"), Value);

		switch(Pair.Value->FieldDefinition.JiraType)
		{
		case EJiraFieldType::Object:
			RequestFieldValuesJson->SetObjectField(Pair.Value->FieldDefinition.Id, JiraObject);
			break;

		case EJiraFieldType::Array:
			const TArray<TSharedPtr<FJsonValue>>* ExistingJiraArrayObjectPtr;
			if(RequestFieldValuesJson->TryGetArrayField(Pair.Value->FieldDefinition.Id, ExistingJiraArrayObjectPtr))
			{
				JiraArrayObject = *ExistingJiraArrayObjectPtr;
			}

			JiraArrayObject.Add(MakeShared<FJsonValueObject>(JiraObject));
			RequestFieldValuesJson->SetArrayField(Pair.Value->FieldDefinition.Id, JiraArrayObject);
			break;

		case EJiraFieldType::String:
			RequestFieldValuesJson->SetStringField(Pair.Value->FieldDefinition.Id, Value);
			break;
		}
	}

	// JW: I think they decided that any jira tickets referenced in the changelist should be added as additional URLs
	RequestJson->SetObjectField(TEXT("requestFieldValues"), RequestFieldValuesJson);

	FString BodyString;
	TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(RequestJson.ToSharedRef(), JsonWriter);

	UE_LOG(LogSubmitToolDebug, Log, TEXT("Create Jira request body:\n%s"), *BodyString);

	ServiceDeskRequest->SetContentAsString(BodyString);

	ServiceDeskRequest->ProcessRequest();
}

void FJiraService::CreateServiceDeskRequest_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, const FOnBooleanValueChanged OnComplete)
{
	if(!bSucceeded)
	{
		if(HttpResponse.IsValid())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Unable to create JIRA service desk. Failed with code %d\nResponse: %s"), HttpResponse->GetResponseCode(), *HttpResponse->GetContentAsString());
		}
		else
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Unable to create JIRA service desk. Unknown failure"));
		}
		OnComplete.ExecuteIfBound(false);
		return;
	}

	if (HttpResponse.IsValid())
	{
		if (EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
		{
			TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(HttpResponse->GetContentAsString());
			TSharedPtr<FJsonObject> JsonObj;
			if(!FJsonSerializer::Deserialize(Reader, JsonObj))
			{
				UE_LOG(LogSubmitTool, Error, TEXT("Unable to deserialize swarm create response"));
				return;
			}

			FString CreatedTicketId = JsonObj->GetStringField(TEXT("issueKey"));
			FString WebLink;
			const TSharedPtr<FJsonObject>* Links;
			if(JsonObj->TryGetObjectField(TEXT("_links"), Links))
			{
				WebLink = Links->Get()->GetStringField(TEXT("web"));
			}

			// If the service desk request was created successfully
			UE_LOG(LogSubmitTool, Log, TEXT("Jira service desk ticket creation was successful: %s %s"), *CreatedTicketId, *WebLink);
			UE_LOG(LogSubmitToolDebug, Log, TEXT("Jira service desk ticket creation was successful\n%s"), *HttpResponse->GetContentAsString());
			FDialogFactory::ShowInformationDialog(FText::FromString(TEXT("Integration Request Successful")), FText::FromString(TEXT("The Integration has sucessfully been requested!")));			
			OnComplete.ExecuteIfBound(true);
		}
		else
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Unable to create JIRA service desk. Failed with code %d\nResponse: %s"), HttpResponse->GetResponseCode(), *HttpResponse->GetContentAsString());
			FDialogFactory::ShowInformationDialog(FText::FromString(TEXT("Integration Request FAILED")), FText::FromString(TEXT("Unable to create JIRA service desk ticket.\nPlease check the logs for more info.")));
			OnComplete.ExecuteIfBound(false);
		}
	}
}
