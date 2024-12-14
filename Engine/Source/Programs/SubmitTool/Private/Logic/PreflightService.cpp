// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreflightService.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Docking/SDockTab.h"
#include "Internationalization/Regex.h"

#include "CommandLine/CmdLineParameters.h"
#include "Configuration/Configuration.h"
#include "Logic/Services/Interfaces/ISTSourceControlService.h"
#include "Logic/CredentialsService.h"
#include "Logic/TagService.h"
#include "Logging/SubmitToolLog.h"
#include "Models/ModelInterface.h"
#include "Parameters/SubmitToolParameters.h"

FPreflightService::FPreflightService(
	const FHordeParameters& InSettings,
	FModelInterface* InModelInterface,
	TSharedRef<FSubmitToolServiceProvider> InServiceProvider)
	:
	Definition(InSettings),
	ServiceProvider(InServiceProvider),
	ModelInterface(InModelInterface),
	State(EPreflightServiceState::Idle),
	LastErrorMessage(TEXT(""))
{
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FPreflightService::Tick));

	DeleteShelveCallback = FSourceControlOperationComplete::CreateRaw(this, &FPreflightService::OnDeleteShelveOperationComplete);
	CreateShelveCallback = FSourceControlOperationComplete::CreateRaw(this, &FPreflightService::OnCreateShelveOperationComplete);

	PreflightTag = ServiceProvider->GetService<FTagService>()->GetTag(TEXT("#preflight"));
}

FPreflightService::~FPreflightService()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	OnPreflightDataUpdated.Clear();
}

bool FPreflightService::Tick(float DeltaTime)
{
	TSharedPtr<FChangelistService> ChangelistService = ServiceProvider->GetService<FChangelistService>();
	switch(State)
	{
		//////////////////////////////////////////////////
		case EPreflightServiceState::Idle:
			// Do nothing, wait for someone to press the "Start" preflight button
			break;

			//////////////////////////////////////////////////
		case EPreflightServiceState::RequestDeleteShelve:

			if(ChangelistService->GetFilesInCL().Num() == 0)
			{
				LastErrorMessage = TEXT("Missing local files!  We can't update the shelved files!");
				State = EPreflightServiceState::Error;
			}
			else if(ChangelistService->HasShelvedFiles())
			{
				EDialogFactoryResult DialogResult = static_cast<EDialogFactoryResult>(ShowRecreateShelveDialog());
				if(DialogResult == EDialogFactoryResult::FirstButton)
				{
					UE_LOG(LogSubmitTool, Log, TEXT("Preflight: Deleting shelved files"));
					State = EPreflightServiceState::WaitingForDeleteShelve;
					bCheckShelveInstead = false;
					ChangelistService->DeleteShelvedFiles(DeleteShelveCallback);
				}
				else if(DialogResult == EDialogFactoryResult::SecondButton)
				{
					UE_LOG(LogSubmitTool, Log, TEXT("Preflight: Using existing shelved files"));
					bCheckShelveInstead = true;
					State = EPreflightServiceState::StartPreflight;
				}
				else
				{
					UE_LOG(LogSubmitTool, Log, TEXT("Preflight: Request cancelled"));
					State = EPreflightServiceState::Idle;
				}
			}
			else
			{
				State = EPreflightServiceState::RequestCreateShelve;
			}
			break;

			//////////////////////////////////////////////////
		case EPreflightServiceState::WaitingForDeleteShelve:
			// Do nothing, wait for the delete shelve callback
			break;

			//////////////////////////////////////////////////
		case EPreflightServiceState::RequestCreateShelve:
			UE_LOG(LogSubmitTool, Log, TEXT("Preflight: Creating shelved files..."));
			State = EPreflightServiceState::WaitingForCreateShelve;
			ChangelistService->CreateShelvedFiles(CreateShelveCallback);
			break;

			//////////////////////////////////////////////////
		case EPreflightServiceState::WaitingForCreateShelve:
			// Do nothing, wait for the create shelve callback
			break;

			//////////////////////////////////////////////////
		case EPreflightServiceState::StartPreflight:
			StartPreflight();
			break;

			//////////////////////////////////////////////////
		case EPreflightServiceState::Error:
			UE_LOG(LogSubmitTool, Error, TEXT("Preflight: \"%s\""), *LastErrorMessage);
			State = EPreflightServiceState::Idle;
			break;
	}

	return true;
}

TMap<FString, FStringFormatArg> FPreflightService::GetFormatParameters() const
{
	TMap<FString, FStringFormatArg> FormatMap =
	{
		{ TEXT("URL"), Definition.HordeServerAddress },
		{ TEXT("CLID"), ServiceProvider->GetService<FChangelistService>()->GetCLID()}
	};

	FormatMap.Add(TEXT("Stream"), StreamName);
	FormatMap.Add(TEXT("Template"), FString());
	FormatMap.Add(TEXT("AdditionalTasks"), FString());

	FPreflightTemplateDefinition Template;
	if(SelectPreflightTemplate(Template))
	{
		FormatMap[TEXT("Template")] = Template.Template;
		FormatMap[TEXT("AdditionalTasks")] = GetAdditionalTasksString(Template);
	}
	else
	{
		FormatMap[TEXT("Template")] = Definition.DefaultPreflightTemplate;
	}

	return FormatMap;
}

void FPreflightService::RequestPreflight()
{
	if(State == EPreflightServiceState::Idle)
	{
		if(!ServiceProvider->GetService<ISTSourceControlService>()->GetClientStreams().IsEmpty())
		{
			UE_LOG(LogSubmitTool, Log, TEXT("Preflight: Requesting..."));
			State = EPreflightServiceState::RequestDeleteShelve;
		}
		else
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Couldn't retrieve stream name in this p4 client. Submit tool can't start a preflight, see previous errors."));
		}
	}
	else
	{
		// Do nothing, we're already busy trying to start a preflight
	}
}

void FPreflightService::QueueFetch(bool bRequeue, float InSeconds)
{
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, bRequeue](float DeltaTime) { FetchPreflightInfo(bRequeue); return false; }), InSeconds);
}

void FPreflightService::Requeue()
{
	float WaitTime = Definition.FetchPreflightEachSeconds;

	if(HordePreflights.IsValid())
	{
		for(const FPreflightData& PFData : HordePreflights->PreflightList)
		{
			if(PFData.CachedResults.State != EPreflightState::Completed)
			{
				WaitTime = Definition.FetchPreflightEachSecondsWhenInProgress;
				break;
			}
		}
	}

	for(const TPair<FString, FPreflightData>& Pair : UnlinkedHordePreflights)
	{
		if(Pair.Value.CachedResults.State != EPreflightState::Completed)
		{
			WaitTime = Definition.FetchPreflightEachSecondsWhenInProgress;
			break;
		}
	}

	QueueFetch(true, WaitTime);
}

void FPreflightService::FetchPreflightInfo(bool bRequeue, const FString& InOAuthToken)
{
	TSharedPtr<FCredentialsService> CredentialsService = ServiceProvider->GetService<FCredentialsService>();
	if(Definition.HordeServerAddress.IsEmpty() || !CredentialsService->IsOIDCTokenEnabled())
	{
		return;
	}

	TSharedPtr<FChangelistService> ChangelistService = ServiceProvider->GetService<FChangelistService>();

	// Don't bother with the default changelist
	if(ChangelistService->GetCLID() == TEXT("default"))
	{
		if(bRequeue)
		{
			QueueFetch(bRequeue, Definition.FetchPreflightEachSeconds);
		}

		return;
	}

	const FString& OIDCToken = CredentialsService->IsTokenReady() ? CredentialsService->GetToken() : InOAuthToken;

	if(!OIDCToken.IsEmpty())
	{
		if(!LinkedPFRequest.IsValid())
		{
			LinkedPFRequest = FHttpModule::Get().CreateRequest();

			FString FetchPreflightUrl = FString::Format(*Definition.FindPreflightURLFormat, GetFormatParameters());
			LinkedPFRequest->SetURL(FetchPreflightUrl);
			LinkedPFRequest->SetVerb(TEXT("GET"));
		}
		else if(LinkedPFRequest->GetStatus() == EHttpRequestStatus::Processing)
		{
			// if it's still Processing, do not try to request again.
			return;
		}

		// ensure the token is the most up to date
		LinkedPFRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("bearer %s"), *OIDCToken));

		if(!LinkedPFRequest->OnProcessRequestComplete().IsBound())
		{
			LinkedPFRequest->OnProcessRequestComplete().Unbind();
		}

		LinkedPFRequest->OnProcessRequestComplete().BindLambda([this, bRequeue, &OIDCToken](FHttpRequestPtr Request, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
			{
				if(!bConnectedSuccessfully)
				{
					if(HttpResponse.IsValid())
					{
						UE_LOG(LogSubmitTool, Warning, TEXT("Unable to connect to horde. Connection error %d"), HttpResponse->GetResponseCode());
						UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unable to connect to horde. Connection error\nResponse: %s"), *HttpResponse->GetContentAsString());
					}
					else
					{
						UE_LOG(LogSubmitTool, Warning, TEXT("Unable to connect to horde. Connection error, no response."));
					}
					return;
				}

				if(HttpResponse.IsValid())
				{
					//UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Fetch Preflight Response: %s"), *HttpResponse->GetContentAsString());
					if(EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
					{
						TUniquePtr<FPreflightList> NewHordePreflights = MakeUnique<FPreflightList>();
						FJsonObjectConverter::JsonObjectStringToUStruct<FPreflightList>(FString::Printf(TEXT("{\"PreflightList\" : %s}"), *HttpResponse->GetContentAsString()), NewHordePreflights.Get());
						NewHordePreflights->Initialize();

						if(PreflightTag != nullptr && !Definition.FindSinglePreflightURLFormat.IsEmpty())
						{
							for(FString PreflightId : PreflightTag->GetValues())
							{
								if(PreflightId.Equals(TEXT("skip")) || PreflightId.Equals(TEXT("none")))
								{
									continue;
								}

								if(PreflightId.Contains(TEXT("/")))
								{
									int32 SlashIdx;
									PreflightId.FindLastChar(TCHAR('/'), SlashIdx);
									PreflightId = PreflightId.RightChop(SlashIdx + 1);
								}

								PreflightId.TrimStartAndEndInline();

								FRegexPattern Pattern = FRegexPattern(TEXT("(?:[0-9]|[a-f]){24}"), ERegexPatternFlags::CaseInsensitive);
								FRegexMatcher regex = FRegexMatcher(Pattern, PreflightId);
								bool match = regex.FindNext();
								if(match)
								{
									const FPreflightData* FoundData = NewHordePreflights->PreflightList.FindByPredicate([&PreflightId](const FPreflightData& InData) { return InData.ID == PreflightId; });
									if(FoundData == nullptr)
									{
										FetchUnlinkedPreflight(PreflightId, bRequeue, OIDCToken);
									}
								}
							}
						}

						if(!HordePreflights.IsValid() || *NewHordePreflights != *HordePreflights)
						{
							UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Newer Preflight information received"));

							// Only log when there's a different number of preflights
							if(!HordePreflights.IsValid() || HordePreflights->PreflightList.Num() != NewHordePreflights->PreflightList.Num())
							{
								UE_LOG(LogSubmitTool, Log, TEXT("Retrieved %d preflights for CL %s"), NewHordePreflights->PreflightList.Num(), *ServiceProvider->GetService<FChangelistService>()->GetCLID());
							}

							HordePreflights = MoveTemp(NewHordePreflights);

							if(PreflightTag != nullptr)
							{
								bool bCLDescriptionModified = false;

								if(HordePreflights->PreflightList.Num() != 0)
								{
									FString CurrentTagValue = PreflightTag->GetValuesText();

									if(!bStopAskingTagUpdate && !CurrentTagValue.Contains(HordePreflights->PreflightList[0].ID))
									{
										EDialogFactoryResult Result = ShowUpdatePreflightTagDialog();
										if(Result == EDialogFactoryResult::FirstButton)
										{
											// Set the latest one as the tag value
											ModelInterface->SetTagValues(*PreflightTag, HordePreflights->PreflightList[0].ID);
											bCLDescriptionModified = true;
										}
										else
										{
											bStopAskingTagUpdate = true;
										}
									}
								}

								if(bCLDescriptionModified)
								{
									ModelInterface->ValidateCLDescription();
								}
							}
						}
					}
					else
					{
						UE_LOG(LogSubmitTool, Warning, TEXT("Could not retrieve preflights, Http code %d."), HttpResponse->GetResponseCode());
						UE_LOG(LogSubmitToolDebug, Error, TEXT("Fetch preflight failed. Response %s"), *HttpResponse->GetContentAsString());
					}
				}
				else
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Unable to fetch preflights. Failed with code %d"), HttpResponse->GetResponseCode());
					UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unable to fetch preflights. Failed with code %d\nResponse: %s"), HttpResponse->GetResponseCode(), *HttpResponse->GetContentAsString());
				}

				if(ActiveUnlinkedRequests == 0)
				{
					if(OnPreflightDataUpdated.IsBound())
					{
						OnPreflightDataUpdated.Broadcast(HordePreflights, UnlinkedHordePreflights);
					}

					if(bRequeue)
					{
						Requeue();
					}
				}
			});

		FTimespan TimeSinceLast = FDateTime::UtcNow() - LastRequest;
		if(bRequeue || TimeSinceLast.GetTotalSeconds() > 3)
		{
			LastRequest = FDateTime::UtcNow();
			UE_LOG(LogSubmitToolDebug, Log, TEXT("Fetching preflights for CL %s. URL: %s"), *ChangelistService->GetCLID(), *LinkedPFRequest->GetURL())
			LinkedPFRequest->ProcessRequest();
		}
	}
	else
	{
		CredentialsService->QueueWorkForToken([this, bRequeue](const FString& InToken)
			{
				if(!InToken.IsEmpty())
				{
					FetchPreflightInfo(bRequeue, InToken);
				}
				else
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Couldn't obtain OAuth token login, communication with Horde is not possible."));
				}
			});
	}
}

void FPreflightService::FetchUnlinkedPreflight(const FString& InPreflightId, bool bRequeue, const FString& InOAuthToken)
{
	if(InOAuthToken.IsEmpty())
	{
		return;
	}

	FHttpRequestPtr& UnlinkedPFRequest = UnlinkedPFRequests.FindOrAdd(InPreflightId);

	if(!UnlinkedPFRequest.IsValid())
	{
		UnlinkedPFRequest = FHttpModule::Get().CreateRequest();

		FStringFormatNamedArguments ReplaceStringArgs = GetFormatParameters();
		ReplaceStringArgs.Add(TEXT("PreflightId"), InPreflightId);


		FString FetchPreflightUrl = FString::Format(*Definition.FindSinglePreflightURLFormat, ReplaceStringArgs);
		UnlinkedPFRequest->SetURL(FetchPreflightUrl);
		UnlinkedPFRequest->SetVerb(TEXT("GET"));
	}
	else if(UnlinkedPFRequest->GetStatus() == EHttpRequestStatus::Processing)
	{
		// if it's still Processing, do not try to request again.
		return;
	}

	UnlinkedPFRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("bearer %s"), *InOAuthToken));

	if(!UnlinkedPFRequest->OnProcessRequestComplete().IsBound())
	{
		UnlinkedPFRequest->OnProcessRequestComplete().Unbind();
	}

	ActiveUnlinkedRequests++;
	UnlinkedPFRequest->OnProcessRequestComplete().BindLambda([this, bRequeue, &InOAuthToken, InPreflightId](FHttpRequestPtr Request, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			ActiveUnlinkedRequests--;
			if(!bConnectedSuccessfully)
			{
				if(HttpResponse.IsValid())
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Unable to connect to horde. Connection error %d"), HttpResponse->GetResponseCode());
					UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unable to connect to horde. Connection error\nResponse: %s"), *HttpResponse->GetContentAsString());
				}
				else
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Unable to connect to horde. Connection error, no response."));
				}
				return;
			}

			if(HttpResponse.IsValid())
			{
				UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Fetch Single Preflight Response: %s"), *HttpResponse->GetContentAsString());
				if(EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
				{
					FPreflightData ReceivedPreflightInfo;
					FJsonObjectConverter::JsonObjectStringToUStruct<FPreflightData>(*HttpResponse->GetContentAsString(), &ReceivedPreflightInfo);
					ReceivedPreflightInfo.RecalculateCachedResults();

					if(!UnlinkedHordePreflights.Contains(InPreflightId) || UnlinkedHordePreflights[InPreflightId] != ReceivedPreflightInfo)
					{
						UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Newer %s Preflight information received"), *InPreflightId);

						// Only log when the preflight is new
						if(!UnlinkedHordePreflights.Contains(InPreflightId))
						{
							UE_LOG(LogSubmitTool, Log, TEXT("Retrieved information from preflight %s"), *InPreflightId);
							UnlinkedHordePreflights.Add(InPreflightId, ReceivedPreflightInfo);
						}
						else
						{
							UnlinkedHordePreflights[InPreflightId] = MoveTemp(ReceivedPreflightInfo);
						}

					}
				}
				else
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("Could not retrieve preflights, Http code %d."), HttpResponse->GetResponseCode());
					UE_LOG(LogSubmitToolDebug, Error, TEXT("Fetch preflight failed. Response %s"), *HttpResponse->GetContentAsString());
				}
			}
			else
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("Unable to fetch preflights. Failed with code %d"), HttpResponse->GetResponseCode());
				UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unable to fetch preflights. Failed with code %d\nResponse: %s"), HttpResponse->GetResponseCode(), *HttpResponse->GetContentAsString());
			}

			if(ActiveUnlinkedRequests == 0)
			{
				if(OnPreflightDataUpdated.IsBound())
				{
					OnPreflightDataUpdated.Broadcast(HordePreflights, UnlinkedHordePreflights);
				}

				if(bRequeue)
				{
					Requeue();
				}
			}
		});

	UnlinkedPFRequest->ProcessRequest();
}


void FPreflightService::StartPreflight()
{
	TSharedPtr<ISTSourceControlService> SCCService = ServiceProvider->GetService<ISTSourceControlService>();
	StreamName = SCCService->GetRootStreamName();

	const TArray<FString>& ShelvedPaths = ServiceProvider->GetService<FChangelistService>()->GetShelvedFilesDepotPaths(true);

	if (ShelvedPaths.IsEmpty())
	{
		LastErrorMessage = TEXT("Shelve is empty or it couldn't be retrieved from p4, can't request preflight");
		State = EPreflightServiceState::Error;
		return;
	}

	FString CommonPath = ShelvedPaths[0];
	const FString& LastPath = ShelvedPaths.Last();

	for(size_t i = 0; i < FMath::Min(CommonPath.Len(), LastPath.Len()); ++i)
	{
		if(CommonPath[i] != LastPath[i])
		{
			CommonPath = CommonPath.Left(i);
			break;
		}
	}

	if(!CommonPath.Equals(TEXT("//")))
	{
		int32 NextSlash = CommonPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 2);
		const FString Depot = CommonPath.Mid(2, NextSlash - 2);
		size_t StreamDepth = SCCService->GetDepotStreamLength(Depot);

		for(size_t i = 0; i < StreamDepth; ++i)
		{
			NextSlash = CommonPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, NextSlash+1);
		}

		if(NextSlash != -1)
		{
			const FSCCStream* FoundStream = SCCService->GetSCCStream(CommonPath.Left(NextSlash));

			if(FoundStream != nullptr)
			{
				StreamName = FoundStream->Name;
			}
		}
	}


	FString StartPreflightUrl = FString::Format(*Definition.StartPreflightURLFormat, GetFormatParameters());

	// If for some reason, our preflight settings are missing, this will be empty, let's not popup a browser with nothing in it
	if(!StartPreflightUrl.IsEmpty())
	{
		UE_LOG(LogSubmitTool, Log, TEXT("Preflight: Starting preflight with URL: \"%s\""), *StartPreflightUrl);

		FPlatformProcess::LaunchURL(*StartPreflightUrl, nullptr, nullptr);
		State = EPreflightServiceState::Idle;

		// Do a Fetch in 10 and 30 s to try and capture the triggered preflight
		QueueFetch(false, 10.f);
		QueueFetch(false, 30.f);
	}
	else
	{
		LastErrorMessage = TEXT("Missing INI preflight settings");
		State = EPreflightServiceState::Error;
	}
}

void FPreflightService::OnDeleteShelveOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result)
{
	if(State == EPreflightServiceState::WaitingForDeleteShelve)
	{
		if(Result == ECommandResult::Type::Succeeded)
		{
			State = EPreflightServiceState::RequestCreateShelve;
		}
		else
		{
			LastErrorMessage = TEXT("Unable to delete shelve for preflight");
			State = EPreflightServiceState::Error;
		}
	}
	else
	{
		LastErrorMessage = TEXT("Received delete shelve callback when not waiting for it");
		State = EPreflightServiceState::Error;
	}
}

void FPreflightService::OnCreateShelveOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result)
{
	if(State == EPreflightServiceState::WaitingForCreateShelve)
	{
		if(Result == ECommandResult::Type::Succeeded)
		{
			State = EPreflightServiceState::StartPreflight;
		}
		else
		{
			LastErrorMessage = TEXT("Unable to create shelve for preflight");
			State = EPreflightServiceState::Error;
		}
	}
	else
	{
		LastErrorMessage = TEXT("Received create shelve callback when not waiting for it");
		State = EPreflightServiceState::Error;
	}
}

bool FPreflightService::SelectPreflightTemplate(FPreflightTemplateDefinition& OutTemplate) const
{
	bool bFoundTemplate = false;
	TSharedPtr<FChangelistService> ChangelistService = ServiceProvider->GetService<FChangelistService>();

	// Get the list of files in the changelist
	const TArray<FString>& FilesInCl = bCheckShelveInstead ? ChangelistService->GetShelvedFilesDepotPaths() : ChangelistService->GetFilesDepotPaths();

	// Loop through each definition to see if the files are in the path then check extension
	for(const FPreflightTemplateDefinition& Def : Definition.Definitions)
	{
		FString RegexPat = FConfiguration::Substitute(Def.RegexPath);
		FRegexPattern Pattern = FRegexPattern(RegexPat, ERegexPatternFlags::CaseInsensitive);
		for(const FString& File : FilesInCl)
		{
			FRegexMatcher regex = FRegexMatcher(Pattern, File);
			if(regex.FindNext())
			{
				OutTemplate = Def;
				return true;
			}
		}
	}

	return false;
}

FString FPreflightService::GetAdditionalTasksString(const FPreflightTemplateDefinition& InTemplate) const
{
	TStringBuilder<256> AdditionalTaskStrBuilder;
	const FString BaseString = TEXT("&id-additional-tasks.");
	const FString EndString = TEXT("=true");
	TSharedPtr<FChangelistService> ChangelistService = ServiceProvider->GetService<FChangelistService>();

	const TArray<FString>& FilesInCl = bCheckShelveInstead ? ChangelistService->GetShelvedFilesDepotPaths() : ChangelistService->GetFilesDepotPaths();
	for(const FPreflightAdditionalTask& AdditionalTask : InTemplate.AdditionalTasks)
	{
		FString RegexPat = FConfiguration::Substitute(AdditionalTask.RegexPath);
		FRegexPattern Pattern = FRegexPattern(RegexPat, ERegexPatternFlags::CaseInsensitive);
		for(const FString& File : FilesInCl)
		{
			FRegexMatcher regex = FRegexMatcher(Pattern, File);
			if(regex.FindNext())
			{
				AdditionalTaskStrBuilder.Append(BaseString);
				AdditionalTaskStrBuilder.Append(AdditionalTask.TaskId);
				AdditionalTaskStrBuilder.Append(EndString);
			}
		}
	}

	return AdditionalTaskStrBuilder.ToString();
}

EDialogFactoryResult FPreflightService::ShowRecreateShelveDialog() const
{
	if(ModelInterface->GetMainTab().IsValid() && ModelInterface->GetMainTab().Pin()->GetParentWindow().IsValid())
	{
		ModelInterface->GetMainTab().Pin()->GetParentWindow()->DrawAttention(FWindowDrawAttentionParameters());
	}

	const FText TextTitle = FText::FromString(FString::Printf(TEXT("Preflight CL %s: Recreate shelved files?"), *ServiceProvider->GetService<FChangelistService>()->GetCLID()));
	const FText TextDescription = FText::FromString(TEXT("There are already shelved files in this changelist.\n\nDo you want to delete and recreate your shelf from the latest changes in your local files for use in the preflight?"));

	return FDialogFactory::ShowDialog(TextTitle, TextDescription, TArray<FString>{ TEXT("Re-shelve files"), TEXT("Use existing Shelve"), TEXT("Cancel") });
}

EDialogFactoryResult FPreflightService::ShowUpdatePreflightTagDialog() const
{
	if(ModelInterface->GetMainTab().IsValid() && ModelInterface->GetMainTab().Pin()->GetParentWindow().IsValid())
	{
		ModelInterface->GetMainTab().Pin()->GetParentWindow()->DrawAttention(FWindowDrawAttentionParameters());
	}

	const FText TextTitle = FText::FromString(FString::Printf(TEXT("Preflight CL %s: Newer preflight available"), *ServiceProvider->GetService<FChangelistService>()->GetCLID()));
	const FText TextDescription = FText::FromString(FString::Printf(TEXT("There is a newer preflight for this changelist:\n<a id=\"browser\" style=\"Hyperlink\" href=\"%sjob/%s\">%s - %s</>\n\nDo you want to update the #preflight tag?"), *Definition.HordeServerAddress, *HordePreflights->PreflightList[0].ID, *HordePreflights->PreflightList[0].Name, *HordePreflights->PreflightList[0].ID));

	return FDialogFactory::ShowDialog(TextTitle, TextDescription, TArray<FString>{ TEXT("Update Tag Value"), TEXT("Cancel") });
}
