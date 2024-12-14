// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelInterface.h"
#include "SubmitToolUserPrefs.h"
#include "Logic/JiraService.h"
#include "Logic/PreflightService.h"
#include "Framework/Application/SlateApplication.h"
#include "Version/AppVersion.h"
#include "Widgets/Docking/SDockTab.h"
#include "Containers/Ticker.h"
#include "Configuration/Configuration.h"

#include "Logic/Validators/PreflightValidator.h"
#include "Logic/Validators/TagValidator.h"
#include "Logic/Validators/UBTValidator.h"
#include "Logic/Validators/ValidatorBase.h"
#include "Logic/Validators/ValidatorRunExecutable.h"
#include "Logic/Validators/CrossChangelistValidator.h"
#include "Logic/PreSubmitOperations/VirtualizationOperation.h"
#include "Logic/Services/CacheDataService.h"
#include "Logic/Services/SourceControl/SubmitToolPerforce.h"

ESubmitToolAppState FModelInterface::SubmitToolState = ESubmitToolAppState::Initializing;

FModelInterface::FModelInterface(const FSubmitToolParameters& InParameters) :
	Parameters(InParameters)
{
	// initialize call backs
	CLReadyCallback = FOnChangeListReadyDelegate::CreateRaw(this, &FModelInterface::OnChangelistReady);
	CLRefreshCallback = FOnChangelistRefreshDelegate::CreateRaw(this, &FModelInterface::OnChangelistRefresh);
	SubmitFinishedCallback = FSourceControlOperationComplete::CreateRaw(this, &FModelInterface::OnSubmitOperationComplete);
	DeleteShelveCallback = FSourceControlOperationComplete::CreateRaw(this, &FModelInterface::OnDeleteShelveOperationComplete);
	RevertUnchangedCallback = FSourceControlOperationComplete::CreateRaw(this, &FModelInterface::OnRevertUnchangedOperationComplete);

	ServiceProvider = MakeShared<FSubmitToolServiceProvider>();
	// Initialize services
	if(Parameters.GeneralParameters.CacheFile.IsEmpty())
	{
		ServiceProvider->RegisterService<ICacheDataService>(MakeShared<FNoOpCacheDataService>());
	}
	else
	{
		ServiceProvider->RegisterService<ICacheDataService>(MakeShared<FCacheDataService>(Parameters.GeneralParameters));
	}

	SourceControlService = MakeShared<FSubmitToolPerforce>(InParameters);
	ServiceProvider->RegisterService<ISTSourceControlService>(SourceControlService.ToSharedRef());
	ValidationService = MakeShared<FTasksService>(InParameters.Validators, TEXT("SubmitTool.StandAlone.Validator"));
	ServiceProvider->RegisterService<FTasksService>(ValidationService.ToSharedRef());
	PresubmitOperationsService = MakeShared<FTasksService>(InParameters.PresubmitOperations, TEXT("SubmitTool.StandAlone.PresubmitOperation"));
	ServiceProvider->RegisterService<FTasksService>(PresubmitOperationsService.ToSharedRef());
	CredentialsService = MakeShared<FCredentialsService>(InParameters.OAuthParameters);
	ServiceProvider->RegisterService<FCredentialsService>(CredentialsService.ToSharedRef());
	ChangelistService = MakeShared<FChangelistService>(InParameters.GeneralParameters, SourceControlService, CLReadyCallback, CLRefreshCallback);
	ServiceProvider->RegisterService<FChangelistService>(ChangelistService.ToSharedRef());
	P4LockdownService = MakeShared<FP4LockdownService>(InParameters.P4LockdownParameters, ServiceProvider.ToSharedRef());
	ServiceProvider->RegisterService<FP4LockdownService>(P4LockdownService.ToSharedRef());
	TagService = MakeShared<FTagService>(InParameters, ChangelistService);
	ServiceProvider->RegisterService<FTagService>(TagService.ToSharedRef());
	SwarmService = MakeShared<FSwarmService>(ServiceProvider.ToSharedRef());
	ServiceProvider->RegisterService<FSwarmService>(SwarmService.ToSharedRef());
	PreflightService = MakeShared<FPreflightService>(InParameters.HordeParameters, this, ServiceProvider.ToSharedRef());
	ServiceProvider->RegisterService<FPreflightService>(PreflightService.ToSharedRef());
	JiraService = MakeShared<FJiraService>(InParameters.JiraParameters, 256, ServiceProvider.ToSharedRef());
	ServiceProvider->RegisterService<FJiraService>(JiraService.ToSharedRef());
	FNIntegrationService = MakeShared<FIntegrationService>(InParameters.IntegrationParameters, ServiceProvider);
	ServiceProvider->RegisterService<FIntegrationService>(FNIntegrationService.ToSharedRef());
	UpdateService = MakeShared<FUpdateService>(InParameters.HordeParameters, InParameters.AutoUpdateParameters, ServiceProvider);
	ServiceProvider->RegisterService<FUpdateService>(UpdateService.ToSharedRef());

	ParseValidators();
	ParsePreSubmitOperations();

	OnValidationStateUpdatedHandle = ValidationService->OnTasksRunResultUpdated.Add(FOnTaskRunStateChanged::FDelegate::CreateLambda([this](bool bIsValid) {
		if(bIsValid)
		{
			bool bOptionalFailures = false;
			for(const TWeakPtr<const FValidatorBase>& Validator : ValidationService->GetTasks())
			{
				if(!Validator.Pin()->GetIsRunningOrQueued() && !Validator.Pin()->GetHasPassed())
				{
					bOptionalFailures = true;
				}
			}

			UE_LOG(LogSubmitTool, Log, TEXT("The required local validation has succeeded, you're ALLOWED TO SUBMIT."))
			if(ValidationService->GetIsAnyTaskRunning())
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("You still have optional validations running you might want to consider waiting for them to finish."))
			}
			if(bOptionalFailures)
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("You have optional validations that have failed, you can still proceed with the submission if you consider that these failures are not relevant. Please make sure this is the case."))
			}
		}
	}));
	
	OnSingleValidationFinishedHandle = ValidationService->OnSingleTaskFinished.AddLambda([this](const FValidatorBase& InTask) {
			ReevaluateSubmitToolTag();

			if(bPreflightQueued && CanLaunchPreflight())
			{
				bPreflightQueued = false;
				PreflightService->RequestPreflight();
			}
		});

	OnValidationFinishedHandle = ValidationService->OnTasksQueueFinished.Add(FOnTaskFinished::FDelegate::CreateLambda([this](bool bIsValid)
		{
			MainTab.Pin()->GetParentWindow()->DrawAttention(FWindowDrawAttentionParameters());
			
			if(bIsValid)
			{
				if (!bPreflightQueued && bSubmitOnSuccessfulValidation && !IsIntegrationRequired())
				{
					bool bAllSucceedIncludingOptional = true;
					for(const TWeakPtr<const FValidatorBase>& Validator : ValidationService->GetTasks())
					{
						if(Validator.IsValid() && !Validator.Pin()->GetHasPassed())
						{
							bAllSucceedIncludingOptional = false;
						}
					}


					if(bAllSucceedIncludingOptional)
					{
						UE_LOG(LogSubmitTool, Log, TEXT("User has opted in to automatically submit on validation successful. Proceeding with submission..."));
						StartSubmitProcess();
					}
					else
					{
						UE_LOG(LogSubmitTool, Warning, TEXT("User has opted in to automatically submit on validation successful but not all validations succeeded. Fix optional validation errors or submit manually if you want to bypass them."));
						FDialogFactory::ShowInformationDialog(FText::FromString(TEXT("Auto-Submit Cancelled")), FText::FromString(TEXT("Submit tool couldn't auto submit because there were optional validations that failed.\n\nFix these errors or manually submit if you are certain that you should ignore them.")));
					}
				}
			}

		}));

	OnPresubmitFinishedHandle = PresubmitOperationsService->OnTasksQueueFinished.Add(FOnTaskFinished::FDelegate::CreateRaw(this, &FModelInterface::OnPresubmitOperationsComplete));

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FModelInterface::Tick));
}

FModelInterface::~FModelInterface()
{
	PrepareSubmitCallBack.Clear();
	FileRefreshedCallback.Clear();
	ValidationService->OnTasksRunResultUpdated.Remove(OnValidationStateUpdatedHandle);
	ValidationService->OnTasksQueueFinished.Remove(OnValidationFinishedHandle);
	ValidationService->OnSingleTaskFinished.Remove(OnSingleValidationFinishedHandle);
	PresubmitOperationsService->OnTasksQueueFinished.Remove(OnPresubmitFinishedHandle);
}

void FModelInterface::Dispose() const
{
	ChangelistService->CancelP4Operations();

	if(GetInputEnabled())
	{
		ChangelistService->SendCLDescriptionToP4(EConcurrency::Synchronous);
	}

	for(const TPair<FString, TSharedPtr<FIntegrationOptionBase>>& IntegrationOption : ServiceProvider->GetService<FIntegrationService>()->GetIntegrationOptions())
	{
		FString Value;
		if(IntegrationOption.Value->GetJiraValue(Value) && !Value.IsEmpty())
		{
			ServiceProvider->GetService<ICacheDataService>()->SetIntegrationFieldValue(GetCLID(), IntegrationOption.Key, Value);
		}
	}

	ServiceProvider->GetService<ICacheDataService>()->SaveCacheToDisk();
}

void FModelInterface::ParseValidators() const
{
	TArray<TSharedRef<FValidatorBase>> Tasks;

	for(const TPair<FName, FString> DefinitionPair : Parameters.Validators)
	{
		FValidatorDefinition TaskDefinition;
		FStringOutputDevice Errors;
		FValidatorDefinition::StaticStruct()->ImportText(*DefinitionPair.Value, &TaskDefinition, nullptr, 0, &Errors, FValidatorDefinition::StaticStruct()->GetName());

		if(!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
			continue;
		}

		if(TaskDefinition.Type.TrimStartAndEnd().IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Task %s didn't have a Type."), *DefinitionPair.Key.ToString());
			continue;
		}

		if(TaskDefinition.Type.Equals(SubmitToolParseConstants::TagValidator, ESearchCase::IgnoreCase))
		{
			Tasks.Add(MakeShared<FTagValidator>(DefinitionPair.Key, Parameters, ServiceProvider.ToSharedRef(), DefinitionPair.Value));
		}
		else if(TaskDefinition.Type.Equals(SubmitToolParseConstants::UBTValidator, ESearchCase::IgnoreCase))
		{
			Tasks.Add(MakeShared<FUBTValidator>(DefinitionPair.Key, Parameters, ServiceProvider.ToSharedRef(), DefinitionPair.Value));
		}
		else if(TaskDefinition.Type.Equals(SubmitToolParseConstants::CustomValidator, ESearchCase::IgnoreCase))
		{
			Tasks.Add(MakeShared<FValidatorRunExecutable>(DefinitionPair.Key, Parameters, ServiceProvider.ToSharedRef(), DefinitionPair.Value));
		}
		else if(TaskDefinition.Type.Equals(SubmitToolParseConstants::CrossChangelistValidator, ESearchCase::IgnoreCase))
		{
			Tasks.Add(MakeShared<FCrossChangelistValidator>(DefinitionPair.Key, Parameters, ServiceProvider.ToSharedRef(), DefinitionPair.Value));
		}
		else if(TaskDefinition.Type.Equals(SubmitToolParseConstants::PreflightValidator, ESearchCase::IgnoreCase))
		{
			Tasks.Add(MakeShared<FPreflightValidator>(DefinitionPair.Key, Parameters, ServiceProvider.ToSharedRef(), DefinitionPair.Value));
		}
		else
		{
			UE_LOG(LogSubmitTool, Error, TEXT("[%s] is not a recognized validator type and has not been activated."), *DefinitionPair.Key.ToString());
		}
	}

	ValidationService->InitializeTasks(Tasks);
}

void FModelInterface::ParsePreSubmitOperations() const
{
	TArray<TSharedRef<FValidatorBase>> Tasks;

	for(const TPair<FName, FString> DefinitionPair : Parameters.PresubmitOperations)
	{
		FValidatorDefinition TaskDefinition;
		FStringOutputDevice Errors;
		FValidatorDefinition::StaticStruct()->ImportText(*DefinitionPair.Value, &TaskDefinition, nullptr, 0, &Errors, FValidatorDefinition::StaticStruct()->GetName());

		if(!Errors.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
			FModelInterface::SetErrorState();
			continue;
		}

		if(TaskDefinition.Type.TrimStartAndEnd().IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Task %s didn't have a Type."), *DefinitionPair.Key.ToString());
			continue;
		}

		if(TaskDefinition.Type.Equals(SubmitToolParseConstants::CustomValidator, ESearchCase::IgnoreCase))
		{
			Tasks.Add(MakeShared<FValidatorRunExecutable>(DefinitionPair.Key, Parameters, ServiceProvider.ToSharedRef(), DefinitionPair.Value));
		}
		else if(TaskDefinition.Type.Equals(SubmitToolParseConstants::VirtualizationToolOp, ESearchCase::IgnoreCase))
		{
			Tasks.Add(MakeShared<FVirtualizationOperation>(DefinitionPair.Key, Parameters, ServiceProvider.ToSharedRef(), DefinitionPair.Value));
		}
		else
		{
			UE_LOG(LogSubmitTool, Error, TEXT("[%s] is not a recognized pre submit operation type and has not been activated."), *DefinitionPair.Key.ToString());
		}
	}

	PresubmitOperationsService->InitializeTasks(Tasks);
}



void FModelInterface::SetCLDescription(const FText& newDescription, bool DoNotInvalidate) const
{
	if(ChangelistService->SetCLDescription(newDescription.ToString()))
	{
		TagService->ParseCLDescription();

		if(!DoNotInvalidate)
		{
			ValidationService->InvalidateForChanges(ETaskArea::Changelist);
		}
	}
}

void FModelInterface::SendDescriptionToP4() const
{
	if(GetInputEnabled())
	{
		if(IsP4OperationRunning())
		{
			UE_LOG(LogSubmitToolP4, Log, TEXT("Attempted to send description to P4, but another operation is already running"));
			return;
		}

		ChangelistService->SendCLDescriptionToP4();
	}
}

bool FModelInterface::CanLaunchPreflight() const
{
	// Check Validators which are validating files, ignore changelist (description, valid tags) validators when we evaluate if we 
	// allow the user to trigger a preflight
	for(const TWeakPtr<const FValidatorBase>& Validator : ValidationService->GetTasks())
	{
		if(Validator.IsValid())
		{
			const TSharedPtr<const FValidatorBase> Pinned = Validator.Pin();
			if((Pinned->Definition->TaskArea & ETaskArea::ShelveAndLocalFiles) != ETaskArea::None)
			{
				if(!Pinned->GetHasPassed())
				{
					return false;
				}
			}
		}
	}

	return true;
}

void FModelInterface::ReevaluateSubmitToolTag()
{
	UpdateSubmitToolTag(ValidationService->GetIsRunSuccessful());	
}

void FModelInterface::UpdateSubmitToolTag(bool InbAdd)
{
	// add a special tag to the CL description
	FString SubmitToolTag = FString::Format(TEXT("#submittool {0}\n"), { FAppVersion::GetVersion() });
	FString DescriptionCopy = ChangelistService->GetCLDescription();

	if(InbAdd)
	{
		if(!HasSubmitToolTag())
		{
			UE_LOG(LogSubmitToolDebug, Log, TEXT("Added Submit Tool tag"));
			DescriptionCopy.Append(TEXT("\n") + SubmitToolTag);
			ChangelistService->SetCLDescription(DescriptionCopy, true);
		}
	}
	else if(HasSubmitToolTag())
	{
		FString VersionlessTag = TEXT("#submittool ");
		int32 Loc = DescriptionCopy.Find(VersionlessTag);
		if(Loc >= 0)
		{
			size_t EndPos = Loc + VersionlessTag.Len();
			while(EndPos < DescriptionCopy.Len())
			{
				if(DescriptionCopy[EndPos] == TCHAR('\n'))
				{
					++EndPos;
					break;
				}
				++EndPos;
			}

			if(Loc != 0 && DescriptionCopy[Loc - 1] == '\n' && (EndPos - DescriptionCopy.Len()) < 2)
			{
				Loc--;
			}

			UE_LOG(LogSubmitToolDebug, Log, TEXT("Removed Submit Tool tag"));
			DescriptionCopy.RemoveAt(Loc, EndPos - Loc);
			ChangelistService->SetCLDescription(DescriptionCopy, true);
			TagService->ParseCLDescription();
		}
	}
}


bool FModelInterface::HasSubmitToolTag() const
{
	// Only Checking that it has a submit tool tag, regardless of version.
	return ChangelistService->GetCLDescription().Find(TEXT("#submittool ")) != INDEX_NONE;
}


void FModelInterface::UpdateCLFromP4Async() const
{
	if(GetInputEnabled() || SubmitToolState == ESubmitToolAppState::Errored || SubmitToolState == ESubmitToolAppState::SubmitLocked)
	{
		ChangelistService->FetchChangelistDataAsync();
	}
}

bool FModelInterface::GetInputEnabled()
{
	return SubmitToolState == ESubmitToolAppState::WaitingUserInput || SubmitToolState == ESubmitToolAppState::SubmitLocked;
}

void FModelInterface::RequestPreflight()
{
	if(CanLaunchPreflight())
	{
		PreflightService->RequestPreflight();
	}
	else
	{
		bPreflightQueued = true;
	}
}

void FModelInterface::ShowSwarmReview()
{
	if (HasSwarmReview() && SwarmService.IsValid())
	{
		FString Url;
		if (SwarmService->GetCurrentReviewUrl(Url))
		{
			UE_LOG(LogSubmitTool, Log, TEXT("Swarm: Opening Swarm Review with URL: \"%s\""), *Url);

			FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
		}
	}
}

void FModelInterface::RequestSwarmReview()
{
	if (!HasSwarmReview() && SwarmService.IsValid())
	{
		if (!HasShelvedFiles())
		{
			ChangelistService->CreateShelvedFiles(FSourceControlOperationComplete::CreateLambda([this](const FSourceControlOperationRef& DeleteShelvedOp, ECommandResult::Type Result)
				{
					if (Result == ECommandResult::Succeeded)
					{
						RequestSwarmReview();
					}
					else
					{
						UE_LOG(LogSubmitTool, Error, TEXT("Failed to shelve files, Swarm Review request is cancelled"));
					}
				}));

			return;
		}

		SwarmService->CreateReview(OnCreateReviewComplete::CreateRaw(this, &FModelInterface::OnSwarmCreateCompleted));
	}
}

void FModelInterface::StartSubmitProcess()
{
	PresubmitOperationsService->ResetStates();

	// Check if any last minute file changes have come in that invalidated any validators.
	CheckForFileEdits();
	if(IsCLValid())
	{
		if(PrepareSubmitCallBack.IsBound())
		{
			PrepareSubmitCallBack.Broadcast();
		}

		UpdateSubmitToolTag(true);

		if(Parameters.IncompatibleFilesParams.IncompatibleFileGroups.Num() > 0)
		{
			const TArray<FString>& FilesInCL = ChangelistService->GetFilesDepotPaths();

			for(const FIncompatibleFilesGroup& FileGroup : Parameters.IncompatibleFilesParams.IncompatibleFileGroups)
			{
				TArray<size_t, TInlineAllocator<8>> Indexes;

				for(const FString& File : FilesInCL)
				{
					for(size_t i = 0; i < FileGroup.FileGroups.Num(); ++i)
					{
						if(File.Contains(FConfiguration::Substitute(FileGroup.FileGroups[i]), ESearchCase::IgnoreCase))
						{
							if(!Indexes.Contains(i))
							{
								Indexes.Add(i);
							}
							break;
						}
					}
				}

				if(Indexes.Num() > 1)
				{
					const FText TextTitle = FText::FromString(FileGroup.Title);
					const FText TextDescription = FText::FromString(FileGroup.GetMessage());

					if(FileGroup.bIsError)
					{
						FDialogFactory::ShowInformationDialog(TextTitle, TextDescription);
						UE_LOG(LogSubmitTool, Log, TEXT("Submission canceled due to incompatible files"));
						return;
					}
					else
					{
						if(FDialogFactory::ShowConfirmDialog(TextTitle, TextDescription) != EDialogFactoryResult::Confirm)
						{
							UE_LOG(LogSubmitTool, Log, TEXT("Submission canceled by user"));
							return;
						}
					}
				}				
			}
		}

		ChangeState(ESubmitToolAppState::Submitting, SubmitToolState == ESubmitToolAppState::SubmitLocked && bIsUserInAllowlist);
		

		if(HasShelvedFiles())
		{
			DeleteShelvedFiles();
		}
		else
		{
			RevertUnchangedAndSubmit();
		}
	}
	else
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Attempted to submit, but all validators have not passed. Aborting submit."));
	}
}

void FModelInterface::RequestIntegration() const
{
	FNIntegrationService->RequestIntegration(FOnBooleanValueChanged::CreateLambda([this](bool bSuccess)
		{
			if(bSuccess)
			{
				ChangeState(ESubmitToolAppState::Finished);
			}
		}));
}
void FModelInterface::RefreshStateBasedOnFiles()
{
	TArray<FSourceControlStateRef> LocalFiles = ChangelistService->GetFilesInCL();

	if(LocalFiles.IsEmpty())
	{
		const TArray<FSourceControlStateRef>& ShelvedFiles = ChangelistService->GetShelvedFilesInCL();
		if(ShelvedFiles.IsEmpty())
		{
			UE_LOG(LogSubmitTool, Error, TEXT("There are no files in CL %s, SUBMIT IS DISABLED"), *ChangelistService->GetCLID());
			ChangeState(ESubmitToolAppState::Errored);
		}
		else
		{
			if(!HasSubmitToolTag())
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("This CL hasn't been validated and there are no local files. You need to unshelve and run validations."), *ChangelistService->GetCLID());
				ChangeState(ESubmitToolAppState::Errored);
			}
			else
			{
				UE::Tasks::Launch(UE_SOURCE_LOCATION, [this] {
					if(P4LockdownService->ArePathsInLockdown(ChangelistService->GetShelvedFilesDepotPaths(), bIsUserInAllowlist))
					{
						UE_LOG(LogSubmitTool, Log, TEXT("There are no local files in CL %s, Submit is disabled but you can still request an Integration with your shelved files"), *ChangelistService->GetCLID());
						ChangeState(ESubmitToolAppState::SubmitLocked);
					}
					else
					{
						UE_LOG(LogSubmitTool, Error, TEXT("There are no files in CL %s, SUBMIT IS DISABLED"), *ChangelistService->GetCLID());
						ChangeState(ESubmitToolAppState::Errored);
					}
					});
			}
		}
	}
	else
	{
		const TArray<FSCCStream*>& Streams = SourceControlService->GetClientStreams();
		if(!Streams.IsEmpty())
		{
			FString StreamsMsg = FString::JoinBy(Streams, TEXT(" -> "), [](const FSCCStream* InStr) { return InStr->Name; });
			for(const FString& File : ChangelistService->GetFilesDepotPaths())
			{
				bool bMappedToView = false;

				for(const FSCCStream* Str : Streams)
				{
					if(File.StartsWith(Str->Name))
					{
						bMappedToView = true;
						break;						
					}

					for(const FString& ImportStream : Str->AdditionalImportPaths)
					{
						if(File.StartsWith(ImportStream))
						{
							bMappedToView = true;
							break;
						}
					}
				}

				if(!bMappedToView)
				{
					UE_LOG(LogSubmitTool, Warning, TEXT("File %s is not in the stream that the workspace is set to: %s"), *File, *StreamsMsg);
				}
			}
		}

		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this] {
			if(P4LockdownService->ArePathsInLockdown(ChangelistService->GetFilesDepotPaths(), bIsUserInAllowlist))
			{
				ChangeState(ESubmitToolAppState::SubmitLocked);
			}
			else
			{
				ChangeState(ESubmitToolAppState::WaitingUserInput);
			}
			});
	}
}

void FModelInterface::OnChangelistReady(bool bIsValid)
{
	if(SubmitToolState == ESubmitToolAppState::Initializing)
	{
		if(bIsValid)
		{
			UE_LOG(LogSubmitTool, Log, TEXT("Retrieved information for CL %s"), *ChangelistService->GetCLID());
			PreflightService->FetchPreflightInfo(true);

			TagService->ParseCLDescription();
			SwarmService->FetchReview(OnGetReviewComplete::CreateRaw(this, &FModelInterface::OnGetUsersFromSwarmCompleted));

			RefreshStateBasedOnFiles();
			if(!ChangelistService->GetFilesInCL().IsEmpty())
			{
				UpdateSubmitToolTag(false);
				ValidationService->CheckForTagSkips();

				ETaskArea ValidateArea = ~ETaskArea::Changelist;
				for(const FTag* Tag : TagService->GetTagsArray())
				{
					if(Tag->GetValues().Num() != 0)
					{
						ValidateArea = ETaskArea::Everything;
						break;
					}
				}

				ValidationService->QueueByArea(ValidateArea);
			}

			FileRefreshedCallback.Broadcast();
		}
		else
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Couldn't retrieve information for CL %s"), *ChangelistService->GetCLID());
			ChangeState(ESubmitToolAppState::Errored);
		}
	}
}


void FModelInterface::RevertUnchangedAndSubmit()
{
	bool bHasEditOrAdd = false;
	for(const FSourceControlStateRef& file : ChangelistService->GetFilesInCL())
	{
		bHasEditOrAdd |= file->IsAdded() || file->IsCheckedOut();
	}

	if(bHasEditOrAdd)
	{
		ChangelistService->RevertUnchangedFilesAsync(RevertUnchangedCallback);
	}
	else
	{
		Submit();
	}
}

void FModelInterface::Submit()
{
	if(PresubmitOperationsService->AreTasksPendingQueue())
	{
		// This will call submit again when it's done
		PresubmitOperationsService->CheckForTagSkips();
		if(PresubmitOperationsService->QueueAll())
		{
			return;
		}
	}

	auto AddendumAccumulator = [](const TArray<FString>& InAddendums, const FString& InDescription, FString& InOutAccumulated) {
		for(const FString& Str : InAddendums)
		{
			if(!InDescription.Contains(Str, ESearchCase::IgnoreCase))
			{
				InOutAccumulated += (TEXT("\n") + Str);
			}
		}
	};

	const FString& CLDescription = GetCLDescription();
	FString Addendums;
	AddendumAccumulator(ValidationService->GetAddendums(), CLDescription, Addendums);
	AddendumAccumulator(PresubmitOperationsService->GetAddendums(), CLDescription, Addendums);

	ChangelistService->Submit(Addendums, SubmitFinishedCallback);
}

void FModelInterface::OnDeleteShelveOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result)
{
	if(SubmitToolState == ESubmitToolAppState::Submitting)
	{
		if(Result == ECommandResult::Succeeded)
		{
			RevertUnchangedAndSubmit();
		}
		else if(Result == ECommandResult::Failed)
		{
			ChangeState(ESubmitToolAppState::WaitingUserInput);
		}
		else if(Result == ECommandResult::Cancelled)
		{
			ChangeState(ESubmitToolAppState::WaitingUserInput);
		}
	}
	else
	{
		ChangeState(ESubmitToolAppState::WaitingUserInput);
	}
}

void FModelInterface::OnRevertUnchangedOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result)
{
	if(SubmitToolState == ESubmitToolAppState::Submitting)
	{
		// Revert Unchanged returns as failed if there were no files to revert, check ErrorMessages to see actual failures
		if(Result == ECommandResult::Cancelled)
		{
			ChangeState(ESubmitToolAppState::WaitingUserInput);
		}
		else if(Result == ECommandResult::Succeeded || Operation->GetResultInfo().ErrorMessages.Num() == 0)
		{
			Submit();
		}
		else if(Result == ECommandResult::Failed)
		{
			ChangeState(ESubmitToolAppState::WaitingUserInput);
		}
	}
	else
	{
		ChangeState(ESubmitToolAppState::WaitingUserInput);
	}
}


void FModelInterface::OnPresubmitOperationsComplete(bool bInSuccess)
{
	if(bInSuccess)
	{
		Submit();
	}
	else
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Presubmit operations have failed, submission is not possible, please fix errors and try again."));
		PresubmitOperationsService->ResetStates();
	}
}


void FModelInterface::OnSubmitOperationComplete(const FSourceControlOperationRef& Operation, ECommandResult::Type Result)
{
	if(Result == ECommandResult::Succeeded)
	{
		// We've submitted, or tried to submit and failed so we only let the user close the app
		ChangeState(ESubmitToolAppState::Finished);

		if(FSubmitToolUserPrefs::Get()->bCloseOnSubmit)
		{
			MainTab.Pin()->RequestCloseTab();
		}
	}
	else
	{
		ChangeState(ESubmitToolAppState::WaitingUserInput);
	}
}

bool FModelInterface::Tick(float InDeltaTime)
{
	switch(SubmitToolState)
	{
	case ESubmitToolAppState::WaitingUserInput:
		if(SwarmService->IsRequestRunning() || JiraService->IsBlockingRequestRunning())
		{
			ChangeState(ESubmitToolAppState::P4BlockingOperation);
		}
		break;

	case ESubmitToolAppState::P4BlockingOperation:
		if(!SwarmService->IsRequestRunning() && !ChangelistService->IsP4OperationRunning() && !JiraService->IsBlockingRequestRunning())
		{
			ChangeState(ESubmitToolAppState::WaitingUserInput);
		}
		break;

	default:
		break;
	}

	return true;
}

void FModelInterface::ChangeState(ESubmitToolAppState newState, bool bForce)
{
	if(bForce)
	{
		UE_LOG(LogSubmitToolDebug, Log, TEXT("Transitioned state from '%s' to '%s'"), *StaticEnum<ESubmitToolAppState>()->GetNameStringByValue(static_cast<int64>(SubmitToolState)), *StaticEnum<ESubmitToolAppState>()->GetNameStringByValue(static_cast<int64>(newState)));
		SubmitToolState = newState;
	}
	else
	{
		if(SubmitToolAppState::AllowedTransitions.Contains(SubmitToolState))
		{
			const TArray<ESubmitToolAppState>& allowedStates = SubmitToolAppState::AllowedTransitions[SubmitToolState];
			if(allowedStates.Contains(newState))
			{
				UE_LOG(LogSubmitToolDebug, Log, TEXT("Transitioned state from '%s' to '%s'"), *StaticEnum<ESubmitToolAppState>()->GetNameStringByValue(static_cast<int64>(SubmitToolState)), *StaticEnum<ESubmitToolAppState>()->GetNameStringByValue(static_cast<int64>(newState)));
				SubmitToolState = newState;
			}
			else
			{
				UE_LOG(LogSubmitToolDebug, Warning, TEXT("Invalid state transition requested from '%s' to '%s'"), *StaticEnum<ESubmitToolAppState>()->GetNameStringByValue(static_cast<int64>(SubmitToolState)), *StaticEnum<ESubmitToolAppState>()->GetNameStringByValue(static_cast<int64>(newState)));
			}
		}
		else
		{
			UE_LOG(LogSubmitToolDebug, Warning, TEXT("Transition not allowed from '%s' to '%s'"), *StaticEnum<ESubmitToolAppState>()->GetNameStringByValue(static_cast<int64>(SubmitToolState)), *StaticEnum<ESubmitToolAppState>()->GetNameStringByValue(static_cast<int64>(newState)));
		}
	}	
}

void FModelInterface::OnGetUsersFromSwarmCompleted(const TUniquePtr<FSwarmReview>& InReview, const FString& InErrorMessage)
{
	if(!InReview.IsValid())
	{
		UE_LOG(LogSubmitTool, Log, TEXT("Could not retrieve swarm review for current changelist. %s"), *InErrorMessage);
		return;
	}

	TArray<const FTag*> TargetTags;

	for(const FTag* Tag : TagService->GetTagsArray())
	{
		if(Tag->Definition.InputSubType.Equals(TEXT("Swarm"), ESearchCase::IgnoreCase))
		{
			TargetTags.Add(Tag);
		}
	}

	if(!TargetTags.IsEmpty())
	{
		TArray<FString> SwarmUserValues;

		for(const TPair<FString, FSwarmReviewParticipant>& Participant : InReview->Participants)
		{
			if(Participant.Key.Equals(InReview->Author, ESearchCase::IgnoreCase))
			{
				continue;
			}

			if(Participant.Value.Vote.Value == 1)
			{
				if(!SwarmUserValues.Contains(Participant.Key) && !SwarmUserValues.Contains(TEXT("@") + Participant.Key))
				{
					SwarmUserValues.Add(Participant.Key);
				}
			}
		}

		if(!SwarmUserValues.IsEmpty())
		{
			bool bApplied = false;

			for(const FTag* Tag : TargetTags)
			{
				if(Tag->GetValues() != SwarmUserValues)
				{
					SetTagValues(*Tag, SwarmUserValues);
					bApplied = true;
				}
			}

			if(bApplied)
			{
				UE_LOG(LogSubmitTool, Log, TEXT("RB tag set to users that upvoted review '%d' Users: %s"), InReview->Id, *FString::Join(SwarmUserValues, TEXT(", ")));
				UE_LOG(LogSubmitToolDebug, Log, TEXT("Re-running Tag validator after applying the #rb from swarm"));
				ValidateCLDescription();
			}
		}
	}
}

void FModelInterface::OnSwarmCreateCompleted(bool InResult, const FString& InErrorMessage)
{
	if(InResult)
	{
		OnGetUsersFromSwarmCompleted(SwarmService->GetReview(), InErrorMessage);
		ShowSwarmReview();
	}
}
